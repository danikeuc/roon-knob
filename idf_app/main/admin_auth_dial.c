#include "admin_auth_dial.h"
#include "admin_backend_dial.h"
#include <string.h>

typedef struct {
    uint32_t version, iterations, failures;
    uint8_t pin_salt[16], pin_hash[32], recovery_salt[16], recovery_hash[32];
} auth_record_t;
typedef struct {
    bool loaded, configured, established;
    auth_record_t record;
    uint64_t locked_until, last_activity;
    char session[65];
} auth_state_t;
static auth_state_t state;
static size_t bounded_length(const char *s, size_t max) {
    if (!s) return max;
    size_t n = 0; while (n < max && s[n]) n++; return n;
}
static bool pin_valid(const char *s) {
    if (bounded_length(s, 5) != 4) return false;
    for (unsigned i=0; i<4; i++) if (s[i] < '0' || s[i] > '9') return false;
    return true;
}
static bool code_valid(const char *s) {
    if (bounded_length(s, 33) != 32) return false;
    for (unsigned i=0;i<32;i++) if (!((s[i]>='0' && s[i]<='9') || (s[i]>='a' && s[i]<='f'))) return false;
    return true;
}
static bool equal(const void *a, const void *b, size_t n) {
    const uint8_t *x=a,*y=b; volatile uint8_t diff=0;
    while(n--) diff |= *x++ ^ *y++;
    return diff==0;
}
static void hex_random(char *out, size_t bytes) {
    static const char hex[]="0123456789abcdef";
    uint8_t random[32]; admin_backend_random(random,bytes);
    for(size_t i=0;i<bytes;i++){out[2*i]=hex[random[i]>>4];out[2*i+1]=hex[random[i]&15];}
    out[2*bytes]=0;admin_secure_zero(random,sizeof(random));
}
static void invalidate_session(void) { admin_secure_zero(state.session,sizeof(state.session)); }
#define ESTABLISHED_MARKER 0x41440101u
static admin_result_t establish(void) {
    const uint32_t marker = ESTABLISHED_MARKER;
    uint32_t verify = 0;
    esp_err_t err = admin_backend_write("established", &marker, sizeof(marker));
    if (err == ESP_OK) err = admin_backend_commit();
    if (err == ESP_OK) err = admin_backend_read("established", &verify, sizeof(verify));
    if (err != ESP_OK || verify != marker) {
        state.loaded = false;
        invalidate_session();
        return ADMIN_STORAGE_ERROR;
    }
    state.established = true;
    return ADMIN_OK;
}
static admin_result_t load(void) {
    if (state.loaded) return ADMIN_OK;
    uint32_t marker = 0;
    esp_err_t err = admin_backend_read("established", &marker, sizeof(marker));
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) return ADMIN_STORAGE_ERROR;
    if (err == ESP_OK && marker != ESTABLISHED_MARKER) return ADMIN_STORAGE_ERROR;
    state.established = err == ESP_OK;
    auth_record_t r = {0};
    err = admin_backend_read("auth", &r, sizeof(r));
    if (err == ESP_ERR_NOT_FOUND) {
        /* The sticky marker prevents one damaged/missing credential key from
         * reopening first setup. Never erase it automatically. */
        if (state.established) return ADMIN_STORAGE_ERROR;
        state.configured = false;
        state.loaded = true;
        return ADMIN_OK;
    }
    if (err != ESP_OK || r.version != 1 || r.iterations != ADMIN_KDF_ITERATIONS || r.failures > 5) {
        admin_secure_zero(&r, sizeof(r));
        return ADMIN_STORAGE_ERROR;
    }
    /* Preserve valid credentials when only the marker is absent. */
    if (!state.established && establish() != ADMIN_OK) {
        admin_secure_zero(&r, sizeof(r));
        return ADMIN_STORAGE_ERROR;
    }
    state.record = r;
    state.configured = true;
    state.loaded = true;
    state.locked_until = r.failures == 5 ? admin_backend_seconds() + 60 : 0;
    admin_secure_zero(&r, sizeof(r));
    return ADMIN_OK;
}
static admin_result_t save(const auth_record_t *next) {
    auth_record_t verify={0};
    esp_err_t err=admin_backend_write("auth",next,sizeof(*next));
    if(err==ESP_OK) err=admin_backend_commit();
    if(err==ESP_OK) err=admin_backend_read("auth",&verify,sizeof(verify));
    bool ok=err==ESP_OK && equal(next,&verify,sizeof(verify));
    admin_secure_zero(&verify,sizeof(verify));
    if(!ok){state.loaded=false;invalidate_session();return ADMIN_STORAGE_ERROR;}
    state.record=*next;state.configured=true;state.loaded=true;return ADMIN_OK;
}
static bool session_valid(const char *session, bool touch) {
    if(!state.loaded || !state.configured || !state.session[0] ||
        bounded_length(session,65)!=64 || !equal(session,state.session,64)) return false;
    uint64_t now=admin_backend_seconds();
    if(now-state.last_activity>=900){invalidate_session();return false;}
    if(touch) state.last_activity=now;
    return true;
}
static admin_result_t failed_attempt(void) {
    auth_record_t next=state.record;
    next.failures++;
    admin_result_t result=save(&next);
    admin_secure_zero(&next,sizeof(next));
    if(result!=ADMIN_OK) return result;
    if(state.record.failures==5) state.locked_until=admin_backend_seconds()+60;
    return ADMIN_DENIED;
}
static bool verifier(const char *secret,const uint8_t salt[16],const uint8_t hash[32],bool *crypto_ok) {
    uint8_t computed[32]={0};
    *crypto_ok=admin_backend_kdf(secret,salt,computed);
    bool match=*crypto_ok && equal(computed,hash,32);
    admin_secure_zero(computed,sizeof(computed));return match;
}
static bool set_pin(auth_record_t *next,const char *pin) {
    admin_backend_random(next->pin_salt,16);
    return admin_backend_kdf(pin,next->pin_salt,next->pin_hash);
}
static bool set_recovery(auth_record_t *next,char code[33]) {
    hex_random(code,16);admin_backend_random(next->recovery_salt,16);
    return admin_backend_kdf(code,next->recovery_salt,next->recovery_hash);
}
typedef enum { SETUP, LOGIN, RECOVER, CHANGE, REISSUE, CONFIGURED } operation_t;
typedef struct {
    operation_t op;
    char first[65], pin[6], repeated[6], output[65];
    bool configured;
    admin_result_t result;
} request_t;
static void process(void *arg) {
    request_t *r=arg; r->result=ADMIN_STORAGE_ERROR;
    if(!admin_backend_lock()) return;
    admin_result_t result=load();
    if(result!=ADMIN_OK) goto done;
    if(r->op==CONFIGURED){r->configured=state.configured;goto done;}
    if(r->op==SETUP && state.configured){result=ADMIN_CONFLICT;goto done;}
    if(r->op!=SETUP && !state.configured){result=ADMIN_DENIED;goto done;}
    if(state.configured && state.record.failures==5) {
        if(admin_backend_seconds()<state.locked_until){result=ADMIN_LOCKED;goto done;}
        auth_record_t next=state.record;next.failures=0;
        result=save(&next);admin_secure_zero(&next,sizeof(next));
        if(result!=ADMIN_OK) goto done;
    }
    if(r->op==CHANGE || r->op==REISSUE) {
        if(!session_valid(r->first,false)){result=ADMIN_DENIED;goto done;}
    }
    if(r->op==SETUP || r->op==RECOVER || r->op==CHANGE) {
        if(!pin_valid(r->pin) || strcmp(r->pin,r->repeated)){result=ADMIN_INVALID;goto done;}
    }
    if(r->op==LOGIN || r->op==REISSUE || r->op==RECOVER) {
        const char *secret=r->op==RECOVER?r->first:r->pin;
        bool valid=r->op==RECOVER?code_valid(secret):pin_valid(secret);
        bool crypto_ok=true;
        bool match=valid && verifier(secret,
            r->op==RECOVER?state.record.recovery_salt:state.record.pin_salt,
            r->op==RECOVER?state.record.recovery_hash:state.record.pin_hash,&crypto_ok);
        if(!crypto_ok){result=ADMIN_STORAGE_ERROR;goto done;}
        if(!match){result=failed_attempt();goto done;}
    }
    auth_record_t next=state.record;
    next.version=1;next.iterations=ADMIN_KDF_ITERATIONS;next.failures=0;
    bool ok=true;
    if(r->op==SETUP || r->op==RECOVER || r->op==CHANGE) ok=set_pin(&next,r->pin);
    if(ok && (r->op==SETUP || r->op==RECOVER || r->op==REISSUE)) ok=set_recovery(&next,r->output);
    result = ok ? ADMIN_OK : ADMIN_STORAGE_ERROR;
    if (result == ADMIN_OK && !state.established) result = establish();
    if (result == ADMIN_OK) result = save(&next);
    admin_secure_zero(&next,sizeof(next));
    if(result!=ADMIN_OK) goto done;
    if(r->op==LOGIN){invalidate_session();hex_random(state.session,32);state.last_activity=admin_backend_seconds();memcpy(r->output,state.session,65);}
    if(r->op==RECOVER || r->op==CHANGE) invalidate_session();
    if(r->op==REISSUE) state.last_activity=admin_backend_seconds();
 done:
    if(result!=ADMIN_OK) admin_secure_zero(r->output,sizeof(r->output));
    r->result=result;admin_backend_unlock();
}
static void copy_input(char *to,size_t capacity,const char *from) {
    size_t n=bounded_length(from,capacity);
    if(n>=capacity) { memset(to,'!',capacity-1);to[capacity-1]=0;return; }
    memcpy(to,from,n+1);
}
static admin_result_t dispatch(operation_t op,const char *first,const char *pin,const char *repeated,char *out) {
    request_t r={.op=op,.result=ADMIN_STORAGE_ERROR};
    copy_input(r.first,sizeof(r.first),first?first:"");
    copy_input(r.pin,sizeof(r.pin),pin?pin:"");
    copy_input(r.repeated,sizeof(r.repeated),repeated?repeated:"");
    /* Copy inputs before clearing output to support recover(code,...,code). */
    if(out) out[0]=0;
    bool accepted=admin_backend_execute(process,&r);
    admin_result_t result=accepted?r.result:ADMIN_CONFLICT;
    if(result==ADMIN_OK && out) memcpy(out,r.output,op==LOGIN?65:33);
    admin_secure_zero(&r,sizeof(r));return result;
}
admin_result_t admin_auth_setup(const char *p,const char *repeat,char out[33]) { return out?dispatch(SETUP,NULL,p,repeat,out):ADMIN_INVALID; }
admin_result_t admin_auth_login(const char *p,char out[65]) { return out?dispatch(LOGIN,NULL,p,NULL,out):ADMIN_INVALID; }
admin_result_t admin_auth_recover(const char *code,const char *p,const char *repeat,char out[33]) { return out?dispatch(RECOVER,code,p,repeat,out):ADMIN_INVALID; }
admin_result_t admin_auth_change_pin(const char *session,const char *p,const char *repeat) { return dispatch(CHANGE,session,p,repeat,NULL); }
admin_result_t admin_auth_reissue_recovery(const char *session,const char *p,char out[33]) { return out?dispatch(REISSUE,session,p,NULL,out):ADMIN_INVALID; }
admin_result_t admin_auth_is_configured(bool *configured) {
    if(!configured)return ADMIN_INVALID;
    request_t r={.op=CONFIGURED,.result=ADMIN_STORAGE_ERROR};
    bool accepted=admin_backend_execute(process,&r);
    admin_result_t result=accepted?r.result:ADMIN_CONFLICT;
    if(result==ADMIN_OK)*configured=r.configured;
    admin_secure_zero(&r,sizeof(r));return result;
}
bool admin_auth_session_valid(const char *session,bool touch) {
    if(!admin_backend_lock())return false;
    bool valid=session_valid(session,touch);admin_backend_unlock();return valid;
}
void admin_auth_logout(const char *session) {
    if(!admin_backend_lock())return;
    if(session_valid(session,false))invalidate_session();
    admin_backend_unlock();
}
