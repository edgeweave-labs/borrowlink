#include <borrowlink/session.h>

#include <string.h>

/* Follow-up actions the reducer owes the caller after ACTION_DONE. */
enum {
    BL_PENDING_NONE = 0,
    BL_PENDING_LINK_IDLE,
    BL_PENDING_DISCONNECT,
    BL_PENDING_RESET_LOCAL
};

#define BL_TX_SCRATCH_MIN \
    (BORROWLINK_FRAME_HEADER_SIZE + BORROWLINK_HELLO_PAYLOAD_SIZE)

bl_error_class bl_reset_error_class(uint8_t reason)
{
    switch (reason) {
    case BORROWLINK_RESET_NO_NETWORK:
    case BORROWLINK_RESET_UPSTREAM_FAILED:
    case BORROWLINK_RESET_BUSY:
        return BL_ERR_TRANSPORT;
    case BORROWLINK_RESET_UPSTREAM_TIMEOUT:
        return BL_ERR_TIMEOUT;
    case BORROWLINK_RESET_SECURITY_ERROR:
        return BL_ERR_SECURITY;
    case BORROWLINK_RESET_LIMIT_EXCEEDED:
        return BL_ERR_LIMIT;
    default:
        return BL_ERR_PROTOCOL;
    }
}

bl_result bl_session_init(bl_session *state, const bl_session_config *cfg)
{
    uint8_t scratch[BORROWLINK_HELLO_PAYLOAD_SIZE];
    bl_hello probe;
    bl_result result;

    if (state == NULL || cfg == NULL) {
        return BL_ERROR_ARGUMENT;
    }
    if (cfg->role == BL_ROLE_RESPONDER) {
        return BL_ERROR_UNSUPPORTED_ROLE;
    }
    if (cfg->role != BL_ROLE_INITIATOR || cfg->tx_buf == NULL ||
        cfg->max_rx_payload < BORROWLINK_ACCEPT_PAYLOAD_SIZE) {
        return BL_ERROR_ARGUMENT;
    }
    if (cfg->tx_buf_size < BL_TX_SCRATCH_MIN) {
        return BL_ERROR_BUFFER_TOO_SMALL;
    }
    /* Reuse the HELLO validator for version/profile/delivery/node_id. */
    probe.version = BORROWLINK_PROTOCOL_VERSION;
    probe.profile = cfg->profile;
    probe.delivery = cfg->delivery;
    probe.xid = 1u;
    probe.node_id = cfg->node_id;
    probe.max_rx_payload = cfg->max_rx_payload;
    result = bl_hello_encode(scratch, sizeof(scratch), &probe);
    if (result != BL_OK) {
        return result;
    }
    if (cfg->delivery != BORROWLINK_DELIVERY_RELIABLE) {
        return BL_ERROR_UNSUPPORTED_DELIVERY;
    }
    memset(state, 0, sizeof(*state));
    state->cfg = *cfg;
    state->phase = BL_PHASE_IDLE;
    return BL_OK;
}

/* ------------------------------------------------------------ helpers */

static void bl_clear_session(bl_session *s)
{
    s->delivery = BORROWLINK_DELIVERY_RELIABLE;
    s->tx_ended = 0u;
    s->rx_ended = 0u;
    s->tx_msg_open = 0u;
    s->rx_msg_open = 0u;
    s->xid = 0u;
    s->tx_seq = 0u;
    s->rx_seq = 0u;
    s->peer_max_rx = 0u;
    s->tx_chunk_max = 0u;
    s->hello_ms = 0u;
    s->rx_msg_bytes = 0u;
    s->peer_node_id = 0u;
    s->pending_kind = BL_PENDING_NONE;
}

static void bl_end_session(bl_session *s, uint8_t reset_reason,
                           bl_error_class error)
{
    s->phase = BL_PHASE_IDLE;
    s->last_reset_reason = reset_reason;
    s->last_error = (uint8_t)error;
}

static void bl_set_action(bl_session *s, bl_action *action,
                          bl_action_type type)
{
    action->type = (uint8_t)type;
    s->outstanding = (uint8_t)type;
}

static bl_channel bl_channel_for_opcode(uint8_t opcode)
{
    return opcode >= BORROWLINK_OPCODE_HELLO ? BL_CHANNEL_CONTROL
                                             : BL_CHANNEL_RELIABLE;
}

static int bl_opcode_is_data(uint8_t opcode)
{
    return opcode <= BORROWLINK_OPCODE_DATA_END_MESSAGE_STREAM;
}

