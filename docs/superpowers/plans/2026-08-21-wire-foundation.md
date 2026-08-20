# BorrowLink Draft-2 Wire Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (- [ ]) syntax for tracking.

**Goal:** Replace the draft-1 constants with a host-tested, allocation-free C11 codec for draft-2 Beacon Data, common frames, HELLO, and ACCEPT.

**Architecture:** Keep one public header and one wire implementation. Every encoder writes into caller-owned memory, every decoder returns value structs or borrowed views, and all multi-byte fields use big-endian order. This plan stops at byte-level protocol correctness; session policy and platform I/O remain separate.

**Tech Stack:** C11, C++17 public-header compatibility, POSIX shell, ESP-IDF CMake

## Global Constraints

- The normative source is docs/PROTOCOL.md draft-2, sections 4, 7, and 8.
- Public include path remains <borrowlink/borrowlink.h>.
- Core code contains no heap allocation, thread, I/O, clock, random, ESP-IDF, FreeRTOS, socket, HTTP, or BLE headers.
- Every output buffer and decoded view is caller-owned.
- All multi-byte wire integers are network byte order (big-endian).
- Beacon Data is exactly 6 bytes; frame header is exactly 3 bytes; HELLO payload is exactly 15 bytes; ACCEPT payload is exactly 14 bytes.
- Decoder inputs are trust boundaries: validate lengths before reads and leave output objects unchanged on error.
- Encoder inputs are programmer boundaries: reject null pointers, reserved values, and invalid field combinations.
- C11 and C++17 checks compile with -Wall -Wextra -Werror.
- This plan does not implement the session reducer, GATT characteristics, ESP-IDF adapter, HTTP/message payload codecs, subscription storage, power policy, or Mesh.
- docs/superpowers/plans/2026-08-20-protocol-foundation.md remains historical and is superseded by this plan for draft-2 wire values.

## File Map

- Modify include/borrowlink/borrowlink.h: public constants, POD types, borrowed views, and codec declarations.
- Create src/wire.c: endian helpers, validators, and byte encoding/decoding.
- Modify CMakeLists.txt: register src/wire.c.
- Modify tests/host/test_protocol_constants.c: assert draft-2 values.
- Create tests/host/test_wire.c: golden vectors and malformed-input checks.
- Modify tests/host/run.sh: run tests as C11 and C++17 callers.
- Modify README.md: state the implemented scope.

---

### Task 1: Publish the draft-2 numeric schema

**Files:**

- Modify: include/borrowlink/borrowlink.h
- Modify: tests/host/test_protocol_constants.c

**Interfaces:**

- Consumes: draft-2 values from protocol sections 4, 7, and 8.
- Produces: BORROWLINK_* constants and bl_result values used by later tasks.

- [ ] **Step 1: Replace the constants test with a failing draft-2 check**

Replace tests/host/test_protocol_constants.c:

~~~c
#include <assert.h>
#include <stdint.h>

#include <borrowlink/borrowlink.h>

int main(void)
{
    assert(BORROWLINK_PROTOCOL_VERSION == UINT8_C(0x01));
    assert(BORROWLINK_BEACON_SIZE == 6u);
    assert(BORROWLINK_PROFILE_PRESENCE == UINT8_C(0x00));
    assert(BORROWLINK_PROFILE_HTTP == UINT8_C(0x01));
    assert(BORROWLINK_PROFILE_STREAM == UINT8_C(0x02));
    assert(BORROWLINK_PROFILE_MESSAGE == UINT8_C(0x03));
    assert(BORROWLINK_BEACON_FLAG_REALTIME == UINT8_C(0x01));
    assert(BORROWLINK_BEACON_FLAG_ANNOUNCEMENT == UINT8_C(0x02));
    assert(BORROWLINK_BEACON_FLAG_KEEP_CONNECTED == UINT8_C(0x04));
    assert(BORROWLINK_BEACON_FLAG_KNOWN_MASK == UINT8_C(0x07));

    assert(BORROWLINK_FRAME_HEADER_SIZE == 3u);
    assert(BORROWLINK_OPCODE_DATA == UINT8_C(0x00));
    assert(BORROWLINK_OPCODE_DATA_END_MESSAGE == UINT8_C(0x01));
    assert(BORROWLINK_OPCODE_DATA_END_STREAM == UINT8_C(0x02));
    assert(BORROWLINK_OPCODE_DATA_END_MESSAGE_STREAM == UINT8_C(0x03));
    assert(BORROWLINK_OPCODE_HELLO == UINT8_C(0x10));
    assert(BORROWLINK_OPCODE_ACCEPT == UINT8_C(0x11));
    assert(BORROWLINK_OPCODE_STATUS == UINT8_C(0x12));
    assert(BORROWLINK_OPCODE_RESET == UINT8_C(0x13));

    assert(BORROWLINK_DELIVERY_RELIABLE == UINT8_C(0x00));
    assert(BORROWLINK_DELIVERY_REALTIME == UINT8_C(0x01));
    assert(BORROWLINK_HELLO_PAYLOAD_SIZE == 15u);
    assert(BORROWLINK_ACCEPT_PAYLOAD_SIZE == 14u);

    assert(BORROWLINK_RESET_NO_NETWORK == UINT8_C(0x10));
    assert(BORROWLINK_RESET_UPSTREAM_FAILED == UINT8_C(0x11));
    assert(BORROWLINK_RESET_UPSTREAM_TIMEOUT == UINT8_C(0x12));
    assert(BORROWLINK_RESET_REJECTED == UINT8_C(0x13));
    assert(BORROWLINK_RESET_UNSUPPORTED_PROFILE == UINT8_C(0x14));
    assert(BORROWLINK_RESET_PROTOCOL_ERROR == UINT8_C(0x15));
    assert(BORROWLINK_RESET_BUSY == UINT8_C(0x16));
    assert(BORROWLINK_RESET_SECURITY_ERROR == UINT8_C(0x17));
    assert(BORROWLINK_RESET_LIMIT_EXCEEDED == UINT8_C(0x18));
    assert(BORROWLINK_RESET_UNSUPPORTED_OPCODE == UINT8_C(0x19));

    assert(BL_OK == 0);
    assert(BL_ERROR_ARGUMENT != BL_OK);
    return 0;
}
~~~

