#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <borrowlink/borrowlink.h>

static void test_beacon_round_trip(void)
{
    static const uint8_t expected[BORROWLINK_BEACON_SIZE] = {
        0x01, 0x01, 0x12, 0x34, 0x00, 0x00
    };
    uint8_t encoded[BORROWLINK_BEACON_SIZE] = {0};
    bl_beacon input;
    bl_beacon output;

    input.version = BORROWLINK_PROTOCOL_VERSION;
    input.profile = BORROWLINK_PROFILE_HTTP;
    input.xid = UINT16_C(0x1234);
    input.attempt = UINT8_C(0);
    input.flags = UINT8_C(0);

    assert(bl_beacon_encode(encoded, sizeof(encoded), &input) == BL_OK);
    assert(memcmp(encoded, expected, sizeof(expected)) == 0);
    memset(&output, 0, sizeof(output));
    assert(bl_beacon_decode(&output, encoded, sizeof(encoded)) == BL_OK);
    assert(output.version == input.version);
    assert(output.profile == input.profile);
    assert(output.xid == input.xid);
    assert(output.attempt == input.attempt);
    assert(output.flags == input.flags);
}

static void test_beacon_rejects_invalid_input(void)
{
    static const uint8_t unknown_version[BORROWLINK_BEACON_SIZE] = {
        0x02, 0x01, 0x12, 0x34, 0x00, 0x00
    };
    static const uint8_t unknown_profile[BORROWLINK_BEACON_SIZE] = {
        0x01, 0x7f, 0x12, 0x34, 0x00, 0x00
    };
    static const uint8_t future_flags[BORROWLINK_BEACON_SIZE] = {
        0x01, 0x01, 0x12, 0x34, 0x00, 0x80
    };
    uint8_t encoded[BORROWLINK_BEACON_SIZE];
    bl_beacon beacon;
    bl_beacon output;

    memset(&output, 0xa5, sizeof(output));
    assert(bl_beacon_decode(&output, unknown_version,
                            sizeof(unknown_version)) ==
           BL_ERROR_UNSUPPORTED_VERSION);
    assert(bl_beacon_decode(&output, unknown_profile,
                            sizeof(unknown_profile)) ==
           BL_ERROR_UNSUPPORTED_PROFILE);
    assert(bl_beacon_decode(&output, unknown_profile,
                            sizeof(unknown_profile) - 1u) ==
           BL_ERROR_MALFORMED);
    assert(output.version == UINT8_C(0xa5));

    beacon.version = BORROWLINK_PROTOCOL_VERSION;
    beacon.profile = BORROWLINK_PROFILE_PRESENCE;
    beacon.xid = UINT16_C(1);
    beacon.attempt = UINT8_C(0);
    beacon.flags = UINT8_C(0);
    assert(bl_beacon_encode(encoded, sizeof(encoded), &beacon) ==
           BL_ERROR_MALFORMED);

    beacon.profile = BORROWLINK_PROFILE_HTTP;
    beacon.xid = UINT16_C(0);
    assert(bl_beacon_encode(encoded, sizeof(encoded), &beacon) ==
           BL_ERROR_MALFORMED);

    beacon.xid = UINT16_C(1);
    beacon.flags = UINT8_C(0x80);
    assert(bl_beacon_encode(encoded, sizeof(encoded), &beacon) ==
           BL_ERROR_MALFORMED);

    assert(bl_beacon_decode(&output, future_flags,
                            sizeof(future_flags)) == BL_OK);
    assert(output.flags == UINT8_C(0x80));
}

int main(void)
{
    test_beacon_round_trip();
    test_beacon_rejects_invalid_input();
    return 0;
}
