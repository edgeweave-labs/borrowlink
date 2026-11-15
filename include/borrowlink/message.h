#pragma once

/*
 * BorrowLink Message broker profile (PROTOCOL.md section 11).
 *
 * Every command is one logical message.  PUBLISH and DELIVER carry an
 * arbitrary payload after a fixed header, so this layer never asks the caller
 * to assemble a whole message:
 *
 *   send:    bl_msg_header_encode() gives the fixed bytes; the caller pushes
 *            them and then the payload chunks through the session
 *            (BL_EV_TX_DATA), setting end_message on the last chunk.
 *   receive: bl_msg_parser is fed the DATA payloads in arrival order and
 *            reports a header, payload chunks and the end of the message.
 */

#include <borrowlink/borrowlink.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BORROWLINK_MSG_SUBSCRIBE UINT8_C(0x01)
#define BORROWLINK_MSG_UNSUBSCRIBE UINT8_C(0x02)
#define BORROWLINK_MSG_PUBLISH UINT8_C(0x03)
#define BORROWLINK_MSG_DELIVER UINT8_C(0x04)
#define BORROWLINK_MSG_ACK UINT8_C(0x05)

#define BORROWLINK_MSG_SUBSCRIBE_SIZE 16u
#define BORROWLINK_MSG_UNSUBSCRIBE_SIZE 13u
#define BORROWLINK_MSG_PUBLISH_HEADER_SIZE 5u
#define BORROWLINK_MSG_DELIVER_HEADER_SIZE 13u
#define BORROWLINK_MSG_ACK_SIZE 4u
#define BORROWLINK_MSG_HEADER_MAX 16u

#define BORROWLINK_QUEUE_ONLINE_ONLY UINT8_C(0x00)
#define BORROWLINK_QUEUE_LATEST UINT8_C(0x01)
#define BORROWLINK_QUEUE_FIFO_BOUNDED UINT8_C(0x02)

#define BORROWLINK_ACK_ACCEPTED UINT8_C(0x00)
#define BORROWLINK_ACK_REJECTED UINT8_C(0x01)
#define BORROWLINK_ACK_NO_CAPACITY UINT8_C(0x02)
#define BORROWLINK_ACK_UNKNOWN_SOURCE UINT8_C(0x03)
#define BORROWLINK_ACK_NOT_AUTHORIZED UINT8_C(0x04)

/* One struct for all five commands; which fields apply depends on `cmd`:
 *
 *   SUBSCRIBE    id = request_id, source_node_id, topic, lease_seconds,
 *                queue_policy
 *   UNSUBSCRIBE  id = request_id, source_node_id, topic
 *   PUBLISH      id = message_id, topic            (payload follows)
 *   DELIVER      id = message_id, source_node_id, topic   (payload follows)
 *   ACK          id = request_id (message_id when acknowledging a PUBLISH or
 *                DELIVER, see docs/draft-3-proposal.md), status
 */
typedef struct {
    uint8_t cmd;
    uint8_t queue_policy;
    uint8_t status;
    uint16_t id;
    uint16_t topic;
    uint16_t lease_seconds;
    uint64_t source_node_id;
} bl_msg_header;

/* Size of the fixed bytes for `cmd`, or 0 for an unknown command. */
size_t bl_msg_header_size(uint8_t cmd);

/* Writes the fixed bytes (for PUBLISH/DELIVER: everything except the
 * payload).  Rejects unknown commands, queue policies and ACK statuses. */
bl_result bl_msg_header_encode(uint8_t *output, size_t output_size,
                               size_t *written, const bl_msg_header *header);

/* Decodes exactly bl_msg_header_size(input[0]) bytes (input_size must be at
 * least that; extra payload bytes after a PUBLISH/DELIVER header are ignored
 * by this call).  Fixed-size commands must match the size exactly. */
bl_result bl_msg_header_decode(bl_msg_header *header, const uint8_t *input,
                               size_t input_size);

/* ---- streaming receive ------------------------------------------------- */

typedef enum {
    BL_MSG_EV_NONE = 0,  /* nothing more until the next feed */
    BL_MSG_EV_HEADER,    /* header complete; event.header is valid */
    BL_MSG_EV_PAYLOAD,   /* event.data/size: a slice of the payload */
    BL_MSG_EV_END        /* the logical message ended and was well formed */
} bl_msg_event_kind;

typedef struct {
    uint8_t kind; /* bl_msg_event_kind */
    bl_msg_header header;
    const uint8_t *data;
    size_t size;
} bl_msg_event;

typedef struct {
    uint8_t buf[BORROWLINK_MSG_HEADER_MAX];
    uint8_t have;      /* header bytes collected so far */
    uint8_t need;      /* header size once the command byte is known, else 0 */
    uint8_t header_done;
    uint8_t failed;    /* sticky; call bl_msg_parser_init() again */
} bl_msg_parser;

void bl_msg_parser_init(bl_msg_parser *parser);

/* Feed bytes of the current message.  `end_message` says the message ends
 * after these bytes.  Call in a loop, advancing by *consumed, until the event
 * kind is BL_MSG_EV_NONE:
 *
 *     do {
 *         r = bl_msg_parser_feed(&p, d + off, n - off, end, &used, &ev);
 *         off += used;  ...handle ev...
 *     } while (r == BL_OK && ev.kind != BL_MSG_EV_NONE &&
 *              ev.kind != BL_MSG_EV_END);
 *
 * Stop after BL_MSG_EV_END: feeding zero bytes with end_message again would
 * read as a new, empty (and therefore malformed) message.
 *
 * BL_ERROR_MALFORMED: unknown command or value, wrong length, empty message,
 * or a message that ends before its header is complete.  The parser is then
 * dead until re-initialized (the session should RESET).  Payload views point
 * into the caller's input. */
bl_result bl_msg_parser_feed(bl_msg_parser *parser, const uint8_t *input,
                             size_t input_size, int end_message,
                             size_t *consumed, bl_msg_event *event);

/* ---- subscriber-side bookkeeping --------------------------------------- */

/* Tracks ids that were sent and not yet acknowledged.  PROTOCOL.md 11: a
 * request_id is not reused before its ACK; message_id increases modulo
 * 65536.  This implementation uses one counter for both (an ACK carries only
 * one id field), skips 0 and skips ids still pending.  Storage is the caller's. */
typedef struct {
    uint16_t *pending;
    size_t capacity;
    size_t count;
    uint16_t next;
} bl_msg_client;

bl_result bl_msg_client_init(bl_msg_client *client, uint16_t *pending,
                             size_t capacity);
/* Reserves the next id and marks it pending.  BL_ERROR_LIMIT_EXCEEDED when
 * `capacity` ids are already pending. */
bl_result bl_msg_client_alloc_id(bl_msg_client *client, uint16_t *id);
/* An ACK arrived for `id`.  BL_ERROR_UNKNOWN_ID if it was not pending. */
bl_result bl_msg_client_release_id(bl_msg_client *client, uint16_t id);
int bl_msg_client_is_pending(const bl_msg_client *client, uint16_t id);

#ifdef __cplusplus
}
#endif