- [ ] **Step 2: Verify the check fails**

Run:

~~~sh
tests/host/run.sh
~~~

Expected: compilation fails on BORROWLINK_BEACON_SIZE or another draft-2 name.

- [ ] **Step 3: Replace the public header**

Replace include/borrowlink/borrowlink.h:

~~~c
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BORROWLINK_PROTOCOL_VERSION UINT8_C(0x01)
#define BORROWLINK_BEACON_SIZE 6u

#define BORROWLINK_PROFILE_PRESENCE UINT8_C(0x00)
#define BORROWLINK_PROFILE_HTTP UINT8_C(0x01)
#define BORROWLINK_PROFILE_STREAM UINT8_C(0x02)
#define BORROWLINK_PROFILE_MESSAGE UINT8_C(0x03)

#define BORROWLINK_BEACON_FLAG_REALTIME UINT8_C(0x01)
#define BORROWLINK_BEACON_FLAG_ANNOUNCEMENT UINT8_C(0x02)
#define BORROWLINK_BEACON_FLAG_KEEP_CONNECTED UINT8_C(0x04)
#define BORROWLINK_BEACON_FLAG_KNOWN_MASK UINT8_C(0x07)

#define BORROWLINK_FRAME_HEADER_SIZE 3u
#define BORROWLINK_OPCODE_DATA UINT8_C(0x00)
#define BORROWLINK_OPCODE_DATA_END_MESSAGE UINT8_C(0x01)
#define BORROWLINK_OPCODE_DATA_END_STREAM UINT8_C(0x02)
#define BORROWLINK_OPCODE_DATA_END_MESSAGE_STREAM UINT8_C(0x03)
#define BORROWLINK_OPCODE_HELLO UINT8_C(0x10)
#define BORROWLINK_OPCODE_ACCEPT UINT8_C(0x11)
#define BORROWLINK_OPCODE_STATUS UINT8_C(0x12)
#define BORROWLINK_OPCODE_RESET UINT8_C(0x13)

#define BORROWLINK_DELIVERY_RELIABLE UINT8_C(0x00)
#define BORROWLINK_DELIVERY_REALTIME UINT8_C(0x01)
#define BORROWLINK_HELLO_PAYLOAD_SIZE 15u
#define BORROWLINK_ACCEPT_PAYLOAD_SIZE 14u

#define BORROWLINK_RESET_NO_NETWORK UINT8_C(0x10)
#define BORROWLINK_RESET_UPSTREAM_FAILED UINT8_C(0x11)
#define BORROWLINK_RESET_UPSTREAM_TIMEOUT UINT8_C(0x12)
#define BORROWLINK_RESET_REJECTED UINT8_C(0x13)
#define BORROWLINK_RESET_UNSUPPORTED_PROFILE UINT8_C(0x14)
#define BORROWLINK_RESET_PROTOCOL_ERROR UINT8_C(0x15)
#define BORROWLINK_RESET_BUSY UINT8_C(0x16)
#define BORROWLINK_RESET_SECURITY_ERROR UINT8_C(0x17)
#define BORROWLINK_RESET_LIMIT_EXCEEDED UINT8_C(0x18)
#define BORROWLINK_RESET_UNSUPPORTED_OPCODE UINT8_C(0x19)

