#include "admin_store_dial.h"
#include "test_backend.h"
static void test_defaults_and_generation(void) {
    admin_settings_t s={0}; CHECK(admin_store_load(&s)==ESP_OK);
    CHECK(s.duration_seconds==600 && !s.rotation_override && s.generation==0);
    s.duration_seconds=120;s.rotation_degrees=270;s.rotation_override=true;
    CHECK(admin_store_save(&s,0)==ESP_OK);
    CHECK(admin_store_save(&s,0)==ADMIN_ERR_CONFLICT);
    CHECK(admin_store_load(&s)==ESP_OK && s.duration_seconds==120 && s.generation==1);
    s.duration_seconds=61;CHECK(admin_store_save(&s,1)==ESP_ERR_INVALID_ARG);
}
static void test_storage_failure_preserves_confirmed_state(void) {
    for(int step=1;step<=4;step++) {
        admin_settings_t s;CHECK(admin_store_load(&s)==ESP_OK);
        s.duration_seconds=180;s.rotation_degrees=90;s.rotation_override=true;
        operations=0;fail_at=step;
        CHECK(admin_store_save(&s,s.generation)!=ESP_OK);
        fail_at=0;CHECK(admin_store_load(&s)==ESP_OK);
        CHECK(s.duration_seconds==120 || s.duration_seconds==180);
    }
    admin_settings_t sentinel={.duration_seconds=42};operations=0;fail_at=1;
    CHECK(admin_store_load(&sentinel)!=ESP_OK && sentinel.duration_seconds==42);fail_at=0;
}
int main(void){test_defaults_and_generation();test_storage_failure_preserves_confirmed_state();puts("admin store: PASS");}
