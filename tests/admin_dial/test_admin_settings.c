#define _POSIX_C_SOURCE 200809L
#include "admin_settings_dial.h"
#include "admin_store_dial.h"
#include "admin_server_dial.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>
static admin_settings_t durable = {600,0,false,0};
static uint16_t applied, selected;
static bool local_override, fail_save, capability=true, capability_available=true;
static atomic_bool pump=true, running=true, block_store, store_entered, cancel_next, stop_done;
static const admin_settings_binding_t *binding;
void admin_server_bind_settings(const admin_settings_binding_t *b) { binding=b; }
void admin_settings_wait_tick(void);
esp_err_t admin_store_load(admin_settings_t *out) { *out=durable; return ESP_OK; }
esp_err_t admin_store_save(const admin_settings_t *in,uint32_t gen) {
 if (atomic_load(&block_store)) {atomic_store(&store_entered,true);while(atomic_load(&block_store))admin_settings_wait_tick();}
 if (gen!=durable.generation) return ADMIN_ERR_CONFLICT;
 if(fail_save)return ESP_FAIL;
 durable=*in; durable.generation=gen+1; return ESP_OK;
}
bool valve_ui_duration_capability_get(bool *out) { if(!capability_available)return false; *out=capability; return true; }
bool valve_client_selected_duration_publish(uint16_t s) {selected=s;return true;}
bool platform_display_try_rotation(uint16_t d) {applied=d;return true;}
uint16_t platform_display_rotation_get(void) {return applied;}
bool platform_display_rotation_override_get(void) {return local_override;}
void platform_display_rotation_override_set(bool value) {local_override=value;}
void platform_display_cancel_input(void) {}
void admin_settings_wait_tick(void) {struct timespec t={0,1000000};nanosleep(&t,0);}
static void *ui(void *p) {(void)p;while(atomic_load(&running)){if(atomic_exchange(&cancel_next,false)){admin_settings_cancel_ui();atomic_store(&stop_done,true);}else if(atomic_load(&pump))admin_settings_process_ui();admin_settings_wait_tick();}return 0;}
static admin_result_t background_result;
static void *save_background(void *p) {(void)p;background_result=admin_settings_set_rotation(270,2);return 0;}
int main(void) {
 admin_settings_init(); pthread_t thread;pthread_create(&thread,0,ui,0);
 assert(admin_settings_set_rotation(90,0)==ADMIN_OK);
 assert(applied==90 && durable.rotation_degrees==90 && local_override);
 assert(admin_settings_set_rotation(180,0)==ADMIN_CONFLICT);
 fail_save=true;assert(admin_settings_set_rotation(180,1)==ADMIN_STORAGE_ERROR);
 assert(applied==90 && durable.rotation_degrees==90);fail_save=false;
 assert(admin_settings_set_duration(180,1)==ADMIN_OK);assert(selected==180);
 capability=false;assert(admin_settings_set_duration(120,2)==ADMIN_INVALID);
 capability_available=false;capability=true;
 assert(admin_settings_set_duration(120,2)==ADMIN_INVALID);
 assert(selected==180);admin_http_settings_t out;
 assert(binding && binding->read(&out)==ADMIN_OK);assert(out.duration_minutes==3);
 assert(!out.duration_capability_available && !out.short_duration_supported);
 atomic_store(&pump,false);admin_settings_wait_tick();
 assert(admin_settings_set_rotation(270,2)==ADMIN_STORAGE_ERROR);
 atomic_store(&pump,true);for(int i=0;i<5;i++)admin_settings_wait_tick();
 assert(applied==90 && durable.rotation_degrees==90);
 assert(!admin_settings_input_blocked());
 /* Stop while persistence owns the handler: UI must cancel/rollback without
  * waiting on storage, and later storage completion cannot reapply or succeed. */
 atomic_store(&block_store,true);pthread_t http;
 pthread_create(&http,0,save_background,0);
 while(!atomic_load(&store_entered))admin_settings_wait_tick();
 assert(admin_settings_set_duration(600,2)==ADMIN_CONFLICT);
 atomic_store(&cancel_next,true);
 while(!atomic_load(&stop_done))admin_settings_wait_tick();
 assert(applied==90 && !admin_settings_input_blocked());
 atomic_store(&block_store,false);pthread_join(http,0);
 assert(background_result==ADMIN_STORAGE_ERROR && applied==90 && selected==180);
 assert(durable.rotation_degrees==270);
 /* An explicit subsequent read reconciles the verified durable record. */
 assert(binding->read(&out)==ADMIN_OK && out.rotation_degrees==270);
 assert(applied==270 && out.generation==3);
 atomic_store(&running,false);pthread_join(thread,0);
 puts("admin settings integration: PASS");
}
