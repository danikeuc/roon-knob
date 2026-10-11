"""Compile the actual custom transport; fake only IDF socket/clock/allocation."""
from pathlib import Path
import subprocess,tempfile
source=Path('idf_app/main/platform_http_idf.c').read_text()
start=source.index('int platform_http_get_dial_artwork(')
body=source[start:source.index('\n#endif',start)]
preamble=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "esp_http_client.h"
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define PLAYLIST_ARTWORK_BYTES 259200U
static uint64_t clock_value;
static unsigned bytes=259200,allocation,reads;
static bool stale,stall;
static int64_t esp_timer_get_time(void){return (int64_t)clock_value*1000;}
static void *heap_caps_malloc(size_t n,unsigned caps){(void)caps;assert(n==259200);allocation++;return malloc(n);}
static bool current(void *p){(void)p;return !stale;}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c){assert(c->disable_auto_redirect);return (void*)1;}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c,const char *k,const char *v){(void)c;(void)k;(void)v;return 0;}
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t c,int n){(void)c;assert(n>0&&n<=3000);return 0;}
esp_err_t esp_http_client_open(esp_http_client_handle_t c,int n){(void)c;(void)n;if(stall)clock_value+=5000;return 0;}
int esp_http_client_fetch_headers(esp_http_client_handle_t c){(void)c;return (int)bytes;}
int esp_http_client_get_status_code(esp_http_client_handle_t c){(void)c;return 200;}
int esp_http_client_read(esp_http_client_handle_t c,char *out,int n){(void)c;reads++;if(!bytes)return 0;unsigned used=bytes<(unsigned)n?bytes:(unsigned)n;memset(out,0,used);bytes-=used;return (int)used;}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c){(void)c;return bytes==0;}
esp_err_t esp_http_client_close(esp_http_client_handle_t c){(void)c;return 0;}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c){(void)c;return 0;}
'''
main=r'''
int main(void){char *out=NULL;size_t size=0;assert(platform_http_get_dial_artwork("http://bridge",&out,&size,current,NULL)==0&&size==259200);free(out);unsigned before=allocation;bytes=259201;assert(platform_http_get_dial_artwork("http://bridge",&out,&size,current,NULL)!=0&&allocation==before);bytes=259200;stall=true;before=reads;assert(platform_http_get_dial_artwork("http://bridge",&out,&size,current,NULL)!=0&&reads==before);stall=false;stale=true;assert(platform_http_get_dial_artwork("http://bridge",&out,&size,current,NULL)!=0);puts("production bounded artwork payload/deadline PASS");}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory)/'art.c';p.write_text(preamble+body+main)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Itests/weather_runtime/fakes','-Itests/admin_dial/fakes',str(p),'-o',directory+'/art'],check=True)
 subprocess.run([directory+'/art'],check=True)
