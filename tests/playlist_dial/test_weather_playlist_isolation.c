#define _POSIX_C_SOURCE 200809L
#include <stdatomic.h>
#include <time.h>
#include "playlist_ui_dial.h"
#include "playlist_client_dial.h"
#include "playlist_admin_dial.h"
#include "runtime_fixture.h"
#include "weather_status_fixtures.h"
#include "valve_ui_dial.h"
#include "admin_settings_dial.h"
#include "admin_store_dial.h"
#include "admin_server_dial.h"
#include "valve_client_dial.h"
#include "valve_config_dial.h"
#include "bridge_command_plan.h"
#include "controller_input.h"
#include "controller_action_router.h"
#include "controller_config.h"
#include "platform/platform_http.h"
#include "platform/platform_task.h"
#include "platform/platform_log.h"
#include "controller_presentation.h"
#include "lvgl.h"
#include "display_rotation_dial.h"
#include "esp_timer.h"
#include "freertos/queue.h"
#include "freertos/FreeRTOS.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static atomic_uint fake_now = ATOMIC_VAR_INIT(1000);
static int gets, valve_indicator;
static lv_obj_t objects[160];
static unsigned object_count;
static lv_obj_t screen;
uint64_t combined_clock_ms(void) {return fake_now;}
static uint64_t client_clock(void) {return fake_now;}
lv_obj_t *lv_screen_active(void) { return &screen; }
lv_obj_t *lv_obj_create(lv_obj_t *p) { (void)p; assert(object_count < 160); return &objects[object_count++]; }
lv_obj_t *lv_label_create(lv_obj_t *p) { return lv_obj_create(p); }
lv_obj_t *lv_btn_create(lv_obj_t *p) { return lv_obj_create(p); }
void lv_obj_set_size(lv_obj_t *o, int w, int h) { (void)o;(void)w;(void)h; }
void lv_obj_center(lv_obj_t *o) { (void)o; }
void lv_obj_set_style_bg_color(lv_obj_t *o, int c, int s) {(void)o;(void)c;(void)s;}
void lv_obj_set_style_bg_opa(lv_obj_t *o, int c, int s) {(void)o;(void)c;(void)s;}
void lv_obj_set_style_border_width(lv_obj_t *o, int c, int s) {(void)o;(void)c;(void)s;}
void lv_obj_set_style_radius(lv_obj_t *o, int c, int s) {(void)o;(void)c;(void)s;}
void lv_obj_set_style_pad_all(lv_obj_t *o, int c, int s) {(void)o;(void)c;(void)s;}
void lv_obj_add_flag(lv_obj_t *o, unsigned f) { o->flags |= f; }
void lv_obj_clear_flag(lv_obj_t *o, unsigned f) { o->flags &= ~f; }
void lv_obj_align(lv_obj_t *o, int a, int x, int y) {(void)o;(void)a;(void)x;(void)y;}
void lv_obj_set_pos(lv_obj_t *o, int x, int y) {(void)o;(void)x;(void)y;}
void lv_label_set_text(lv_obj_t *o, const char *s) { snprintf(o->text, sizeof o->text, "%s", s); }
void lv_obj_move_foreground(lv_obj_t *o) { (void)o; }
void lv_obj_add_state(lv_obj_t *o, unsigned s) { o->state |= s; }
void lv_obj_clear_state(lv_obj_t *o, unsigned s) { o->state &= ~s; }
int lv_color_hex(unsigned c) { return (int)c; }
void lv_obj_set_style_border_color(lv_obj_t *o, int c, int s) {(void)o;(void)c;(void)s;}
void ui_set_valve_active(bool active) { valve_indicator = active; }


