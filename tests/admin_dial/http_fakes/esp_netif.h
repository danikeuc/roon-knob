#pragma once
#include "esp_err.h"
typedef struct esp_netif esp_netif_t;
esp_netif_t *esp_netif_get_handle_from_ifkey(const char*);
esp_err_t esp_netif_get_hostname(esp_netif_t*,const char**);
