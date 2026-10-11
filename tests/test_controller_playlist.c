#include "controller_playlist.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
 controller_playlist_t s={0};playlist_favorites_t f={.count=5,.revision=3,.core_epoch=7};
 strcpy(f.zone_id,"sauna");
 for(unsigned i=0;i<5;i++){snprintf(f.entries[i].id,sizeof f.entries[i].id,"id-%u",i);strcpy(f.entries[i].title,"Music");f.entries[i].available=true;}
 assert(controller_playlist_open(&s,&f,"other",100).kind==PLAYLIST_ACTION_NONE);
 assert(controller_playlist_open(&s,&f,"sauna",100).kind==PLAYLIST_ACTION_RENDER);
 assert(s.phase==PLAYLIST_PICKING && s.selected==0);
 controller_playlist_rotate(&s,-100);assert(s.selected==0);
 for(int i=1;i<=5;i++){controller_playlist_rotate(&s,1);assert(s.selected==i);}
 controller_playlist_rotate(&s,100);assert(s.selected==5);
 assert(controller_playlist_confirm(&s,101).kind==PLAYLIST_ACTION_CANCEL && s.phase==PLAYLIST_IDLE);
 controller_playlist_open(&s,&f,"sauna",100);
 controller_playlist_rotate(&s,3);
 playlist_action_t a=controller_playlist_confirm(&s,101);
 assert(a.kind==PLAYLIST_ACTION_PREPARE_PLAY && !strcmp(a.favorite_id,"id-3") && a.revision==3 && a.core_epoch==7);
 memset(&f,0,sizeof f);assert(!strcmp(s.favorites.entries[3].id,"id-3"));
 assert(controller_playlist_confirm(&s,102).kind==PLAYLIST_ACTION_NONE);
 assert(controller_playlist_tick(&s,15100).kind==PLAYLIST_ACTION_NONE);
 controller_playlist_cancel(&s);
 f.count=0;strcpy(f.zone_id,"sauna");controller_playlist_open(&s,&f,"sauna",100);
 assert(controller_playlist_tick(&s,15099).kind==PLAYLIST_ACTION_NONE);
 assert(controller_playlist_tick(&s,15100).kind==PLAYLIST_ACTION_CANCEL);
 f.count=6;assert(controller_playlist_open(&s,&f,"sauna",0).kind==PLAYLIST_ACTION_NONE);
 puts("shared playlist state PASS");
}
