#include "controller_playlist.h"
#include <string.h>
#include <limits.h>
static playlist_action_t action(playlist_action_kind_t kind) { return (playlist_action_t){.kind=kind}; }
bool playlist_label_copy(char *out,size_t capacity,const char *text,size_t limit) {
 if(!out||!capacity||!text)return false;
 size_t i=0,end=0;
 while(i<=limit && text[i]) {
  unsigned char c=(unsigned char)text[i];size_t n=c<0x80?1:(c>=0xc2&&c<=0xdf)?2:(c>=0xe0&&c<=0xef)?3:(c>=0xf0&&c<=0xf4)?4:0;
  if(!n||i+n>limit)return false;
  for(size_t j=1;j<n;j++)if(((unsigned char)text[i+j]&0xc0)!=0x80)return false;
  if(n==3&&((c==0xe0&&(unsigned char)text[i+1]<0xa0)||(c==0xed&&(unsigned char)text[i+1]>=0xa0)))return false;
  if(n==4&&((c==0xf0&&(unsigned char)text[i+1]<0x90)||(c==0xf4&&(unsigned char)text[i+1]>=0x90)))return false;
  i+=n;if(i<capacity)end=i;
 }
 if(i>limit)return false;
 memcpy(out,text,end);out[end]=0;return true;
}
playlist_action_t controller_playlist_open(controller_playlist_t *s,const playlist_favorites_t *f,const char *zone,uint64_t now) {
 if(!s||!f||!zone||f->count>5||!memchr(f->zone_name,0,sizeof f->zone_name)||!memchr(f->zone_id,0,sizeof f->zone_id)||strcmp(f->zone_id,zone)||!*zone)return action(PLAYLIST_ACTION_NONE);
 for(unsigned i=0;i<f->count;i++) {
  const playlist_favorite_t *e=&f->entries[i];
  if(!memchr(e->id,0,sizeof e->id)||!e->id[0]||!memchr(e->title,0,sizeof e->title))return action(PLAYLIST_ACTION_NONE);
  for(unsigned j=0;j<i;j++)if(!strcmp(e->id,f->entries[j].id))return action(PLAYLIST_ACTION_NONE);
 }
 *s=(controller_playlist_t){.phase=PLAYLIST_PICKING,.favorites=*f,.activity_ms=now};return action(PLAYLIST_ACTION_RENDER);
}
playlist_action_t controller_playlist_rotate(controller_playlist_t *s,int delta) {
 if(!s||s->phase!=PLAYLIST_PICKING||!delta)return action(PLAYLIST_ACTION_NONE);
 int64_t index=(int64_t)s->selected+delta;
 s->selected=index<0?0:index>(int64_t)s->favorites.count?(int)s->favorites.count:(int)index;
 return action(PLAYLIST_ACTION_RENDER);
}
playlist_action_t controller_playlist_cancel(controller_playlist_t *s) {
 if(!s)return action(PLAYLIST_ACTION_NONE);
 memset(s,0,sizeof *s);return action(PLAYLIST_ACTION_CANCEL);
}
playlist_action_t controller_playlist_confirm(controller_playlist_t *s,uint64_t now) {
 if(!s||s->phase!=PLAYLIST_PICKING)return action(PLAYLIST_ACTION_NONE);
 if(s->selected==(int)s->favorites.count)return controller_playlist_cancel(s);
 if(!s->favorites.entries[s->selected].available){s->phase=PLAYLIST_ERROR;s->activity_ms=now;return action(PLAYLIST_ACTION_RENDER);}
 s->phase=PLAYLIST_PREPARING;s->activity_ms=now;
 playlist_action_t a=action(PLAYLIST_ACTION_PREPARE_PLAY);
 strcpy(a.favorite_id,s->favorites.entries[s->selected].id);a.revision=s->favorites.revision;a.core_epoch=s->favorites.core_epoch;return a;
}
playlist_action_t controller_playlist_tick(controller_playlist_t *s,uint64_t now) {
 if(!s||s->phase==PLAYLIST_IDLE||s->phase==PLAYLIST_PREPARING||s->phase==PLAYLIST_PENDING)return action(PLAYLIST_ACTION_NONE);
 if(now<s->activity_ms||now-s->activity_ms>=PLAYLIST_INACTIVITY_MS)return controller_playlist_cancel(s);
 return action(PLAYLIST_ACTION_NONE);
}