typedef enum {
    BL_OK = 0,
    BL_ERROR_ARGUMENT,
    BL_ERROR_BUFFER_TOO_SMALL,
    BL_ERROR_MALFORMED,
    BL_ERROR_UNSUPPORTED_VERSION,
    BL_ERROR_UNSUPPORTED_PROFILE,
    BL_ERROR_UNSUPPORTED_DELIVERY,
    BL_ERROR_UNSUPPORTED_OPCODE
} bl_result;

#ifdef __cplusplus
}
#endif
~~~

- [ ] **Step 4: Verify the check passes**

Run: tests/host/run.sh

Expected: C11 and C++17 checks exit 0 with no output.

- [ ] **Step 5: Commit**

~~~sh
git add include/borrowlink/borrowlink.h tests/host/test_protocol_constants.c
git commit -m "feat: align BorrowLink constants with draft-2"
~~~

---

### Task 2: Add the Beacon Data codec

**Files:**

- Modify: include/borrowlink/borrowlink.h
- Create: src/wire.c
- Modify: CMakeLists.txt
- Create: tests/host/test_wire.c
- Modify: tests/host/run.sh

**Interfaces:**

- Consumes: bl_result and Beacon constants from Task 1.
- Produces bl_beacon, bl_beacon_encode, and bl_beacon_decode.

- [ ] **Step 1: Add the failing golden-vector test**

Create tests/host/test_wire.c:

~~~c
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
~~~

- [ ] **Step 2: Declare the interface and verify link failure**

Insert before the closing C++ guard in include/borrowlink/borrowlink.h:

~~~c
typedef struct {
    uint8_t version;
    uint8_t profile;
    uint16_t xid;
    uint8_t attempt;
    uint8_t flags;
} bl_beacon;

bl_result bl_beacon_encode(uint8_t *output,
                           size_t output_size,
                           const bl_beacon *beacon);
bl_result bl_beacon_decode(bl_beacon *beacon,
                           const uint8_t *input,
                           size_t input_size);
~~~

Run:

~~~sh
cc -std=c11 -Wall -Wextra -Werror -Iinclude \
    tests/host/test_wire.c -o /tmp/borrowlink-test-wire
~~~

Expected: link fails on bl_beacon_encode and bl_beacon_decode.

- [ ] **Step 3: Implement the Beacon codec**

Create src/wire.c:

~~~c
#include <borrowlink/borrowlink.h>

static uint16_t bl_read_u16_be(const uint8_t *input)
{
    return (uint16_t)(((uint16_t)input[0] << 8) | input[1]);
}

static void bl_write_u16_be(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)(value >> 8);
    output[1] = (uint8_t)value;
}

static int bl_profile_is_known(uint8_t profile)
{
    return profile <= BORROWLINK_PROFILE_MESSAGE;
}

static bl_result bl_beacon_validate(const bl_beacon *beacon, int encoding)
{
    if (beacon->version != BORROWLINK_PROTOCOL_VERSION) {
        return BL_ERROR_UNSUPPORTED_VERSION;
    }
    if (!bl_profile_is_known(beacon->profile)) {
        return BL_ERROR_UNSUPPORTED_PROFILE;
    }
    if (beacon->profile == BORROWLINK_PROFILE_PRESENCE) {
        if (beacon->xid != 0u || beacon->attempt != 0u ||
            (beacon->flags & BORROWLINK_BEACON_FLAG_KNOWN_MASK) != 0u ||
            (encoding && beacon->flags != 0u)) {
            return BL_ERROR_MALFORMED;
        }
    } else if (beacon->xid == 0u) {
        return BL_ERROR_MALFORMED;
    }
    if (encoding &&
        (beacon->flags & (uint8_t)~BORROWLINK_BEACON_FLAG_KNOWN_MASK) != 0u) {
        return BL_ERROR_MALFORMED;
    }
    return BL_OK;
}

bl_result bl_beacon_encode(uint8_t *output,
                           size_t output_size,
                           const bl_beacon *beacon)
{
    bl_result result;

    if (output == NULL || beacon == NULL) {
        return BL_ERROR_ARGUMENT;
    }
    if (output_size < BORROWLINK_BEACON_SIZE) {
        return BL_ERROR_BUFFER_TOO_SMALL;
    }
    result = bl_beacon_validate(beacon, 1);
    if (result != BL_OK) {
        return result;
    }
    output[0] = beacon->version;
    output[1] = beacon->profile;
    bl_write_u16_be(output + 2, beacon->xid);
    output[4] = beacon->attempt;
    output[5] = beacon->flags;
    return BL_OK;
}