static unsigned start_posts,drain_posts,media_posts,pi_fixture=3;
static uint32_t config_generation;
uint32_t weather_admin_generation(void){return 0;}
uint32_t valve_config_generation(void){return config_generation;}
bool valve_config_load(char *url,size_t ul,char *token,size_t tl){assert(ul>25&&tl>10);strcpy(url,"http://fake-pi:8081");strcpy(token,"synthetic-display");return true;}
static int fake_pi_transport(const char *method,const char *url,const char *token,const char *body,size_t request_len,int *status,char *response,size_t capacity,size_t *length,void *context){
 (void)context;assert(!strcmp(token,"synthetic-display"));assert(strstr(url,"http://fake-pi:8081/api/v1/display/")==url);const char *value=weather_fixture_0;
 if(!strcmp(method,"POST")){
  assert(body&&request_len==strlen(body));
  if(strstr(url,"/stop")){drain_posts++;pi_fixture=2;}else{start_posts++;assert(!"media input leaked a shower START");}
  const char *prefix="{\"request_id\":\"";assert(!strncmp(body,prefix,strlen(prefix)));char id[37];memcpy(id,body+strlen(prefix),36);id[36]=0;
  int n=snprintf(response,capacity,"{\"request_id\":\"%s\",\"accepted\":true,\"control_revision\":5,\"status\":%s}",id,weather_fixture_2);assert(n>0&&(size_t)n<capacity);*length=(size_t)n;
 }else{
  assert(!strcmp(method,"GET")&&!body);gets++;
  if(pi_fixture==1)value=weather_fixture_1;else if(pi_fixture==2)value=weather_fixture_2;else if(pi_fixture==3)value=weather_fixture_3;
  assert(strlen(value)<capacity);strcpy(response,value);*length=strlen(value);
 }
 *status=200;return 0;
}
int platform_http_post_json(const char *url,const char *json,char **out,size_t *len){(void)url;(void)json;media_posts++;*out=NULL;*len=0;return 0;}
void platform_http_free(char *ptr) { free(ptr); }
void platform_log_backend(const char *level, const char *fmt, va_list args) {
    (void)level; vfprintf(stderr, fmt, args); fputc('\n', stderr);
}
void controller_presentation_zone_picker_scroll(int delta) { (void)delta; }
void controller_presentation_show_settings(void) {}
void controller_presentation_show_volume_change(float volume, float step) {
    (void)volume; (void)step;
}
void controller_presentation_hide_zone_picker(void) {}
void controller_presentation_zone_picker_get_selected_id(char *out, size_t len) {
    if (len) out[0] = 0;
}
bool controller_presentation_zone_picker_is_current_selection(void) { return false; }
void controller_presentation_set_network_status(const char *status) { (void)status; }
void controller_presentation_set_zone_name(const char *name) { (void)name; }
void controller_presentation_set_message(const char *msg) { (void)msg; }
void controller_presentation_show_zone_picker(const char **names, const char **ids,
                                               int count, int selected) {
    (void)names; (void)ids; (void)count; (void)selected;
}
controller_config_write_result_t controller_config_set_zone(
    const char *id, controller_config_snapshot_t *out) {
    (void)id; (void)out; return CONTROLLER_CONFIG_NOT_COMMITTED;
}
static void flush(void) {
    valve_client_test_run_pending();
    valve_ui_process(fake_now, true);
}

extern atomic_bool combined_open_blocked,combined_release_open;
static controller_playlist_t rendered;
static char media_message[160];
static unsigned input_fences,screen_fences;
bool playlist_ui_context_allowed(void){return !valve_ui_visible();}
bool ui_playlist_touch_allowed(int x,int y){return x>=40&&x<=320&&y>=124&&y<260;}
bool playlist_ui_current_zone(char *out,size_t capacity){assert(capacity>=6);strcpy(out,"sauna");return true;}
uint64_t playlist_ui_now_ms(void){return fake_now;}
void playlist_ui_render(const controller_playlist_t *state,const char *message){rendered=*state;snprintf(media_message,sizeof media_message,"%s",message);}
void platform_input_fence(void){input_fences++;}

#define LCD_H_RES 360
#define LCD_V_RES 360
#define LV_INDEV_STATE_RELEASED 0
#define LV_INDEV_STATE_PRESSED 1
#define DISPLAY_STATE_NORMAL 0
#define DISPLAY_STATE_ART_MODE 1
typedef int display_state_t;
typedef int lv_indev_t;
typedef struct {dial_point_t point;int state;} lv_indev_data_t;
static int device;
static int *s_touch_indev=&device;
static bool pressed;
static uint16_t rawx,rawy;
static bool tpGetCoordinates(uint16_t *x,uint16_t *y){*x=rawx;*y=rawy;return pressed;}
static display_state_t display_get_state(void){return DISPLAY_STATE_NORMAL;}
bool admin_settings_input_blocked(void){return false;}
static bool display_is_touch_suppressed(void){return false;}
static void display_activity_detected(void){}
static bool ui_is_zone_picker_visible(void){return false;}
static bool ui_is_settings_visible(void){return false;}
static bool bridge_client_is_ready_for_art_mode(void){return true;}
static void lv_indev_reset(lv_indev_t *i,void *obj){(void)i;(void)obj;screen_fences++;}
static void display_art_mode(void){assert(!"stray art gesture");}
static void display_wake(void){}
static void display_process_pending(void){}
#include "production_touch.inc"
static void sample(int x,int y,bool down){
 rawx=x;rawy=y;
 if(s_current_rotation==90){rawx=359-y;rawy=x;}
 if(s_current_rotation==180){rawx=359-x;rawy=359-y;}
 if(s_current_rotation==270){rawx=y;rawy=359-x;}
 pressed=down;lv_indev_data_t data={0};lvgl_touch_read_cb(&device,&data);
}
static void swipe(void){
 fake_now+=100;sample(80,180,true);fake_now+=100;sample(220,180,true);sample(220,180,false);
 assert(s_pending_page_switch);platform_display_process_pending();
 assert(!playlist_ui_visible());
}

