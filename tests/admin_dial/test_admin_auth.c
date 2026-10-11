#include "admin_auth_dial.h"
#include "admin_store_dial.h"
#include "test_backend.h"
#include <sys/wait.h>
#include <unistd.h>
static char code[33],session[65];
static void setup(void){CHECK(admin_auth_setup("0012","0012",code)==ADMIN_OK);}
static void test_pin_leading_zero_and_exact_ascii_length(void) {
    CHECK(admin_auth_setup("123","123",code)==ADMIN_INVALID);
    CHECK(admin_auth_setup("12345","12345",code)==ADMIN_INVALID);
    CHECK(admin_auth_setup("a123","a123",code)==ADMIN_INVALID);
    CHECK(admin_auth_setup("１２３４","１２３４",code)==ADMIN_INVALID);
    CHECK(admin_auth_setup("0012","0013",code)==ADMIN_INVALID);
    setup();CHECK(admin_auth_login("0012",session)==ADMIN_OK);
    CHECK(strlen(code)==32 && strlen(session)==64);
}
static admin_result_t race_results[2];
static void*racer(void*p){char out[33];race_results[(size_t)p]=admin_auth_setup("0012","0012",out);return NULL;}
static void test_setup_race_has_one_winner(void){pthread_t a,b;pthread_create(&a,NULL,racer,(void*)0);pthread_create(&b,NULL,racer,(void*)1);pthread_join(a,NULL);pthread_join(b,NULL);CHECK((race_results[0]==ADMIN_OK && race_results[1]==ADMIN_CONFLICT)||(race_results[1]==ADMIN_OK && race_results[0]==ADMIN_CONFLICT));}
static void test_recovery_is_single_use(void){setup();char old[33];strcpy(old,code);CHECK(admin_auth_recover(old,"9999","9999",code)==ADMIN_OK);CHECK(admin_auth_recover(old,"1111","1111",code)==ADMIN_DENIED);}
static void test_lost_recovery_response_new_pin_works(void){setup();CHECK(admin_auth_recover(code,"9999","9999",code)==ADMIN_OK);CHECK(admin_auth_login("9999",session)==ADMIN_OK);CHECK(admin_auth_login("0012",session)==ADMIN_DENIED);}
static void test_lockout_60_seconds(void){setup();for(int i=0;i<5;i++)CHECK(admin_auth_login("8888",session)==ADMIN_DENIED);now=59;CHECK(admin_auth_login("0012",session)==ADMIN_LOCKED);now=60;CHECK(admin_auth_login("0012",session)==ADMIN_OK);}
static void test_session_idle_900_seconds(void){setup();CHECK(admin_auth_login("0012",session)==ADMIN_OK);now=899;CHECK(admin_auth_session_valid(session,true));now=1798;CHECK(admin_auth_session_valid(session,false));now=1799;CHECK(!admin_auth_session_valid(session,false));}
static void test_change_and_reissue(void){setup();CHECK(admin_auth_login("0012",session)==ADMIN_OK);char old[33];strcpy(old,code);CHECK(admin_auth_reissue_recovery(session,"0012",code)==ADMIN_OK);CHECK(admin_auth_recover(old,"9999","9999",old)==ADMIN_DENIED);CHECK(admin_auth_change_pin(session,"9999","9999")==ADMIN_OK);CHECK(!admin_auth_session_valid(session,false));CHECK(admin_auth_login("9999",session)==ADMIN_OK);admin_auth_logout(session);CHECK(!admin_auth_session_valid(session,false));}
static void test_corrupt_auth_not_setup(void){lengths[0]=1;bool configured=false;CHECK(admin_auth_is_configured(&configured)==ADMIN_STORAGE_ERROR);CHECK(admin_auth_setup("0012","0012",code)==ADMIN_STORAGE_ERROR);}
static void test_storage_failure(void){setup();for(int step=1;step<=3;step++){operations=0;fail_at=step;CHECK(admin_auth_login("8888",session)==ADMIN_STORAGE_ERROR);fail_at=0;}}
static void test_recovery_preserves_settings(void) {
    admin_settings_t before={.duration_seconds=120,.rotation_degrees=180,.rotation_override=true};
    CHECK(admin_store_save(&before,0)==ESP_OK);
    setup();CHECK(admin_auth_recover(code,"9999","9999",code)==ADMIN_OK);
    admin_settings_t after;CHECK(admin_store_load(&after)==ESP_OK);
    CHECK(after.duration_seconds==120 && after.rotation_degrees==180 && after.rotation_override && after.generation==1);
}
static void test_one_session_and_pin_confirmation(void) {
    setup();CHECK(admin_auth_login("0012",session)==ADMIN_OK);char old[65];strcpy(old,session);
    CHECK(admin_auth_login("0012",session)==ADMIN_OK);CHECK(!admin_auth_session_valid(old,false));
    CHECK(admin_auth_session_valid(session,false));
    CHECK(admin_auth_reissue_recovery(session,"9999",code)==ADMIN_DENIED);
    CHECK(admin_auth_change_pin(session,"9999","9998")==ADMIN_INVALID);
    CHECK(admin_auth_login("0012",session)==ADMIN_OK);
}
/* Exec a fresh process: real module statics disappear, fake durable NVS remains. */
typedef struct { unsigned char data[3][256]; size_t size[3]; unsigned rng; char old_session[65]; } reboot_snapshot_t;
static void restart_check(const char *mode) {
    reboot_snapshot_t snapshot; memcpy(snapshot.data,blobs,sizeof(blobs));
    memcpy(snapshot.size,lengths,sizeof(lengths));snapshot.rng=entropy;
    memcpy(snapshot.old_session,session,sizeof(session));
    int fds[2];CHECK(pipe(fds)==0);
    CHECK(write(fds[1],&snapshot,sizeof(snapshot))==(ssize_t)sizeof(snapshot));close(fds[1]);
    fflush(NULL);pid_t child=fork();CHECK(child>=0);
    if(!child){char fd[16];snprintf(fd,sizeof(fd),"%d",fds[0]);execl("/proc/self/exe","test_auth",mode,fd,(char*)NULL);_exit(2);}
    close(fds[0]);int status;waitpid(child,&status,0);CHECK(WIFEXITED(status)&&WEXITSTATUS(status)==0);
}
static void after_restart(const char *mode,int fd) {
    reboot_snapshot_t snapshot;CHECK(read(fd,&snapshot,sizeof(snapshot))==(ssize_t)sizeof(snapshot));close(fd);
    memcpy(blobs,snapshot.data,sizeof(blobs));memcpy(lengths,snapshot.size,sizeof(lengths));entropy=snapshot.rng;
    CHECK(!admin_auth_session_valid(snapshot.old_session,false));
    if(!strcmp(mode,"missing_auth")) {
        bool configured=false;CHECK(admin_auth_is_configured(&configured)==ADMIN_STORAGE_ERROR);
        CHECK(admin_auth_setup("9999","9999",code)==ADMIN_STORAGE_ERROR);
    } else if(!strcmp(mode,"missing_marker")) {
        CHECK(admin_auth_login("0012",session)==ADMIN_OK);CHECK(lengths[2]>0);
    } else if(!strcmp(mode,"locked")) {
        now=0;CHECK(admin_auth_login("0012",session)==ADMIN_LOCKED);
        now=59;CHECK(admin_auth_login("0012",session)==ADMIN_LOCKED);
        now=60;CHECK(admin_auth_login("0012",session)==ADMIN_OK);
    } else if(!strcmp(mode,"partial")) {
        CHECK(admin_auth_login("8888",session)==ADMIN_DENIED);
        CHECK(admin_auth_login("8888",session)==ADMIN_DENIED);
        CHECK(admin_auth_login("0012",session)==ADMIN_LOCKED);
    } else CHECK(admin_auth_login("0012",session)==ADMIN_OK);
}
static void test_lockout_survives_reboot(void) {setup();for(int i=0;i<5;i++)CHECK(admin_auth_login("8888",session)==ADMIN_DENIED);now=20;restart_check("locked");}
static void test_partial_failures_survive_reboot(void) {setup();for(int i=0;i<3;i++)CHECK(admin_auth_login("8888",session)==ADMIN_DENIED);restart_check("partial");}
static void test_sessions_do_not_survive_reboot(void) {setup();CHECK(admin_auth_login("0012",session)==ADMIN_OK);restart_check("session");}
static void test_missing_auth_never_reopens_setup(void) {setup();lengths[0]=0;restart_check("missing_auth");}
static void test_valid_auth_repairs_missing_marker(void) {setup();lengths[2]=0;restart_check("missing_marker");}
static void test_malformed_attempts_share_lockout(void) {
    setup();for(int i=0;i<5;i++)CHECK(admin_auth_login("invalid",session)==ADMIN_DENIED);
    CHECK(admin_auth_recover(code,"9999","9999",code)==ADMIN_LOCKED);
    CHECK(admin_auth_login("",session)==ADMIN_LOCKED);
}
/* Each subprocess interrupts an actual storage operation; commit may have
 * already persisted. Output is suppressed and no old session may authorize. */
