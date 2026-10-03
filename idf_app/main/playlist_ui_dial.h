#pragma once
#include "controller_playlist.h"
bool playlist_ui_touch(int x,int y,bool pressed,bool moved,uint64_t now_ms);
bool playlist_ui_visible(void);
void playlist_ui_cancel(void);
void playlist_ui_process(void);
void playlist_ui_rotate(int delta);
#ifdef PLAYLIST_UI_HOST_TEST
bool playlist_ui_context_allowed(void);
bool playlist_ui_current_zone(char *,size_t);
uint64_t playlist_ui_now_ms(void);
void playlist_ui_render(const controller_playlist_t *,const char *);
#endif
