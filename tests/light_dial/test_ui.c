#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "light_client_dial.h"
#include "light_ui_dial.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
static lv_obj_t objects[160],screen,top;
static unsigned object_count,gen,sess=1,requests,actions;
static uint64_t now=1000;
static bool shower;
static light_action_t last_action;
struct fake_queue {size_t size;bool ready;char data[512];};
static struct fake_queue queue;
QueueHandle_t xQueueCreate(unsigned capacity,size_t size){assert(capacity==1&&size<=512);queue.size=size;return &queue;}
int xQueueOverwrite(QueueHandle_t q,const void *v){memcpy(q->data,v,q->size);q->ready=true;return pdTRUE;}
int xQueueReceive(QueueHandle_t q,void *v,unsigned ticks){(void)ticks;if(!q->ready)return pdFALSE;memcpy(v,q->data,q->size);q->ready=false;return pdTRUE;}
lv_obj_t *lv_layer_top(void){return &top;}
lv_obj_t *lv_screen_active(void) { return &screen; }
lv_obj_t *lv_obj_create(lv_obj_t *p) {
    assert(object_count < 160);
    lv_obj_t *obj = &objects[object_count++];
    obj->parent = p;
    return obj;
}
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
void lv_obj_set_pos(lv_obj_t *o, int x, int y) {o->x = x; o->y = y;}
void lv_label_set_text(lv_obj_t *o, const char *s) { snprintf(o->text, sizeof o->text, "%s", s); }
void lv_obj_move_foreground(lv_obj_t *o) { (void)o; }
void lv_obj_add_state(lv_obj_t *o, unsigned s) { o->state |= s; }
void lv_obj_clear_state(lv_obj_t *o, unsigned s) { o->state &= ~s; }
int lv_color_hex(unsigned c) { return (int)c; }
void lv_obj_set_style_border_color(lv_obj_t *o, int c, int s) {(void)o;(void)c;(void)s;}

const lv_font_t *font_manager_get_small(void){return &lv_font_montserrat_20;}
unsigned light_config_generation(void){return gen;}
unsigned light_client_session(void){return sess;}
uint64_t light_client_now(void){return now;}
bool light_client_start(void){return true;}
bool valve_ui_visible(void){return shower;}
void valve_ui_show(bool show){shower=show;}
bool light_client_request(const char *kind,const char *body,unsigned offset,light_callback_t cb,void *ctx){(void)kind;(void)body;(void)offset;(void)cb;(void)ctx;requests++;return true;}
bool light_client_action(light_action_t action,uint64_t revision,unsigned generation,light_callback_t cb,void *ctx){(void)revision;(void)generation;(void)cb;(void)ctx;actions++;last_action=action;return true;}
bool light_settings_parse(const char *b,size_t n,uint64_t *r,unsigned *h,unsigned *m){(void)b;(void)n;(void)r;(void)h;(void)m;return false;}
bool light_status_parse(const char *body,size_t n,light_status_t *s){(void)body;(void)n;(void)s;return false;}
bool light_status_fresh(const light_status_t *s,uint64_t received,uint64_t current){return s&&current>=received&&current-received<LIGHT_STALE_MS;}
#include "../../idf_app/main/light_ui_dial.c"
static void publish(light_mode_t mode,unsigned seconds){completion_t c={.ok=true,.generation=gen,.session=sess,.received=now,.status={.revision=42,.mode=mode,.manual_remaining_seconds=seconds}};strcpy(c.status.health,"OK");xQueueOverwrite(completions,&c);light_ui_process();}
static void tap(int x,int y){light_ui_touch(x,y,true,false,(uint32_t)now);light_ui_touch(x,y,false,false,(uint32_t)now);}
int main(void){
 light_ui_init();settings_known=true;settings_at=now;manual_seconds=3600;assert(!light_ui_capable());light_ui_next_page(1);assert(shower&&!light_ui_visible());light_ui_next_page(1);assert(!shower&&!light_ui_visible());
 publish(LIGHT_MANUAL_OFF,1);assert(light_ui_capable());assert(!(badge->flags&LV_OBJ_FLAG_HIDDEN));now+=2000;light_ui_process();assert(!(badge->flags&LV_OBJ_FLAG_HIDDEN));assert(strstr(timer->text,"M"));now+=10000;light_ui_process();assert(!(badge->flags&LV_OBJ_FLAG_HIDDEN));assert(strstr(badge->text,"?"));
 publish(LIGHT_AUTO,0);assert(badge->flags&LV_OBJ_FLAG_HIDDEN);light_ui_next_page(-1);assert(light_ui_visible()&&!shower);light_ui_next_page(-1);assert(shower&&!light_ui_visible());light_ui_next_page(-1);assert(!shower&&!light_ui_visible());light_ui_next_page(1);assert(shower);light_ui_next_page(1);assert(light_ui_visible());light_ui_next_page(1);assert(!shower&&!light_ui_visible());
 light_ui_show(true);pending=false;settings_known=true;settings_at=now;tap(180,145);assert(actions==1&&last_action==LIGHT_ACTION_TIMED_ON);pending=false;publish(LIGHT_AUTO,0);status.desired_on=true;tap(180,145);assert(actions==2&&last_action==LIGHT_ACTION_OFF);
 pending=false;settings_at=now;settings_known=true;publish(LIGHT_MANUAL_ON,60);tap(180,145);assert(actions==3&&last_action==LIGHT_ACTION_MANUAL_OFF);pending=false;publish(LIGHT_MANUAL_OFF,60);tap(180,145);assert(actions==4&&last_action==LIGHT_ACTION_MANUAL_ON);
 pending=false;publish(LIGHT_MANUAL_OFF,60);light_ui_touch(180,145,true,false,now);sess++;light_ui_touch(180,145,false,false,now);assert(actions==4);light_ui_show(false);light_ui_process();assert(!(badge->flags&LV_OBJ_FLAG_HIDDEN));gen+=2;light_ui_process();assert(!light_ui_capable()&&!light_ui_visible());
 puts("production light UI: 3 pages both directions, AUTO tap, explicit manual, offline zero M reminder, config/session touch fences PASS");
}
