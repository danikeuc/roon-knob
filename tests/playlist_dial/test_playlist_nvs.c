/* Link production persistence (no PLAYLIST_HOST_TEST) against the real IDF NVS
 * declarations. Only the flash backend is replaced by a file-backed fake. */
#include "playlist_admin_dial.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *directory;
static char path[512], pending[260];
static size_t pending_size;
static nvs_open_mode_t access_mode;
static bool opened;
static enum { NONE, OPEN, SET, COMMIT, GET } failure;

esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *handle) {
 if(!name||!*name||strlen(name)>=NVS_NS_NAME_MAX_SIZE)return ESP_ERR_NVS_INVALID_NAME;
 if(failure==OPEN)return ESP_FAIL;
 assert(!opened);
 int size=snprintf(path,sizeof path,"%s/%s",directory,name);assert(size>0&&(size_t)size<sizeof path);
 if(mode==NVS_READONLY){FILE *file=fopen(path,"rb");if(!file)return ESP_ERR_NVS_NOT_FOUND;fclose(file);}
 access_mode=mode;opened=true;pending_size=0;*handle=1;return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle,const char *key,const void *value,size_t size) {
 assert(opened&&handle==1&&!strcmp(key,"credential")&&access_mode==NVS_READWRITE&&size<=sizeof pending);
 if(failure==SET)return ESP_FAIL;
 memcpy(pending,value,size);pending_size=size;return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle) {
 assert(opened&&handle==1&&access_mode==NVS_READWRITE);
 if(failure==COMMIT)return ESP_FAIL;
 FILE *file=fopen(path,"wb");assert(file);assert(fwrite(pending,1,pending_size,file)==pending_size);assert(fclose(file)==0);return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t handle,const char *key,void *value,size_t *size) {
 assert(opened&&handle==1&&!strcmp(key,"credential"));
 if(failure==GET)return ESP_FAIL;
 FILE *file=fopen(path,"rb");if(!file)return ESP_ERR_NVS_NOT_FOUND;
 char saved[260];size_t count=fread(saved,1,sizeof saved,file);assert(!ferror(file));fclose(file);
 if(*size<count){*size=count;return ESP_ERR_NVS_INVALID_LENGTH;}
 memcpy(value,saved,count);*size=count;return ESP_OK;
}
void nvs_close(nvs_handle_t handle) {assert(opened&&handle==1);opened=false;memset(pending,0,sizeof pending);pending_size=0;}

int main(int argc,char **argv) {
 assert(argc==3);directory=argv[2];char token[129];
 if(!strcmp(argv[1],"save")) {
  assert(playlist_private_save("http://bridge/roon","synthetic-private"));
  assert(playlist_private_save("http://bridge/roon",""));
 } else if(!strcmp(argv[1],"restart-load")) {
  /* Invoked in a fresh process: only the fake flash file survives. */
  assert(playlist_private_load("http://bridge/roon",token,sizeof token));
  assert(!strcmp(token,"synthetic-private"));
  assert(!playlist_private_load("http://other/roon",token,sizeof token)&&!*token);
 } else {
  assert(!strcmp(argv[1],"failures"));
  for(int fail=OPEN;fail<=COMMIT;fail++) {
   failure=fail;assert(!playlist_private_save("http://bridge/roon","replacement"));assert(!opened);
   failure=NONE;assert(playlist_private_load("http://bridge/roon",token,sizeof token)&&!strcmp(token,"synthetic-private"));
  }
  failure=GET;assert(!playlist_private_load("http://bridge/roon",token,sizeof token)&&!*token);
  assert(!playlist_private_save("http://bridge/roon","readback-fails"));assert(!opened);
  failure=OPEN;assert(!playlist_private_load("http://bridge/roon",token,sizeof token)&&!*token);
 }
 assert(!opened);puts("production playlist NVS persistence PASS");
}