bl_result bl_beacon_decode(bl_beacon *beacon,
                           const uint8_t *input,
                           size_t input_size)
{
    bl_beacon decoded;
    bl_result result;

    if (beacon == NULL || input == NULL) {
        return BL_ERROR_ARGUMENT;
    }
    if (input_size != BORROWLINK_BEACON_SIZE) {
        return BL_ERROR_MALFORMED;
    }
    decoded.version = input[0];
    decoded.profile = input[1];
    decoded.xid = bl_read_u16_be(input + 2);
    decoded.attempt = input[4];
    decoded.flags = input[5];
    result = bl_beacon_validate(&decoded, 0);
    if (result != BL_OK) {
        return result;
    }
    *beacon = decoded;
    return BL_OK;
}
~~~

- [ ] **Step 4: Register the source and replace the host runner**

Replace CMakeLists.txt:

~~~cmake
idf_component_register(
    SRCS "src/wire.c"
    INCLUDE_DIRS "include"
)
~~~

Replace tests/host/run.sh:

~~~sh
#!/bin/sh
set -eu

borrowlink_test_dir="$(mktemp -d /tmp/borrowlink-test.XXXXXX)"
trap 'rm -rf "$borrowlink_test_dir"' EXIT HUP INT TERM

c_flags="-std=c11 -Wall -Wextra -Werror -Iinclude"
cxx_flags="-x c++ -std=c++17 -Wall -Wextra -Werror -Iinclude"

cc $c_flags -c src/wire.c -o "$borrowlink_test_dir/wire.o"

for test_source in \
    tests/host/test_protocol_constants.c \
    tests/host/test_wire.c
do
    test_name="$(basename "$test_source" .c)"
    cc $c_flags "$test_source" "$borrowlink_test_dir/wire.o" \
        -o "$borrowlink_test_dir/$test_name-c"
    "$borrowlink_test_dir/$test_name-c"

    c++ $cxx_flags -c "$test_source" \
        -o "$borrowlink_test_dir/$test_name-cpp.o"
    c++ "$borrowlink_test_dir/$test_name-cpp.o" \
        "$borrowlink_test_dir/wire.o" \
        -o "$borrowlink_test_dir/$test_name-cpp"
    "$borrowlink_test_dir/$test_name-cpp"
done
~~~

Run: tests/host/run.sh

Expected: all C11 and C++17 caller checks exit 0.

- [ ] **Step 5: Commit**

~~~sh
git add CMakeLists.txt include/borrowlink/borrowlink.h src/wire.c \
    tests/host/test_wire.c tests/host/run.sh
git commit -m "feat: add BorrowLink beacon codec"
~~~

---

### Task 3: Add the common frame codec

**Files:**

- Modify: include/borrowlink/borrowlink.h
- Modify: src/wire.c
- Modify: tests/host/test_wire.c

**Interfaces:**

- Consumes: endian helpers and frame constants.
- Produces bl_frame_view, bl_frame_encode, and bl_frame_decode.

- [ ] **Step 1: Add failing frame checks**

Insert before main in tests/host/test_wire.c:

~~~c
static void test_frame_round_trip(void)
{
    static const uint8_t payload[] = {0xaa, 0xbb};
    static const uint8_t expected[] = {
        0x12, 0x34, 0x01, 0xaa, 0xbb
    };
    uint8_t encoded[sizeof(expected)] = {0};
    size_t written = 0u;
    bl_frame_view frame;

    assert(bl_frame_encode(encoded, sizeof(encoded), &written,
                           UINT16_C(0x1234),
                           BORROWLINK_OPCODE_DATA_END_MESSAGE,
                           payload, sizeof(payload)) == BL_OK);
    assert(written == sizeof(expected));
    assert(memcmp(encoded, expected, sizeof(expected)) == 0);
    assert(bl_frame_decode(&frame, encoded, sizeof(encoded)) == BL_OK);
    assert(frame.seq == UINT16_C(0x1234));
    assert(frame.opcode == BORROWLINK_OPCODE_DATA_END_MESSAGE);
    assert(frame.payload == encoded + BORROWLINK_FRAME_HEADER_SIZE);
    assert(frame.payload_size == sizeof(payload));
    assert(memcmp(frame.payload, payload, sizeof(payload)) == 0);
}

static void test_frame_rejects_malformed_input(void)
{
    static const uint8_t short_frame[] = {0x00, 0x00};
    static const uint8_t unknown_opcode[] = {0x00, 0x00, 0x7f};
    uint8_t output[BORROWLINK_FRAME_HEADER_SIZE] = {0};
    size_t written = 99u;
    bl_frame_view frame;

    memset(&frame, 0xa5, sizeof(frame));
    assert(bl_frame_decode(&frame, short_frame, sizeof(short_frame)) ==
           BL_ERROR_MALFORMED);
    assert(frame.opcode == UINT8_C(0xa5));
    assert(bl_frame_decode(&frame, unknown_opcode,
                           sizeof(unknown_opcode)) ==
           BL_ERROR_UNSUPPORTED_OPCODE);
    assert(bl_frame_encode(output, sizeof(output), &written, 0u, 0x7fu,
                           NULL, 0u) ==
           BL_ERROR_UNSUPPORTED_OPCODE);
    assert(written == 99u);
    assert(bl_frame_encode(output, sizeof(output) - 1u, &written, 0u,
                           BORROWLINK_OPCODE_DATA, NULL, 0u) ==
           BL_ERROR_BUFFER_TOO_SMALL);
}
~~~

