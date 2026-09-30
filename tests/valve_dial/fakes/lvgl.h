#ifndef FAKE_LVGL_H
#define FAKE_LVGL_H
#include <stdint.h>
typedef struct lv_obj { unsigned flags, state; char text[96]; } lv_obj_t;
#define LV_OBJ_FLAG_HIDDEN 1u
#define LV_STATE_DISABLED 2u
#define LV_ALIGN_TOP_MID 0
#define LV_ALIGN_BOTTOM_MID 1
#define LV_OPA_COVER 255
lv_obj_t *lv_screen_active(void);
lv_obj_t *lv_obj_create(lv_obj_t *parent);
lv_obj_t *lv_label_create(lv_obj_t *parent);
lv_obj_t *lv_btn_create(lv_obj_t *parent);
void lv_obj_set_size(lv_obj_t *, int, int);
void lv_obj_center(lv_obj_t *);
void lv_obj_set_style_bg_color(lv_obj_t *, int, int);
void lv_obj_set_style_bg_opa(lv_obj_t *, int, int);
void lv_obj_set_style_border_width(lv_obj_t *, int, int);
void lv_obj_set_style_radius(lv_obj_t *, int, int);
void lv_obj_set_style_pad_all(lv_obj_t *, int, int);
void lv_obj_add_flag(lv_obj_t *, unsigned);
void lv_obj_clear_flag(lv_obj_t *, unsigned);
void lv_obj_align(lv_obj_t *, int, int, int);
void lv_obj_set_pos(lv_obj_t *, int, int);
void lv_label_set_text(lv_obj_t *, const char *);
void lv_obj_move_foreground(lv_obj_t *);
void lv_obj_add_state(lv_obj_t *, unsigned);
void lv_obj_clear_state(lv_obj_t *, unsigned);
int lv_color_hex(unsigned);
void lv_obj_set_style_border_color(lv_obj_t *, int, int);
#endif
