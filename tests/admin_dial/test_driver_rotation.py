"""Compile the actual production flush callback, with only LCD/LVGL I/O faked."""
from pathlib import Path
import subprocess, tempfile, os
source=Path('idf_app/main/platform_display_idf.c').read_text()
start=source.index('static uint8_t *s_rotate_buf')
end=source.index('// LVGL tick timer callback',start)
preamble=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "display_rotation_dial.h"
#define LCD_H_RES 360
#define LCD_V_RES 360
#define ESP_LOGE(...) ((void)0)
#define ESP_ERROR_CHECK(x) assert((x)==0)
#define LV_DISPLAY_ROTATION_180 2
#define LV_DISPLAY_ROTATION_90 1
#define LVGL_BUF_HEIGHT 24
#define ESP_OK 0
#define ESP_FAIL -1
typedef int esp_err_t;
typedef void *esp_lcd_panel_handle_t;
typedef int lv_display_t;
typedef int lv_display_rotation_t;
typedef struct {int x1,y1,x2,y2;} lv_area_t;
static uint16_t s_current_rotation;
static void *s_io_handle;
static bool inflight;
static int draws;
static uint16_t frame[360*360];
static void *lv_display_get_user_data(lv_display_t *d){(void)d;return 0;}
static int lv_display_get_rotation(lv_display_t *d){(void)d;return s_current_rotation/90;}
static int lv_area_get_width(const lv_area_t *a){return a->x2-a->x1+1;}
static int lv_area_get_height(const lv_area_t *a){return a->y2-a->y1+1;}
static int esp_lcd_panel_draw_bitmap(void *p,int x,int y,int x2,int y2,const void *pixels){
 (void)p;assert(!inflight);inflight=true;draws++;
 const uint16_t *src=pixels;
 for(int yy=y;yy<y2;yy++)for(int xx=x;xx<x2;xx++)frame[yy*360+xx]=*src++;
 return 0;
}
static int esp_lcd_panel_io_tx_param(void *io,int cmd,const void *p,size_t n){(void)io;assert(cmd==-1 && !p && !n);inflight=false;return 0;}
static void lv_display_flush_ready(lv_display_t *d){(void)d;assert(!inflight);}
'''
main=r'''
int main(void){
 s_rotate_buf=malloc(ROTATE_BUF_SIZE);assert(s_rotate_buf);
 const uint16_t angles[]={0,90,180,270};
 /* All blocks, including a full-screen flush larger than scratch. */
 for(int a=0;a<4;a++){
  s_current_rotation=angles[a]; memset(frame,0,sizeof(frame));draws=0;
  uint16_t *pixels=malloc(360*360*2);
  for(int i=0;i<360*360;i++)pixels[i]=(uint16_t)(i+1);
  lv_area_t area={0,0,359,359};lvgl_flush_cb(0,&area,(uint8_t*)pixels);
  for(int y=0;y<360;y++)for(int x=0;x<360;x++){
   int px=x,py=y;
   if(a==1){px=359-y;py=x;}if(a==2){px=359-x;py=359-y;}if(a==3){px=y;py=359-x;}
   uint16_t v=(uint16_t)(y*360+x+1);v=(uint16_t)((v>>8)|(v<<8));
   assert(frame[py*360+px]==v);
  }
  free(pixels);
  /* Unequal edge partials reuse the same DMA allocation back-to-back. */
  uint16_t partial[24]={0};
  for(int pass=0;pass<2;pass++){
   memset(frame,0,sizeof(frame));
   for(int i=0;i<24;i++)partial[i]=(uint16_t)(0x1210+i+pass);
   lv_area_t edge={354,352,359,355};lvgl_flush_cb(0,&edge,(uint8_t*)partial);
   for(int y=0;y<4;y++)for(int x=0;x<6;x++){
    int px=354+x,py=352+y;
    if(a==1){px=7-y;py=354+x;}if(a==2){px=5-x;py=7-y;}if(a==3){px=352+y;py=5-x;}
    uint16_t v=(uint16_t)(0x1210+y*6+x+pass);v=(uint16_t)((v>>8)|(v<<8));
    assert(frame[py*360+px]==v);
   }
  }
 }
 free(s_rotate_buf);puts("production flush four-angle DMA/chunks: PASS");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'driver.c';p.write_text(preamble+source[start:end]+main)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-c23-extensions','-Wno-unused-function','-Wno-unused-variable','-Iidf_app/main',str(p),'idf_app/main/display_rotation_dial.c','-o',d+'/test'],check=True)
 subprocess.run([d+'/test'],check=True)
