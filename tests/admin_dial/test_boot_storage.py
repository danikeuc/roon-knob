"""Hardware entry point is not host-linkable; guard its destructive recovery path."""
from pathlib import Path
boot = Path('idf_app/main/main_idf.c').read_text().split('void app_main(void)', 1)[1]
assert 'nvs_flash_erase(' not in boot, 'NVS boot failure must preserve admin credentials'
assert 'ESP_ERROR_CHECK(err)' in boot, 'NVS initialization must fail closed'
print('boot storage failure policy: PASS')
