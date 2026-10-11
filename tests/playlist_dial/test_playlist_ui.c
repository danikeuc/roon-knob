#include "playlist_ui_dial.h"
#include "playlist_client_dial.h"
#include "controller_input.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static char rendered[160];
static const char *current_zone="sauna";static uint64_t clock_value;static bool allowed=true,blocked;static unsigned plays,fences,transitions;static playlist_client_result_t result;static bool ready;
bool playlist_ui_context_allowed(void){return !blocked;}
bool ui_playlist_touch_allowed(int x,int y){return allowed&&x>=40&&x<=320&&y>=124&&y<260;}
void platform_input_fence(void){fences++;}
void platform_display_playlist_transition(void){transitions++;}
bool playlist_client_request_favorites(void){return true;}
bool playlist_client_request_play(const char *id,uint64_t r,uint64_t e){assert(!strcmp(id,"id")&&r==3&&e==7);plays++;return true;}
void playlist_client_cancel(void){}
void playlist_client_process(void){}
bool playlist_client_take(playlist_client_result_t *out){if(!ready)return false;*out=result;ready=false;return true;}
void playlist_ui_render(const controller_playlist_t *s,const char *message){(void)s;snprintf(rendered,sizeof rendered,"%s",message);}
uint64_t playlist_ui_now_ms(void){return clock_value;}
static void favorites(void){result=(playlist_client_result_t){.status=PLAYLIST_CLIENT_FAVORITES,.favorites={.count=1,.revision=3,.core_epoch=7}};strcpy(result.favorites.zone_id,"sauna");strcpy(result.favorites.entries[0].id,"id");strcpy(result.favorites.entries[0].title,"Music");result.favorites.entries[0].available=true;ready=true;}
bool playlist_ui_current_zone(char *out,size_t cap){assert(cap>=6);strcpy(out,current_zone);return true;}
int main(void){
 controller_input_set_context(CONTROLLER_INTERACTION_CONTEXT_MEDIA);
 assert(!playlist_ui_touch(180,180,true,false,0));assert(!playlist_ui_touch(180,180,true,false,1999));
 assert(playlist_ui_touch(180,180,true,false,2000)&&playlist_ui_visible());favorites();clock_value=2001;playlist_ui_process();
 assert(playlist_ui_touch(180,180,false,false,2002)&&plays==0);
 assert(playlist_ui_touch(180,180,true,false,2010));assert(playlist_ui_touch(180,180,false,false,2030)&&plays==1);
 assert(playlist_ui_touch(180,180,true,false,2040));assert(playlist_ui_touch(180,180,false,false,2050)&&plays==1);
 playlist_ui_cancel();assert(!playlist_ui_visible()&&fences==2&&transitions==2);
 assert(!playlist_ui_touch(180,180,true,false,3000));assert(!playlist_ui_touch(180,180,true,true,5000));assert(!playlist_ui_visible());playlist_ui_touch(180,180,false,true,5001);
 allowed=false;assert(!playlist_ui_touch(180,180,true,false,6000));assert(!playlist_ui_touch(180,180,true,false,8000));assert(!playlist_ui_visible());allowed=true;playlist_ui_touch(180,180,false,false,8001);
 blocked=true;assert(!playlist_ui_touch(180,180,true,false,9000));assert(!playlist_ui_touch(180,180,true,false,11000));blocked=false;playlist_ui_touch(180,180,false,false,11001);
 assert(!playlist_ui_touch(180,180,true,false,12000));assert(playlist_ui_touch(180,180,true,false,14000));favorites();clock_value=14001;playlist_ui_process();playlist_ui_touch(180,180,false,false,14002);
 playlist_ui_rotate(1);clock_value=14003;playlist_ui_touch(180,180,true,false,14004);playlist_ui_touch(180,180,false,false,14005);assert(!playlist_ui_visible()&&plays==1);
 assert(!playlist_ui_touch(180,180,true,false,16000));assert(playlist_ui_touch(180,180,true,false,18000));favorites();clock_value=18001;playlist_ui_process();playlist_ui_touch(180,180,false,false,18002);
 current_zone="other-zone";playlist_ui_touch(180,180,true,false,18003);playlist_ui_touch(180,180,false,false,18004);assert(plays==1);playlist_ui_cancel();
 current_zone="sauna";
 for(int accepted=0;accepted<2;accepted++) {
  clock_value+=3000;playlist_ui_touch(180,180,true,false,clock_value);
  clock_value+=2000;assert(playlist_ui_touch(180,180,true,false,clock_value));
  favorites();playlist_ui_process();playlist_ui_touch(180,180,false,false,clock_value+1);
  unsigned before=plays;
  if(accepted){result=(playlist_client_result_t){.status=PLAYLIST_CLIENT_ACCEPTED};ready=true;playlist_ui_process();assert(strstr(rendered,"Ukaz sprejet"));}
  result=(playlist_client_result_t){.status=PLAYLIST_CLIENT_UNKNOWN};ready=true;playlist_ui_process();
  assert((strstr(rendered,"Ukaz sprejet")!=NULL)==(accepted!=0));
  assert(plays==before);playlist_ui_cancel();
 }
 puts("playlist production gesture release/new-tap and confirmation zone binding PASS");
}
