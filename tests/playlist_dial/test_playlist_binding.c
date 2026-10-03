#include "playlist_client_dial.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint64_t now;
static unsigned intents,plays;
static playlist_endpoint_t current;
static bool mutate_open;
static uint64_t clock_ms(void){return now;}
static bool snapshot(playlist_endpoint_t *out){*out=current;return true;}
static int transport(const playlist_http_request_t *r,int *status,char *out,size_t cap,size_t *len){
 const char *body="{}";*status=200;
 if(strstr(r->path,"capabilities"))body="{\"version\":1,\"max_favorites\":5,\"replace_queue\":true}";
 else if(strstr(r->path,"favorites"))body="{\"revision\":3,\"core_epoch\":7,\"zone_id\":\"sauna\",\"zone_name\":\"Sauna\",\"favorites\":[{\"id\":\"opaque\",\"title\":\"Music\",\"available\":true,\"reason\":null}]}";
 else if(strstr(r->path,"play-intents")){
  if(mutate_open){strcpy(current.zone,"other-zone");assert(!r->valid(r->context));return -3;}
  intents++;body="{\"intent_id\":\"intent\",\"expires_in_ms\":10000}";
 } else if(!strcmp(r->path,"/dial/v1/play")){plays++;body="{\"request_id\":\"00000000-0000-4000-8000-000000000001\",\"status\":\"accepted\",\"zone_id\":\"sauna\"}";}
 assert(strlen(body)<cap);strcpy(out,body);*len=strlen(body);return 0;
}
static void load(void){
 playlist_client_cancel();current=(playlist_endpoint_t){.generation=1,.session=1,.credential_generation=2};strcpy(current.base,"http://bridge/roon");strcpy(current.zone,"sauna");strcpy(current.token,"synthetic");
 assert(playlist_client_request_favorites());playlist_client_test_run();playlist_client_result_t result;assert(playlist_client_take(&result)&&result.status==PLAYLIST_CLIENT_FAVORITES);
}
int main(void){
 playlist_client_test_hooks(clock_ms,snapshot,transport);assert(playlist_client_init());playlist_client_connected(true);
 for(unsigned change=0;change<5;change++){
  load();if(change==0)strcpy(current.zone,"other-zone");if(change==1)strcpy(current.base,"http://other/roon");if(change==2)current.generation++;if(change==3)current.session++;if(change==4)current.credential_generation+=2;
  assert(!playlist_client_request_play("opaque",3,7));playlist_client_test_run();assert(intents==0&&plays==0);
 }
 load();assert(!playlist_client_request_play("other-id",3,7));assert(!playlist_client_request_play("opaque",4,7));assert(!playlist_client_request_play("opaque",3,8));
 load();assert(playlist_client_request_play("opaque",3,7));mutate_open=true;playlist_client_test_run();assert(intents==0&&plays==0);
 mutate_open=false;load();assert(playlist_client_request_play("opaque",3,7));playlist_client_test_run();assert(intents==1&&plays==1);
 puts("production client captured favorites zone/endpoint/session/credential binding PASS");
}
