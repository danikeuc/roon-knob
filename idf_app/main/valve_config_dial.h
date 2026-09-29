#ifndef VALVE_CONFIG_DIAL_H
#define VALVE_CONFIG_DIAL_H

#include <stdbool.h>
#include <stddef.h>

/* Includes the terminating NUL. Tokens may contain up to 128 bytes. */
#define VALVE_CONFIG_URL_LEN 129
#define VALVE_CONFIG_TOKEN_LEN 129

bool valve_config_load(char *base_url, size_t url_len, char *token, size_t token_len);
bool valve_config_save(const char *base_url, const char *token);
bool valve_config_clear(void);

#endif
