#pragma once

/*
 * Scripted Host for the fake adapter tests.  It speaks the stream binding with
 * the core codecs only (no sockets, no clock), keeps what the node sent, and
 * lets a test inject any frame, including broken ones.
 */

#include <stddef.h>
#include <stdint.h>

#include <borrowlink/binding.h>
#include <borrowlink/message.h>

#define FH_MAX_FRAMES 64u
#define FH_PAYLOAD_MAX 512u
#define FH_OUT_MAX 8192u

typedef struct {
    uint8_t channel;
    uint16_t seq;
    uint8_t opcode;
    size_t payload_size;
    uint8_t payload[FH_PAYLOAD_MAX];
} fh_frame;

typedef struct {
    bl_binding_rx rx;
    uint8_t rx_buf[FH_PAYLOAD_MAX + 3u];
    fh_frame frames[FH_MAX_FRAMES]; /* every frame the node sent, in order */
    size_t frame_count;
    uint16_t tx_seq;                /* next seq the host will use */
    uint8_t out[FH_OUT_MAX];        /* bytes waiting to go to the node */
    size_t out_len;
    uint8_t auto_accept;            /* reply to HELLO with ACCEPT */
    uint16_t accept_max_rx;
    uint64_t node_id;
    bl_hello hello;                 /* last HELLO seen */
    int protocol_errors;            /* things the node did that the host rejects */
} fake_host;

void fh_init(fake_host *h, uint16_t accept_max_rx);

/* The node's bytes arrive (any fragmentation). */
void fh_receive(fake_host *h, const uint8_t *bytes, size_t n);

/* Queue one frame with an explicit seq (the host's own counter is not used). */
void fh_queue_frame(fake_host *h, uint8_t channel, uint16_t seq,
                    uint8_t opcode, const uint8_t *payload, size_t n);
/* Queue a frame with the next seq and advance the counter. */
void fh_send(fake_host *h, uint8_t channel, uint8_t opcode,
             const uint8_t *payload, size_t n);
/* Queue a logical message split into frames of at most `chunk` bytes. */
void fh_send_message(fake_host *h, const uint8_t *payload, size_t n,
                     size_t chunk);
/* Queue raw bytes (to inject broken records). */
void fh_queue_raw(fake_host *h, const uint8_t *bytes, size_t n);

/* Reassemble the node's DATA frames into logical messages: returns the
 * number of complete messages and copies the index'th one. */
size_t fh_messages(const fake_host *h, size_t index, uint8_t *out,
                   size_t out_size, size_t *out_len);

/* Take up to `max` bytes of queued output. */
size_t fh_take(fake_host *h, uint8_t *dst, size_t max);
