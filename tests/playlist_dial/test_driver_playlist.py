"""Exercise actual hardware callback and encoder drain with real picker/resolver."""
import ast,subprocess,tempfile
from pathlib import Path
original=ast.parse(Path('tests/admin_dial/test_driver_input.py').read_text())
preamble=next(ast.literal_eval(n.value) for n in original.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='preamble' for t in n.targets))
preamble=preamble.replace('static bool playlist_ui_visible(void){return false;}\n','')
preamble=preamble.replace('static void playlist_ui_cancel(void){}\n','').replace('static bool playlist_ui_touch(int x,int y,bool down,bool moved,uint64_t time){(void)x;(void)y;(void)down;(void)moved;(void)time;return false;}\n','')
preamble=preamble.replace('static bool display_is_touch_suppressed(void){return false;}','static bool wake;\nstatic bool display_is_touch_suppressed(void){return wake;}')
preamble+='''
#include <stdatomic.h>
#include <string.h>
#include "playlist_ui_dial.h"
#include "playlist_client_dial.h"
#include "controller_input.h"
#include "controller_input_mailbox.h"
#include "platform/platform_input.h"
static unsigned favorite_requests,plays,volumes;
static bool favorite_ready;
static int s_input_queue=1;
typedef struct {int ticks;unsigned generation;} encoder_batch_t;
static atomic_uint encoder_generation;
static encoder_batch_t batches[10];static unsigned batch_count,batch_read;
static uint32_t s_last_logged_overflow;
#define pdTRUE 1
#define ESP_LOGW(...) ((void)0)
static int xQueueReceive(int queue,encoder_batch_t *out,unsigned wait){(void)queue;(void)wait;if(batch_read==batch_count)return 0;*out=batches[batch_read++];return 1;}
static void xQueueReset(int queue){(void)queue;batch_read=batch_count=0;}
static void record_coalesced(unsigned n){(void)n;}
controller_input_mailbox_stats_t platform_input_mailbox_stats(void){return (controller_input_mailbox_stats_t){0};}
static bool display_is_encoder_suppressed(void){return wake;}
bool playlist_ui_context_allowed(void){return !visible&&!settings&&!zone&&!blocked&&!wake;}
bool ui_playlist_touch_allowed(int x,int y){return x>=40&&x<=320&&y>=124&&y<260;}
bool playlist_ui_current_zone(char *out,size_t cap){assert(cap>=6);strcpy(out,"sauna");return true;}
uint64_t playlist_ui_now_ms(void){return now;}
void playlist_ui_render(const controller_playlist_t *state,const char *message){(void)state;(void)message;}
bool playlist_client_request_favorites(void){favorite_requests++;favorite_ready=true;return true;}
bool playlist_client_request_play(const char *id,uint64_t revision,uint64_t epoch){assert(!strcmp(id,"opaque")&&revision==3&&epoch==7);plays++;return true;}
void playlist_client_cancel(void){}
void playlist_client_process(void){}
bool playlist_client_take(playlist_client_result_t *out){if(!favorite_ready)return false;favorite_ready=false;*out=(playlist_client_result_t){.status=PLAYLIST_CLIENT_FAVORITES,.favorites={.count=1,.revision=3,.core_epoch=7}};strcpy(out->favorites.zone_id,"sauna");strcpy(out->favorites.entries[0].id,"opaque");strcpy(out->favorites.entries[0].title,"Music");out->favorites.entries[0].available=true;return true;}
static bool actions(const controller_action_t *action){if(action->kind==CONTROLLER_ACTION_SCROLL_PLAYLIST_PICKER){playlist_ui_rotate(action->value.picker_delta);return true;}if(action->kind==CONTROLLER_ACTION_COMMAND)volumes++;return true;}
'''
s=Path('idf_app/main/platform_display_idf.c').read_text()
globals=s[s.index('// Swipe gesture detection'):s.index('// LVGL tick timer (')]
a=s.index('static void lvgl_touch_read_cb(');b=s.index('\nbool platform_display_init',a);touch=s[a:s.rfind('}',a,b)+1]
a=s.index('void platform_display_cancel_input(');b=s.index('void platform_display_apply_config',a);hooks=s[a:b]
a=s.index('void platform_display_playlist_transition(');transition=s[a:]
s=Path('idf_app/main/platform_input_idf.c').read_text();a=s.index('void platform_input_process_events(');b=s.index('controller_input_mailbox_stats_t platform_input_mailbox_stats(',a);encoder=s[a:b];a=s.index('void platform_input_fence(');encoder+=s[a:]
main=r'''
static void sample(int angle,int x,int y,bool down,lv_indev_data_t *data){rawx=x;rawy=y;if(angle==90){rawx=359-y;rawy=x;}if(angle==180){rawx=359-x;rawy=359-y;}if(angle==270){rawx=y;rawy=359-x;}pressed=down;lvgl_touch_read_cb(&device,data);}
static void queue(int ticks,unsigned generation){batches[batch_count++]=(encoder_batch_t){ticks,generation};}
int main(void){controller_input_set_action_handler(actions);lv_indev_data_t data={0};
 for(int angle=0;angle<360;angle+=90){
  platform_display_try_rotation(angle);sample(angle,0,0,false,&data);controller_input_set_context(CONTROLLER_INTERACTION_CONTEXT_MEDIA);
  now+=1000;uint32_t started=now;queue(3,atomic_load(&encoder_generation));sample(angle,180,180,true,&data);
  now=started+1999;sample(angle,180,180,true,&data);assert(!playlist_ui_visible());
  now=started+2000;sample(angle,180,180,true,&data);assert(playlist_ui_visible()&&s_last_tap_time==0&&data.state==0);playlist_ui_process();
  sample(angle,180,180,false,&data);assert(plays==(unsigned)(angle/90));platform_input_process_events();assert(volumes==0);
  queue(1,atomic_load(&encoder_generation)-1);platform_input_process_events(); /* old epoch discarded */
  queue(1,atomic_load(&encoder_generation));platform_input_process_events();queue(-1,atomic_load(&encoder_generation));platform_input_process_events();
  assert(!controller_input_dispatch_control(CONTROLLER_CONTROL_INTENT_ACTIVATE));assert(!controller_input_dispatch_control(CONTROLLER_CONTROL_INTENT_NEXT));
  now++;sample(angle,180,180,true,&data);now+=50;sample(angle,180,180,false,&data);assert(plays==(unsigned)(angle/90+1)&&volumes==0);
  playlist_ui_cancel();assert(s_last_tap_time==0&&!playlist_ui_visible());
  for(int context=0;context<4;context++){visible=context==0;settings=context==1;zone=context==2;wake=context==3;now+=1000;sample(angle,180,180,true,&data);now+=2000;sample(angle,180,180,true,&data);sample(angle,180,180,false,&data);assert(!playlist_ui_visible());}visible=settings=zone=wake=false;
 }
 assert(favorite_requests==4&&volumes==0);puts("production touch/encoder playlist four rotations, stale epoch, wake/overlay isolation PASS");}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory)/'driver.c';p.write_text(preamble+globals+touch+hooks+transition+encoder+main)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-DPLAYLIST_UI_HOST_TEST','-DRK_CUSTOM_VALVE_FIRMWARE=1','-Iidf_app/main','-Icommon','-Iinclude',str(p),'idf_app/main/playlist_ui_dial.c','idf_app/main/display_rotation_dial.c','idf_app/main/valve_logic.c','common/controller_playlist.c','common/controller_input.c','common/controller_input_mailbox.c','common/platform/platform_task.c','idf_app/main/controller_input_profile_dial.c','-o',directory+'/driver'],check=True)
 subprocess.run([directory+'/driver'],check=True)