/* Encodes a frame into tx_buf (payload already at tx_buf + 3, or copied from
 * `payload`) and fills a SEND_FRAME action. */
static bl_result bl_send_frame(bl_session *s, bl_action *action,
                               uint8_t opcode, const uint8_t *payload,
                               size_t payload_size, int payload_in_place)
{
    size_t written = 0u;
    bl_result result;

    if (!payload_in_place && payload_size != 0u) {
        memcpy(s->cfg.tx_buf + BORROWLINK_FRAME_HEADER_SIZE, payload,
               payload_size);
    }
    result = bl_frame_encode(s->cfg.tx_buf, s->cfg.tx_buf_size, &written,
                             s->tx_seq, opcode,
                             s->cfg.tx_buf + BORROWLINK_FRAME_HEADER_SIZE,
                             payload_size);
    if (result != BL_OK) {
        return result;
    }
    s->tx_seq = (uint16_t)(s->tx_seq + 1u);
    bl_set_action(s, action, BL_ACT_SEND_FRAME);
    action->channel = (uint8_t)bl_channel_for_opcode(opcode);
    action->opcode = opcode;
    action->data = s->cfg.tx_buf;
    action->size = written;
    return BL_OK;
}

/* Our own decision to end the session: send RESET, then report it once the
 * adapter says the frame is out. */
static bl_result bl_reset_session(bl_session *s, bl_action *action,
                                  uint8_t reason)
{
    uint8_t payload;
    bl_result result;
    bl_error_class error = bl_reset_error_class(reason);

    payload = reason;
    result = bl_send_frame(s, action, BORROWLINK_OPCODE_RESET, &payload, 1u, 0);
    if (result != BL_OK) {
        return result;
    }
    action->reset_reason = reason;
    action->error = (uint8_t)error;
    action->local = 1u;
    bl_end_session(s, reason, error);
    s->pending_kind = BL_PENDING_RESET_LOCAL;
    s->pending_reason = reason;
    s->pending_error = (uint8_t)error;
    return BL_OK;
}

static void bl_session_reset_action(bl_session *s, bl_action *action,
                                    uint8_t reason, int local,
                                    bl_error_class error)
{
    bl_set_action(s, action, BL_ACT_SESSION_RESET);
    action->reset_reason = reason;
    action->local = local ? 1u : 0u;
    action->error = (uint8_t)error;
}

/* Both directions closed: the session is done, link policy follows. */
static void bl_complete_session(bl_session *s)
{
    bl_end_session(s, 0u, BL_ERR_NONE);
    s->pending_kind = s->cfg.keep_connected ? BL_PENDING_LINK_IDLE
                                            : BL_PENDING_DISCONNECT;
}

/* ------------------------------------------------------------ events */

static bl_result bl_on_start(bl_session *s, const bl_event *ev,
                             bl_action *action)
{
    bl_hello hello;
    bl_result result;

    if (s->phase != BL_PHASE_IDLE) {
        return BL_ERROR_STATE;
    }
    if (ev->xid == 0u) {
        return BL_ERROR_ARGUMENT;
    }
    bl_clear_session(s);
    s->xid = ev->xid;
    s->hello_ms = ev->now_ms;
    s->last_reset_reason = 0u;
    s->last_error = BL_ERR_NONE;
    hello.version = BORROWLINK_PROTOCOL_VERSION;
    hello.profile = s->cfg.profile;
    hello.delivery = s->cfg.delivery;
    hello.xid = s->xid;
    hello.node_id = s->cfg.node_id;
    hello.max_rx_payload = s->cfg.max_rx_payload;
    result = bl_hello_encode(s->cfg.tx_buf + BORROWLINK_FRAME_HEADER_SIZE,
                             s->cfg.tx_buf_size - BORROWLINK_FRAME_HEADER_SIZE,
                             &hello);
    if (result != BL_OK) {
        return result;
    }
    result = bl_send_frame(s, action, BORROWLINK_OPCODE_HELLO,
                           s->cfg.tx_buf + BORROWLINK_FRAME_HEADER_SIZE,
                           BORROWLINK_HELLO_PAYLOAD_SIZE, 1);
    if (result != BL_OK) {
        return result;
    }
    s->phase = BL_PHASE_HELLO_SENT;
    return BL_OK;
}

