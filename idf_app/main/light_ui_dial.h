#pragma once
#include <stdbool.h>
#include <stdint.h>
void light_ui_init(void);
void light_ui_process(void);
void light_ui_show(bool);
bool light_ui_visible(void);
bool light_ui_capable(void);
void light_ui_next_page(int direction);
void light_ui_touch(int x,int y,bool pressed,bool moved,uint32_t now);
void light_ui_cancel_touch(void);
