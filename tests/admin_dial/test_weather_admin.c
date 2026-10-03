#include "weather_admin_dial.h"
#include "valve_config_dial.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned puts_count, gets_count, generation, session=1;
static bool alias_readback,observe,change_observation_generation;
static bool token_present=true, timeout_put, change_generation;
static uint64_t saved_revision;
static bool saved_enabled;
uint32_t valve_config_generation(void) { return generation; }
uint32_t valve_client_current_session(void) { return session; }
bool valve_config_weather_load(char *url,size_t ul,char *token,size_t tl) {
    assert(ul>=VALVE_CONFIG_URL_LEN && tl>=VALVE_CONFIG_TOKEN_LEN);
    if (!token_present) return false;
    strcpy(url,"http://192.0.2.10:8081"); strcpy(token,"private-weather-only"); return true;
}
bool valve_config_load(char *url,size_t ul,char *token,size_t tl){(void)ul;(void)tl;if(!observe)return false;strcpy(url,"http://192.0.2.10:8081");strcpy(token,"synthetic-display");return true;}
static int transport(const char *method,const char *url,const char *header,const char *token,
    const char *body,size_t body_len,int *status,char *out,size_t capacity,size_t *length,void *context) {
    (void)context;
    if(strstr(url,"/display/status")){
        assert(!strcmp(method,"GET")&&!body&&!body_len&&!strcmp(header,"X-Display-Token")&&!strcmp(token,"synthetic-display"));
        if(change_observation_generation)generation+=2;
        return -1;
    }
    assert(!strcmp(url,"http://192.0.2.10:8081/api/v1/display/weather-settings"));
    assert(!strcmp(header,"X-Weather-Settings-Token") && !strcmp(token,"private-weather-only"));
    *status=200;
    if (!strcmp(method,"PUT")) {
        ++puts_count; assert(body_len==strlen(body));
        cJSON *json=cJSON_Parse(body); assert(json && cJSON_GetArraySize(json)==5);
        cJSON *revision=cJSON_GetObjectItem(json,"expected_revision");
        if ((uint64_t)revision->valuedouble!=saved_revision) *status=409;
        else { ++saved_revision; saved_enabled=cJSON_IsTrue(cJSON_GetObjectItem(json,"enabled")); }
        cJSON_Delete(json);
        if (timeout_put) return -3;
    } else { assert(!strcmp(method,"GET") && !body_len); ++gets_count; }
    if (change_generation) { generation+=2; change_generation=false; }
    int n=snprintf(out,capacity,"{\"revision\":%llu,\"enabled\":%s,\"latitude\":46.05,\"longitude\":14.5,\"timezone\":\"Europe/Ljubljana\"}",
        (unsigned long long)saved_revision,saved_enabled?"true":"false");
    assert(n>0 && (size_t)n<capacity);
    if(alias_readback) {
        n=snprintf(out,capacity,"{\"revision\":2,\"enabled\\u0000unexpected\":true,\"latitude\":46.05,\"longitude\":14.5,\"timezone\":\"Europe/Ljubljana\"}");
    }
    *length=(size_t)n; return 0;
}
int main(void) {
    weather_admin_test_transport(transport,NULL);
    weather_settings_view_t view;
    assert(weather_admin_get(&view) && view.revision==0 && !view.enabled);
    weather_settings_write_t write={.expected_revision=0,.enabled=true,.coordinates_available=true,
        .latitude=46.05,.longitude=14.5,.timezone="Europe/Ljubljana"};
    assert(weather_admin_update(&write,&view) && view.enabled && view.revision==1);
    assert(!weather_admin_update(&write,&view) && view.result==WEATHER_ADMIN_CONFLICT);
    write.expected_revision=1; timeout_put=true;
    unsigned count=puts_count;
    assert(!weather_admin_update(&write,&view) && view.result==WEATHER_ADMIN_TIMEOUT);
    assert(puts_count==count+1);
    timeout_put=false;
    assert(weather_admin_get(&view) && view.revision==2 && puts_count==count+1);
    token_present=false; count=gets_count;
    assert(!weather_admin_get(&view) && gets_count==count);
    token_present=true; change_generation=true;
    assert(!weather_admin_get(&view));
    alias_readback=true;assert(!weather_admin_get(&view) && view.result==WEATHER_ADMIN_INVALID);alias_readback=false;
    observe=true;assert(weather_admin_get(&view)&&!view.observation[0]);
    change_observation_generation=true;assert(!weather_admin_get(&view)&&view.result==WEATHER_ADMIN_UNAVAILABLE);observe=false;
    const char *bad[]={
        "{\"expected_revision\":2,\"enabled\\u0000unexpected\":true,\"latitude\":46.05,\"longitude\":14.5,\"timezone\":\"Europe/Ljubljana\"}",
        "{\"expected_revision\":9007199254740992,\"enabled\":false,\"latitude\":null,\"longitude\":null,\"timezone\":\"UTC\"}",
        "{\"expected_revision\":1.0,\"enabled\":false,\"latitude\":null,\"longitude\":null,\"timezone\":\"UTC\"}",
        "{\"expected_revision\":1,\"enabled\":1,\"latitude\":null,\"longitude\":null,\"timezone\":\"UTC\"}",
        "{\"expected_revision\":1,\"enabled\":true,\"latitude\":null,\"longitude\":null,\"timezone\":\"UTC\"}",
        "{\"expected_revision\":1,\"enabled\":false,\"latitude\":91,\"longitude\":0,\"timezone\":\"UTC\"}",
        "{\"expected_revision\":1,\"enabled\":false,\"latitude\":null,\"longitude\":null,\"timezone\":\"UTC\",\"token\":\"bad\"}",
        "{\"expected_revision\":1,\"expected_revision\":1,\"enabled\":false,\"latitude\":null,\"longitude\":null,\"timezone\":\"UTC\"}"
    };
    for(unsigned i=0;i<sizeof bad/sizeof bad[0];++i) assert(!weather_settings_write_parse(bad[i],strlen(bad[i]),&write));
    const char *max="{\"expected_revision\":9007199254740991,\"enabled\":false,\"latitude\":null,\"longitude\":null,\"timezone\":\"UTC\"}";
    assert(weather_settings_write_parse(max,strlen(max),&write) && write.expected_revision==UINT64_C(9007199254740991));
    puts("weather production settings/parser/transport: PASS");
}
