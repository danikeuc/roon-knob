#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define PLAYLIST_MAX_FAVORITES 5
#define PLAYLIST_ID_LEN 129
#define PLAYLIST_LABEL_LEN 97
#define PLAYLIST_ZONE_LEN 129
#define PLAYLIST_INACTIVITY_MS UINT64_C(15000)
#define PLAYLIST_UNSENT_MS UINT64_C(10000)
typedef struct { char id[PLAYLIST_ID_LEN],title[PLAYLIST_LABEL_LEN],reason[PLAYLIST_LABEL_LEN];bool available; } playlist_favorite_t;
typedef struct { unsigned count;uint64_t revision,core_epoch;char zone_id[PLAYLIST_ZONE_LEN];playlist_favorite_t entries[PLAYLIST_MAX_FAVORITES]; } playlist_favorites_t;
typedef enum { PLAYLIST_IDLE,PLAYLIST_PICKING,PLAYLIST_PREPARING,PLAYLIST_PENDING,PLAYLIST_ERROR } playlist_phase_t;
typedef enum { PLAYLIST_ACTION_NONE,PLAYLIST_ACTION_RENDER,PLAYLIST_ACTION_PREPARE_PLAY,PLAYLIST_ACTION_CANCEL } playlist_action_kind_t;
typedef struct { playlist_action_kind_t kind;char favorite_id[PLAYLIST_ID_LEN];uint64_t revision,core_epoch; } playlist_action_t;
typedef struct { playlist_phase_t phase;playlist_favorites_t favorites;int selected;uint64_t activity_ms; } controller_playlist_t;
playlist_action_t controller_playlist_open(controller_playlist_t *,const playlist_favorites_t *,const char *,uint64_t);
playlist_action_t controller_playlist_rotate(controller_playlist_t *,int);
playlist_action_t controller_playlist_confirm(controller_playlist_t *,uint64_t);
playlist_action_t controller_playlist_cancel(controller_playlist_t *);
playlist_action_t controller_playlist_tick(controller_playlist_t *,uint64_t);
/* Validates complete UTF-8, then truncates labels only at a codepoint boundary. */
bool playlist_label_copy(char *,size_t,const char *,size_t);
