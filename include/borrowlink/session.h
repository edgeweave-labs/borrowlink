#pragma once

/*
 * BorrowLink session reducer (PROTOCOL.md sections 7, 8 and 14).
 *
 *   bl_result bl_session_step(bl_session *state, const bl_event *event,
 *                             bl_action *action);
 *
 * The reducer is deterministic and does no I/O.  The caller (an adapter)
 * translates transport callbacks into bl_event values, executes the returned
 * bl_action, and reports the outcome with BL_EV_ACTION_DONE or
 * BL_EV_ACTION_FAILED.  Rules:
 *
 *   - One event produces at most one action.
 *   - After an action other than BL_ACT_NONE the caller must report
 *     ACTION_DONE/ACTION_FAILED before it feeds any event except
 *     BL_EV_LINK_LOST or BL_EV_LINK_CLOSED.  Anything else returns BL_ERROR_BUSY and changes
 *     nothing.  The reducer may answer ACTION_DONE with a follow-up action
 *     (for example BL_ACT_SET_LINK_IDLE once both directions are closed).
 *   - Byte views inside an action (data) are valid until the next step.
 *   - An event that is illegal in the current phase returns BL_ERROR_STATE and
 *     changes nothing.  A peer's protocol violation is not a caller error: it
 *     is answered with BL_OK and a RESET frame action.
 *   - State, tx buffer, time and limits all belong to the caller.
 *
 * P2 scope: initiator role, RELIABLE delivery.  The responder role and
 * REALTIME delivery are reserved: bl_session_init() refuses them.
 */

#include <borrowlink/borrowlink.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BL_ROLE_INITIATOR = 0,
    BL_ROLE_RESPONDER = 1 /* reserved */
} bl_role;

typedef enum {
    BL_PHASE_IDLE = 0,   /* no session; also the state after a session ends */
    BL_PHASE_HELLO_SENT, /* initiator waiting for ACCEPT */
    BL_PHASE_ACTIVE      /* HELLO/ACCEPT done, DATA flows */
} bl_phase;

typedef struct {
    uint8_t role;             /* bl_role */
    uint8_t profile;          /* BORROWLINK_PROFILE_* (not PRESENCE) */
    uint8_t delivery;         /* requested; P2 supports RELIABLE only */
    uint8_t keep_connected;   /* beacon KEEP_CONNECTED: go to link idle when done */
    uint64_t node_id;         /* my non-zero node_id */
    uint64_t expected_peer_node_id; /* 0 = accept any; else ACCEPT must match (5.2) */
    uint16_t max_rx_payload;  /* largest DATA payload I accept (>= 14) */
    uint32_t max_message_size;      /* bytes per logical message; 0 = unlimited */
    uint32_t handshake_timeout_ms;  /* HELLO_SENT timeout; 0 = none */
    uint8_t *tx_buf;          /* caller-owned frame scratch, >= 3 + 15 bytes */
    size_t tx_buf_size;
} bl_session_config;

typedef struct {
    bl_session_config cfg;
    uint8_t phase;            /* bl_phase */
    uint8_t outstanding;      /* an action awaits ACTION_DONE/FAILED */
    uint8_t pending_kind;     /* follow-up action to emit on ACTION_DONE */
    uint8_t pending_reason;
    uint8_t pending_error;
    uint8_t delivery;         /* selected delivery */
    uint8_t tx_ended;         /* I sent DATA_END_STREAM */
    uint8_t rx_ended;         /* peer sent DATA_END_STREAM */
    uint8_t tx_msg_open;      /* I sent DATA without end_message */
    uint8_t rx_msg_open;
    uint8_t last_reset_reason;/* 0 if the last session did not end with RESET */
    uint8_t last_error;       /* bl_error_class of the last session end */
    uint16_t xid;
    uint16_t tx_seq;
    uint16_t rx_seq;
    uint16_t peer_max_rx;
    uint16_t tx_chunk_max;    /* min(peer_max_rx, tx_buf_size - 3) */
    uint32_t hello_ms;
    uint32_t rx_msg_bytes;
    uint64_t peer_node_id;
} bl_session;

