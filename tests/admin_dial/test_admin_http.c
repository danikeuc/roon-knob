#include "http_fixture.h"
#include "config_server.h"
#include "captive_portal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "admin_server_dial.h"
static void test_old_config_routes_require_session(void) {
    fixture_reset();
    config_server_start();
    for(int i=0;i<route_count;i++) {
        if(!strncmp(routes[i].uri,"/admin",6))continue;
        httpd_req_t r=fixture_request(routes[i].uri,routes[i].method,"enabled=1");
        fixture_call(&r);
        assert(fixture_status(&r)==401);
        assert(reads==0&&writes==0);
    }
    config_server_stop();
    fixture_reset();
    assert(captive_portal_start());
    const char *paths[]= {
        "/","/configure","/wifi-remove"
    };
    for(int i=0;i<3;i++) {
        httpd_req_t r=fixture_request(paths[i],i?HTTP_POST:HTTP_GET,"ssid=private&pass=secret");
        fixture_call(&r);
        assert(fixture_status(&r)==401);
        assert(reads==0&&writes==0);
    }
    captive_portal_stop();
}
static char cookie[90],csrf[65];
static void login(void) {
    httpd_req_t r=fixture_request("/admin/api/login",HTTP_POST,"{\"pin\":\"0123\"}");
    fixture_call(&r);
    assert(fixture_status(&r)==200);
    assert(strstr(r.set_cookie,"HttpOnly; SameSite=Strict; Path=/"));
    assert(!strstr(r.response,fixture_session));
    snprintf(cookie,sizeof cookie,"dial_session=%s",fixture_session);
    r=fixture_request("/admin/api/session",HTTP_GET,NULL);
    r.cookie=cookie;
    fixture_call(&r);
    assert(fixture_status(&r)==200);
    cJSON *j=cJSON_Parse(r.response);
    assert(j);
    cJSON *c=cJSON_GetObjectItemCaseSensitive(j,"csrf");
    assert(cJSON_IsString(c));
    assert(strlen(c->valuestring)==64);
    strcpy(csrf,c->valuestring);
    cJSON_Delete(j);
}
static void test_cross_origin_mutation_denied(void) {
    fixture_reset();
    config_server_start();
    login();
    const char *origins[]= {
        "http://evil.example","null","http://192.168.4.1.evil",NULL
    };
    for(size_t i=0;i<4;i++) {
        httpd_req_t r=fixture_request("/admin/api/login",HTTP_POST,"{\"pin\":\"0123\"}");
        r.origin=origins[i];
        fixture_call(&r);
        assert(fixture_status(&r)==403);
        r=fixture_request("/ble-scan",HTTP_POST,"");
        r.cookie=cookie;
        r.csrf=csrf;
        r.origin=origins[i];
        fixture_call(&r);
        assert(fixture_status(&r)==403);
        assert(writes==0);
    }
    httpd_req_t r=fixture_request("/ble-scan",HTTP_POST,"");
    r.cookie=cookie;
    r.csrf="bad";
    fixture_call(&r);
    assert(fixture_status(&r)==403);
    r=fixture_request("/admin/api/login",HTTP_POST,"{}");
    r.host="evil.example";
    r.origin="http://evil.example";
    fixture_call(&r);
    assert(fixture_status(&r)==403);
    r=fixture_request("/admin/api/login",HTTP_POST,"{}");
    r.type="text/plain";
    fixture_call(&r);
    assert(fixture_status(&r)==415);
    config_server_stop();
}
static void test_session_cookie_and_expiry(void) {
    fixture_reset();
    config_server_start();
    assert(auth_queries==0);
    login();
    httpd_req_t r=fixture_request("/ble-scan",HTTP_POST,"");
    r.cookie=cookie;
    r.csrf=csrf;
    fixture_call(&r);
    assert(fixture_status(&r)==303);
    assert(writes==1);
    fixture_expire();
    r=fixture_request("/",HTTP_GET,NULL);
    r.cookie=cookie;
    fixture_call(&r);
    assert(fixture_status(&r)==401);
    r=fixture_request("/admin/api/session",HTTP_GET,NULL);
    r.cookie=cookie;
    fixture_call(&r);
    assert(!strstr(r.response,"csrf"));
    assert(!strstr(r.response,fixture_session));
    assert(!strcmp(r.cache,"no-store"));
    config_server_stop();
}
static void test_setup_requires_matching_pin(void) {
    fixture_reset();
    configured=false;
    assert(captive_portal_start());
    assert(auth_queries==0);
    httpd_req_t r=fixture_request("/admin/api/session",HTTP_GET,NULL);
    fixture_call(&r);
    assert(strstr(r.response,"\"setup_required\":true"));
    r=fixture_request("/admin/api/setup",HTTP_POST,"{\"pin\":\"0123\",\"repeated_pin\":\"9999\"}");
    fixture_call(&r);
    assert(fixture_status(&r)==400);
    assert(!configured);
    r=fixture_request("/admin/api/setup",HTTP_POST,"{\"pin\":\"0123\",\"repeated_pin\":\"0123\"}");
    fixture_call(&r);
    assert(fixture_status(&r)==200);
    assert(strstr(r.response,"recovery_code"));
    assert(configured);
    r=fixture_request("/admin/api/setup",HTTP_POST,"{\"pin\":\"0123\",\"repeated_pin\":\"0123\"}");
    fixture_call(&r);
    assert(fixture_status(&r)==409);
    r=fixture_request("/configure?ssid=private&pass=submitted-secret",HTTP_GET,NULL);
    fixture_call(&r);
    assert(fixture_status(&r)==302);
    assert(!strcmp(r.location,"http://192.168.4.1/"));
    assert(writes==0);
    assert(!strstr(fixture_logs,"submitted-secret"));
    const char *discovery[]={"/hotspot-detect.html","/generate_204","/unknown"};
    for(size_t i=0;i<3;i++) {
        r=fixture_request(discovery[i],HTTP_GET,NULL);
        r.host="captive.apple.com";
        fixture_call(&r);
        assert(fixture_status(&r)==302);
        assert(!strcmp(r.location,"http://192.168.4.1/"));
    }
    login();
    r=fixture_request("/",HTTP_GET,NULL);
    r.cookie=cookie;
    fixture_call(&r);
    assert(fixture_status(&r)==200);
    assert(strstr(r.response,"/admin/forms.js"));
    assert(strstr(r.response,"method='POST' action='/configure'"));
    r=fixture_request("/configure",HTTP_POST,"ssid=private&pass=secret");
    r.cookie=cookie;
    r.csrf=csrf;
    r.type="application/x-www-form-urlencoded";
    fixture_call(&r);
    assert(fixture_status(&r)==200);
    assert(writes==1);
    captive_portal_stop();
}
static void test_recovery_form_preserves_settings(void) {
    fixture_reset();
    config_server_start();
    login();
    httpd_req_t r=fixture_request("/admin/api/recover",HTTP_POST,"{\"code\":\"eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee\",\"pin\":\"4567\",\"repeated_pin\":\"4567\"}");
    fixture_call(&r);
    assert(fixture_status(&r)==200);
    assert(strstr(r.response,"recovery_code"));
    assert(reads==0&&writes==0);
    assert(!valid_session);
    r=fixture_request("/admin/api/session",HTTP_GET,NULL);
    fixture_call(&r);
    assert(!strstr(r.response,"recovery_code"));
    config_server_stop();
}
static void test_storage_error_and_registration_fail_closed(void) {
    fixture_reset();
    config_server_start();
    storage_error=true;
    httpd_req_t r=fixture_request("/admin/api/session",HTTP_GET,NULL);
    fixture_call(&r);
    assert(fixture_status(&r)==503);
    assert(!strstr(r.response,"setup_required"));
    r=fixture_request("/",HTTP_GET,NULL);
    fixture_call(&r);
    assert(fixture_status(&r)==503);
    assert(reads==0);
    config_server_stop();
    for(int i=0;i<23;i++) {
        fixture_reset();
        fail_registration=i;
        config_server_start();
        assert(!config_server_is_running());
        assert(route_count==0);
    }
    for(int i=0;i<20;i++) {
        fixture_reset();
        fail_registration=i;
        assert(!captive_portal_start());
        assert(route_count==0);
    }
}
static void test_all_legacy_forms_and_escaped_values(void) {
    fixture_reset();
    config_server_start();
    login();
    const char *pages[]= {
        "/","/valves-config","/ble"
    };
    for(size_t i=0;i<3;i++) {
        httpd_req_t r=fixture_request(pages[i],HTTP_GET,NULL);
        r.cookie=cookie;
        fixture_call(&r);
        assert(fixture_status(&r)==200);
        assert(strstr(r.response,"/admin/forms.js"));
        if(i==0) {
            assert(!strstr(r.response,"' onfocus="));
            assert(strstr(r.response,"&#39;"));
        }
    }
    config_server_stop();
}
static admin_result_t binding_result;
static admin_http_settings_t binding_value;
static admin_result_t settings_read(admin_http_settings_t *out) {
    *out=binding_value;
    return binding_result;
}
static admin_result_t settings_shower(uint16_t m,uint32_t g,admin_http_settings_t *out) {
    assert(m==3&&g==7);
    *out=binding_value;
    return binding_result;
}
static admin_result_t settings_rotation(uint16_t d,uint32_t g,admin_http_settings_t *out) {
    assert(d==90&&g==7);
    *out=binding_value;
    return binding_result;
}
static void test_settings_binding_and_protected_api(void) {
    fixture_reset();
    config_server_start();
    const char *paths[]= {
        "/admin/api/logout","/admin/api/pin","/admin/api/recovery-code","/admin/api/settings","/admin/api/shower","/admin/api/rotation"
    };
    for(size_t i=0;i<6;i++) {
        httpd_req_t r=fixture_request(paths[i],i==3?HTTP_GET:HTTP_POST,"{}");
        fixture_call(&r);
        assert(fixture_status(&r)==401);
    }
    login();
    httpd_req_t r=fixture_request("/admin/api/settings",HTTP_GET,NULL);
    r.cookie=cookie;
    fixture_call(&r);
    assert(fixture_status(&r)==503);
    r=fixture_request("/admin/api/shower",HTTP_POST,"{\"duration_minutes\":3,\"generation\":7}");
    r.cookie=cookie;
    r.csrf=csrf;
    fixture_call(&r);
    assert(fixture_status(&r)==503);
    const admin_settings_binding_t binding= {
        settings_read,settings_shower,settings_rotation
    };
    admin_server_bind_settings(&binding);
    binding_value=(admin_http_settings_t) {
        3,90,8,true,true
    };
    binding_result=ADMIN_OK;
    for(size_t i=0;i<2;i++) {
        r=fixture_request(i?"/admin/api/rotation":"/admin/api/shower",HTTP_POST,i?"{\"rotation_degrees\":90,\"generation\":7}":"{\"duration_minutes\":3,\"generation\":7}");
        r.cookie=cookie;
        r.csrf=csrf;
        fixture_call(&r);
        assert(fixture_status(&r)==200);
        assert(strstr(r.response,"\"generation\":8"));
        assert(strstr(r.response,"\"short_duration_supported\":true"));
    }
    binding_result=ADMIN_CONFLICT;
    r=fixture_request("/admin/api/shower",HTTP_POST,"{\"duration_minutes\":3,\"generation\":7}");
    r.cookie=cookie;
    r.csrf=csrf;
    fixture_call(&r);
    assert(fixture_status(&r)==409);
    const char *bad[]= {
        "{\"duration_minutes\":0,\"generation\":7}","{\"duration_minutes\":11,\"generation\":7}","{\"duration_minutes\":1.5,\"generation\":7}","{\"duration_minutes\":3,\"generation\":-1}","{\"duration_minutes\":3,\"generation\":4294967296}","{\"duration_minutes\":3,\"duration_minutes\":4,\"generation\":7}"
    };
    for(size_t i=0;i<6;i++) {
        r=fixture_request("/admin/api/shower",HTTP_POST,bad[i]);
        r.cookie=cookie;
        r.csrf=csrf;
        fixture_call(&r);
        assert(fixture_status(&r)==400);
    }
    admin_server_bind_settings(NULL);
    config_server_stop();
}
static void test_auth_error_mapping_and_malformed_json(void) {
    fixture_reset();
    config_server_start();
    const admin_result_t failures[]= {
        ADMIN_INVALID,ADMIN_DENIED,ADMIN_LOCKED,ADMIN_CONFLICT,ADMIN_STORAGE_ERROR
    };
    const int statuses[]= {
        400,401,429,409,503
    };
    for(size_t i=0;i<5;i++) {
        next_auth=failures[i];
        httpd_req_t r=fixture_request("/admin/api/login",HTTP_POST,"{\"pin\":\"1234\"}");
        fixture_call(&r);
        assert(fixture_status(&r)==statuses[i]);
        assert(!r.set_cookie[0]);
        assert(!strstr(r.response,fixture_session));
    }
    next_auth=ADMIN_OK;
    const char *bad[]= {
        "[]","{}junk","{\"pin\":\"1234\",\"pin\":\"5678\"}","{\"pin\":\"1234\\u0000evil\"}"
    };
    for(size_t i=0;i<4;i++) {
        httpd_req_t r=fixture_request("/admin/api/login",HTTP_POST,bad[i]);
        fixture_call(&r);
        assert(fixture_status(&r)==400);
    }
    config_server_stop();
}

