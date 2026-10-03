"""Execute the production LVGL rendering with bounded parsed target identity."""
from pathlib import Path
import subprocess, tempfile, os
source = Path('idf_app/main/playlist_ui_dial.c').read_text()
a = source.index('static void playlist_ui_render(')
b = source.index('\n#endif', a)
declarations = source[source.index('static lv_obj_t *'):source.index('\nbool playlist_ui_context_allowed')]
preamble = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "playlist_admin_dial.h"
typedef struct {char text[320];} lv_obj_t;
static lv_obj_t objects[20];static unsigned count;
#define LV_TEXT_ALIGN_CENTER 0
#define LV_LABEL_LONG_DOT 0
#define LV_ALIGN_TOP_MID 0
#define LV_ALIGN_BOTTOM_MID 0
#define LV_OPA_COVER 255
#define LV_OBJ_FLAG_HIDDEN 1
static lv_obj_t *lv_screen_active(void){return NULL;}
static lv_obj_t *lv_obj_create(lv_obj_t *parent){(void)parent;return &objects[count++];}
#define lv_label_create lv_obj_create
#define lv_obj_set_size(...) ((void)0)
#define lv_obj_center(...) ((void)0)
#define lv_obj_set_style_bg_color(...) ((void)0)
#define lv_obj_set_style_bg_opa(...) ((void)0)
#define lv_obj_align(...) ((void)0)
#define lv_obj_set_width(...) ((void)0)
#define lv_obj_set_style_text_align(...) ((void)0)
#define lv_label_set_long_mode(...) ((void)0)
#define lv_obj_add_flag(...) ((void)0)
#define lv_obj_clear_flag(...) ((void)0)
#define lv_obj_move_foreground(...) ((void)0)
static void lv_label_set_text(lv_obj_t *obj,const char *text){snprintf(obj->text,sizeof obj->text,"%s",text);}
static bool contains(const char *text){for(unsigned i=0;i<count;i++)if(strstr(objects[i].text,text))return true;return false;}
'''
main = r'''
int main(void){
 const char *body="{\"revision\":1,\"core_epoch\":2,\"zone_id\":\"opaque-zone\",\"zone_name\":\"Sauna <target>\",\"favorites\":[]}";
 playlist_favorites_t favorites;assert(playlist_favorites_parse(body,strlen(body),&favorites));
 controller_playlist_t state={0};assert(controller_playlist_open(&state,&favorites,"opaque-zone",10).kind==PLAYLIST_ACTION_RENDER);
 playlist_ui_render(&state,"");assert(contains("Sauna <target>")&&contains("Nazaj"));
 state.phase=PLAYLIST_PENDING;playlist_ui_render(&state,"Pending");assert(contains("Sauna <target>")&&contains("Pending"));
 state.phase=PLAYLIST_ERROR;playlist_ui_render(&state,"Unknown");assert(contains("Sauna <target>"));
 assert(controller_playlist_open(&state,&favorites,"different-zone",10).kind!=PLAYLIST_ACTION_RENDER);
 puts("production picker target rendering from parsed exact zone, empty/pending/error PASS");
}
'''
with tempfile.TemporaryDirectory() as d:
    p=Path(d)/'render.c';p.write_text(preamble+declarations+source[a:b]+main)
    idf=os.environ.get('IDF_PATH','/tmp/esp-idf-5.5.5')
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-DPLAYLIST_HOST_TEST','-Icommon','-Iinclude','-Iidf_app/main','-I'+idf+'/components/json/cJSON',str(p),'common/controller_playlist.c','idf_app/main/playlist_admin_dial.c',idf+'/components/json/cJSON/cJSON.c','-lm','-o',d+'/render'],check=True)
    subprocess.run([d+'/render'],check=True)
