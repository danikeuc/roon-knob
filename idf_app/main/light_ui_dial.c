#include "light_ui_dial.h"
#include "light_client_dial.h"
#include "light_config_dial.h"
#include "valve_ui_dial.h"
#include "ui.h"
#include "font_manager.h"
#include <lvgl.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdio.h>
#include <string.h>
static lv_obj_t *overlay,*state,*timer,*bulb,*badge,*badge_bulb,*manual,*back,*manual_on_label,*manual_off_label;
static QueueHandle_t completions;
static light_status_t status;
static uint64_t received,last_poll;
static unsigned generation,session;
static bool capable,visible,pending,valid,settings_known;
static unsigned hold_seconds,manual_seconds;
static uint64_t settings_at,last_settings_attempt;
static int target=-1;
static uint64_t touch_revision;
static unsigned touch_generation,touch_session;
typedef struct {
    light_status_t status;
    bool ok,settings;
    unsigned hold,manual;
    unsigned generation,session;
    uint64_t received;
} completion_t;
static bool fresh(void) {
    return valid&&generation==light_config_generation()&&session==light_client_session()&&light_status_fresh(&status,received,light_client_now());
}
static void completion(const light_reply_t *r,void *unused) {
    (void)unused;
    if (!r) {
        completion_t failed= {
            .generation=light_config_generation(),.session=light_client_session()
        };
        if (completions)xQueueOverwrite(completions,&failed);
        return;
    }
    completion_t c= {
        .generation=r->generation,.session=r->session,.received=r->received_ms
    };
    c.ok=r->http_status==200&&light_status_parse(r->body,r->length,&c.status);
    if (completions)xQueueOverwrite(completions,&c);
}
static void settings_completion(const light_reply_t *r,void *unused) {
    (void)unused;
    completion_t c= {
        .settings=true,.generation=light_config_generation(),.session=light_client_session()
    };
    uint64_t revision;
    if (r) {
        c.generation=r->generation;
        c.session=r->session;
        c.received=r->received_ms;
        c.ok=r->http_status==200&&light_settings_parse(r->body,r->length,&revision,&c.hold,&c.manual);
    }
    if (completions)xQueueOverwrite(completions,&c);
}
static lv_obj_t *label(lv_obj_t *p,const char *text,int y) {
    lv_obj_t *o=lv_label_create(p);
    lv_obj_set_style_text_font(o,font_manager_get_small(),0);
    lv_label_set_text(o,text);
    lv_obj_align(o,LV_ALIGN_TOP_MID,0,y);
    return o;
}
static lv_obj_t *vector_bulb(lv_obj_t *p,int x,int y,int scale) {
    lv_obj_t *head=lv_obj_create(p);
    lv_obj_set_pos(head,x,y);
    lv_obj_set_size(head,scale*4,scale*4);
    lv_obj_set_style_radius(head,scale*2,0);
    lv_obj_set_style_border_width(head,2,0);
    lv_obj_set_style_border_color(head,lv_color_hex(0xB8EBFF),0);
    lv_obj_set_style_bg_opa(head,LV_OPA_TRANSP,0);
    lv_obj_set_style_pad_all(head,0,0);
    lv_obj_remove_flag(head,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *base=lv_obj_create(head);
    lv_obj_set_pos(base,scale,scale*3-2);
    lv_obj_set_size(base,scale*2,scale);
    lv_obj_set_style_bg_color(base,lv_color_hex(0xB8EBFF),0);
    lv_obj_set_style_border_width(base,0,0);
    return head;
}
void light_ui_cancel_touch(void) {
    target=-1;
}
static void render(void) {
    bool f=fresh();
    char text[96];
    lv_label_set_text(state,f?(strcmp(status.health,"OK")?(!strcmp(status.health,"FAULT")?"Napaka luči":"Luč ni na voljo"):(status.mode==LIGHT_AUTO?"AUTO":status.mode==LIGHT_MANUAL_ON?"ROČNO ON":"ROČNO OFF")):"Stanje ni znano");
    if (status.mode!=LIGHT_AUTO&&capable) {
        uint64_t elapsed=light_client_now()>=received?(light_client_now()-received)/1000:0;
        unsigned seconds=elapsed<status.manual_remaining_seconds?status.manual_remaining_seconds-(unsigned)elapsed:0;
        /* A local zero never clears the manual reminder. Only fresh AUTO can. */
        if (f&&seconds)snprintf(text,sizeof text,"AUTO čez %u min",(seconds+59)/60);
        else snprintf(text,sizeof text,"M · stanje ni znano");
        lv_label_set_text(timer,text);
        lv_label_set_text(manual,"ROČNO — kamera\nzačasno izključena");
        if (!visible) {
            lv_obj_clear_flag(badge,LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(badge_bulb,LV_OBJ_FLAG_HIDDEN);
        }
        else {
            lv_obj_add_flag(badge,LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(badge_bulb,LV_OBJ_FLAG_HIDDEN);
        }
        lv_label_set_text(badge,f&&seconds?"M":"M ?");
    }
    else {
        lv_label_set_text(manual,"");
        lv_obj_add_flag(badge,LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(badge_bulb,LV_OBJ_FLAG_HIDDEN);
        if (f)snprintf(text,sizeof text,"%s · %u s",status.desired_on?"ON":"OFF",(unsigned)status.remaining_seconds);
        else snprintf(text,sizeof text,"Preveri povezavo in nastavitve.");
        lv_label_set_text(timer,text);
    }
    if (settings_known) {
        char on[48],off[48];
        snprintf(on,sizeof on,"ROČNO ON\n%u min",manual_seconds/60);
        snprintf(off,sizeof off,"ROČNO OFF\n%u min",manual_seconds/60);
        lv_label_set_text(manual_on_label,on);
        lv_label_set_text(manual_off_label,off);
    }
    lv_obj_set_style_bg_opa(bulb,f&&status.desired_on?LV_OPA_COVER:LV_OPA_TRANSP,0);
    lv_obj_set_style_bg_color(bulb,lv_color_hex(0x48D7E8),0);
    lv_obj_set_style_opa(back,f&&!pending?LV_OPA_COVER:LV_OPA_40,0);
}
void light_ui_init(void) {
    completions=xQueueCreate(1,sizeof(completion_t));
    overlay=lv_obj_create(lv_screen_active());
    lv_obj_set_size(overlay,360,360);
    lv_obj_center(overlay);
    lv_obj_set_style_bg_color(overlay,lv_color_hex(0x05080B),0);
    lv_obj_set_style_bg_opa(overlay,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(overlay,0,0);
    lv_obj_set_style_pad_all(overlay,0,0);
    lv_obj_add_flag(overlay,LV_OBJ_FLAG_HIDDEN);
    label(overlay,"Luč",28);
    state=label(overlay,"Stanje ni znano",60);
    bulb=vector_bulb(overlay,140,95,20);
    timer=label(overlay,"",186);
    manual=label(overlay,"",199);
    lv_obj_set_width(manual,290);
    lv_obj_set_style_text_align(manual,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_font(manual,font_manager_get_small(),0);
    manual_on_label=label(overlay,"ROČNO ON",257);
    lv_obj_set_pos(manual_on_label,60,257);
    manual_off_label=label(overlay,"ROČNO OFF",257);
    lv_obj_set_pos(manual_off_label,195,257);
    back=label(overlay,"Nazaj na AUTO",316);
    badge_bulb=vector_bulb(lv_layer_top(),240,54,4);
    badge=lv_label_create(lv_layer_top());
    lv_obj_set_pos(badge,260,53);
    lv_obj_set_style_text_color(badge,lv_color_hex(0xEFC070),0);
    lv_obj_set_style_text_font(badge,&lv_font_montserrat_14,0);
    lv_label_set_text(badge,"M");
    lv_obj_add_flag(badge,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(badge_bulb,LV_OBJ_FLAG_HIDDEN);
    generation=light_config_generation();
    session=light_client_session();
    light_client_start();
}
bool light_ui_capable(void) {
    return capable&&generation==light_config_generation();
}
bool light_ui_visible(void) {
    return visible;
}
void light_ui_show(bool show) {
    visible=show;
    light_ui_cancel_touch();
    if (show) {
        lv_obj_move_foreground(overlay);
        lv_obj_clear_flag(overlay,LV_OBJ_FLAG_HIDDEN);
        last_poll=0;
    }
    else lv_obj_add_flag(overlay,LV_OBJ_FLAG_HIDDEN);
}
void light_ui_next_page(int direction) {
    unsigned pages=light_ui_capable()?3:2;
    unsigned current=visible?2:valve_ui_visible()?1:0;
    unsigned next=(current+pages+(direction<0?-1:1))%pages;
    light_ui_show(next==2);
    valve_ui_show(next==1);
}
void light_ui_process(void) {
    if (!overlay) return;
    unsigned g=light_config_generation(),s=light_client_session();
    if (g!=generation) {
        generation=g;
        capable=valid=pending=settings_known=false;
        status=(light_status_t) {
            0
        };
        light_ui_cancel_touch();
        if (visible) light_ui_show(false);
        last_poll=0;
        last_settings_attempt=0;
    }
    if (s!=session) {
        session=s;
        valid=false;
        settings_known=false;
        light_ui_cancel_touch();
        last_poll=0;
        last_settings_attempt=0;
    }
    completion_t c;
    if (completions&&xQueueReceive(completions,&c,0)==pdTRUE) {
        pending=false;
        if (c.generation==g&&c.session==s) {
            if (c.settings) {
                settings_known=c.ok;
                if (c.ok) {
                    hold_seconds=c.hold;
                    manual_seconds=c.manual;
                    settings_at=c.received;
                }
            }
            else {
                valid=c.ok;
                if (c.ok) {
                    status=c.status;
                    received=c.received;
                    capable=true;
                }
            }
            light_ui_cancel_touch();
        }
    }
    uint64_t now=light_client_now();
    /* Status has first opportunity when overdue. Settings failures retry at a
     * separate bounded cadence and cannot monopolize the worker or controls. */
    if (!pending&&(now<last_poll||now-last_poll>=5000||!last_poll)) {
        if (light_client_request("status",NULL,0,completion,NULL)) {
            pending=true;
            last_poll=now;
        }
    } else if (!pending&&capable&&
               (!last_settings_attempt||now<last_settings_attempt||now-last_settings_attempt>=15000)) {
        if (light_client_request("settings",NULL,0,settings_completion,NULL)) {
            pending=true;
            last_settings_attempt=now;
        }
    }
    render();
}
static int touch_target(int x,int y) {
    if (y>=253&&y<=306&&x>=50&&x<=310)return x<180?1:2;
    if (y>=310&&y<=350&&x>=75&&x<=285)return 3;
    if (y>=90&&y<=192&&x>=80&&x<=280)return 0;
    return -1;
}
void light_ui_touch(int x,int y,bool pressed,bool moved,uint32_t now) {
    (void)now;
    if (!visible||moved) {
        light_ui_cancel_touch();
        return;
    }
    if (pressed) {
        if (target<0&&fresh()&&!pending) {
            target=touch_target(x,y);
            touch_revision=status.revision;
            touch_generation=generation;
            touch_session=session;
        }
        return;
    }
    int selected=target;
    light_ui_cancel_touch();
    if (selected<0||touch_target(x,y)!=selected||!fresh()||pending||touch_revision!=status.revision||touch_generation!=generation||touch_session!=session)return;
    light_action_t action=selected==1?LIGHT_ACTION_MANUAL_ON:selected==2?LIGHT_ACTION_MANUAL_OFF:selected==3?LIGHT_ACTION_AUTO:status.mode==LIGHT_AUTO?(status.desired_on?LIGHT_ACTION_OFF:LIGHT_ACTION_TIMED_ON):status.mode==LIGHT_MANUAL_ON?LIGHT_ACTION_MANUAL_OFF:LIGHT_ACTION_MANUAL_ON;
    if ((action==LIGHT_ACTION_MANUAL_ON||action==LIGHT_ACTION_MANUAL_OFF)&&(!settings_known||light_client_now()<settings_at||light_client_now()-settings_at>=15000))return;
    if ((action==LIGHT_ACTION_TIMED_ON||action==LIGHT_ACTION_MANUAL_ON)&&strcmp(status.health,"OK"))return;
    if (light_client_action(action,status.revision,generation,completion,NULL)) {
        pending=true;
        valid=false;
        last_poll=light_client_now();
    }
}