static void mutation_fault(int op,int step) {
    if(op) { setup();CHECK(admin_auth_login("0012",session)==ADMIN_OK); }
    char output[65]="sentinel",old_code[33],old_session[65];
    memcpy(old_code,code,sizeof(code));memcpy(old_session,session,sizeof(session));
    operations=0;fail_at=step;
    admin_result_t result=ADMIN_OK;
    switch(op) {
        case 0: result=admin_auth_setup("0012","0012",output);break;
        case 1: result=admin_auth_login("0012",output);break;
        case 2: result=admin_auth_recover(old_code,"9999","9999",output);break;
        case 3: result=admin_auth_change_pin(old_session,"9999","9999");break;
        case 4: result=admin_auth_reissue_recovery(old_session,"0012",output);break;
    }
    CHECK(result==ADMIN_STORAGE_ERROR);if(op!=3)CHECK(output[0]==0);
    CHECK(!admin_auth_session_valid(old_session,false));
    /* Reconciliation failure must not turn an uncertain write into setup. */
    operations=0;fail_at=1;bool configured=false;
    CHECK(admin_auth_is_configured(&configured)==ADMIN_STORAGE_ERROR);
    fail_at=0;
    if(!op && step>=4 && step<=6) {
        CHECK(admin_auth_is_configured(&configured)==ADMIN_STORAGE_ERROR);
        CHECK(admin_auth_setup("0012","0012",output)==ADMIN_STORAGE_ERROR);
        return;
    }
    CHECK(admin_auth_is_configured(&configured)==ADMIN_OK);
    if(op) {
        CHECK(configured);
        const char *expected=(op==2 || op==3) && step>1?"9999":"0012";
        CHECK(admin_auth_login(expected,output)==ADMIN_OK);
    }
}
static void test_every_mutation_commit_and_readback(void) {
    for(int op=0;op<5;op++)for(int step=1;step<=(op?3:8);step++) {
        fflush(NULL);pid_t child=fork();CHECK(child>=0);if(!child){mutation_fault(op,step);_exit(0);}
        int status;waitpid(child,&status,0);CHECK(WIFEXITED(status)&&WEXITSTATUS(status)==0);
    }
}
static void run(void(*fn)(void),const char*name){fflush(NULL);pid_t p=fork();CHECK(p>=0);if(!p){fn();exit(0);}int status;waitpid(p,&status,0);CHECK(WIFEXITED(status)&&WEXITSTATUS(status)==0);puts(name);}
int main(int argc,char **argv){
if(argc==3){after_restart(argv[1],atoi(argv[2]));return 0;}
#define RUN(fn) run(fn,#fn)
RUN(test_pin_leading_zero_and_exact_ascii_length);RUN(test_setup_race_has_one_winner);RUN(test_recovery_is_single_use);RUN(test_lost_recovery_response_new_pin_works);RUN(test_lockout_60_seconds);RUN(test_session_idle_900_seconds);RUN(test_change_and_reissue);RUN(test_corrupt_auth_not_setup);RUN(test_storage_failure);RUN(test_recovery_preserves_settings);RUN(test_one_session_and_pin_confirmation);RUN(test_lockout_survives_reboot);RUN(test_partial_failures_survive_reboot);RUN(test_sessions_do_not_survive_reboot);RUN(test_malformed_attempts_share_lockout);RUN(test_missing_auth_never_reopens_setup);RUN(test_valid_auth_repairs_missing_marker);RUN(test_every_mutation_commit_and_readback);puts("admin auth: PASS");}
