#include "unity.h"

#include "system_diag.h"

void test_system_diag_defaults_to_clear_state(void)
{
    system_diag_snapshot_t snapshot;

    system_diag_init();
    TEST_ASSERT_EQUAL(ESP_OK, system_diag_get_snapshot(&snapshot));
    TEST_ASSERT_EQUAL(SYSTEM_DIAG_STAGE_BOOT, snapshot.stage);
    TEST_ASSERT_EQUAL(0, snapshot.wifi_retries);
    TEST_ASSERT_EQUAL(0, snapshot.auth_failures);
    TEST_ASSERT_EQUAL_STRING("", snapshot.last_error);
}
