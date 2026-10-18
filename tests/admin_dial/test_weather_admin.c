#include "weather_admin_dial.h"
#include "valve_config_dial.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static unsigned puts_count, gets_count, generation, session=1;
static bool alias_readback,observe,change_observation_generation;
static bool token_present=true, timeout_put, change_generation;
static const char *observation_body;
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
        if(observation_body) {
            *length=strlen(observation_body);assert(*length<capacity);
            memcpy(out,observation_body,*length);*status=200;return 0;
        }
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
static void observation_codes_cases(void) {
    weather_settings_view_t view;
    char body[2048],serialized[1536];
    const char *codes[]={"",",\"weather_codes\":[]",",\"weather_codes\":[0,61,73,3,97]",
        ",\"weather_codes\":[0,999,73,3,97]",",\"weather_codes\":[0,true,73,3,97]",",\"weather_codes\":[0,61]"};
    observe=true;change_observation_generation=false;
    for(unsigned i=0;i<sizeof codes/sizeof codes[0];++i) {
        snprintf(body,sizeof body,"{\"mode\":\"weather_assisted\",\"weather_assistance_version\":1,\"operation\":\"AUTO_SUPPLY\",\"state\":\"AUTO_SUPPLY\",\"command\":\"SUPPLY\",\"reason\":\"weather_eligible\",\"remaining_seconds\":0,\"control_revision\":8,\"action_nonce\":null,\"sensor_health\":\"HEALTHY\",\"pipe_temperature_c\":6.4,\"weather\":{\"enabled\":true,\"available\":true,\"eligible\":true,\"reason\":\"eligible\",\"dates\":[\"2026-10-03\",\"2026-10-04\",\"2026-10-05\",\"2026-10-06\",\"2026-10-07\"],\"minima_c\":[5,6,7,8,9],\"last_successful_check\":\"2026-10-03T10:00:00+00:00\"%s}}",codes[i]);
        observation_body=body;
        assert(weather_admin_get(&view) && view.observation[0]);
        assert(strlen(view.observation)<sizeof view.observation);
        assert(weather_admin_view_json(&view,serialized,sizeof serialized));
        cJSON *json=cJSON_Parse(view.observation);assert(json);
        cJSON *forwarded=cJSON_GetObjectItemCaseSensitive(json,"weather_codes");
        assert((forwarded!=NULL)==(i==1||i==2));
        if(i==2)assert(cJSON_GetArraySize(forwarded)==5 && cJSON_GetArrayItem(forwarded,4)->valueint==97);
        cJSON *pipe=cJSON_GetObjectItemCaseSensitive(json,"pipe_temperature_c");
        assert(cJSON_IsNumber(pipe) && pipe->valuedouble>6.39 && pipe->valuedouble<6.41);
        assert(!cJSON_GetObjectItemCaseSensitive(json,"action_nonce"));
        cJSON_Delete(json);
    }
    char *health=strstr(body,"HEALTHY");assert(health);memcpy(health,"MISSING",7);
    assert(weather_admin_get(&view));
    cJSON *missing=cJSON_Parse(view.observation);assert(missing);
    assert(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(missing,"pipe_temperature_c")));
    cJSON_Delete(missing);
    observation_body=NULL;observe=false;
}

