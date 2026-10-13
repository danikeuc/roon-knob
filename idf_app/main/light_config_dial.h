#pragma once
#include <stdbool.h>
#include <stdint.h>
#define LIGHT_URL_LEN 129
#define LIGHT_TOKEN_LEN 513
typedef struct {char url[LIGHT_URL_LEN],display[LIGHT_TOKEN_LEN],settings[LIGHT_TOKEN_LEN];} light_config_t;
uint32_t light_config_generation(void);
bool light_config_valid(const light_config_t *);
bool light_config_load(light_config_t *);
bool light_config_save(const light_config_t *);
bool light_config_clear(void);
