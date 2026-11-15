#pragma once

/*
 * BorrowLink stream binding (proposal for draft-3, docs/draft-3-proposal.md).
 *
 * Carries BorrowLink frames over a reliable byte stream such as TCP.  Each
 * frame is preceded by a 3-byte record header:
 *
 *   channel  1 byte   bl_channel (0 Control, 1 Reliable, 2 Realtime)
 *   length   2 bytes  big-endian, size of the frame that follows
 *            (includes the 3-byte frame header)
 *
 * Both halves are pure byte transformations.  The socket belongs to the
 * adapter: it hands received bytes to bl_binding_rx_feed() and sends the
 * bytes produced by bl_binding_encode().
 */

#include <borrowlink/borrowlink.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BORROWLINK_BINDING_HEADER_SIZE 3u
#define BORROWLINK_BINDING_MAX_FRAME 0xffffu
/* length is 16 bits, so a frame holds at most 65535 - 3 payload bytes. */
#define BORROWLINK_BINDING_MAX_RX_PAYLOAD 65532u

/* Writes record header + frame.  `frame` may overlap `output` (memmove). */
bl_result bl_binding_encode(uint8_t *output, size_t output_size,
                            size_t *written, uint8_t channel,
                            const uint8_t *frame, size_t frame_size);

/* Writes only the 3-byte record header (for adapters that gather-write). */
bl_result bl_binding_header_encode(uint8_t *output, size_t output_size,
                                   uint8_t channel, size_t frame_size);

typedef struct {
    uint8_t *buf;        /* caller-owned frame assembly buffer */
    size_t buf_size;
    size_t max_frame;    /* largest frame accepted: my max_rx_payload + 3 */
    size_t frame_size;   /* length announced by the current record */
    size_t have;         /* bytes of the current record collected */
    uint8_t head[BORROWLINK_BINDING_HEADER_SIZE];
    uint8_t head_have;
    uint8_t channel;
    uint8_t failed;      /* sticky; re-init after a protocol violation */
} bl_binding_rx;

typedef struct {
    uint8_t ready;       /* 1: a whole record is available */
    uint8_t channel;
    const uint8_t *frame;/* points into rx->buf, valid until the next feed */
    size_t frame_size;
} bl_binding_record;

/* max_frame must be 3..65535 and not exceed buf_size. */
bl_result bl_binding_rx_init(bl_binding_rx *rx, uint8_t *buf, size_t buf_size,
                             size_t max_frame);

/* Consumes bytes up to the end of the next record.  `*consumed` tells how
 * many of `input` were used; call again with the rest.  At most one record
 * is reported per call (record->ready).
 *
 * BL_ERROR_LIMIT_EXCEEDED  announced length above max_frame
 * BL_ERROR_MALFORMED       length below 3, or unknown channel
 * The receiver is then dead (BL_ERROR_STATE) until re-initialised; the
 * session should RESET with bl_binding_reset_reason(). */
bl_result bl_binding_rx_feed(bl_binding_rx *rx, const uint8_t *input,
                             size_t input_size, size_t *consumed,
                             bl_binding_record *record);

/* RESET reason to send for a binding error. */
uint8_t bl_binding_reset_reason(bl_result error);

#ifdef __cplusplus
}
#endif
