#include "light_config_dial.h"
#include <string.h>
#include <stdatomic.h>
#ifndef LIGHT_CONFIG_HOST_TEST
#include "nvs.h"
#endif
static atomic_uint generation;
uint32_t light_config_generation(void) {
    return atomic_load(&generation);
}
static bool token_valid(const char *v) {
    size_t n=strnlen(v,LIGHT_TOKEN_LEN);
    if (n<32||n>=LIGHT_TOKEN_LEN)return false;
    for (size_t i=0;i<n;i++)if ((unsigned char)v[i]<33||(unsigned char)v[i]>126)return false;
    return true;
}
bool light_config_valid(const light_config_t *c) {
    if (!c||strncmp(c->url,"http://",7)||strnlen(c->url,LIGHT_URL_LEN)>=LIGHT_URL_LEN||!token_valid(c->display)||!token_valid(c->settings)||!strcmp(c->display,c->settings))return false;
    const char *p=c->url+7,*host=p;
    while (*p&&*p!=':') {
        char x=*p++;
        if (!((x>='a'&&x<='z')||(x>='A'&&x<='Z')||(x>='0'&&x<='9')||x=='.'||x=='-'))return false;
    }
    if (p==host||*host=='.'||*host=='-'||p[-1]=='.'||p[-1]=='-')return false;
    if (*p) {
        unsigned port=0;
        p++;
        if (!*p)return false;
        while (*p) {
            if (*p<'0'||*p>'9')return false;
            port=port*10+(*p++-'0');
            if (port>65535)return false;
        }
        if (!port)return false;
    }
    return true;
}
#ifdef LIGHT_CONFIG_HOST_TEST
extern bool light_test_store_read(void *,size_t);
extern bool light_test_store_write(const void *,size_t);
extern bool light_test_store_clear(void);
#else
static bool light_test_store_read(void *v,size_t len) {
    nvs_handle_t h;
    if (nvs_open("light_private",NVS_READONLY,&h)!=ESP_OK)return false;
    size_t n=len;
    bool ok=nvs_get_blob(h,"credentials",v,&n)==ESP_OK&&n==len;
    nvs_close(h);
    return ok;
}
static bool light_test_store_write(const void *v,size_t n) {
    nvs_handle_t h;
    if (nvs_open("light_private",NVS_READWRITE,&h)!=ESP_OK)return false;
    bool ok=nvs_set_blob(h,"credentials",v,n)==ESP_OK&&nvs_commit(h)==ESP_OK;
    nvs_close(h);
    return ok;
}
static bool light_test_store_clear(void) {
    nvs_handle_t h;
    esp_err_t e=nvs_open("light_private",NVS_READWRITE,&h);
    if (e==ESP_ERR_NVS_NOT_FOUND)return true;
    if (e!=ESP_OK)return false;
    bool ok=nvs_erase_all(h)==ESP_OK&&nvs_commit(h)==ESP_OK;
    nvs_close(h);
    return ok;
}
#endif
bool light_config_load(light_config_t *out) {
    if (!out)return false;
    memset(out,0,sizeof *out);
    unsigned g=atomic_load(&generation);
    bool ok=!(g&1)&&light_test_store_read(out,sizeof *out)&&light_config_valid(out)&&g==atomic_load(&generation);
    if (!ok)memset(out,0,sizeof *out);
    return ok;
}
static bool begin(void) {
    unsigned g=atomic_load(&generation);
    return !(g&1)&&atomic_compare_exchange_strong(&generation,&g,g+1);
}
bool light_config_save(const light_config_t *c) {
    if (!light_config_valid(c)||!begin())return false;
    /* One atomic NVS blob binds both private roles to the configured host. */
    light_config_t copy= {
        0
    }
    ,verify= {
        0
    };
    strcpy(copy.url,c->url);
    strcpy(copy.display,c->display);
    strcpy(copy.settings,c->settings);
    bool ok=light_test_store_write(&copy,sizeof copy)&&light_test_store_read(&verify,sizeof verify)&&!memcmp(&copy,&verify,sizeof copy);
    memset(&copy,0,sizeof copy);
    memset(&verify,0,sizeof verify);
    atomic_fetch_add(&generation,1);
    return ok;
}
bool light_config_clear(void) {
    if (!begin())return false;
    bool ok=light_test_store_clear();
    atomic_fetch_add(&generation,1);
    return ok;
}
