/* Compile-only subset for the bridge command host fixture. The test links away
 * response-parsing sections and never executes a cJSON API. */
#ifndef VALVE_TEST_FAKE_CJSON_H
#define VALVE_TEST_FAKE_CJSON_H
#include <stddef.h>
typedef struct cJSON {
    struct cJSON *next, *prev, *child;
    int type;
    char *valuestring;
    int valueint;
    double valuedouble;
    char *string;
} cJSON;
typedef struct cJSON_Hooks {
    void *(*malloc_fn)(size_t size);
    void (*free_fn)(void *ptr);
} cJSON_Hooks;
void cJSON_InitHooks(cJSON_Hooks *hooks);
cJSON *cJSON_Parse(const char *text);
void cJSON_Delete(cJSON *item);
cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *object, const char *key);
int cJSON_IsBool(const cJSON *item);
int cJSON_IsNumber(const cJSON *item);
int cJSON_IsObject(const cJSON *item);
int cJSON_IsString(const cJSON *item);
int cJSON_IsTrue(const cJSON *item);
#endif