Add to main:

~~~c
    test_frame_round_trip();
    test_frame_rejects_malformed_input();
~~~

- [ ] **Step 2: Declare the interface and verify link failure**

Insert after the Beacon declarations:

~~~c
typedef struct {
    uint16_t seq;
    uint8_t opcode;
    const uint8_t *payload;
    size_t payload_size;
} bl_frame_view;

bl_result bl_frame_encode(uint8_t *output,
                          size_t output_size,
                          size_t *written,
                          uint16_t seq,
                          uint8_t opcode,
                          const uint8_t *payload,
                          size_t payload_size);
bl_result bl_frame_decode(bl_frame_view *frame,
                          const uint8_t *input,
                          size_t input_size);
~~~

Run: tests/host/run.sh

Expected: link fails on bl_frame_encode and bl_frame_decode.

- [ ] **Step 3: Implement the frame codec**

Add at the top of src/wire.c:

~~~c
#include <string.h>
~~~

Append to src/wire.c:

~~~c
static int bl_opcode_is_known(uint8_t opcode)
{
    switch (opcode) {
    case BORROWLINK_OPCODE_DATA:
    case BORROWLINK_OPCODE_DATA_END_MESSAGE:
    case BORROWLINK_OPCODE_DATA_END_STREAM:
    case BORROWLINK_OPCODE_DATA_END_MESSAGE_STREAM:
    case BORROWLINK_OPCODE_HELLO:
    case BORROWLINK_OPCODE_ACCEPT:
    case BORROWLINK_OPCODE_STATUS:
    case BORROWLINK_OPCODE_RESET:
        return 1;
    default:
        return 0;
    }
}

bl_result bl_frame_encode(uint8_t *output,
                          size_t output_size,
                          size_t *written,
                          uint16_t seq,
                          uint8_t opcode,
                          const uint8_t *payload,
                          size_t payload_size)
{
    size_t frame_size;

    if (output == NULL || written == NULL ||
        (payload == NULL && payload_size != 0u)) {
        return BL_ERROR_ARGUMENT;
    }
    if (!bl_opcode_is_known(opcode)) {
        return BL_ERROR_UNSUPPORTED_OPCODE;
    }
    if (payload_size > SIZE_MAX - BORROWLINK_FRAME_HEADER_SIZE) {
        return BL_ERROR_BUFFER_TOO_SMALL;
    }
    frame_size = BORROWLINK_FRAME_HEADER_SIZE + payload_size;
    if (output_size < frame_size) {
        return BL_ERROR_BUFFER_TOO_SMALL;
    }
    bl_write_u16_be(output, seq);
    output[2] = opcode;
    if (payload_size != 0u) {
        memcpy(output + BORROWLINK_FRAME_HEADER_SIZE,
               payload, payload_size);
    }
    *written = frame_size;
    return BL_OK;
}

bl_result bl_frame_decode(bl_frame_view *frame,
                          const uint8_t *input,
                          size_t input_size)
{
    bl_frame_view decoded;

    if (frame == NULL || input == NULL) {
        return BL_ERROR_ARGUMENT;
    }
    if (input_size < BORROWLINK_FRAME_HEADER_SIZE) {
        return BL_ERROR_MALFORMED;
    }
    if (!bl_opcode_is_known(input[2])) {
        return BL_ERROR_UNSUPPORTED_OPCODE;
    }
    decoded.seq = bl_read_u16_be(input);
    decoded.opcode = input[2];
    decoded.payload = input + BORROWLINK_FRAME_HEADER_SIZE;
    decoded.payload_size = input_size - BORROWLINK_FRAME_HEADER_SIZE;
    *frame = decoded;
    return BL_OK;
}
~~~

- [ ] **Step 4: Run and commit**

Run:

~~~sh
tests/host/run.sh
git add include/borrowlink/borrowlink.h src/wire.c tests/host/test_wire.c
git commit -m "feat: add BorrowLink frame codec"
~~~

Expected: tests pass before the commit; the commit contains only frame work.

---

### Task 4: Add HELLO and ACCEPT payload codecs

**Files:**

- Modify: include/borrowlink/borrowlink.h
- Modify: src/wire.c
- Modify: tests/host/test_wire.c
- Modify: README.md

