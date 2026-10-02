#include "admin_server_dial.h"
#include "admin_page_dial.h"
#include "cJSON.h"
#include "esp_netif.h"
#include "mbedtls/sha256.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#ifdef ESP_PLATFORM
#include "lwip/sockets.h"
#include "lwip/inet.h"
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#define SESSION_LEN 64
#define BODY_MAX 384
static const admin_settings_binding_t *settings;

void admin_server_bind_settings(const admin_settings_binding_t *binding) {
    settings = binding;
}

static void no_store(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
}

static esp_err_t reply(httpd_req_t *req, const char *status, const char *body) {
    no_store(req);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    return httpd_resp_sendstr(req, body);
}

static esp_err_t result_reply(httpd_req_t *req, admin_result_t result) {
    switch (result) {
    case ADMIN_OK: return reply(req, "200 OK", "{\"ok\":true}");
    case ADMIN_INVALID: return reply(req, "400 Bad Request", "{\"error\":\"invalid\"}");
    case ADMIN_DENIED: return reply(req, "401 Unauthorized", "{\"error\":\"unauthorized\"}");
    case ADMIN_LOCKED:
        httpd_resp_set_hdr(req, "Retry-After", "60");
        return reply(req, "429 Too Many Requests", "{\"error\":\"locked\"}");
    case ADMIN_CONFLICT: return reply(req, "409 Conflict", "{\"error\":\"conflict\"}");
    default: return reply(req, "503 Service Unavailable", "{\"error\":\"unavailable\"}");
    }
}

static bool header(httpd_req_t *req, const char *name, char *out, size_t size) {
    size_t length = httpd_req_get_hdr_value_len(req, name);
    return length && length < size &&
           httpd_req_get_hdr_value_str(req, name, out, size) == ESP_OK;
}

/* Trust only the address of this accepted socket or the configured interface
 * hostname. Comparing Origin to an arbitrary client-supplied Host is unsafe. */
static bool trusted_host(httpd_req_t *req, char host[96]) {
    if (!header(req, "Host", host, 96)) return false;
    char name[96];
    snprintf(name, sizeof(name), "%s", host);
    char *port = strchr(name, ':');
    if (port) {
        if (strcmp(port, ":80") != 0) return false;
        *port = '\0';
    }
    struct sockaddr_in local = {0};
    socklen_t size = sizeof(local);
    char address[INET_ADDRSTRLEN];
    if (getsockname(httpd_req_to_sockfd(req), (struct sockaddr *)&local, &size) == 0 &&
        local.sin_family == AF_INET &&
        inet_ntop(AF_INET, &local.sin_addr, address, sizeof(address)) &&
        strcmp(name, address) == 0) return true;
    const char *keys[] = {"WIFI_STA_DEF", "WIFI_AP_DEF"};
    for (size_t i = 0; i < 2; ++i) {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey(keys[i]);
        const char *hostname = NULL;
        if (netif && esp_netif_get_hostname(netif, &hostname) == ESP_OK && hostname) {
            char mdns[96];
            int n = snprintf(mdns, sizeof(mdns), "%s.local", hostname);
            if (!strcmp(name, hostname) ||
                (n > 0 && n < (int)sizeof(mdns) && !strcmp(name, mdns))) return true;
        }
    }
    return false;
}

static bool origin_allowed(httpd_req_t *req) {
    char host[96], origin[104], expected[104];
    if (!trusted_host(req, host) || !header(req, "Origin", origin, sizeof(origin))) return false;
    snprintf(expected, sizeof(expected), "http://%s", host);
    return strcmp(origin, expected) == 0;
}

static bool session_cookie(httpd_req_t *req, char out[65]) {
    char cookie[512];
    out[0] = '\0';
    if (!header(req, "Cookie", cookie, sizeof(cookie))) return false;
    bool found = false;
    char *part = cookie;
    while (*part) {
        while (*part == ' ') ++part;
        char *end = strchr(part, ';');
        size_t length = end ? (size_t)(end - part) : strlen(part);
        if (length >= 13 && !memcmp(part, "dial_session=", 13)) {
            if (found || length != 13 + SESSION_LEN) return false;
            for (size_t i = 0; i < SESSION_LEN; ++i) {
                char c = part[13 + i];
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
            }
            memcpy(out, part + 13, SESSION_LEN);
            out[SESSION_LEN] = '\0';
            found = true;
        }
        if (!end) break;
        part = end + 1;
    }
    return found;
}

/* Domain-separated one-way token: bound to the random session without keeping
 * a second mutable session table across STA/AP server lifecycles. */
static bool csrf_token(const char session[65], char out[65]) {
    unsigned char digest[32];
    char input[80];
    int n = snprintf(input, sizeof(input), "dial-csrf-v1:%s", session);
    if (mbedtls_sha256((const unsigned char *)input, (size_t)n, digest, 0) != 0) return false;
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(digest); ++i) {
        out[2*i] = hex[digest[i] >> 4];
        out[2*i+1] = hex[digest[i] & 15];
    }
    out[64] = '\0';
    return true;
}

