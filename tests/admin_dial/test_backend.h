#pragma once
#include "admin_backend_dial.h"
#include <assert.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
/* Fake NVS persists at set_blob, including when the subsequent commit fails. */
static unsigned char blobs[3][256];
static size_t lengths[3];
static int fail_at, operations;
static uint64_t now;
static unsigned entropy;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static int key_index(const char *key) { assert(!strcmp(key,"auth") || !strcmp(key,"settings") || !strcmp(key,"established")); return !strcmp(key,"auth") ? 0 : !strcmp(key,"settings") ? 1 : 2; }
static bool fault(void) { return ++operations == fail_at; }
bool admin_backend_lock(void) { return pthread_mutex_lock(&mutex)==0; }
void admin_backend_unlock(void) { assert(pthread_mutex_unlock(&mutex)==0); }
esp_err_t admin_backend_read(const char *key,void*out,size_t size) {
    if(fault()) return ESP_FAIL;
    int i=key_index(key);
    if(!lengths[i]) return ESP_ERR_NOT_FOUND;
    if(lengths[i]!=size) return ESP_FAIL;
    memcpy(out,blobs[i],size); return ESP_OK;
}
esp_err_t admin_backend_write(const char *key,const void*data,size_t size) {
    if(fault()) return ESP_FAIL;
    int i=key_index(key); assert(size<=256);
    memcpy(blobs[i],data,size); lengths[i]=size;return ESP_OK;
}
esp_err_t admin_backend_commit(void) { return fault()?ESP_FAIL:ESP_OK; }
uint64_t admin_backend_seconds(void) { return now; }
void admin_backend_random(void*out,size_t size) { unsigned char*p=out; while(size--) *p++=(unsigned char)++entropy; }
bool admin_backend_kdf(const char*s,const uint8_t salt[16],uint8_t out[32]) {
    /* Fast deterministic stand-in; production adapter separately vector-tested. */
    for(unsigned i=0;i<32;i++)out[i]=salt[i%16];
    for(unsigned i=0;s[i];i++)out[i%32]^=(unsigned char)s[i];
    return true;
}
bool admin_backend_execute(void(*fn)(void*),void*arg){fn(arg);return true;}
void admin_secure_zero(void*p,size_t n){volatile unsigned char*v=p;while(n--)*v++=0;}
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL %s line %d\n",__func__,__LINE__); exit(1); } } while(0)
