#include "admin_settings_dial.h"
#include "admin_store_dial.h"
#include "admin_server_dial.h"
#include <stdatomic.h>
#ifdef ADMIN_SETTINGS_HOST_TEST
#define UI_WAIT_STEPS 500u
void admin_settings_wait_tick(void);
bool platform_display_try_rotation(uint16_t degrees);
uint16_t platform_display_rotation_get(void);
bool platform_display_rotation_override_get(void);
void platform_display_rotation_override_set(bool enabled);
void platform_display_cancel_input(void);
bool valve_ui_duration_capability_get(bool *supported);
bool valve_client_selected_duration_publish(uint16_t seconds);
#else
#include "platform_display_idf.h"
#include "valve_ui_dial.h"
#include "valve_client_dial.h"
/* Always yield a real tick; pdMS_TO_TICKS(1) is zero at 100 Hz. */
#define UI_WAIT_STEPS ((unsigned)pdMS_TO_TICKS(500) + 1u)
static void admin_settings_wait_tick(void) { vTaskDelay(1); }
#endif
/* The mailbox owns all request data for its whole lifetime. No stack pointer or
 * HTTP request survives here. Neither lock is held during NVS/auth or a wait.
 * UI operations under mailbox_lock are bounded, allocation/network-free. */
typedef enum { EMPTY, APPLY, APPLIED, FINISH, ROLLBACK, DONE } stage_t;
static atomic_flag admission = ATOMIC_FLAG_INIT;
static atomic_flag mailbox_lock = ATOMIC_FLAG_INIT;
static atomic_bool cancelled, blocked;
static atomic_uint cancellation_epoch;
static struct {
    stage_t stage;
    uint16_t target, previous, effective;
    unsigned epoch;
    bool override, previous_override, changed, ok;
} job;
static bool take(void) { return !atomic_flag_test_and_set(&mailbox_lock); }
static void give(void) { atomic_flag_clear(&mailbox_lock); }
static bool lock_worker(void) {
    for (unsigned i=0;i<UI_WAIT_STEPS;i++) { if(take())return true; admin_settings_wait_tick(); }
    return false;
}
bool admin_settings_input_blocked(void) { return atomic_load(&blocked); }
void admin_settings_process_ui(void) {
    if (!take()) return;
    if (job.stage != EMPTY && job.stage != DONE &&
        (atomic_load(&cancelled) || job.epoch != atomic_load(&cancellation_epoch)))
        job.stage = ROLLBACK;
    if (job.stage == APPLY) {
        job.previous = platform_display_rotation_get();
        job.previous_override = platform_display_rotation_override_get();
        platform_display_cancel_input();
        job.ok = platform_display_try_rotation(job.override ? job.target : job.previous);
        job.changed = job.ok;
        if (job.ok) platform_display_rotation_override_set(job.override);
        job.effective = platform_display_rotation_get();
        job.stage = job.ok ? APPLIED : DONE;
    } else if (job.stage == ROLLBACK) {
        if (job.changed) {
            /* Buffers were reserved before apply; rollback cannot allocate. */
            job.ok = platform_display_try_rotation(job.previous);
            if (job.ok) platform_display_rotation_override_set(job.previous_override);
        } else {
            /* Cancelling unstarted work changed neither applied value nor input. */
            job.ok = true;
        }
        job.stage = DONE;
    } else if (job.stage == FINISH) {
        job.stage = DONE;
    }
    if (job.stage == DONE) atomic_store(&blocked, !job.ok);
    give();
}
/* Called on UI before httpd_stop, including when a handler awaits UI ACK.
 * Handler waits see cancellation without needing another UI iteration. */
