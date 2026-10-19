#pragma once
#include "controller_playlist.h"
typedef enum { PLAYLIST_CLIENT_ERROR=1,PLAYLIST_CLIENT_FAVORITES,PLAYLIST_CLIENT_ACCEPTED,PLAYLIST_CLIENT_PLAYING,PLAYLIST_CLIENT_UNKNOWN,PLAYLIST_CLIENT_WAITING } playlist_client_status_t;
typedef struct { char base[129],zone[129],token[129];uint32_t generation,session,credential_generation; } playlist_endpoint_t;
typedef struct { const char *method,*path,*body,*token,*url,*zone;uint64_t deadline_ms;bool (*valid)(void *);void *context; } playlist_http_request_t;
typedef struct { playlist_client_status_t status;playlist_favorites_t favorites;char request_id[37],zone[129];uint32_t generation; } playlist_client_result_t;
/* Internal admin failure stages. The HTTP handler always returns 503 for these. */
typedef enum {
 PLAYLIST_ADMIN_ADMISSION=590,PLAYLIST_ADMIN_CREDENTIAL,PLAYLIST_ADMIN_BINDING,
 PLAYLIST_ADMIN_CAPABILITIES,PLAYLIST_ADMIN_ALLOCATION,PLAYLIST_ADMIN_EXCHANGE,
 PLAYLIST_ADMIN_REMOTE_STATUS,PLAYLIST_ADMIN_RESPONSE,PLAYLIST_ADMIN_TIMEOUT
} playlist_admin_failure_t;
bool playlist_client_init(void);
void playlist_client_connected(bool);
void playlist_client_cancel(void);
bool playlist_client_request_favorites(void);
bool playlist_client_request_play(const char *,uint64_t,uint64_t);
bool playlist_client_take(playlist_client_result_t *);
void playlist_client_process(void);
int playlist_client_admin(const char *,const char *,const char *,size_t,char **,size_t *);
#ifdef PLAYLIST_HOST_TEST
void playlist_client_test_hooks(uint64_t (*clock)(void),bool (*snapshot)(playlist_endpoint_t *),int (*transport)(const playlist_http_request_t *,int *,char *,size_t,size_t *));
void playlist_client_test_run(void);
#endif