static bl_result bl_on_accept(bl_session *s, const bl_frame_view *f,
                              bl_action *action)
{
    bl_accept accept;

    if (bl_accept_decode(&accept, f->payload, f->payload_size) != BL_OK ||
        accept.xid != s->xid || accept.delivery > s->cfg.delivery) {
        /* Wrong xid or a delivery upgrade (PROTOCOL.md 8: no upgrade). */
        return bl_reset_session(s, action, BORROWLINK_RESET_PROTOCOL_ERROR);
    }
    if (s->cfg.expected_peer_node_id != 0u &&
        accept.node_id != s->cfg.expected_peer_node_id) {
        /* PROTOCOL.md 5.2: node_id must match the one bound to the bond. */
        return bl_reset_session(s, action, BORROWLINK_RESET_SECURITY_ERROR);
    }
    s->phase = BL_PHASE_ACTIVE;
    s->delivery = accept.delivery;
    s->peer_node_id = accept.node_id;
    s->peer_max_rx = accept.max_rx_payload;
    s->tx_chunk_max = s->peer_max_rx;
    if ((size_t)s->tx_chunk_max >
        s->cfg.tx_buf_size - BORROWLINK_FRAME_HEADER_SIZE) {
        s->tx_chunk_max = (uint16_t)(s->cfg.tx_buf_size -
                                     BORROWLINK_FRAME_HEADER_SIZE);
    }
    s->rx_seq = 1u;
    bl_set_action(s, action, BL_ACT_SESSION_OPEN);
    action->peer_node_id = s->peer_node_id;
    action->peer_max_rx = s->peer_max_rx;
    action->delivery = s->delivery;
    action->xid = s->xid;
    return BL_OK;
}

static bl_result bl_on_peer_reset(bl_session *s, const bl_frame_view *f,
                                  bl_action *action)
{
    uint8_t reason = f->payload_size > 0u ? f->payload[0]
                                          : BORROWLINK_RESET_PROTOCOL_ERROR;
    bl_error_class error = bl_reset_error_class(reason);

    bl_end_session(s, reason, error);
    bl_session_reset_action(s, action, reason, 0, error);
    return BL_OK;
}

static bl_result bl_on_data(bl_session *s, const bl_frame_view *f,
                            bl_action *action)
{
    int end_message = f->opcode == BORROWLINK_OPCODE_DATA_END_MESSAGE ||
                      f->opcode == BORROWLINK_OPCODE_DATA_END_MESSAGE_STREAM;
    int end_stream = f->opcode == BORROWLINK_OPCODE_DATA_END_STREAM ||
                     f->opcode == BORROWLINK_OPCODE_DATA_END_MESSAGE_STREAM;

    if (s->rx_ended) {
        return bl_reset_session(s, action, BORROWLINK_RESET_PROTOCOL_ERROR);
    }
    if (f->payload_size > UINT32_MAX - s->rx_msg_bytes ||
        (s->cfg.max_message_size != 0u &&
         s->rx_msg_bytes + (uint32_t)f->payload_size >
             s->cfg.max_message_size)) {
        return bl_reset_session(s, action, BORROWLINK_RESET_LIMIT_EXCEEDED);
    }
    s->rx_msg_bytes += (uint32_t)f->payload_size;
    if (end_message) {
        s->rx_msg_bytes = 0u;
        s->rx_msg_open = 0u;
    } else if (f->payload_size != 0u) {
        s->rx_msg_open = 1u;
    }
    bl_set_action(s, action, BL_ACT_DELIVER_DATA);
    action->data = f->payload;
    action->size = f->payload_size;
    action->end_message = end_message ? 1u : 0u;
    action->end_stream = end_stream ? 1u : 0u;
    if (end_stream) {
        s->rx_ended = 1u;
        action->msg_truncated = s->rx_msg_open;
        if (s->tx_ended) {
            bl_complete_session(s);
        }
    }
    return BL_OK;
}

