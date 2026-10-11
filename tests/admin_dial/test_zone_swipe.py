"""Exercise down-swipe through production touch and deferred action routing."""
import ast, subprocess, tempfile
from pathlib import Path
source = Path('idf_app/main/platform_display_idf.c').read_text()
fixture = ast.parse(Path('tests/admin_dial/test_driver_input.py').read_text())
preamble = next(ast.literal_eval(n.value) for n in fixture.body if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'preamble' for t in n.targets))
preamble = preamble.replace('static display_state_t display_get_state(void){return DISPLAY_STATE_NORMAL;}', 'static int display_state; static display_state_t display_get_state(void){return display_state;}')
preamble = preamble.replace('static void display_activity_detected(void){}', 'static void display_activity_detected(void){display_state=DISPLAY_STATE_NORMAL;}')
preamble = preamble.replace('static bool playlist_ui_visible(void){return false;}', 'static bool picker; static bool playlist_ui_visible(void){return picker;}')
preamble = preamble.replace('static void playlist_ui_cancel(void){}', 'static void playlist_ui_cancel(void){picker=false;}')
preamble = preamble.replace('(void)time;return false;}', '(void)time;return picker;}')
preamble += r"""
#include "controller_input.h"
static unsigned opened;
static void valve_ui_toggle_page(void){}
static void display_art_mode(void){}
static void display_wake(void){display_state=DISPLAY_STATE_NORMAL;}
static void display_process_pending(void){}
bool controller_input_dispatch_action(const controller_action_t *action){
 assert(action->kind==CONTROLLER_ACTION_OPEN_ZONE_PICKER);opened++;zone=true;return true;
}
"""
parts=[source[source.index('// Swipe gesture detection'):source.index('// LVGL tick timer (')]]
for start,end in [('static void lvgl_touch_read_cb(', '\nbool platform_display_init'),('void platform_display_process_pending(', '\nvoid platform_display_cancel_input'),('void platform_display_cancel_input(', '\nvoid platform_display_apply_config')]:
 a=source.index(start); b=source.index(end,a);parts.append(source[a:source.rfind('}',a,b)+1])
main=r"""
static void sample(int angle,int x,int y,bool down,lv_indev_data_t *data){
 rawx=x;rawy=y;if(angle==90){rawx=359-y;rawy=x;}if(angle==180){rawx=359-x;rawy=359-y;}if(angle==270){rawx=y;rawy=359-x;}
 pressed=down;lvgl_touch_read_cb(&device,data);now+=50;
}
int main(void){
 lv_indev_data_t data={0};
 for(int angle=0;angle<360;angle+=90){
  assert(platform_display_try_rotation(angle)); sample(angle,0,0,false,&data);
  for(int mode=0;mode<3;mode++){
   zone=false;picker=mode==2;display_state=mode==1?DISPLAY_STATE_ART_MODE:DISPLAY_STATE_NORMAL;
   unsigned before=opened;sample(angle,180,60,true,&data);sample(angle,180,180,true,&data);sample(angle,180,180,false,&data);
   platform_display_process_pending();assert(opened==before+1 && zone && !picker);
   platform_display_process_pending();assert(opened==before+1);zone=false;
  }
  for(int block=0;block<6;block++){
   visible=block==0;settings=block==1;zone=block==2;blocked=block==3;
   display_state=block>=4?block:DISPLAY_STATE_NORMAL;unsigned before=opened;
   sample(angle,180,60,true,&data);sample(angle,180,180,true,&data);sample(angle,180,180,false,&data);
   platform_display_process_pending();assert(opened==before);
  }visible=settings=zone=blocked=false;display_state=DISPLAY_STATE_NORMAL;
 }
 puts("production down-swipe opens zones: media/art/playlist, four rotations, modal guards PASS");
}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'zone-swipe.c';p.write_text(preamble+'\n'.join(parts)+main)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Iidf_app/main','-Icommon','-Iinclude',str(p),'idf_app/main/display_rotation_dial.c','idf_app/main/valve_logic.c','-o',d+'/test'],check=True)
 subprocess.run([d+'/test'],check=True)
