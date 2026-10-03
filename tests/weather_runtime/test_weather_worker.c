#include "runtime_fixture.h"
#include "http_fixture.h"
#include "admin_server_dial.h"
#include "weather_admin_dial.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static admin_result_t local_settings(admin_http_settings_t *out) {
    *out=(admin_http_settings_t){.duration_minutes=10,.rotation_degrees=0,.generation=1};return ADMIN_OK;
}
int main(void) {
    fixture_reset();assert(admin_server_register((void *)1)==ESP_OK);
    valid_session=true;char cookie[90];snprintf(cookie,sizeof cookie,"dial_session=%s",fixture_session);
    httpd_req_t session=fixture_request("/admin/api/session",HTTP_GET,NULL);session.cookie=cookie;fixture_call(&session);
    const char *start=strstr(session.response,"\"csrf\":\"");assert(start);start+=strlen("\"csrf\":\"");
    char csrf[65];memcpy(csrf,start,64);csrf[64]=0;
    static const admin_settings_binding_t settings={.read=local_settings};admin_server_bind_settings(&settings);
    const char *body="{\"expected_revision\":0,\"enabled\":true,\"latitude\":46.05,\"longitude\":14.5,\"timezone\":\"Europe/Ljubljana\"}";
    runtime_http_case(HTTP_WEATHER_STALLED_OPEN);
    uint64_t began=runtime_wall_ms();
    httpd_req_t request=fixture_request("/admin/api/weather",HTTP_PUT,body);request.cookie=cookie;request.csrf=csrf;fixture_call(&request);
    assert(fixture_status(&request)==503 && runtime_wall_ms()-began<150);
    httpd_req_t local=fixture_request("/admin/api/settings",HTTP_GET,NULL);local.cookie=cookie;fixture_call(&local);
    assert(fixture_status(&local)==200 && strstr(local.response,"duration_minutes"));
    weather_settings_view_t view;assert(!weather_admin_get(&view)); /* one outstanding job, no growth */
    runtime_wait_cleanup(1);assert(atomic_load(&fixture_body_writes)==0);
    runtime_http_case(HTTP_WEATHER_SLOW_RESPONSE);
    began=runtime_wall_ms();request=fixture_request("/admin/api/weather",HTTP_PUT,body);request.cookie=cookie;request.csrf=csrf;fixture_call(&request);
    assert(fixture_status(&request)==503 && runtime_wall_ms()-began<150);
    local=fixture_request("/admin/api/settings",HTTP_GET,NULL);local.cookie=cookie;fixture_call(&local);assert(fixture_status(&local)==200);
    runtime_wait_cleanup(1);assert(atomic_load(&fixture_body_writes)==1); /* uncertain dispatched PUT never replayed */
    runtime_http_case(HTTP_WEATHER_NORMAL);assert(weather_admin_get(&view) && view.revision==1);
    runtime_wait_counts(2,1);
    runtime_http_case(HTTP_OBSERVATION_STALLED_OPEN);
    began=runtime_wall_ms();request=fixture_request("/admin/api/weather",HTTP_GET,NULL);request.cookie=cookie;fixture_call(&request);
    assert(fixture_status(&request)==200&&runtime_wall_ms()-began<150);
    assert(strstr(request.response,"\"revision\":1")&&strstr(request.response,"\"observation\":null"));
    assert(!weather_admin_get(&view)); /* No second worker while optional GET stalls. */
    runtime_wait_counts(2,1);assert(atomic_load(&fixture_body_writes)==0);
    runtime_http_case(HTTP_OBSERVATION_MALFORMED);
    request=fixture_request("/admin/api/weather",HTTP_GET,NULL);request.cookie=cookie;fixture_call(&request);
    assert(fixture_status(&request)==200&&strstr(request.response,"\"observation\":null")&&strstr(request.response,"\"revision\":1"));
    runtime_wait_counts(2,1);assert(atomic_load(&fixture_body_writes)==0);
    puts("production weather worker/HTTP bounded wait, late result, no PUT retry: PASS");
}