static bool authorized_session(httpd_req_t *req, char session[65], bool touch) {
    return session_cookie(req, session) && admin_auth_session_valid(session, touch);
}

bool admin_http_authorize(httpd_req_t *req, bool mutation) {
    no_store(req);
    bool configured = false;
    if (admin_auth_is_configured(&configured) != ADMIN_OK) {
        result_reply(req, ADMIN_STORAGE_ERROR);
        return false;
    }
    char session[65], host[96];
    if (!configured || !authorized_session(req, session, false)) {
        if (req->method == HTTP_GET && strncmp(req->uri, "/admin/api/", 11)) {
            httpd_resp_set_status(req, "401 Unauthorized");
            httpd_resp_set_type(req, "text/html; charset=utf-8");
            httpd_resp_sendstr(req, ADMIN_PAGE);
        } else result_reply(req, ADMIN_DENIED);
        return false;
    }
    if (!trusted_host(req, host)) {
        reply(req, "403 Forbidden", "{\"error\":\"origin\"}");
        return false;
    }
    if (mutation) {
        char supplied[65], expected[65];
        if (!origin_allowed(req) || !header(req, "X-CSRF-Token", supplied, sizeof(supplied)) ||
            !csrf_token(session, expected) || strcmp(supplied, expected)) {
            reply(req, "403 Forbidden", "{\"error\":\"csrf\"}");
            return false;
        }
    }
    if (!admin_auth_session_valid(session, true)) {
        result_reply(req, ADMIN_DENIED);
        return false;
    }
    return true;
}

static esp_err_t shell_handler(httpd_req_t *req) {
    no_store(req);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req, ADMIN_PAGE);
}

static esp_err_t forms_handler(httpd_req_t *req) {
    no_store(req);
    httpd_resp_set_type(req, "application/javascript; charset=utf-8");
    return httpd_resp_sendstr(req, ADMIN_FORMS_JS);
}

static esp_err_t session_handler(httpd_req_t *req) {
    char host[96];
    if (!trusted_host(req, host)) return reply(req, "403 Forbidden", "{\"error\":\"origin\"}");
    bool configured = false;
    if (admin_auth_is_configured(&configured) != ADMIN_OK) return result_reply(req, ADMIN_STORAGE_ERROR);
    char session[65], csrf[65], body[160];
    if (configured && authorized_session(req, session, true)) {
        if (!csrf_token(session, csrf)) return result_reply(req, ADMIN_STORAGE_ERROR);
        snprintf(body, sizeof(body), "{\"setup_required\":false,\"authenticated\":true,\"csrf\":\"%s\"}", csrf);
    } else snprintf(body, sizeof(body), "{\"setup_required\":%s,\"authenticated\":false}", configured ? "false" : "true");
    return reply(req, "200 OK", body);
}

static cJSON *read_json(httpd_req_t *req) {
    char type[64], body[BODY_MAX + 1];
    if (!header(req, "Content-Type", type, sizeof(type)) ||
        (strcmp(type, "application/json") && strcmp(type, "application/json; charset=utf-8"))) {
        reply(req, "415 Unsupported Media Type", "{\"error\":\"content_type\"}");
        return NULL;
    }
    if (!req->content_len || req->content_len > BODY_MAX) {
        result_reply(req, ADMIN_INVALID);
        return NULL;
    }
    size_t received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, body + received, req->content_len - received);
        if (n <= 0) { result_reply(req, ADMIN_INVALID); return NULL; }
        received += (size_t)n;
    }
    body[received] = '\0';
    const char *end = NULL;
    cJSON *json = cJSON_ParseWithLengthOpts(body, received + 1, &end, true);
    if (!cJSON_IsObject(json) || memchr(body, '\0', received) || strstr(body, "\\u0000")) {
        cJSON_Delete(json);
        result_reply(req, ADMIN_INVALID);
        return NULL;
    }
    /* Duplicate keys are ambiguous to clients and must never choose a secret. */
    for (cJSON *a = json->child; a; a = a->next) {
        for (cJSON *b = a->next; b; b = b->next) {
            if (!strcmp(a->string, b->string)) {
                cJSON_Delete(json); result_reply(req, ADMIN_INVALID); return NULL;
            }
        }
    }
    return json;
}

static const char *string_field(cJSON *json, const char *key) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key);
    return cJSON_IsString(item) ? item->valuestring : "";
}

static bool integer_field(cJSON *json, const char *key, uint32_t max, uint32_t *out) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) || item->valuedouble < 0 ||
        item->valuedouble > max || floor(item->valuedouble) != item->valuedouble) return false;
    *out = (uint32_t)item->valuedouble;
    return true;
}

static void clear_cookie(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Set-Cookie", "dial_session=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0");
}

