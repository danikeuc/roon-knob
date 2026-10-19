#include "playlist_client_dial.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint64_t now;static int prepare,play,receipts;static bool timeout,old,expire,zone_change,deny_catalog;
static uint64_t clock_ms(void){return now;}
static bool snapshot(playlist_endpoint_t *out){*out=(playlist_endpoint_t){.generation=1,.session=1};strcpy(out->base,"http://bridge/roon");strcpy(out->zone,"sauna");strcpy(out->token,"synthetic");return true;}
static int exchange(const playlist_http_request_t *r,int *status,char *out,size_t cap,size_t *len){
 const char *json="{}";*status=200;
 assert(!strcmp(r->token,"synthetic"));
 if(deny_catalog&&strstr(r->path,"playlists")){*status=403;return 0;}
 if(strstr(r->path,"capabilities"))json=old?"{\"version\":0,\"max_favorites\":5,\"replace_queue\":true}":"{\"version\":1,\"max_favorites\":5,\"replace_queue\":true}";
 else if(strstr(r->path,"favorites"))json="{\"revision\":3,\"core_epoch\":7,\"zone_id\":\"sauna\",\"zone_name\":\"Sauna\",\"favorites\":[{\"id\":\"opaque\",\"title\":\"Test\",\"available\":true,\"reason\":null}]}";
 else if(strstr(r->path,"play-intents")){prepare++;json="{\"intent_id\":\"intent\",\"expires_in_ms\":10000}";if(expire)now+=10000;if(zone_change)playlist_client_cancel();}
 else if(strstr(r->path,"requests/")){receipts++;json="{\"request_id\":\"00000000-0000-4000-8000-000000000001\",\"status\":\"unknown\",\"zone_id\":\"sauna\",\"error\":\"playback could not be confirmed\"}";}
 else if(!strcmp(r->path,"/dial/v1/play")){play++;if(timeout)return -3;json="{\"request_id\":\"00000000-0000-4000-8000-000000000001\",\"status\":\"accepted\",\"zone_id\":\"sauna\"}";}
 assert(strlen(json)<cap);strcpy(out,json);*len=strlen(json);return 0;
}
int main(void){
 playlist_client_test_hooks(clock_ms,snapshot,exchange);playlist_client_init();playlist_client_connected(true);
 assert(playlist_client_request_favorites());assert(!playlist_client_request_favorites());playlist_client_test_run();
 playlist_client_result_t r;assert(playlist_client_take(&r)&&r.status==PLAYLIST_CLIENT_FAVORITES&&r.favorites.count==1);
 assert(playlist_client_request_play("opaque",3,7));playlist_client_test_run();assert(prepare==1&&play==1);assert(playlist_client_take(&r)&&r.status==PLAYLIST_CLIENT_ACCEPTED);
 now+=1000;playlist_client_process();playlist_client_test_run();assert(receipts==1&&play==1&&playlist_client_take(&r)&&r.status==PLAYLIST_CLIENT_UNKNOWN);
 timeout=true;assert(playlist_client_request_play("opaque",3,7));playlist_client_test_run();assert(play==2);assert(playlist_client_take(&r)&&r.status==PLAYLIST_CLIENT_WAITING);now+=1000;playlist_client_process();playlist_client_test_run();assert(play==2&&receipts==2);
 expire=true;assert(playlist_client_request_play("opaque",3,7));playlist_client_test_run();assert(play==2);playlist_client_take(&r);expire=false;
 zone_change=true;assert(playlist_client_request_play("opaque",3,7));playlist_client_test_run();assert(play==2);zone_change=false;
 old=true;assert(playlist_client_request_favorites());playlist_client_test_run();assert(playlist_client_take(&r)&&r.status==PLAYLIST_CLIENT_ERROR);
 char *reply=NULL;size_t length=0;
 assert(playlist_client_admin("GET","/dial/v1/playlists",NULL,1024,&reply,&length)==PLAYLIST_ADMIN_CAPABILITIES);
 old=false;
 assert(playlist_client_admin("GET","/dial/v1/playlists",NULL,1024,&reply,&length)==PLAYLIST_ADMIN_RESPONSE);
 deny_catalog=true;
 assert(playlist_client_admin("GET","/dial/v1/playlists",NULL,1024,&reply,&length)==PLAYLIST_ADMIN_REMOTE_STATUS);
 deny_catalog=false;
 playlist_client_connected(false);assert(!playlist_client_request_play("opaque",3,7));
 assert(playlist_client_admin("GET","/dial/v1/playlists",NULL,1024,&reply,&length)==PLAYLIST_ADMIN_ADMISSION);
 puts("playlist client no-replay/deadline PASS");
}