void admin_settings_cancel_pending(void) {
    atomic_fetch_add(&cancellation_epoch, 1);
    atomic_store(&cancelled, true);
}
void admin_settings_cancel_ui(void) {
    admin_settings_cancel_pending();
    admin_settings_process_ui();
}
static bool wait_stage(stage_t wanted) {
    for (unsigned i=0;i<UI_WAIT_STEPS;i++) {
        if (atomic_load(&cancelled)) return false;
        if (take()) {
            bool stopped = job.epoch != atomic_load(&cancellation_epoch);
            bool done=job.stage==wanted, failed=job.stage==DONE && wanted!=DONE;
            give();
            if(stopped)return false;
            if(done)return true;
            if(failed)return false;
        }
        admin_settings_wait_tick();
    }
    atomic_store(&cancelled,true);
    return false;
}
static bool apply(const admin_settings_t *value, unsigned epoch) {
    if (!lock_worker()) return false;
    if (job.stage!=EMPTY && job.stage!=DONE) { give(); return false; }
    if (atomic_load(&cancellation_epoch) != epoch) { give(); return false; }
    job.epoch=epoch;
    job.target=value->rotation_degrees;job.override=value->rotation_override;
    job.changed=false;job.ok=false;job.stage=APPLY;
    atomic_store(&cancelled,false);atomic_store(&blocked,true);
    give();
    return wait_stage(APPLIED);
}
static bool finish(bool success) {
    if (!lock_worker()) { atomic_store(&cancelled,true);return false; }
    if (atomic_load(&cancelled) || job.epoch != atomic_load(&cancellation_epoch)) {give();return false;}
    job.stage=success?FINISH:ROLLBACK;give();
    return wait_stage(DONE);
}
static admin_result_t transact(int kind, uint16_t value, uint32_t generation,
                                admin_http_settings_t *out) {
    if (atomic_flag_test_and_set(&admission)) return ADMIN_CONFLICT;
    unsigned epoch = atomic_load(&cancellation_epoch);
    admin_result_t result=ADMIN_STORAGE_ERROR;
    admin_settings_t saved;
    if(admin_store_load(&saved)!=ESP_OK)goto end;
    if(kind && saved.generation!=generation){result=ADMIN_CONFLICT;goto end;}
    if(kind==1) {
        bool supported=false;
        if(value<60 || value>600 || value%60 ||
           (value<600 && (!valve_ui_duration_capability_get(&supported)||!supported))) {
            result=ADMIN_INVALID;goto end;
        }
        saved.duration_seconds=value;
    }
    if(kind==2) {
        if(value>270 || value%90){result=ADMIN_INVALID;goto end;}
        saved.rotation_degrees=value;saved.rotation_override=true;
    }
    if(!apply(&saved, epoch))goto end;
    if(kind) {
        esp_err_t err=admin_store_save(&saved,generation);
        if(err!=ESP_OK) {
            (void)finish(false);
            result=err==ADMIN_ERR_CONFLICT?ADMIN_CONFLICT:ADMIN_STORAGE_ERROR;
            goto end;
        }
        saved.generation=generation+1;
    }
    if(!finish(true))goto end;
    /* Persisted read/startup reconciliation is also a verified store read;
     * mutations only publish after successful commit/readback and UI ACK. */
    if(!valve_client_selected_duration_publish(saved.duration_seconds))goto end;
    if(out) {
        bool supported=false;
        out->duration_minutes=saved.duration_seconds/60;
        out->rotation_degrees=job.effective;out->generation=saved.generation;
        out->duration_capability_available=valve_ui_duration_capability_get(&supported);
        out->short_duration_supported=out->duration_capability_available && supported;
    }
    result=ADMIN_OK;
end:
    atomic_flag_clear(&admission);return result;
}
admin_result_t admin_settings_set_duration(uint16_t seconds,uint32_t generation) {
    return transact(1,seconds,generation,0);
}
admin_result_t admin_settings_set_rotation(uint16_t degrees,uint32_t generation) {
    return transact(2,degrees,generation,0);
}
static admin_result_t read_settings(admin_http_settings_t *out) {return transact(0,0,0,out);}
static admin_result_t shower(uint16_t minutes,uint32_t generation,admin_http_settings_t *out) {
    if(minutes<1 || minutes>10)return ADMIN_INVALID;
    return transact(1,minutes*60,generation,out);
}
static admin_result_t rotation(uint16_t degrees,uint32_t generation,admin_http_settings_t *out) {
    return transact(2,degrees,generation,out);
}
void admin_settings_init(void) {
    static const admin_settings_binding_t binding={read_settings,shower,rotation};
    admin_server_bind_settings(&binding);
}

admin_result_t admin_settings_restore(void) { return transact(0,0,0,0); }
