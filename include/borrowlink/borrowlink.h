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
    BL_ERROR_UNSUPPORTED_OPCODE,
    /* Added for the session, message-profile and stream-binding layers.
     * The values above keep their numbers. */
    BL_ERROR_STATE,            /* event or call not legal in the current state */
    BL_ERROR_BUSY,             /* the previous action has not been reported done */
    BL_ERROR_LIMIT_EXCEEDED,   /* a caller-supplied limit was exceeded */
    BL_ERROR_UNSUPPORTED_ROLE, /* responder role is reserved, not implemented */
    BL_ERROR_UNKNOWN_ID        /* no such outstanding request/message id */
} bl_result;

/* PROTOCOL.md section 6: the three logical channels.  GATT maps them to the
 * Control, Reliable Data and Realtime Data characteristics; the stream binding
 * (docs/draft-3-proposal.md) carries the same numbers in its record header. */
typedef enum {
    BL_CHANNEL_CONTROL = 0,
    BL_CHANNEL_RELIABLE = 1,
    BL_CHANNEL_REALTIME = 2
} bl_channel;

/* PROTOCOL.md section 14: core error classes. */
typedef enum {
    BL_ERR_NONE = 0,
    BL_ERR_CALLER,
    BL_ERR_PROTOCOL,
    BL_ERR_SECURITY,
    BL_ERR_LIMIT,
    BL_ERR_TIMEOUT,
    BL_ERR_TRANSPORT
} bl_error_class;

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

#ifdef __cplusplus
}
#endif