**Interfaces:**

- Consumes: handshake sizes, profiles, deliveries, and endian helpers.
- Produces bl_hello, bl_accept, and their four encode/decode functions.

- [ ] **Step 1: Add failing handshake vectors**

Insert before main in tests/host/test_wire.c:

~~~c
static void test_hello_round_trip(void)
{
    static const uint8_t expected[BORROWLINK_HELLO_PAYLOAD_SIZE] = {
        0x01, 0x01, 0x00, 0x12, 0x34,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x00, 0x11
    };
    uint8_t encoded[BORROWLINK_HELLO_PAYLOAD_SIZE] = {0};
    bl_hello input;
    bl_hello output;

    input.version = BORROWLINK_PROTOCOL_VERSION;
    input.profile = BORROWLINK_PROFILE_HTTP;
    input.delivery = BORROWLINK_DELIVERY_RELIABLE;
    input.xid = UINT16_C(0x1234);
    input.node_id = UINT64_C(0x0102030405060708);
    input.max_rx_payload = UINT16_C(0x0011);
    assert(bl_hello_encode(encoded, sizeof(encoded), &input) == BL_OK);
    assert(memcmp(encoded, expected, sizeof(expected)) == 0);
    assert(bl_hello_decode(&output, encoded, sizeof(encoded)) == BL_OK);
    assert(output.version == input.version);
    assert(output.profile == input.profile);
    assert(output.delivery == input.delivery);
    assert(output.xid == input.xid);
    assert(output.node_id == input.node_id);
    assert(output.max_rx_payload == input.max_rx_payload);
}

static void test_accept_round_trip(void)
{
    static const uint8_t expected[BORROWLINK_ACCEPT_PAYLOAD_SIZE] = {
        0x01, 0x00, 0x12, 0x34,
        0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
        0x00, 0x11
    };
    uint8_t encoded[BORROWLINK_ACCEPT_PAYLOAD_SIZE] = {0};
    bl_accept input;
    bl_accept output;

    input.version = BORROWLINK_PROTOCOL_VERSION;
    input.delivery = BORROWLINK_DELIVERY_RELIABLE;
    input.xid = UINT16_C(0x1234);
    input.node_id = UINT64_C(0x1112131415161718);
    input.max_rx_payload = UINT16_C(0x0011);
    assert(bl_accept_encode(encoded, sizeof(encoded), &input) == BL_OK);
    assert(memcmp(encoded, expected, sizeof(expected)) == 0);
    assert(bl_accept_decode(&output, encoded, sizeof(encoded)) == BL_OK);
    assert(output.version == input.version);
    assert(output.delivery == input.delivery);
    assert(output.xid == input.xid);
    assert(output.node_id == input.node_id);
    assert(output.max_rx_payload == input.max_rx_payload);
}

static void test_handshake_rejects_invalid_fields(void)
{
    uint8_t hello_bytes[BORROWLINK_HELLO_PAYLOAD_SIZE] = {0};
    uint8_t accept_bytes[BORROWLINK_ACCEPT_PAYLOAD_SIZE] = {0};
    bl_hello hello;
    bl_accept accept;

    hello.version = BORROWLINK_PROTOCOL_VERSION;
    hello.profile = BORROWLINK_PROFILE_HTTP;
    hello.delivery = BORROWLINK_DELIVERY_REALTIME;
    hello.xid = UINT16_C(1);
    hello.node_id = UINT64_C(1);
    hello.max_rx_payload = UINT16_C(17);
    assert(bl_hello_encode(hello_bytes, sizeof(hello_bytes), &hello) ==
           BL_ERROR_MALFORMED);

    hello.delivery = BORROWLINK_DELIVERY_RELIABLE;
    hello.node_id = UINT64_C(0);
    assert(bl_hello_encode(hello_bytes, sizeof(hello_bytes), &hello) ==
           BL_ERROR_MALFORMED);

    memset(&hello, 0xa5, sizeof(hello));
    assert(bl_hello_decode(&hello, hello_bytes,
                           sizeof(hello_bytes) - 1u) ==
           BL_ERROR_MALFORMED);
    assert(hello.version == UINT8_C(0xa5));

    accept.version = BORROWLINK_PROTOCOL_VERSION;
    accept.delivery = UINT8_C(0x7f);
    accept.xid = UINT16_C(1);
    accept.node_id = UINT64_C(1);
    accept.max_rx_payload = UINT16_C(17);
    assert(bl_accept_encode(accept_bytes, sizeof(accept_bytes), &accept) ==
           BL_ERROR_UNSUPPORTED_DELIVERY);
}
~~~

Add to main:

~~~c
    test_hello_round_trip();
    test_accept_round_trip();
    test_handshake_rejects_invalid_fields();
~~~

