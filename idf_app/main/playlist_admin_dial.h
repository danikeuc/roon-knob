#pragma once
#include "controller_playlist.h"
#define PLAYLIST_BODY_MAX 4096U
#define PLAYLIST_CATALOG_MAX (192U*1024U)
#define PLAYLIST_FAVORITES_MAX (16U*1024U)
#define PLAYLIST_NUMBER_MAX UINT64_C(9007199254740991)
bool playlist_favorites_parse(const char *,size_t,playlist_favorites_t *);
bool playlist_write_valid(const char *,size_t);
bool playlist_catalog_valid(const char *,size_t);
bool playlist_capabilities_valid(const char *,size_t);
bool playlist_intent_parse(const char *,size_t,char *,size_t,uint64_t *);
bool playlist_receipt_parse(const char *,size_t,const char *,const char *,int *);
/* Strict bounded JSON grammar and raw-key validation precedes cJSON. */
void *playlist_json_parse(const char *,size_t,size_t);
bool playlist_private_save(const char *base,const char *token);
bool playlist_private_load(const char *base,char *token,size_t);
uint32_t playlist_private_generation(void);
/* Owns returned JSON; caller frees it. No token is present in the output. */
int playlist_admin_get(bool catalog,const char *cursor,char **json,size_t *length);
int playlist_admin_put(const char *body,size_t length,char **json,size_t *out_length);
