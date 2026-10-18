#include "admin_store_dial.h"
#include "admin_backend_dial.h"
#include <limits.h>
#include <string.h>
/* Fixed-width on-flash representation; no compiler bool/padding in the blob. */
typedef struct { uint32_t version, duration, rotation, override, generation; } settings_record_t;
static bool valid(const settings_record_t *r) {
    return r->version == 1 && r->duration >= 60 && r->duration <= 600 &&
        r->duration % 60 == 0 && r->rotation <= 270 && r->rotation % 90 == 0 && r->override <= 1;
}
static esp_err_t read_record(settings_record_t *r) {
    esp_err_t err = admin_backend_read("settings", r, sizeof(*r));
    if (err == ESP_ERR_NOT_FOUND) {
        /* No override preserves the existing charging/non-charging rotations.
         * Do not open or migrate the legacy connectivity/preferences namespace. */
        *r = (settings_record_t){1, 600, 0, 0, 0};
        return ESP_OK;
    }
    return err == ESP_OK && !valid(r) ? ESP_FAIL : err;
}
esp_err_t admin_store_load(admin_settings_t *out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    if (!admin_backend_lock()) return ESP_FAIL;
    settings_record_t r;
    esp_err_t err = read_record(&r);
    if (err == ESP_OK) *out = (admin_settings_t){r.duration, r.rotation, r.override != 0, r.generation};
    admin_backend_unlock();
    return err;
}
esp_err_t admin_store_save(const admin_settings_t *candidate, uint32_t expected_generation) {
    if (!candidate) return ESP_ERR_INVALID_ARG;
    settings_record_t next = {1, candidate->duration_seconds, candidate->rotation_degrees,
        candidate->rotation_override, expected_generation + 1};
    if (!valid(&next) || expected_generation == UINT32_MAX) return ESP_ERR_INVALID_ARG;
    if (!admin_backend_lock()) return ESP_FAIL;
    settings_record_t current, verify;
    /* Every mutation rereads NVS: a failed commit/readback cannot leave a stale
     * RAM value eligible for the next compare-and-swap. */
    esp_err_t err = read_record(&current);
    if (err == ESP_OK && current.generation != expected_generation) err = ADMIN_ERR_CONFLICT;
    if (err == ESP_OK) err = admin_backend_write("settings", &next, sizeof(next));
    if (err == ESP_OK) err = admin_backend_commit();
    if (err == ESP_OK) err = admin_backend_read("settings", &verify, sizeof(verify));
    if (err == ESP_OK && memcmp(&next, &verify, sizeof(next))) err = ESP_FAIL;
    admin_backend_unlock();
    return err;
}
