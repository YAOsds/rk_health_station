#include <stdint.h>
#include <stddef.h>

#include "unity.h"

#include "tcp_client.h"

void test_frame_header_uses_big_endian_length(void)
{
    uint8_t header[4] = {0};

    tcp_client_encode_frame_header(0x00000123, header);
    TEST_ASSERT_EQUAL_HEX8(0x00, header[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, header[1]);
    TEST_ASSERT_EQUAL_HEX8(0x01, header[2]);
    TEST_ASSERT_EQUAL_HEX8(0x23, header[3]);
}

void test_frame_header_rejects_oversized_payload(void)
{
    uint8_t header[4] = {0};

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, tcp_client_encode_frame_checked(SIZE_MAX, header));
}
