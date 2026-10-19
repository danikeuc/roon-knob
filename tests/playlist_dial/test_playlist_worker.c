#include "playlist_client_dial.h"
#include "playlist_admin_dial.h"
#include "runtime_fixture.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
void runtime_arm_mailbox_preemption(void);
void runtime_assert_mailbox_preemption(void);
static void pause_ms(unsigned ms){struct timespec t={.tv_nsec=(long)ms*1000000};nanosleep(&t,NULL);}
int main(void){
 assert(playlist_private_save("http://bridge/roon","synthetic-private"));
 playlist_client_connected(true);assert(playlist_client_init());
 runtime_http_case(HTTP_WEATHER_STALLED_OPEN);uint64_t began=runtime_wall_ms();char *out=NULL;size_t length=0;
 assert(playlist_admin_get(false,NULL,&out,&length)==PLAYLIST_ADMIN_TIMEOUT&&runtime_wall_ms()-began<150);
 assert(playlist_admin_get(false,NULL,&out,&length)==PLAYLIST_ADMIN_ADMISSION); /* retained admission */
 runtime_wait_cleanup(1);assert(atomic_load(&fixture_body_writes)==0);
 runtime_http_case(HTTP_WEATHER_NORMAL);assert(playlist_admin_get(false,NULL,&out,&length)==200);free(out);
 runtime_http_case(HTTP_WEATHER_NORMAL);assert(playlist_client_request_favorites());pause_ms(20);playlist_client_result_t result;assert(playlist_client_take(&result)&&result.status==PLAYLIST_CLIENT_FAVORITES);
 runtime_http_case(HTTP_WEATHER_STALLED_OPEN);assert(playlist_client_request_play("opaque",3,7));
 pause_ms(350);assert(atomic_load(&fixture_body_writes)==0);assert(playlist_client_take(&result)&&result.status==PLAYLIST_CLIENT_ERROR);
 runtime_http_case(HTTP_ACTION_EXPIRED_OPEN);assert(playlist_client_request_play("opaque",3,7));pause_ms(20);assert(atomic_load(&fixture_body_writes)==0);assert(playlist_client_take(&result)&&result.status==PLAYLIST_CLIENT_ERROR);
 runtime_http_case(HTTP_WEATHER_NORMAL);assert(playlist_client_request_play("opaque",3,7));pause_ms(20);assert(playlist_client_take(&result)&&result.status==PLAYLIST_CLIENT_ACCEPTED);assert(atomic_load(&fixture_body_writes)==2);
 runtime_http_case(HTTP_WEATHER_NORMAL);assert(playlist_client_request_favorites());pause_ms(20);runtime_arm_mailbox_preemption();assert(playlist_client_take(&result)&&result.status==PLAYLIST_CLIENT_FAVORITES&&result.favorites.revision==3);runtime_assert_mailbox_preemption();pause_ms(20);assert(playlist_client_take(&result)&&result.status==PLAYLIST_CLIENT_FAVORITES&&result.favorites.revision==3);playlist_client_cancel();assert(!playlist_client_take(&result));
 puts("production playlist worker finite wait/owned late jobs/body deadline and forced mailbox preemption PASS");
}