- [ ] **Step 2: Declare handshake interfaces**

Insert after the frame declarations:

~~~c
typedef struct {
    uint8_t version;
    uint8_t profile;
    uint8_t delivery;
    uint16_t xid;
    uint64_t node_id;
    uint16_t max_rx_payload;
} bl_hello;

typedef struct {
    uint8_t version;
    uint8_t delivery;
    uint16_t xid;
    uint64_t node_id;
    uint16_t max_rx_payload;
} bl_accept;

bl_result bl_hello_encode(uint8_t *output, size_t output_size,
                          const bl_hello *hello);
bl_result bl_hello_decode(bl_hello *hello, const uint8_t *input,
                          size_t input_size);
bl_result bl_accept_encode(uint8_t *output, size_t output_size,
                           const bl_accept *accept);
bl_result bl_accept_decode(bl_accept *accept, const uint8_t *input,
                           size_t input_size);
~~~

Run: tests/host/run.sh

Expected: link fails on the four new functions.

- [ ] **Step 3: Add 64-bit endian and common validation**

Insert after the 16-bit helpers in src/wire.c:

~~~c
static uint64_t bl_read_u64_be(const uint8_t *input)
{
    uint64_t value = 0u;
    size_t index;

    for (index = 0u; index < 8u; ++index) {
        value = (value << 8) | input[index];
    }
    return value;
}

static void bl_write_u64_be(uint8_t *output, uint64_t value)
{
    size_t index;

    for (index = 0u; index < 8u; ++index) {
        output[7u - index] = (uint8_t)value;
        value >>= 8;
    }
}

static bl_result bl_delivery_validate(uint8_t delivery)
{
    if (delivery != BORROWLINK_DELIVERY_RELIABLE &&
        delivery != BORROWLINK_DELIVERY_REALTIME) {
        return BL_ERROR_UNSUPPORTED_DELIVERY;
    }
    return BL_OK;
}

static bl_result bl_handshake_common_validate(uint8_t version,
                                              uint8_t delivery,
                                              uint16_t xid,
                                              uint64_t node_id,
                                              uint16_t max_rx_payload)
{
    bl_result result;

    if (version != BORROWLINK_PROTOCOL_VERSION) {
        return BL_ERROR_UNSUPPORTED_VERSION;
    }
    result = bl_delivery_validate(delivery);
    if (result != BL_OK) {
        return result;
    }
    if (xid == 0u || node_id == 0u || max_rx_payload == 0u) {
        return BL_ERROR_MALFORMED;
    }
    return BL_OK;
}
~~~

- [ ] **Step 4: Implement HELLO**

Append to src/wire.c:

~~~c
static bl_result bl_hello_validate(const bl_hello *hello)
{
    bl_result result;

    result = bl_handshake_common_validate(
        hello->version, hello->delivery, hello->xid,
        hello->node_id, hello->max_rx_payload);
    if (result != BL_OK) {
        return result;
    }
    if (hello->profile == BORROWLINK_PROFILE_PRESENCE ||
        !bl_profile_is_known(hello->profile)) {
        return BL_ERROR_UNSUPPORTED_PROFILE;
    }
    if (hello->profile == BORROWLINK_PROFILE_HTTP &&
        hello->delivery != BORROWLINK_DELIVERY_RELIABLE) {
        return BL_ERROR_MALFORMED;
    }
    return BL_OK;
}

bl_result bl_hello_encode(uint8_t *output, size_t output_size,
                          const bl_hello *hello)
{
    bl_result result;

    if (output == NULL || hello == NULL) {
        return BL_ERROR_ARGUMENT;
    }
    if (output_size < BORROWLINK_HELLO_PAYLOAD_SIZE) {
        return BL_ERROR_BUFFER_TOO_SMALL;
    }
    result = bl_hello_validate(hello);
    if (result != BL_OK) {
        return result;
    }
    output[0] = hello->version;
    output[1] = hello->profile;
    output[2] = hello->delivery;
    bl_write_u16_be(output + 3, hello->xid);
    bl_write_u64_be(output + 5, hello->node_id);
    bl_write_u16_be(output + 13, hello->max_rx_payload);
    return BL_OK;
}

bl_result bl_hello_decode(bl_hello *hello, const uint8_t *input,
                          size_t input_size)
{
    bl_hello decoded;
    bl_result result;

    if (hello == NULL || input == NULL) {
        return BL_ERROR_ARGUMENT;
    }
    if (input_size != BORROWLINK_HELLO_PAYLOAD_SIZE) {
        return BL_ERROR_MALFORMED;
    }
    decoded.version = input[0];
    decoded.profile = input[1];
    decoded.delivery = input[2];
    decoded.xid = bl_read_u16_be(input + 3);
    decoded.node_id = bl_read_u64_be(input + 5);
    decoded.max_rx_payload = bl_read_u16_be(input + 13);
    result = bl_hello_validate(&decoded);
    if (result != BL_OK) {
        return result;
    }
    *hello = decoded;
    return BL_OK;
}
~~~

