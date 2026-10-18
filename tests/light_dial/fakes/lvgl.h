#pragma once
#include "../../valve_dial/fakes/lvgl.h"
#define LV_OPA_TRANSP 0
#define LV_OPA_40 102
#define LV_TEXT_ALIGN_CENTER 1
#define LV_OBJ_FLAG_CLICKABLE 4
lv_obj_t *lv_layer_top(void);
static inline void lv_obj_remove_flag(lv_obj_t *o,unsigned f){lv_obj_clear_flag(o,f);}
static inline void lv_obj_set_width(lv_obj_t *o,int n){(void)o;(void)n;}
static inline void lv_obj_set_style_text_align(lv_obj_t *o,int a,int b){(void)o;(void)a;(void)b;}
static inline void lv_obj_set_style_opa(lv_obj_t *o,int a,int b){(void)o;(void)a;(void)b;}

typedef int lv_color_t;
