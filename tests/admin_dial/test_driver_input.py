"""Execute actual touch callback and rotation hooks, faking only LVGL/hardware."""
from pathlib import Path
import subprocess,tempfile
s=Path('idf_app/main/platform_display_idf.c').read_text()
globals=s[s.index('// Swipe gesture detection'):s.index('// LVGL tick timer (')]
a=s.index('static void lvgl_touch_read_cb(');b=s.index('\nbool platform_display_init',a)
# Retain exactly the callback body, excluding comments between functions.
touch=s[a:s.rfind('}',a,b)+1]
a=s.index('void platform_display_cancel_input(');b=s.index('void platform_display_apply_config',a)
hooks=s[a:b]
preamble=r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include "display_rotation_dial.h"
#include "valve_logic.h"
#define LCD_H_RES 360
#define LCD_V_RES 360
#define LV_INDEV_STATE_RELEASED 0
#define LV_INDEV_STATE_PRESSED 1
#define LV_DISPLAY_ROTATION_0 0
#define DISPLAY_STATE_NORMAL 0
#define DISPLAY_STATE_ART_MODE 1
typedef int display_state_t;
typedef int lv_indev_t;
typedef struct {dial_point_t point;int state;} lv_indev_data_t;
static int device,screen;
static int *s_touch_indev=&device,*s_display=&screen;
static uint8_t scratch,*s_rotate_buf=&scratch;
static uint32_t now=1000;
static bool pressed,visible,blocked,settings,zone,ready=true;
static uint16_t rawx,rawy;
static int cancels,touches,resets;
static dial_point_t delivered;
static bool tpGetCoordinates(uint16_t *x,uint16_t *y){*x=rawx;*y=rawy;return pressed;}
static int64_t esp_timer_get_time(void){return (int64_t)now*1000;}
static bool admin_settings_input_blocked(void){return blocked;}
static display_state_t display_get_state(void){return DISPLAY_STATE_NORMAL;}
static bool display_is_touch_suppressed(void){return false;}
static void display_activity_detected(void){}
static bool valve_ui_visible(void){return visible;}
static void valve_ui_wake(void){}
static void valve_ui_cancel_touch(void){cancels++;}
static void valve_ui_touch(int x,int y,bool press,bool moved,uint32_t time){(void)press;(void)moved;(void)time;touches++;delivered=(dial_point_t){x,y};}
static bool ui_is_zone_picker_visible(void){return zone;}
static bool ui_is_settings_visible(void){return settings;}
static bool bridge_client_is_ready_for_art_mode(void){return true;}
static void lv_indev_reset(lv_indev_t *i,void *obj){(void)i;(void)obj;resets++;}
static bool platform_display_is_ready(void){return ready;}
static void lv_display_set_rotation(int *d,int r){(void)d;assert(r==0);}
static void *lv_screen_active(void){return &screen;}
static void lv_obj_invalidate(void *p){(void)p;}
'''
main=r'''
static void sample(int x,int y,bool down,lv_indev_data_t *data){rawx=x;rawy=y;pressed=down;lvgl_touch_read_cb(&device,data);now+=20;}
static void physical(int angle,int x,int y,int *px,int *py){
 *px=x;*py=y;if(angle==90){*px=359-y;*py=x;}if(angle==180){*px=359-x;*py=359-y;}if(angle==270){*px=y;*py=359-x;}
}
int main(void){
 lv_indev_data_t data={0};
 ready=false;assert(!platform_display_try_rotation(90));assert(s_current_rotation==0);ready=true;
 for(int angle=0;angle<360;angle+=90){
  assert(platform_display_try_rotation(angle));
  sample(0,0,false,&data); /* release the cancelled old gesture */
  int x,y;physical(angle,100,260,&x,&y);visible=true;
  sample(x,y,true,&data);assert(delivered.x==100 && delivered.y==260 && data.state==0);
  sample(x,y,false,&data);
  visible=false;sample(x,y,true,&data);assert(data.point.x==100 && data.point.y==260 && data.state==1);
  sample(x,y,false,&data);
  /* Logical horizontal swipe works exactly once at every physical angle. */
  physical(angle,70,150,&x,&y);sample(x,y,true,&data);
  physical(angle,160,150,&x,&y);sample(x,y,true,&data);assert(s_pending_page_switch);
  sample(x,y,false,&data);s_pending_page_switch=false;
  /* Zone/settings overlays retain their own gesture ownership. */
  for(int mode=0;mode<2;mode++){
   zone=mode==0;settings=mode==1;
   physical(angle,70,150,&x,&y);sample(x,y,true,&data);
   physical(angle,160,150,&x,&y);sample(x,y,true,&data);
   sample(x,y,false,&data);assert(!s_pending_page_switch);
  }zone=false;settings=false;
  /* Rotating in a pressed sequence suppresses release/click and fresh holds
   * until a complete physical release. */
  visible=true;physical(angle,100,260,&x,&y);sample(x,y,true,&data);
  int before=touches;assert(platform_display_try_rotation((angle+90)%360));
  sample(x,y,true,&data);sample(x,y,false,&data);assert(touches==before);
  platform_display_rotation_override_set(true);
  platform_display_set_rotation(0);platform_display_set_rotation(180);
  assert(platform_display_rotation_get()==(angle+90)%360);
  platform_display_rotation_override_set(false);
 }
 platform_display_set_rotation(180);assert(s_current_rotation==180);
 platform_display_set_rotation(90);assert(s_current_rotation==0); /* legacy */
 assert(cancels>0 && resets>0);puts("production touch/gesture/override four angles: PASS");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'input.c';p.write_text(preamble+globals+touch+hooks+main)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Iidf_app/main',str(p),'idf_app/main/display_rotation_dial.c','idf_app/main/valve_logic.c','-o',d+'/test'],check=True)
 subprocess.run([d+'/test'],check=True)