/* Observe the real parser boundary without replacing its behavior. A host's
 * larger stack hides the ESP32 stack exhaustion if we check only HTTP status. */
static unsigned parser_calls;
cJSON *admin_test_real_parse(const char *, size_t, const char **,
                                        cJSON_bool);
cJSON *admin_test_observed_parse(const char *body, size_t length,
                                        const char **end, cJSON_bool complete) {
    parser_calls++;
    return admin_test_real_parse(body, length, end, complete);
}

static void test_nested_json_rejected_before_parser_and_auth(void) {
    fixture_reset();
    config_server_start();
    char array[362], nested_array[384], nested_object[384];
    memset(array, '[', 180);
    array[180] = '0';
    memset(array + 181, ']', 180);
    array[361] = '\0';
    snprintf(nested_array, sizeof(nested_array), "{\"unused\":%s}", array);
    size_t position = 0;
    for (unsigned i = 0; i < 60; i++) {
        memcpy(nested_object + position, "{\"x\":", 5);
        position += 5;
    }
    nested_object[position++] = '0';
    for (unsigned i = 0; i < 60; i++) nested_object[position++] = '}';
    nested_object[position] = '\0';
    const char *bodies[] = {array, nested_array, nested_object,
        "{\"pin\":\"0123\",\"unused\":[]}",
        "{\"pin\":\"0123\",\"unused\":{}}"};
    const char *paths[] = {"/admin/api/login", "/admin/api/setup",
                           "/admin/api/recover"};
    for (size_t p = 0; p < sizeof(paths) / sizeof(paths[0]); p++) {
        for (size_t b = 0; b < sizeof(bodies) / sizeof(bodies[0]); b++) {
            httpd_req_t req = fixture_request(paths[p], HTTP_POST, bodies[b]);
            parser_calls = 0;
            fixture_call(&req);
            assert(fixture_status(&req) == 400);
            assert(parser_calls == 0);
            assert(auth_mutations == 0 && auth_queries == 0);
            assert(reads == 0 && writes == 0 && !valid_session);
        }
    }
    /* Delimiters inside strings, escaped quotes and escaped backslashes must
     * not be interpreted as containers. They still use the real JSON parser. */
    const char *valid[] = {
        "{\"pin\":\"0123\",\"note\":\"[{}]\"}",
        "{\"pin\":\"0123\",\"note\":\"escaped \\\"[{}]\\\" quote\"}",
        "{\"pin\":\"0123\",\"note\":\"backslash \\\\\"}",
        " \r\n {\"pin\":\"0123\",\"number\":7,\"bool\":true,\"nil\":null} \t"
    };
    for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); i++) {
        httpd_req_t req = fixture_request("/admin/api/login", HTTP_POST, valid[i]);
        parser_calls = 0;
        fixture_call(&req);
        assert(parser_calls == 1);
        assert(fixture_status(&req) == 200);
    }
    config_server_stop();
}

int main(void) {
    test_nested_json_rejected_before_parser_and_auth();
    test_all_legacy_forms_and_escaped_values();
    test_settings_binding_and_protected_api();
    test_auth_error_mapping_and_malformed_json();
    test_old_config_routes_require_session();
    test_cross_origin_mutation_denied();
    test_session_cookie_and_expiry();
    test_setup_requires_matching_pin();
    test_recovery_form_preserves_settings();
    test_storage_error_and_registration_fail_closed();
    puts("admin HTTP: PASS");
}