static bl_result bl_on_rx_frame(bl_session *s, const bl_event *ev,
                                bl_action *action)
{
    bl_frame_view f;
    bl_result result;
    uint8_t reason;

    if (ev->data == NULL || ev->channel > BL_CHANNEL_REALTIME) {
        return BL_ERROR_ARGUMENT;
    }
    result = bl_frame_decode(&f, ev->data, ev->size);
    if (s->phase == BL_PHASE_IDLE) {
        /* No session.  A Host-initiated HELLO is answered with REJECTED
         * because the responder role is not implemented; every other stray
         * frame (for example a late frame after a RESET) is ignored. */
        if (result == BL_OK && f.opcode == BORROWLINK_OPCODE_HELLO &&
            ev->channel == BL_CHANNEL_CONTROL) {
            bl_clear_session(s); /* the RESET is frame 0 of this direction */
            return bl_reset_session(s, action, BORROWLINK_RESET_REJECTED);
        }
        return BL_OK;
    }
    if (result == BL_ERROR_UNSUPPORTED_OPCODE) {
        return bl_reset_session(s, action, BORROWLINK_RESET_UNSUPPORTED_OPCODE);
    }
    if (result != BL_OK) {
        return bl_reset_session(s, action, BORROWLINK_RESET_PROTOCOL_ERROR);
    }
    if (ev->channel == BL_CHANNEL_REALTIME ||
        (bl_channel)ev->channel != bl_channel_for_opcode(f.opcode)) {
        return bl_reset_session(s, action, BORROWLINK_RESET_PROTOCOL_ERROR);
    }
    if (s->phase == BL_PHASE_HELLO_SENT && f.opcode == BORROWLINK_OPCODE_HELLO) {
        /* Simultaneous HELLO: ours wins (PROTOCOL.md 8), the Host cancels
         * its own.  Ignore without consuming a sequence number. */
        return BL_OK;
    }
    if (f.seq != s->rx_seq) {
        return bl_reset_session(s, action, BORROWLINK_RESET_PROTOCOL_ERROR);
    }
    s->rx_seq = (uint16_t)(s->rx_seq + 1u);
    if (bl_opcode_is_data(f.opcode) &&
        f.payload_size > s->cfg.max_rx_payload) {
        return bl_reset_session(s, action, BORROWLINK_RESET_LIMIT_EXCEEDED);
    }
    if (f.opcode == BORROWLINK_OPCODE_RESET) {
        return bl_on_peer_reset(s, &f, action);
    }
    if (s->phase == BL_PHASE_HELLO_SENT) {
        if (f.opcode == BORROWLINK_OPCODE_ACCEPT) {
            return bl_on_accept(s, &f, action);
        }
        return bl_reset_session(s, action, BORROWLINK_RESET_PROTOCOL_ERROR);
    }
    /* ACTIVE */
    if (bl_opcode_is_data(f.opcode)) {
        return bl_on_data(s, &f, action);
    }
    if (f.opcode == BORROWLINK_OPCODE_STATUS && f.payload_size > 0u) {
        bl_set_action(s, action, BL_ACT_STATUS);
        action->data = f.payload;
        action->size = f.payload_size;
        return BL_OK;
    }
    reason = BORROWLINK_RESET_PROTOCOL_ERROR; /* HELLO/ACCEPT mid-session, empty STATUS */
    return bl_reset_session(s, action, reason);
}

static bl_result bl_on_tx_data(bl_session *s, const bl_event *ev,
                               bl_action *action)
{
    size_t chunk;
    int whole;
    int end_message;
    int end_stream;
    uint8_t opcode;
    bl_result result;

    if (s->phase != BL_PHASE_ACTIVE || s->tx_ended) {
        return BL_ERROR_STATE;
    }
    if (ev->data == NULL && ev->size != 0u) {
        return BL_ERROR_ARGUMENT;
    }
    chunk = ev->size < s->tx_chunk_max ? ev->size : s->tx_chunk_max;
    whole = chunk == ev->size;
    end_message = whole && ev->end_message;
    end_stream = whole && ev->end_stream;
    if (end_message && end_stream) {
        opcode = BORROWLINK_OPCODE_DATA_END_MESSAGE_STREAM;
    } else if (end_stream) {
        opcode = BORROWLINK_OPCODE_DATA_END_STREAM;
    } else if (end_message) {
        opcode = BORROWLINK_OPCODE_DATA_END_MESSAGE;
    } else {
        opcode = BORROWLINK_OPCODE_DATA;
    }
    result = bl_send_frame(s, action, opcode, ev->data, chunk, 0);
    if (result != BL_OK) {
        return result;
    }
    action->consumed = chunk;
    if (end_message) {
        s->tx_msg_open = 0u;
    } else if (chunk != 0u) {
        s->tx_msg_open = 1u;
    }
    if (end_stream) {
        s->tx_ended = 1u;
        if (s->rx_ended) {
            bl_complete_session(s);
        }
    }
    return BL_OK;
}

static bl_result bl_on_action_done(bl_session *s, bl_action *action)
{
    s->outstanding = BL_ACT_NONE;
    switch (s->pending_kind) {
    case BL_PENDING_LINK_IDLE:
        bl_set_action(s, action, BL_ACT_SET_LINK_IDLE);
        break;
    case BL_PENDING_DISCONNECT:
        bl_set_action(s, action, BL_ACT_DISCONNECT);
        break;
    case BL_PENDING_RESET_LOCAL:
        bl_session_reset_action(s, action, s->pending_reason, 1,
                                (bl_error_class)s->pending_error);
        break;
    default:
        break;
    }
    s->pending_kind = BL_PENDING_NONE;
    return BL_OK;
}

