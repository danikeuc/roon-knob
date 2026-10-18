#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define LIGHT_REVISION_MAX UINT64_C(9007199254740991)
#define LIGHT_BODY_MAX 8192
#define LIGHT_STALE_MS 10000
#define LIGHT_JOB_MS 5000
typedef enum {LIGHT_AUTO,LIGHT_MANUAL_ON,LIGHT_MANUAL_OFF} light_mode_t;
typedef enum {LIGHT_ACTION_TIMED_ON,LIGHT_ACTION_OFF,LIGHT_ACTION_MANUAL_ON,LIGHT_ACTION_MANUAL_OFF,LIGHT_ACTION_AUTO} light_action_t;
typedef struct {uint64_t revision;light_mode_t mode;bool desired_on;int gpio_on;char health[12],reason[129],observed_at[41];uint32_t remaining_seconds,manual_remaining_seconds;} light_status_t;
bool light_status_parse(const char *,size_t,light_status_t *);
bool light_status_fresh(const light_status_t *,uint64_t received,uint64_t now);
bool light_action_json(const char *,uint64_t,light_action_t,char *,size_t);
bool light_settings_parse(const char *,size_t,uint64_t *,unsigned *,unsigned *);
bool light_write_valid(const char *kind,const char *,size_t);
bool light_history_parse(const char *,size_t,unsigned offset);
/* All I/O runs on the one bounded worker. Completion runs there; no LVGL. */
typedef struct {int http_status;char body[LIGHT_BODY_MAX+1];size_t length;unsigned session,generation;uint64_t received_ms;} light_reply_t;
typedef void (*light_callback_t)(const light_reply_t *,void *);
bool light_client_start(void);
bool light_client_request(const char *kind,const char *body,unsigned offset,light_callback_t,void *);
bool light_client_request_guarded_for_config(const char *kind,const char *body,unsigned offset,unsigned generation,light_callback_t,void *,bool (*guard)(void *));
bool light_client_request_guarded(const char *kind,const char *body,unsigned offset,light_callback_t,void *,bool (*guard)(void *));
bool light_client_action(light_action_t,uint64_t revision,unsigned generation,light_callback_t,void *);
void light_client_link(bool connected);
unsigned light_client_session(void);
uint64_t light_client_now(void);
#ifdef LIGHT_CLIENT_HOST_TEST
typedef int (*light_transport_t)(const char *,const char *,const char *,const char *,char *,size_t,size_t *,void *);
void light_client_test_transport(light_transport_t,void *);
void light_client_test_clock(uint64_t);
void light_client_test_run(void);
#endif
