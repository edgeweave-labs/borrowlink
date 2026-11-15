#pragma once

/*
 * Platform-neutral adapter glue.
 *
 * Joins the stream binding, the session reducer and the Message profile
 * parser, and executes the reducer's actions.  It performs no I/O of its own:
 * bytes leave through ops->send and arrive through bl_driver_feed(), time
 * comes in as arguments.  A Linux socket adapter, an ESP-IDF Wi-Fi adapter and
 * the in-memory fake adapter in tests/ all plug into the same driver.
 *
 * Not part of the core: it lives under adapters/ and may be replaced by a
 * platform's own event loop.  Profile: Message, RELIABLE, stream binding.
 */

#include <borrowlink/binding.h>
#include <borrowlink/message.h>
#include <borrowlink/session.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Transmit `n` bytes of the stream.  Return 0 on success. */
    int (*send)(void *ctx, const uint8_t *bytes, size_t n);
    /* Optional (may be NULL). */
    void (*on_open)(void *ctx, const bl_action *open);
    void (*on_msg)(void *ctx, const bl_msg_event *event);
    void (*on_status)(void *ctx, const uint8_t *data, size_t size);
    /* SESSION_RESET, SET_LINK_IDLE or DISCONNECT; the driver is then idle. */
    void (*on_end)(void *ctx, const bl_action *end);
} bl_driver_ops;

typedef struct {
    bl_session_config session; /* tx_buf/tx_buf_size are filled by the driver */
    uint8_t *tx_buf;           /* frame scratch for the session */
    size_t tx_buf_size;
    uint8_t *wire_buf;         /* record scratch, >= tx_buf_size + 3 */
    size_t wire_buf_size;
    uint8_t *rx_buf;           /* frame assembly, >= max_rx_payload + 3 */
    size_t rx_buf_size;
} bl_driver_config;

typedef struct {
    bl_session session;
    bl_binding_rx rx;
    bl_msg_parser parser;
    bl_driver_ops ops;
    void *ctx;
    uint8_t *wire_buf;
    size_t wire_buf_size;
    uint8_t opened;
    uint8_t finished;      /* on_end has fired for the current session */
    uint8_t end_type;      /* bl_action_type of the closing action */
    uint8_t end_reset_reason;
    uint8_t end_error;
    uint8_t deferred_reset;/* reason to RESET once the current action is done */
} bl_driver;

bl_result bl_driver_init(bl_driver *d, const bl_driver_config *cfg,
                         const bl_driver_ops *ops, void *ctx);

/* The Control channel is ready: send HELLO. */
bl_result bl_driver_start(bl_driver *d, uint16_t xid, uint32_t now_ms);

/* Bytes arrived from the stream (any fragmentation). */
bl_result bl_driver_feed(bl_driver *d, const uint8_t *bytes, size_t n);

/* Streams `size` bytes of the current logical message, splitting into frames
 * as the peer's max_rx_payload requires.  end_message/end_stream apply after
 * the last byte.  The caller never has to assemble a whole message. */
bl_result bl_driver_send(bl_driver *d, const uint8_t *data, size_t size,
                         int end_message, int end_stream);

/* Closes my direction (DATA_END_STREAM). */
bl_result bl_driver_close(bl_driver *d);
bl_result bl_driver_reset(bl_driver *d, uint8_t reason);
/* The transport failed (error, reset). */
bl_result bl_driver_link_lost(bl_driver *d);
/* The peer closed the transport in an orderly way (TCP FIN, recv() == 0). */
bl_result bl_driver_link_closed(bl_driver *d);
bl_result bl_driver_tick(bl_driver *d, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
