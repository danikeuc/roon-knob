"""Compile the production visibility predicate and feed it to the gesture classifier."""
from pathlib import Path
import subprocess
import tempfile
source = Path('idf_app/main/ui_network.c').read_text()
start = source.index('bool ui_is_settings_visible(void) {')
predicate = source[start:source.index('\n}', start) + 2]
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / 'modal.c'
    path.write_text('''
#include <assert.h>
#include "valve_logic.h"
typedef struct { bool hidden; } lv_obj_t;
#define LV_OBJ_FLAG_HIDDEN 1
static struct { lv_obj_t *panel; } s_widgets;
static lv_obj_t *s_reset_confirm_dialog;
static bool lv_obj_has_flag(lv_obj_t *obj, int flag) { (void)flag; return obj->hidden; }
''' + predicate + '''
int main(void) {
    lv_obj_t panel = {false}, modal = {false};
    s_widgets.panel = &panel;
    assert(ui_is_settings_visible());
    /* show_reset_confirm_dialog hides the panel and creates the modal. */
    panel.hidden = true; s_reset_confirm_dialog = &modal;
    valve_gesture_context_t context = {.settings = ui_is_settings_visible()};
    assert(valve_gesture_classify(85, 2, 200, 0, context) == VALVE_GESTURE_NONE);
    s_reset_confirm_dialog = 0;
    context.settings = ui_is_settings_visible();
    assert(valve_gesture_classify(85, 2, 200, 0, context) == VALVE_GESTURE_SWITCH_SCREEN);
}
''')
    binary = str(Path(directory) / 'modal')
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Iidf_app/main', str(path), 'idf_app/main/valve_logic.c', '-o', binary], check=True)
    subprocess.run([binary], check=True)
print('settings modal gesture regression passed')
