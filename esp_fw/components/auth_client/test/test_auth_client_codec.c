#include <string.h>

#include "unity.h"

#include "auth_client.h"

void test_auth_proof_builder_matches_protocol_contract(void)
{
    rk_device_config_t config = {0};
    char proof[65] = {0};

    strcpy(config.device_id, "watch_001");
    strcpy(config.device_secret, "secret123");
    TEST_ASSERT_EQUAL(ESP_OK,
        auth_client_build_proof(&config, "srv_nonce", "cli_nonce", 1713000001, proof, sizeof(proof)));
    TEST_ASSERT_EQUAL(64, strlen(proof));
}