static bl_result bl_on_action_failed(bl_session *s, bl_action *action)
{
    uint8_t failed = s->outstanding;

    s->outstanding = BL_ACT_NONE;
    s->pending_kind = BL_PENDING_NONE;
    if (failed == BL_ACT_SESSION_RESET || failed == BL_ACT_DISCONNECT) {
        return BL_OK; /* nothing left to tell the caller */
    }
    bl_end_session(s, 0u, BL_ERR_TRANSPORT);
    bl_session_reset_action(s, action, 0u, 1, BL_ERR_TRANSPORT);
    return BL_OK;
}

static bl_result bl_on_link_lost(bl_session *s, bl_action *action)
{
    int was_active = s->phase != BL_PHASE_IDLE;

    s->outstanding = BL_ACT_NONE;
    s->pending_kind = BL_PENDING_NONE;
    if (!was_active) {
        return BL_OK;
    }
    bl_end_session(s, 0u, BL_ERR_TRANSPORT);
    bl_session_reset_action(s, action, 0u, 0, BL_ERR_TRANSPORT);
    return BL_OK;
}

static bl_result bl_on_link_closed(bl_session *s, bl_action *action)
{
    /* Orderly transport close.  If my direction was already closed, the
     * peer's close stands in for its DATA_END_STREAM: the session is complete
     * and no link action is owed because the link is gone. */
    if (s->phase == BL_PHASE_ACTIVE && s->tx_ended && !s->rx_msg_open) {
        s->outstanding = BL_ACT_NONE;
        s->pending_kind = BL_PENDING_NONE;
        s->rx_ended = 1u;
        bl_end_session(s, 0u, BL_ERR_NONE);
        bl_set_action(s, action, BL_ACT_DISCONNECT);
        return BL_OK;
    }
    return bl_on_link_lost(s, action);
}

static bl_result bl_on_tick(bl_session *s, const bl_event *ev,
                            bl_action *action)
{
    if (s->phase == BL_PHASE_HELLO_SENT &&
        s->cfg.handshake_timeout_ms != 0u &&
        (uint32_t)(ev->now_ms - s->hello_ms) >= s->cfg.handshake_timeout_ms) {
        bl_end_session(s, 0u, BL_ERR_TIMEOUT);
        bl_session_reset_action(s, action, 0u, 1, BL_ERR_TIMEOUT);
    }
    return BL_OK;
}

bl_result bl_session_step(bl_session *state,
                          const bl_event *event,
                          bl_action *action)
{
    if (state == NULL || event == NULL || action == NULL) {
        return BL_ERROR_ARGUMENT;
    }
    memset(action, 0, sizeof(*action));
    switch (event->type) {
    case BL_EV_ACTION_DONE:
    case BL_EV_ACTION_FAILED:
        if (state->outstanding == BL_ACT_NONE) {
            return BL_ERROR_STATE;
        }
        return event->type == BL_EV_ACTION_DONE
                   ? bl_on_action_done(state, action)
                   : bl_on_action_failed(state, action);
    case BL_EV_LINK_LOST:
        return bl_on_link_lost(state, action);
    case BL_EV_LINK_CLOSED:
        return bl_on_link_closed(state, action);
    case BL_EV_START:
    case BL_EV_RX_FRAME:
    case BL_EV_TX_DATA:
    case BL_EV_LOCAL_RESET:
    case BL_EV_TICK:
        break;
    default:
        return BL_ERROR_ARGUMENT;
    }
    if (state->outstanding != BL_ACT_NONE) {
        return BL_ERROR_BUSY;
    }
    switch (event->type) {
    case BL_EV_START:
        return bl_on_start(state, event, action);
    case BL_EV_RX_FRAME:
        return bl_on_rx_frame(state, event, action);
    case BL_EV_TX_DATA:
        return bl_on_tx_data(state, event, action);
    case BL_EV_LOCAL_RESET:
        if (state->phase == BL_PHASE_IDLE) {
            return BL_ERROR_STATE;
        }
        if (event->reason == 0u) {
            return BL_ERROR_ARGUMENT;
        }
        return bl_reset_session(state, action, event->reason);
    default: /* BL_EV_TICK */
        return bl_on_tick(state, event, action);
    }
}
