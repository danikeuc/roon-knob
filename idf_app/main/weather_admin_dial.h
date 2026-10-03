#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define WEATHER_TIMEZONE_LEN 65
#define WEATHER_BODY_MAX 4096
#define WEATHER_REVISION_MAX UINT64_C(9007199254740991)
typedef enum { WEATHER_ADMIN_OK, WEATHER_ADMIN_INVALID, WEATHER_ADMIN_CONFLICT,
               WEATHER_ADMIN_TIMEOUT, WEATHER_ADMIN_UNAVAILABLE, WEATHER_ADMIN_UNAUTHORIZED } weather_admin_result_t;
typedef struct {
    uint64_t revision;
    bool enabled, coordinates_available;
    double latitude, longitude;
    char timezone[WEATHER_TIMEZONE_LEN];
    weather_admin_result_t result; /* Internal only; never serialized to the Pi DTO. */
} weather_settings_view_t;
typedef struct {
    uint64_t expected_revision;
    bool enabled, coordinates_available;
    double latitude, longitude;
    char timezone[WEATHER_TIMEZONE_LEN];
} weather_settings_write_t;
bool weather_admin_init(void);
bool weather_admin_get(weather_settings_view_t *out);
bool weather_admin_update(const weather_settings_write_t *request, weather_settings_view_t *out);
bool weather_settings_write_parse(const char *json,size_t length,weather_settings_write_t *out);
bool weather_settings_view_json(const weather_settings_view_t *view,char *out,size_t capacity);
/* Mutation epoch cancels unsent held touches. It never commands a valve. */
uint32_t weather_admin_generation(void);
#ifdef WEATHER_ADMIN_HOST_TEST
typedef int (*weather_admin_transport_fn)(const char *,const char *,const char *,const char *,
    const char *,size_t,int *,char *,size_t,size_t *,void *);
void weather_admin_test_transport(weather_admin_transport_fn transport,void *context);
#endif