static void pause_ms(unsigned ms){struct timespec t={.tv_nsec=(long)ms*1000000};nanosleep(&t,NULL);}
static bool label(const char *text){for(unsigned i=0;i<object_count;i++)if(!strcmp(objects[i].text,text))return true;return false;}
static void open_picker(void){
 if(valve_ui_visible()){swipe();assert(!valve_ui_visible());}
 controller_input_set_context(CONTROLLER_INTERACTION_CONTEXT_MEDIA);
 sample(180,180,true);fake_now+=2000;sample(180,180,true);
 assert(playlist_ui_visible());sample(180,180,false);
}
static void await_stall(void){uint64_t limit=runtime_wall_ms()+500;while(!atomic_load(&combined_open_blocked)&&runtime_wall_ms()<limit)pause_ms(1);assert(atomic_load(&combined_open_blocked));}
int main(void){
 assert(!s_local_rotation_override);
 valve_client_test_clock(client_clock);valve_client_test_transport(fake_pi_transport,NULL);assert(valve_client_selected_duration_publish(180));valve_ui_init();valve_client_on_reconnect();valve_ui_connected(true);
 assert(playlist_private_save("http://bridge/roon","synthetic-private"));playlist_client_connected(true);assert(playlist_client_init());

 const unsigned modes[]={3,1,2};
 for(unsigned angle=0;angle<360;angle+=90)for(unsigned mode=0;mode<3;mode++)for(unsigned phase=0;phase<5;phase++){
  s_current_rotation=angle;pi_fixture=modes[mode];
  if(!valve_ui_visible()){swipe();assert(valve_ui_visible());}
  valve_ui_wake();flush();
  assert(label(modes[mode]==3?"AUTO":modes[mode]==1?"3:00":"HOLD 2s\n3 MIN"));
  runtime_http_case(phase==0?HTTP_WEATHER_STALLED_OPEN:HTTP_WEATHER_NORMAL);
  atomic_store(&combined_open_blocked,false);atomic_store(&combined_release_open,false);
  open_picker();unsigned writes=0;bool stalled=phase==0||phase==2||phase==3;
  if(phase==0){await_stall();assert(rendered.phase==PLAYLIST_PREPARING);}
  else {
   runtime_wait_cleanup(2);playlist_ui_process();assert(rendered.phase==PLAYLIST_PICKING);
   assert(atomic_load(&fixture_body_writes)==0); /* opening release is consumed */
   if(phase>=2){
    if(phase==2)runtime_http_case(HTTP_WEATHER_STALLED_OPEN);
    fake_now++;sample(180,180,true);fake_now+=20;sample(180,180,false);
    if(phase==2){await_stall();assert(rendered.phase==PLAYLIST_PREPARING);}
    else {
     runtime_wait_cleanup(5);playlist_ui_process();assert(rendered.phase==PLAYLIST_PENDING);
     if(phase==3){runtime_http_case(HTTP_WEATHER_STALLED_OPEN);fake_now+=1000;playlist_ui_process();await_stall();}
     else {fake_now+=15000;playlist_ui_process();assert(rendered.phase==PLAYLIST_ERROR);}
    }
   }
   writes=atomic_load(&fixture_body_writes);
  }
  unsigned before_stops=drain_posts;swipe();assert(valve_ui_visible());valve_ui_wake();flush();
  assert(start_posts==0&&media_posts==0&&drain_posts==before_stops);
  if(modes[mode]!=2){sample(100,260,true);fake_now++;sample(100,260,false);flush();assert(drain_posts==before_stops+1&&pi_fixture==2&&label("OFF"));}
  if(stalled){assert(atomic_load(&combined_open_blocked)&&!atomic_load(&combined_release_open));atomic_store(&combined_release_open,true);runtime_wait_cleanup(1);}
  playlist_ui_process();assert(!playlist_ui_visible()&&atomic_load(&fixture_body_writes)==writes);
  fake_now+=16000;playlist_ui_process();assert(!playlist_ui_visible()&&start_posts==0&&media_posts==0);
 }
 assert(input_fences>0&&screen_fences>0);
 puts("production callback -> deferred page -> real valve STOP across five picker phases, three modes, four rotations; stalled real worker cancellation PASS");return 0;
}
