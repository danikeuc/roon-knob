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
static lv_obj_t *overlay,*state,*timer,*power_state,*badge,*badge_bulb,*manual,*back,*manual_on_label,*manual_off_label;
typedef struct {
    lv_obj_t *root,*outline,*base[2],*rays[5];
    lv_point_precise_t outline_points[64],base_points[2][2],ray_points[5][2];
} bulb_icon_t;
static bulb_icon_t main_icon,reminder_icon;
static lv_obj_t *ring,*ring_track,*left_button,*right_button;
static bool options;
static unsigned rendered_ring_points;
static bool rendered_valid;
static const lv_point_precise_t ring_points[73]={{180,11},{195,12},{209,14},{224,17},{238,21},{251,27},{264,34},{277,42},{289,51},{300,60},{309,71},{318,83},{326,95},{333,109},{339,122},{343,136},{346,151},{348,165},{349,180},{348,195},{346,209},{343,224},{339,238},{333,251},{326,264},{318,277},{309,289},{300,300},{289,309},{277,318},{265,326},{251,333},{238,339},{224,343},{209,346},{195,348},{180,349},{165,348},{151,346},{136,343},{122,339},{109,333},{95,326},{83,318},{71,309},{60,300},{51,289},{42,277},{34,265},{27,251},{21,238},{17,224},{14,209},{12,195},{11,180},{12,165},{14,151},{17,136},{21,122},{27,109},{34,96},{42,83},{51,71},{60,60},{71,51},{83,42},{96,34},{109,27},{122,21},{136,17},{151,14},{165,12},{180,11}};

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
static lv_obj_t *icon_line(lv_obj_t *root,lv_point_precise_t *points,unsigned n,int width) {
    lv_obj_t *line=lv_line_create(root);
    lv_line_set_points(line,points,n);
    lv_obj_set_style_line_width(line,width,0);
    lv_obj_set_style_line_rounded(line,true,0);
    lv_obj_remove_flag(line,LV_OBJ_FLAG_CLICKABLE);
    return line;
}
/* Shared vector geometry; point storage lives as long as the LVGL objects. */
static lv_obj_t *vector_bulb(bulb_icon_t *icon,lv_obj_t *parent,int x,int y,int size) {
    static const int shape[][2]={{34,76},{34,70},{29,64},{27,62},{25,59},{23,56},{21,53},{20,50},{19,46},{19,43},{19,39},{20,35},{21,32},{22,29},{24,26},{26,23},{29,20},{31,18},{34,16},{38,15},{41,14},{44,13},{48,13},{52,13},{55,14},{58,15},{62,16},{65,18},{67,20},{70,23},{72,26},{74,29},{75,32},{76,35},{77,39},{77,43},{77,46},{76,50},{75,53},{73,56},{71,59},{69,62},{67,64},{62,70},{62,76},{34,76}};
    static const int rays[5][4]={{48,2,48,9},{14,14,20,20},{2,41,11,41},{82,14,76,20},{94,41,85,41}};
    icon->root=lv_obj_create(parent);
    lv_obj_set_pos(icon->root,x,y);lv_obj_set_size(icon->root,size,size);
    lv_obj_set_style_radius(icon->root,size/2,0);
    lv_obj_set_style_border_width(icon->root,0,0);
    lv_obj_set_style_bg_opa(icon->root,LV_OPA_TRANSP,0);
    lv_obj_set_style_pad_all(icon->root,0,0);
    lv_obj_remove_flag(icon->root,LV_OBJ_FLAG_CLICKABLE);
    for(unsigned i=0;i<sizeof shape/sizeof shape[0];i++)icon->outline_points[i]=(lv_point_precise_t){shape[i][0]*size/96,shape[i][1]*size/96};
    int width=size>30?3:1;
    icon->outline=icon_line(icon->root,icon->outline_points,sizeof shape/sizeof shape[0],width);
    for(unsigned i=0;i<2;i++) {
        icon->base_points[i][0]=(lv_point_precise_t){(i?42:38)*size/96,(81+7*i)*size/96};
        icon->base_points[i][1]=(lv_point_precise_t){(i?54:58)*size/96,(81+7*i)*size/96};
        icon->base[i]=icon_line(icon->root,icon->base_points[i],2,width);
    }
    for(unsigned i=0;i<5;i++) {
        icon->ray_points[i][0]=(lv_point_precise_t){rays[i][0]*size/96,rays[i][1]*size/96};
        icon->ray_points[i][1]=(lv_point_precise_t){rays[i][2]*size/96,rays[i][3]*size/96};
        icon->rays[i]=icon_line(icon->root,icon->ray_points[i],2,width);
    }
    return icon->root;
}
static void bulb_state(bulb_icon_t *icon,bool known,bool on) {
    lv_color_t color=lv_color_hex(!known?0x526773:on?0xFFD38A:0xAAB8BD);
    lv_obj_set_style_line_color(icon->outline,color,0);
    for(unsigned i=0;i<2;i++)lv_obj_set_style_line_color(icon->base[i],color,0);
    for(unsigned i=0;i<5;i++) {
        lv_obj_set_style_line_color(icon->rays[i],color,0);
        if(known&&on)lv_obj_clear_flag(icon->rays[i],LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(icon->rays[i],LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_bg_color(icon->root,lv_color_hex(0x28251C),0);
    lv_obj_set_style_bg_opa(icon->root,LV_OPA_TRANSP,0);
}
void light_ui_cancel_touch(void) {
    target=-1;
}
static void render(void) {
    bool f=fresh();
    bool known=f&&!strcmp(status.health,"OK")&&status.gpio_on>=0;
#ifndef LIGHT_CLIENT_HOST_TEST
    static int last_diagnostic=-1;
    int diagnostic=(f?1:0)|(known?2:0)|(valid?4:0);
    if(diagnostic!=last_diagnostic) {
        printf("LIGHT_UI_DIAG fresh=%d known=%d valid=%d config_match=%d session_match=%d\n",f,known,valid,generation==light_config_generation(),session==light_client_session());
        last_diagnostic=diagnostic;
    }
#endif
    bool cooling=f&&status.mode==LIGHT_MANUAL_OFF&&!strcmp(status.reason,"manual_cooldown");
    bool is_manual=status.mode!=LIGHT_AUTO;
    uint64_t elapsed=light_client_now()>=received?(light_client_now()-received)/1000:0;
    unsigned total=is_manual?status.manual_remaining_seconds:status.remaining_seconds;
    unsigned left=elapsed<total?total-(unsigned)elapsed:0;
    /* LVGL label/style setters invalidate even identical content. Keep input
     * polling independent of redraws: inspect freshness every tick, but touch
     * LVGL objects only when their displayed values actually change. The key
     * is an unsigned array so comparison does not include struct padding. */
    const unsigned visual_key[]={
        f,known,!strcmp(status.health,"FAULT"),cooling,is_manual,
        (unsigned)(status.gpio_on+1),!strcmp(status.reason,"manual_off"),
        left,settings_known,hold_seconds,manual_seconds,
        capable,visible,pending,options
    };
    static unsigned previous_key[sizeof visual_key/sizeof visual_key[0]];
    if(rendered_valid&&!memcmp(previous_key,visual_key,sizeof visual_key))return;
    memcpy(previous_key,visual_key,sizeof visual_key);
    rendered_valid=true;
    char text[96];
    const char *mode=status.mode==LIGHT_AUTO?"AUTO":"ROČNO";
    if(!f)snprintf(text,sizeof text,"NI POVEZAVE");
    else if(cooling)snprintf(text,sizeof text,"PREMOR KAMERE");
    else if(settings_known)snprintf(text,sizeof text,"%s · %u min",mode,(is_manual?manual_seconds:hold_seconds)/60);
    else snprintf(text,sizeof text,"%s · — min",mode);
    lv_label_set_text(state,text);
    lv_obj_set_style_text_color(state,lv_color_hex(is_manual?0xFFD38A:0x99D6BD),0);
    lv_label_set_text(power_state,known?(status.gpio_on?"VKLOPLJENA":"IZKLOPLJENA"):f&&!strcmp(status.health,"FAULT")?"NAPAKA LUČI":"STANJE NI ZNANO");
    lv_obj_set_style_text_color(power_state,lv_color_hex(known&&status.gpio_on?0xFFD38A:0xF2F5F3),0);
    bulb_state(&main_icon,known,status.gpio_on==1);
    bulb_state(&reminder_icon,known,status.gpio_on==1);
    if(!known)snprintf(text,sizeof text,"Čakam podatke krmilnika");
    else if(!left&&(is_manual||status.gpio_on))snprintf(text,sizeof text,"Čakam potrditev");
    else if(cooling||(is_manual&&!status.gpio_on&&!strcmp(status.reason,"manual_off")))snprintf(text,sizeof text,"AUTO čez %u:%02u",left/60,left%60);
    else if(is_manual&&!status.gpio_on)snprintf(text,sizeof text,"Kamera miruje · %u:%02u",left/60,left%60);
    else if(status.gpio_on)snprintf(text,sizeof text,"Izklop čez %u:%02u",left/60,left%60);
    else snprintf(text,sizeof text,"Kamera je pripravljena");
    lv_label_set_text(timer,text);
    lv_label_set_text(manual,"");
    /* Persistent manual reminder is cleared only by a fresh AUTO receipt. */
    if(is_manual&&capable&&!visible) {
        lv_obj_clear_flag(badge,LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(badge_bulb,LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(badge,f&&left?"M":"M ?");
    } else {
        lv_obj_add_flag(badge,LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(badge_bulb,LV_OBJ_FLAG_HIDDEN);
    }
    unsigned duration=cooling?900:is_manual?manual_seconds:hold_seconds;
    unsigned points=duration?1+(unsigned)((uint64_t)left*72/duration):0;
    if(points>73)points=73;
    if(known&&left&&points>=2&&settings_known) {
        /* LVGL invalidates the line even for identical geometry. The ring
         * spans the display, so redrawing it each UI tick can delay touch input
         * while a countdown is active. Only update changed geometry. */
        if (points!=rendered_ring_points) {
            lv_line_set_points(ring,ring_points,points);
            rendered_ring_points=points;
        }
        lv_obj_set_style_line_color(ring,lv_color_hex(is_manual?0xFFD38A:0x99D6BD),0);
        lv_obj_clear_flag(ring,LV_OBJ_FLAG_HIDDEN);
    } else lv_obj_add_flag(ring,LV_OBJ_FLAG_HIDDEN);
    if(options) {
        lv_label_set_text(manual_on_label,"Ročni ON");
        lv_label_set_text(manual_off_label,"Ročni OFF");
        lv_label_set_text(back,"Nazaj");
        lv_obj_clear_flag(back,LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(manual_on_label,known&&status.gpio_on?"Izklopi":"Vklopi");
        lv_label_set_text(manual_off_label,is_manual?"AUTO":"Možnosti");
        lv_obj_add_flag(back,LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_opa(left_button,f&&!pending?LV_OPA_COVER:LV_OPA_40,0);
    lv_obj_set_style_opa(right_button,f&&!pending?LV_OPA_COVER:LV_OPA_40,0);
}
static lv_obj_t *control(int x,unsigned color) {
    lv_obj_t *o=lv_obj_create(overlay);
    lv_obj_set_pos(o,x,257);lv_obj_set_size(o,104,44);
    lv_obj_set_style_radius(o,22,0);lv_obj_set_style_pad_all(o,0,0);
    lv_obj_set_style_border_width(o,1,0);
    lv_obj_set_style_border_color(o,lv_color_hex(0x72858E),0);
    lv_obj_set_style_bg_color(o,lv_color_hex(color),0);
    lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE);
    return o;
}
void light_ui_init(void) {
    rendered_valid=false;
    completions=xQueueCreate(1,sizeof(completion_t));
    overlay=lv_obj_create(lv_screen_active());
    lv_obj_set_size(overlay,360,360);
    lv_obj_center(overlay);
    lv_obj_set_style_bg_color(overlay,lv_color_hex(0x090D10),0);
    lv_obj_set_style_bg_opa(overlay,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(overlay,0,0);
    lv_obj_set_style_pad_all(overlay,0,0);
    lv_obj_add_flag(overlay,LV_OBJ_FLAG_HIDDEN);
    ring_track=icon_line(overlay,(lv_point_precise_t *)ring_points,73,3);
    lv_obj_set_style_line_color(ring_track,lv_color_hex(0x202B30),0);
    ring=icon_line(overlay,(lv_point_precise_t *)ring_points,73,3);
    rendered_ring_points=73;
    lv_obj_add_flag(ring,LV_OBJ_FLAG_HIDDEN);
    state=label(overlay,"NI POVEZAVE",35);
    lv_obj_set_style_text_font(state,font_manager_get_small(),0);
    lv_obj_set_style_bg_color(state,lv_color_hex(0x192328),0);
    lv_obj_set_style_bg_opa(state,LV_OPA_COVER,0);
    lv_obj_set_style_radius(state,15,0);lv_obj_set_style_pad_all(state,5,0);
    vector_bulb(&main_icon,overlay,128,78,104);
    power_state=label(overlay,"STANJE NI ZNANO",187);
    lv_obj_set_style_text_font(power_state,font_manager_get_small(),0);
    timer=label(overlay,"",221);
    lv_obj_set_style_text_font(timer,font_manager_get_small(),0);
    manual=label(overlay,"",239);
    lv_obj_set_style_text_font(manual,font_manager_get_small(),0);
    left_button=control(72,0x99D6BD);right_button=control(184,0x192328);
    manual_on_label=label(left_button,"Vklopi",8);
    manual_off_label=label(right_button,"Možnosti",8);
    lv_obj_set_style_text_font(manual_on_label,font_manager_get_small(),0);
    lv_obj_set_style_text_font(manual_off_label,font_manager_get_small(),0);
    lv_obj_set_style_text_color(manual_on_label,lv_color_hex(0x090D10),0);
    lv_obj_set_style_text_color(manual_off_label,lv_color_hex(0xF2F5F3),0);
    back=label(overlay,"Nazaj",316);
    lv_obj_set_style_text_font(back,font_manager_get_small(),0);
    badge_bulb=vector_bulb(&reminder_icon,lv_layer_top(),236,49,22);
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
    options=false;
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
    if(y>=257&&y<=301) {
        if(x>=72&&x<=176)return options?1:0;
        if(x>=184&&x<=288)return options?2:status.mode==LIGHT_AUTO?4:3;
    }
    if(options&&y>=310&&y<=342&&x>=120&&x<=240)return 5;
    if(!options&&y>=90&&y<=180&&x>=128&&x<=232)return 0;
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
    if(selected==4||selected==5) {
        options=selected==4;
        render();
        return;
    }
    light_action_t action=selected==1?LIGHT_ACTION_MANUAL_ON:selected==2?LIGHT_ACTION_MANUAL_OFF:selected==3?LIGHT_ACTION_AUTO:status.mode==LIGHT_AUTO?(status.desired_on?LIGHT_ACTION_OFF:LIGHT_ACTION_TIMED_ON):status.mode==LIGHT_MANUAL_ON?LIGHT_ACTION_MANUAL_OFF:LIGHT_ACTION_MANUAL_ON;
    if ((action==LIGHT_ACTION_MANUAL_ON||action==LIGHT_ACTION_MANUAL_OFF)&&(!settings_known||light_client_now()<settings_at||light_client_now()-settings_at>=15000))return;
    if ((action==LIGHT_ACTION_TIMED_ON||action==LIGHT_ACTION_MANUAL_ON)&&strcmp(status.health,"OK"))return;
    if (light_client_action(action,status.revision,generation,completion,NULL)) {
        pending=true;
        valid=false;
        last_poll=light_client_now();
    }
}
