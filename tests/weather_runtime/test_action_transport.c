#include "runtime_fixture.h"
#include "valve_client_dial.h"
#include "valve_config_dial.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
uint32_t valve_config_generation(void){return 0;}
uint32_t weather_admin_generation(void){return 0;}
bool valve_config_load(char *url,size_t ul,char *token,size_t tl) {
    (void)ul;(void)tl;strcpy(url,"http://192.0.2.10:8081");strcpy(token,"private-test-token");return true;
}
static uint64_t now(void){return fixture_action_now;}
int main(void) {
    runtime_http_case(HTTP_ACTION_EXPIRED_OPEN);valve_client_test_clock(now);valve_client_on_reconnect();
    fixture_action_now=0;uint32_t id;
    valve_request_t request={.action=VALVE_ACTION_WEATHER_START,.duration_seconds=180};
    assert(valve_client_request_post_for_config(&request,0,&id));
    fixture_action_now=8000;valve_client_test_run_pending();
    assert(atomic_load(&fixture_body_writes)==0); /* nonce is fresh; deliberate gesture is expired after open */
    assert(atomic_load(&fixture_cleanups)>=2); /* failed action plus GET reconciliation */
    assert(atomic_load(&fixture_last_timeout)<=1500);
    puts("production action transport expiration-before-body: PASS");
}