static esp_err_t auth_handler(httpd_req_t *req) {
    const char *op = strrchr(req->uri, '/') + 1;
    bool public = !strcmp(op, "setup") || !strcmp(op, "login") || !strcmp(op, "recover");
    if (!public && !admin_http_authorize(req, true)) return ESP_OK;
    if (!origin_allowed(req)) return reply(req, "403 Forbidden", "{\"error\":\"origin\"}");
    cJSON *json = read_json(req);
    if (!json) return ESP_OK;
    const char *pin = string_field(json, "pin"), *repeat = string_field(json, "repeated_pin");
    char session[65] = {0}, code[33] = {0}, cookie[144];
    admin_result_t result = ADMIN_INVALID;
    if (!strcmp(op, "setup")) result = admin_auth_setup(pin, repeat, code);
    else if (!strcmp(op, "login")) result = admin_auth_login(pin, session);
    else if (!strcmp(op, "recover")) result = admin_auth_recover(string_field(json, "code"), pin, repeat, code);
    else if (session_cookie(req, session)) {
        if (!strcmp(op, "pin")) result = admin_auth_change_pin(session, pin, repeat);
        else if (!strcmp(op, "recovery-code")) result = admin_auth_reissue_recovery(session, pin, code);
        else if (!strcmp(op, "logout")) { admin_auth_logout(session); result = ADMIN_OK; }
    }
    cJSON_Delete(json);
    if (result != ADMIN_OK) return result_reply(req, result);
    if (!strcmp(op, "login")) {
        snprintf(cookie, sizeof(cookie), "dial_session=%s; HttpOnly; SameSite=Strict; Path=/", session);
        httpd_resp_set_hdr(req, "Set-Cookie", cookie);
    } else if (strcmp(op, "recovery-code")) clear_cookie(req);
    if (code[0]) {
        char body[64];
        snprintf(body, sizeof(body), "{\"recovery_code\":\"%s\"}", code);
        return reply(req, "200 OK", body);
    }
    return result_reply(req, ADMIN_OK);
}

static esp_err_t settings_reply(httpd_req_t *req, admin_result_t result,
                                 const admin_http_settings_t *value) {
    if (result != ADMIN_OK) return result_reply(req, result);
    if (!value->duration_minutes || value->duration_minutes > 10 ||
        value->rotation_degrees > 270 || value->rotation_degrees % 90)
        return result_reply(req, ADMIN_STORAGE_ERROR);
    char body[128];
    snprintf(body, sizeof(body), "{\"duration_minutes\":%u,\"rotation_degrees\":%u,\"generation\":%lu}",
             value->duration_minutes, value->rotation_degrees, (unsigned long)value->generation);
    return reply(req, "200 OK", body);
}

static esp_err_t settings_handler(httpd_req_t *req) {
    bool mutation = req->method == HTTP_POST;
    if (!admin_http_authorize(req, mutation)) return ESP_OK;
    admin_http_settings_t value = {0};
    if (!mutation) return settings_reply(req, settings && settings->read ? settings->read(&value) : ADMIN_STORAGE_ERROR, &value);
    cJSON *json = read_json(req);
    if (!json) return ESP_OK;
    uint32_t generation = 0, input = 0;
    bool shower = !strcmp(req->uri, "/admin/api/shower");
    bool valid = integer_field(json, "generation", UINT32_MAX, &generation) &&
        integer_field(json, shower ? "duration_minutes" : "rotation_degrees", shower ? 10 : 270, &input) &&
        (shower ? input >= 1 : input % 90 == 0);
    cJSON_Delete(json);
    if (!valid) return result_reply(req, ADMIN_INVALID);
    admin_result_t result = ADMIN_STORAGE_ERROR;
    if (settings) {
        if (shower && settings->shower) result = settings->shower((uint16_t)input, generation, &value);
        else if (!shower && settings->rotation) result = settings->rotation((uint16_t)input, generation, &value);
    }
    return settings_reply(req, result, &value);
}

esp_err_t admin_server_register(httpd_handle_t server) {
    const httpd_uri_t routes[] = {
        {.uri="/admin", .method=HTTP_GET, .handler=shell_handler},
        {.uri="/admin/forms.js", .method=HTTP_GET, .handler=forms_handler},
        {.uri="/admin/api/session", .method=HTTP_GET, .handler=session_handler},
        {.uri="/admin/api/setup", .method=HTTP_POST, .handler=auth_handler},
        {.uri="/admin/api/login", .method=HTTP_POST, .handler=auth_handler},
        {.uri="/admin/api/recover", .method=HTTP_POST, .handler=auth_handler},
        {.uri="/admin/api/logout", .method=HTTP_POST, .handler=auth_handler},
        {.uri="/admin/api/pin", .method=HTTP_POST, .handler=auth_handler},
        {.uri="/admin/api/recovery-code", .method=HTTP_POST, .handler=auth_handler},
        {.uri="/admin/api/settings", .method=HTTP_GET, .handler=settings_handler},
        {.uri="/admin/api/shower", .method=HTTP_POST, .handler=settings_handler},
        {.uri="/admin/api/rotation", .method=HTTP_POST, .handler=settings_handler},
    };
    for (size_t i = 0; i < sizeof(routes)/sizeof(routes[0]); ++i) {
        esp_err_t result = httpd_register_uri_handler(server, &routes[i]);
        if (result != ESP_OK) return result;
    }
    return ESP_OK;
}
