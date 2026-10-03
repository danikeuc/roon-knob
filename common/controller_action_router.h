#pragma once

#include "controller_action.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void controller_action_router_init(void);
bool controller_action_router_handle(const controller_action_t *action);

#ifdef __cplusplus
}
#endif

/* Custom presentation installs its target callback; other targets reject it. */
void controller_action_router_set_playlist_handler(void (*handler)(int));