static const char *history_body;
static int history_status=200,history_rc;
static unsigned history_calls;
static bool history_change_session,history_change_generation,history_nested;
static int history_transport(const char *method,const char *url,const char *header,const char *token,
    const char *body,size_t body_len,int *status,char *out,size_t capacity,size_t *length,void *context) {
    (void)context;history_calls++;
    assert(!strcmp(method,"GET")&&!body&&!body_len);
    assert(!strcmp(url,"http://192.0.2.10:8081/api/v1/display/history?offset=0"));
    assert(!strcmp(header,"X-Weather-Settings-Token")&&!strcmp(token,"private-weather-only"));
    if(history_change_session)session++;
    if(history_change_generation)generation+=2;
    if(history_nested){char *nested=NULL;assert(!shower_history_get(0,&nested)&&!nested);}
    *status=history_status;*length=strlen(history_body);
    if(*length<capacity)memcpy(out,history_body,*length);
    return history_rc;
}
static void history_cases(void) {
    weather_admin_test_transport(history_transport,NULL);token_present=true;
    const char *valid="{\"items\":[{\"occurred_at\":\"2026-10-11T12:30:00.123456+00:00\",\"mode\":\"weather_assisted\",\"operation\":\"AUTO_SUPPLY\",\"command\":\"SUPPLY\",\"reason\":\"weather_eligible\"}],\"offset\":0,\"has_more\":true}";
    history_body=valid;char *json=NULL;unsigned epoch=weather_admin_generation(),writes=puts_count;
    history_nested=true;assert(shower_history_get(0,&json));history_nested=false;
    assert(strstr(json,"AUTO_SUPPLY")&&!strstr(json,"private"));free(json);
    assert(weather_admin_generation()==epoch&&writes==puts_count);
    const char *bad[]={"{}","{\"items\":[],\"offset\":0.0,\"has_more\":false}",
        "{\"items\":[],\"offset\":1,\"has_more\":false}","{\"items\":{},\"offset\":0,\"has_more\":false}",
        "{\"items\":[],\"offset\":0,\"has_more\":1}","{\"items\":[],\"offset\":0,\"has_more\":false,\"token\":\"private\"}",
        "{\"items\":[],\"offset\":0,\"offset\":0,\"has_more\":false}","{\"items\":[[[{}]]],\"offset\":0,\"has_more\":false}"};
    for(unsigned i=0;i<sizeof bad/sizeof bad[0];i++){history_body=bad[i];assert(!shower_history_get(0,&json)&&!json);}
    const char *fields[]={"occurred_at","occurred_at","mode","operation","command","reason","token","reason"};
    const char *values[]={"2026-02-30T12:00:00Z","2026-10-11T12:00:00+02:00",
        "123456789012345678901234567890123","123456789012345678901234567890123","TOGGLE",
        "1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567","secret","bad\nreason"};
    for(unsigned i=0;i<sizeof fields/sizeof fields[0];i++) {
        cJSON *root=cJSON_Parse(valid),*row=cJSON_GetArrayItem(cJSON_GetObjectItem(root,"items"),0);
        if(i==6)cJSON_AddStringToObject(row,fields[i],values[i]);
        else cJSON_ReplaceItemInObjectCaseSensitive(row,fields[i],cJSON_CreateString(values[i]));
        char *body=cJSON_PrintUnformatted(root);history_body=body;assert(!shower_history_get(0,&json)&&!json);free(body);cJSON_Delete(root);
    }
    cJSON *root=cJSON_Parse(valid),*rows=cJSON_GetObjectItem(root,"items");
    for(unsigned i=1;i<11;i++)assert(cJSON_AddItemToArray(rows,cJSON_Duplicate(cJSON_GetArrayItem(rows,0),true)));
    char *many=cJSON_PrintUnformatted(root);history_body=many;assert(!shower_history_get(0,&json));free(many);cJSON_Delete(root);
    char large[WEATHER_BODY_MAX+1];memset(large,' ',sizeof large-1);large[sizeof large-1]=0;
    history_body=large;assert(!shower_history_get(0,&json)&&!json);
    history_body="{\"items\":[],\"offset\":0,\"has_more\":false}";assert(shower_history_get(0,&json));free(json);
    history_body=valid;
    const int statuses[]={401,404,500};for(unsigned i=0;i<3;i++){history_status=statuses[i];assert(!shower_history_get(0,&json)&&!json);}history_status=200;
    history_rc=-3;assert(!shower_history_get(0,&json));history_rc=0;
    history_change_session=true;assert(!shower_history_get(0,&json));history_change_session=false;
    history_change_generation=true;assert(!shower_history_get(0,&json));history_change_generation=false;
    unsigned calls=history_calls;token_present=false;assert(!shower_history_get(0,&json)&&calls==history_calls);token_present=true;
    assert(!shower_history_get(SHOWER_HISTORY_OFFSET_MAX+1,&json)&&calls==history_calls);
    assert(weather_admin_generation()==epoch&&writes==puts_count);
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
    observation_codes_cases();
    history_cases();
    puts("weather production settings/parser/transport: PASS");
}