- [ ] **Step 5: Implement ACCEPT**

Append to src/wire.c:

~~~c
static bl_result bl_accept_validate(const bl_accept *accept)
{
    return bl_handshake_common_validate(
        accept->version, accept->delivery, accept->xid,
        accept->node_id, accept->max_rx_payload);
}

bl_result bl_accept_encode(uint8_t *output, size_t output_size,
                           const bl_accept *accept)
{
    bl_result result;

    if (output == NULL || accept == NULL) {
        return BL_ERROR_ARGUMENT;
    }
    if (output_size < BORROWLINK_ACCEPT_PAYLOAD_SIZE) {
        return BL_ERROR_BUFFER_TOO_SMALL;
    }
    result = bl_accept_validate(accept);
    if (result != BL_OK) {
        return result;
    }
    output[0] = accept->version;
    output[1] = accept->delivery;
    bl_write_u16_be(output + 2, accept->xid);
    bl_write_u64_be(output + 4, accept->node_id);
    bl_write_u16_be(output + 12, accept->max_rx_payload);
    return BL_OK;
}

bl_result bl_accept_decode(bl_accept *accept, const uint8_t *input,
                           size_t input_size)
{
    bl_accept decoded;
    bl_result result;

    if (accept == NULL || input == NULL) {
        return BL_ERROR_ARGUMENT;
    }
    if (input_size != BORROWLINK_ACCEPT_PAYLOAD_SIZE) {
        return BL_ERROR_MALFORMED;
    }
    decoded.version = input[0];
    decoded.delivery = input[1];
    decoded.xid = bl_read_u16_be(input + 2);
    decoded.node_id = bl_read_u64_be(input + 4);
    decoded.max_rx_payload = bl_read_u16_be(input + 12);
    result = bl_accept_validate(&decoded);
    if (result != BL_OK) {
        return result;
    }
    *accept = decoded;
    return BL_OK;
}
~~~

- [ ] **Step 6: Run host, sanitizer, and formatting checks**

~~~sh
tests/host/run.sh
clang -std=c11 -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Iinclude src/wire.c tests/host/test_wire.c \
    -o /tmp/borrowlink-wire-sanitized
/tmp/borrowlink-wire-sanitized
git diff --check
~~~

Expected: all commands exit 0 and print no errors.

- [ ] **Step 7: Verify the ESP-IDF component**

From /Users/birdyo/Projects/esp32/esp32-s3-epaper-1.54:

~~~sh
source ../.tools/esp-idf/export.sh
borrowlink_build_dir="$(mktemp -d /tmp/borrowlink-draft2.XXXXXX)"
idf.py -B "$borrowlink_build_dir" reconfigure
idf.py -B "$borrowlink_build_dir" build
~~~

Expected: BorrowLink compiles src/wire.c and the firmware build completes.

- [ ] **Step 8: Update README**

Replace README.md Development section with:

~~~~markdown
## Development

BorrowLink currently implements the draft-2 byte codec for Beacon Data,
frames, HELLO, and ACCEPT. Session policy and platform adapters are not yet
implemented.

Include the public API with:

~~~c
#include <borrowlink/borrowlink.h>
~~~

Run all host checks with:

~~~sh
tests/host/run.sh
~~~
~~~~

- [ ] **Step 9: Commit**

~~~sh
git add README.md include/borrowlink/borrowlink.h src/wire.c \
    tests/host/test_wire.c
git commit -m "feat: add BorrowLink handshake codec"
~~~

Expected: worktree clean after commit.

---

## Completion Gate

- Public draft-1 names are absent from the public header.
- Beacon golden vector is exactly 01 01 12 34 00 00.
- HELLO and ACCEPT vectors match docs/PROTOCOL.md.
- Invalid lengths never modify decoded outputs.
- Encoders do not write before validation and capacity checks pass.
- Reserved Beacon bits are rejected by encoders and preserved by decoders.
- Unknown version, profile, delivery, and opcode return distinct errors.
- Frame payload decode is zero-copy through bl_frame_view.
- C11, C++17, ASan, UBSan, ESP-IDF, and git diff checks pass.
- Worktree is clean.

## Deferred Follow-up Plans

1. Reliable single-session Event → State → Action reducer.
2. ESP-IDF GATT, bonding, Beacon, NVS, and power adapter.
3. HTTP streaming profile.
4. Message broker profile and bounded mailbox.
5. Realtime delivery.
6. Opaque stream only when a real WebSocket or tunnel consumer exists.
7. Native Mesh adapter only when protocol section 13 conditions are met.
