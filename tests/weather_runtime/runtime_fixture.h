#pragma once
#include <stdint.h>
#include <stdatomic.h>
typedef enum { HTTP_ACTION_EXPIRED_OPEN,HTTP_WEATHER_STALLED_OPEN,HTTP_WEATHER_SLOW_RESPONSE,HTTP_WEATHER_NORMAL,HTTP_OBSERVATION_STALLED_OPEN,HTTP_OBSERVATION_MALFORMED } fixture_http_case_t;
extern atomic_uint fixture_body_writes,fixture_cleanups,fixture_last_timeout;
extern uint64_t fixture_action_now;
void runtime_http_case(fixture_http_case_t);
uint64_t runtime_wall_ms(void);
void runtime_wait_cleanup(unsigned);

void runtime_wait_counts(unsigned exchanges,unsigned jobs);