typedef enum {
    BL_EV_NONE = 0,
    BL_EV_START,         /* xid, now_ms: control channel is ready, send HELLO */
    BL_EV_RX_FRAME,      /* channel, data/size: one whole frame (with header) */
    BL_EV_TX_DATA,       /* data/size, end_message, end_stream: app has bytes */
    BL_EV_LOCAL_RESET,   /* reason: abort the session */
    BL_EV_ACTION_DONE,
    BL_EV_ACTION_FAILED,
    BL_EV_LINK_LOST,     /* the transport failed or was reset */
    BL_EV_TICK,          /* now_ms */
    BL_EV_LINK_CLOSED    /* the peer closed the transport in an orderly way;
                          * only meaningful for byte-stream bindings, where
                          * it ends the peer's direction implicitly.  If I
                          * had already sent DATA_END_STREAM the session
                          * completes normally, otherwise it is a transport
                          * failure. */
} bl_event_type;

typedef struct {
    uint8_t type;        /* bl_event_type */
    uint8_t channel;     /* RX_FRAME */
    uint8_t end_message; /* TX_DATA */
    uint8_t end_stream;  /* TX_DATA */
    uint8_t reason;      /* LOCAL_RESET: BORROWLINK_RESET_* */
    uint16_t xid;        /* START: non-zero operation id */
    uint32_t now_ms;     /* START, TICK: caller's monotonic clock */
    const uint8_t *data; /* RX_FRAME: frame bytes; TX_DATA: payload */
    size_t size;
} bl_event;

typedef enum {
    BL_ACT_NONE = 0,
    BL_ACT_SEND_FRAME,     /* send data[0..size) on channel (a whole frame) */
    BL_ACT_DELIVER_DATA,   /* hand payload to the profile layer */
    BL_ACT_SESSION_OPEN,   /* ACCEPT received; peer_* fields are valid */
    BL_ACT_STATUS,         /* STATUS frame: data = code + detail */
    BL_ACT_SESSION_RESET,  /* session over without the normal close */
    BL_ACT_SET_LINK_IDLE,  /* both directions closed, KEEP_CONNECTED */
    BL_ACT_DISCONNECT      /* both directions closed */
} bl_action_type;

typedef struct {
    uint8_t type;          /* bl_action_type */
    uint8_t channel;       /* SEND_FRAME: bl_channel */
    uint8_t opcode;        /* SEND_FRAME: opcode of the frame */
    uint8_t end_message;   /* DELIVER_DATA */
    uint8_t end_stream;    /* DELIVER_DATA */
    uint8_t msg_truncated; /* DELIVER_DATA: end_stream while a message is open */
    uint8_t reset_reason;  /* SEND_FRAME RESET / SESSION_RESET; 0 = none */
    uint8_t local;         /* SESSION_RESET: 1 = our decision, 0 = peer's RESET */
    uint8_t error;         /* bl_error_class */
    uint8_t delivery;      /* SESSION_OPEN */
    uint16_t peer_max_rx;  /* SESSION_OPEN */
    uint16_t xid;          /* SESSION_OPEN */
    size_t consumed;       /* SEND_FRAME from TX_DATA: payload bytes taken */
    const uint8_t *data;
    size_t size;
    uint64_t peer_node_id; /* SESSION_OPEN */
} bl_action;

/* Validates cfg, copies it into state and leaves the session in IDLE. */
bl_result bl_session_init(bl_session *state, const bl_session_config *cfg);

bl_result bl_session_step(bl_session *state,
                          const bl_event *event,
                          bl_action *action);

/* Maps a RESET reason received from the peer to a core error class. */
bl_error_class bl_reset_error_class(uint8_t reason);

#ifdef __cplusplus
}
#endif
