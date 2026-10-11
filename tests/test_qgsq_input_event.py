"""Run the production service INPUT case with real report decoding."""
from pathlib import Path
import subprocess
import tempfile
s = Path("components/rk_ble_hid_host/rk_ble_hid_host.c").read_text()
a = s.index("    case EVENT_INPUT: {", s.index("static void process_event("))
b = s.index("    case EVENT_DISCOVERY:", a)
preamble = r"""
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include "rk_ble_hid_host.h"
#define EVENT_INPUT 1
#define ESP_HID_USAGE_CCONTROL 2
#define ESP_HID_USAGE_GENERIC 3
#define ESP_HID_USAGE_KEYBOARD 4
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
typedef struct {int type; struct {struct {int usage;uint8_t report_id,data[8];size_t len;} input;} data;} service_event_t;
static struct {struct {char active_name[32];bool connected;} status;struct {void (*on_media_key)(rk_ble_media_key_t,void*);void *callback_context;} config;rk_ble_media_key_t qgsq_held_key;} s;
static bool stopping;
static unsigned count;
static rk_ble_media_key_t last;
static bool is_stopping(void){return stopping;}
static void key_received(rk_ble_media_key_t key,void *context){(void)context;count++;last=key;}
static void process_input(const service_event_t *event){switch(event->type){
"""
main = r"""
default:break;}}
static void input(int usage,unsigned id,unsigned value,size_t len){service_event_t e={.type=EVENT_INPUT,.data.input={.usage=usage,.report_id=id,.data={value,0},.len=len}};process_input(&e);}
int main(void){
 strcpy(s.status.active_name,"QGSQ");s.status.connected=true;s.config.on_media_key=key_received;
 /* Captured after reconnect: GENERIC report 1, bytes 20 00. */
 input(ESP_HID_USAGE_GENERIC,1,0x20,2);assert(count==1&&last==RK_BLE_MEDIA_KEY_NEXT_TRACK);
 input(ESP_HID_USAGE_GENERIC,1,0,2);
 input(ESP_HID_USAGE_CCONTROL,1,1,2);assert(count==2&&last==RK_BLE_MEDIA_KEY_VOLUME_UP);
 /* Metadata can differ on release: the following same key must work again. */
 input(ESP_HID_USAGE_GENERIC,1,0,2);input(ESP_HID_USAGE_CCONTROL,1,1,2);assert(count==3);
 input(ESP_HID_USAGE_CCONTROL,1,1,2);assert(count==3); /* Held duplicate. */
 input(ESP_HID_USAGE_GENERIC,1,0,2);
 input(ESP_HID_USAGE_KEYBOARD,1,4,2);input(ESP_HID_USAGE_GENERIC,2,4,2);input(ESP_HID_USAGE_GENERIC,1,4,1);input(ESP_HID_USAGE_GENERIC,1,3,2);assert(count==3);
 strcpy(s.status.active_name,"Other");input(ESP_HID_USAGE_GENERIC,1,4,2);assert(count==3);
 strcpy(s.status.active_name,"QGSQ");s.status.connected=false;input(ESP_HID_USAGE_GENERIC,1,4,2);assert(count==3);
 input(ESP_HID_USAGE_GENERIC,1,0,2);s.status.connected=true;stopping=true;input(ESP_HID_USAGE_GENERIC,1,4,2);assert(count==3);
 return 0;
}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/"input.c";p.write_text(preamble+s[a:b]+main)
 subprocess.run(["cc","-std=c11","-Wall","-Wextra","-Werror","-Icomponents/rk_ble_hid_host/include",str(p),"components/rk_ble_hid_host/rk_ble_hid_report.c","-o",d+"/test"],check=True)
 subprocess.run([d+"/test"],check=True)
print("QGSQ production INPUT: generic/consumer press/release, duplicates and rejection guards PASS")
