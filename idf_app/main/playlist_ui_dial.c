#include "playlist_ui_dial.h"
#include "playlist_client_dial.h"
#include "controller_input.h"
#include "ui.h"
#include "platform/platform_input.h"
#ifndef PLAYLIST_UI_HOST_TEST
#include "platform_display_idf.h"
#else
void platform_display_playlist_transition(void);
#endif
#include <string.h>
#include <stdio.h>
#ifndef PLAYLIST_UI_HOST_TEST
#include "valve_ui_dial.h"
#include "admin_settings_dial.h"
#include "display_sleep.h"
#include "bridge_client.h"
#include "esp_timer.h"
#include "lvgl.h"
#endif
static controller_playlist_t state;
static bool shown,loading,candidate,movement,consume_release,new_press,request_accepted;
static uint64_t press_ms;
static char message[97];
#ifndef PLAYLIST_UI_HOST_TEST
static lv_obj_t *panel,*title,*label,*hint,*target;
bool playlist_ui_context_allowed(void){return !valve_ui_visible()&&!admin_settings_input_blocked()&&!ui_is_zone_picker_visible()&&!ui_is_settings_visible()&&display_get_state()==DISPLAY_STATE_NORMAL&&!display_is_touch_suppressed();}
bool playlist_ui_current_zone(char *out,size_t cap){return bridge_client_get_current_zone_id(out,cap);}
uint64_t playlist_ui_now_ms(void){return (uint64_t)(esp_timer_get_time()/1000);}
static void playlist_ui_render(const controller_playlist_t *s,const char *text){
 if(!panel){panel=lv_obj_create(lv_screen_active());lv_obj_set_size(panel,360,360);lv_obj_center(panel);lv_obj_set_style_bg_color(panel,lv_color_hex(0x111827),0);lv_obj_set_style_bg_opa(panel,LV_OPA_COVER,0);title=lv_label_create(panel);lv_label_set_text(title,"Seznami predvajanja");lv_obj_align(title,LV_ALIGN_TOP_MID,0,54);target=lv_label_create(panel);lv_obj_set_width(target,280);lv_obj_set_style_text_align(target,LV_TEXT_ALIGN_CENTER,0);lv_label_set_long_mode(target,LV_LABEL_LONG_DOT);lv_obj_align(target,LV_ALIGN_TOP_MID,0,88);label=lv_label_create(panel);lv_obj_set_width(label,280);lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);lv_label_set_long_mode(label,LV_LABEL_LONG_DOT);lv_obj_center(label);hint=lv_label_create(panel);lv_obj_set_width(hint,290);lv_obj_set_style_text_align(hint,LV_TEXT_ALIGN_CENTER,0);lv_obj_align(hint,LV_ALIGN_BOTTOM_MID,0,-65);
 /* The picker has a fixed dark background; do not inherit theme text colors. */
 lv_obj_set_style_text_color(title,lv_color_hex(0xF9FAFB),0);
 lv_obj_set_style_text_color(label,lv_color_hex(0xF9FAFB),0);
 lv_obj_set_style_text_color(target,lv_color_hex(0xCBD5E1),0);
 lv_obj_set_style_text_color(hint,lv_color_hex(0xCBD5E1),0);
 }
 if(s->phase==PLAYLIST_IDLE){lv_obj_add_flag(panel,LV_OBJ_FLAG_HIDDEN);return;}
 lv_label_set_text(target,s->favorites.zone_name[0]?s->favorites.zone_name:"Območje še ni potrjeno");
 lv_obj_clear_flag(panel,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(panel);
 char line[160];if(s->phase==PLAYLIST_PICKING){snprintf(line,sizeof line,"%s",s->selected==(int)s->favorites.count?"Nazaj":s->favorites.entries[s->selected].title);lv_label_set_text(label,line);lv_label_set_text(hint,"Obrnite za izbiro · tap za predvajanje");}
 else{lv_label_set_text(label,text);lv_label_set_text(hint,s->phase==PLAYLIST_ERROR?"Tap za zapiranje":"Prosimo počakajte");}
}
#endif
static void transition(void){platform_input_fence();platform_display_playlist_transition();}
bool playlist_ui_visible(void){return shown;}
void playlist_ui_cancel(void){
 bool visible=playlist_ui_visible();shown=false;loading=false;request_accepted=false;candidate=false;new_press=false;movement=false;consume_release=false;
 controller_playlist_cancel(&state);playlist_client_cancel();if(visible){controller_input_set_context(CONTROLLER_INTERACTION_CONTEXT_MEDIA);transition();playlist_ui_render(&state,"");}
}
static void show_error(const char *text,uint64_t now){state.phase=PLAYLIST_ERROR;state.activity_ms=now;strncpy(message,text,sizeof message-1);message[sizeof message-1]=0;playlist_ui_render(&state,message);}
bool playlist_ui_touch(int x,int y,bool pressed,bool moved,uint64_t now){
 if(playlist_ui_visible()) {
  if(consume_release){if(!pressed)consume_release=false;return true;}
  if(pressed){if(!new_press){new_press=true;press_ms=now;movement=false;}movement=movement||moved;return true;}
  bool tap=new_press&&!movement&&!moved&&now>=press_ms&&now-press_ms<500;new_press=false;
  if(!tap)return true;
  if(state.phase==PLAYLIST_ERROR){playlist_ui_cancel();return true;}
  char zone[129];
  if(!playlist_ui_context_allowed()||!playlist_ui_current_zone(zone,sizeof zone)||strcmp(zone,state.favorites.zone_id)){playlist_client_cancel();show_error("Območje se je spremenilo. Brez predvajanja.",now);return true;}
  playlist_action_t a=controller_playlist_confirm(&state,now);
  if(a.kind==PLAYLIST_ACTION_CANCEL){playlist_ui_cancel();return true;}
  if(a.kind==PLAYLIST_ACTION_PREPARE_PLAY){if(!playlist_client_request_play(a.favorite_id,a.revision,a.core_epoch))show_error("Ni na voljo. Poskusite znova z novim tapom.",now);else{strcpy(message,"Pripravljam predvajanje …");playlist_ui_render(&state,message);}}
  else if(a.kind==PLAYLIST_ACTION_RENDER)show_error("Ponovno izberite seznam v nastavitvah.",now);
  return true;
 }
 if(!pressed){candidate=false;movement=false;return false;}
 if(!playlist_ui_context_allowed()||controller_input_get_context()!=CONTROLLER_INTERACTION_CONTEXT_MEDIA||!ui_playlist_touch_allowed(x,y)){candidate=false;return false;}
 if(!candidate){candidate=true;press_ms=now;movement=moved;}movement=movement||moved;
 if(movement||now<press_ms||now-press_ms<2000)return false;
 candidate=false;consume_release=true;shown=true;loading=true;request_accepted=false;state.phase=PLAYLIST_PREPARING;state.activity_ms=now;strcpy(message,"Nalagam sezname …");
 controller_input_set_context(CONTROLLER_INTERACTION_CONTEXT_PLAYLIST_PICKER);transition();playlist_ui_render(&state,message);
 if(!playlist_client_request_favorites()){loading=false;show_error("Seznami niso na voljo.",now);}return true;
}
void playlist_ui_rotate(int delta){if(!playlist_ui_visible())return;playlist_action_t a=controller_playlist_rotate(&state,delta);if(a.kind==PLAYLIST_ACTION_RENDER){state.activity_ms=playlist_ui_now_ms();playlist_ui_render(&state,"");}}
void playlist_ui_process(void){
 if(!playlist_ui_visible())return;
 uint64_t now=playlist_ui_now_ms();
 if(!playlist_ui_context_allowed()){playlist_ui_cancel();return;}
 if(!loading && state.phase!=PLAYLIST_ERROR){char zone[129];if(!playlist_ui_current_zone(zone,sizeof zone)||strcmp(zone,state.favorites.zone_id)){playlist_client_cancel();show_error("Območje se je spremenilo. Brez predvajanja.",now);return;}}
 playlist_client_process();playlist_client_result_t result;
 if(playlist_client_take(&result)) {
  loading=false;
  if(result.status==PLAYLIST_CLIENT_FAVORITES){char zone[129];if(!playlist_ui_current_zone(zone,sizeof zone)||controller_playlist_open(&state,&result.favorites,zone,now).kind!=PLAYLIST_ACTION_RENDER)show_error("Območje se je spremenilo. Ponovno izberite.",now);else playlist_ui_render(&state,"");}
  else if(result.status==PLAYLIST_CLIENT_ACCEPTED||result.status==PLAYLIST_CLIENT_WAITING){if(result.status==PLAYLIST_CLIENT_ACCEPTED)request_accepted=true;state.phase=PLAYLIST_PENDING;strcpy(message,request_accepted?"Ukaz sprejet. Preverjam stanje …":"Čakam na odgovor …");playlist_ui_render(&state,message);}
  else if(result.status==PLAYLIST_CLIENT_PLAYING)show_error("Predvajanje je potrjeno.",now);
  else if(result.status==PLAYLIST_CLIENT_UNKNOWN)show_error(request_accepted?"Ukaz sprejet. Predvajanje ni preverjeno.":"Izid ukaza ni znan. Brez ponovitve.",now);
  else show_error("Seznam ni na voljo. Ponovno izberite v nastavitvah.",now);
 }
 if(loading&&(now<state.activity_ms||now-state.activity_ms>=5000)){loading=false;playlist_client_cancel();show_error("Nalagam predolgo. Stanje ni potrjeno.",now);}
 if(state.phase==PLAYLIST_PREPARING&&!loading&&(now<state.activity_ms||now-state.activity_ms>=10000)){playlist_client_cancel();show_error("Odgovor ni potrjen. Brez ponovitve.",now);}
 if(controller_playlist_tick(&state,now).kind==PLAYLIST_ACTION_CANCEL)playlist_ui_cancel();
}
