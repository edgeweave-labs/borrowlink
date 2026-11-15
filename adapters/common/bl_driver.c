#include "bl_driver.h"

#include <string.h>

bl_result bl_driver_init(bl_driver *d, const bl_driver_config *cfg,
                         const bl_driver_ops *ops, void *ctx)
{
    bl_session_config sc;
    bl_result result;

    if (d == NULL || cfg == NULL || ops == NULL || ops->send == NULL ||
        cfg->wire_buf == NULL) {
        return BL_ERROR_ARGUMENT;
    }
    if (cfg->wire_buf_size <
        cfg->tx_buf_size + BORROWLINK_BINDING_HEADER_SIZE) {
        return BL_ERROR_BUFFER_TOO_SMALL;
    }
    memset(d, 0, sizeof(*d));
    sc = cfg->session;
    sc.tx_buf = cfg->tx_buf;
    sc.tx_buf_size = cfg->tx_buf_size;
    result = bl_session_init(&d->session, &sc);
    if (result != BL_OK) {
        return result;
    }
    result = bl_binding_rx_init(&d->rx, cfg->rx_buf, cfg->rx_buf_size,
                                (size_t)sc.max_rx_payload +
                                    BORROWLINK_FRAME_HEADER_SIZE);
    if (result != BL_OK) {
        return result;
    }
    bl_msg_parser_init(&d->parser);
    d->ops = *ops;
    d->ctx = ctx;
    d->wire_buf = cfg->wire_buf;
    d->wire_buf_size = cfg->wire_buf_size;
    return BL_OK;
}

static void bl_driver_deliver(bl_driver *d, const bl_action *act)
{
    const uint8_t *p = act->data;
    size_t left = act->size;
    bl_msg_event ev;
    size_t used;
    bl_result r;

    for (;;) {
        r = bl_msg_parser_feed(&d->parser, p, left, act->end_message, &used,
                               &ev);
        if (r != BL_OK) {
            d->deferred_reset = BORROWLINK_RESET_PROTOCOL_ERROR;
            return;
        }
        p += used;
        left -= used;
        if (ev.kind == BL_MSG_EV_NONE) {
            break;
        }
        if (d->ops.on_msg != NULL) {
            d->ops.on_msg(d->ctx, &ev);
        }
        if (ev.kind == BL_MSG_EV_END) {
            break;
        }
    }
    if (act->end_stream && act->msg_truncated) {
        d->deferred_reset = BORROWLINK_RESET_PROTOCOL_ERROR;
    }
}

/* Executes one action; returns 0 on success, nonzero if the transport failed. */
static int bl_driver_execute(bl_driver *d, const bl_action *act)
{
    size_t n = 0u;

    switch (act->type) {
    case BL_ACT_SEND_FRAME:
        if (bl_binding_encode(d->wire_buf, d->wire_buf_size, &n, act->channel,
                              act->data, act->size) != BL_OK) {
            return 1;
        }
        return d->ops.send(d->ctx, d->wire_buf, n);
    case BL_ACT_DELIVER_DATA:
        bl_driver_deliver(d, act);
        break;
    case BL_ACT_SESSION_OPEN:
        d->opened = 1u;
        if (d->ops.on_open != NULL) {
            d->ops.on_open(d->ctx, act);
        }
        break;
    case BL_ACT_STATUS:
        if (d->ops.on_status != NULL) {
            d->ops.on_status(d->ctx, act->data, act->size);
        }
        break;
    case BL_ACT_SESSION_RESET:
    case BL_ACT_SET_LINK_IDLE:
    case BL_ACT_DISCONNECT:
        d->finished = 1u;
        d->opened = 0u;
        d->end_type = act->type;
        d->end_reset_reason = act->reset_reason;
        d->end_error = act->error;
        bl_msg_parser_init(&d->parser);
        if (d->ops.on_end != NULL) {
            d->ops.on_end(d->ctx, act);
        }
        break;
    default:
        break;
    }
    return 0;
}

/* Runs `ev` and every follow-up the reducer asks for. */
static bl_result bl_driver_apply(bl_driver *d, bl_event ev, size_t *consumed)
{
    bl_result first = BL_OK;
    bl_action act;
    int top = 1;

    for (;;) {
        bl_result r = bl_session_step(&d->session, &ev, &act);

        if (top) {
            first = r;
            if (consumed != NULL) {
                *consumed = act.consumed;
            }
        }
        if (r != BL_OK) {
            return top ? r : first;
        }
        top = 0;
        if (act.type == BL_ACT_NONE) {
            if (d->deferred_reset != 0u && d->session.phase != BL_PHASE_IDLE) {
                memset(&ev, 0, sizeof(ev));
                ev.type = BL_EV_LOCAL_RESET;
                ev.reason = d->deferred_reset;
                d->deferred_reset = 0u;
                continue;
            }
            d->deferred_reset = 0u;
            return first;
        }
        memset(&ev, 0, sizeof(ev));
        ev.type = bl_driver_execute(d, &act) == 0 ? BL_EV_ACTION_DONE
                                                  : BL_EV_ACTION_FAILED;
    }
}

bl_result bl_driver_start(bl_driver *d, uint16_t xid, uint32_t now_ms)
{
    bl_event ev;

    memset(&ev, 0, sizeof(ev));
    ev.type = BL_EV_START;
    ev.xid = xid;
    ev.now_ms = now_ms;
    d->finished = 0u;
    return bl_driver_apply(d, ev, NULL);
}

bl_result bl_driver_feed(bl_driver *d, const uint8_t *bytes, size_t n)
{
    size_t off = 0u;

    while (off < n) {
        bl_binding_record rec;
        size_t used = 0u;
        bl_event ev;
        bl_result r = bl_binding_rx_feed(&d->rx, bytes + off, n - off, &used,
                                         &rec);

        if (r != BL_OK) {
            memset(&ev, 0, sizeof(ev));
            ev.type = BL_EV_LOCAL_RESET;
            ev.reason = bl_binding_reset_reason(r);
            if (d->session.phase == BL_PHASE_IDLE) {
                return r;
            }
            (void)bl_driver_apply(d, ev, NULL);
            return r;
        }
        off += used;
        if (!rec.ready) {
            continue;
        }
        memset(&ev, 0, sizeof(ev));
        ev.type = BL_EV_RX_FRAME;
        ev.channel = rec.channel;
        ev.data = rec.frame;
        ev.size = rec.frame_size;
        r = bl_driver_apply(d, ev, NULL);
        if (r != BL_OK) {
            return r;
        }
    }
    return BL_OK;
}

bl_result bl_driver_send(bl_driver *d, const uint8_t *data, size_t size,
                         int end_message, int end_stream)
{
    size_t off = 0u;

    do {
        bl_event ev;
        size_t consumed = 0u;
        bl_result r;

        memset(&ev, 0, sizeof(ev));
        ev.type = BL_EV_TX_DATA;
        ev.data = data == NULL ? NULL : data + off;
        ev.size = size - off;
        ev.end_message = end_message ? 1u : 0u;
        ev.end_stream = end_stream ? 1u : 0u;
        r = bl_driver_apply(d, ev, &consumed);
        if (r != BL_OK) {
            return r;
        }
        if (d->session.phase == BL_PHASE_IDLE && consumed < size - off) {
            return BL_ERROR_STATE; /* the session ended under us */
        }
        off += consumed;
    } while (off < size);
    return BL_OK;
}

bl_result bl_driver_close(bl_driver *d)
{
    return bl_driver_send(d, NULL, 0u, 0, 1);
}

bl_result bl_driver_reset(bl_driver *d, uint8_t reason)
{
    bl_event ev;

    memset(&ev, 0, sizeof(ev));
    ev.type = BL_EV_LOCAL_RESET;
    ev.reason = reason;
    return bl_driver_apply(d, ev, NULL);
}

bl_result bl_driver_link_lost(bl_driver *d)
{
    bl_event ev;

    memset(&ev, 0, sizeof(ev));
    ev.type = BL_EV_LINK_LOST;
    return bl_driver_apply(d, ev, NULL);
}

bl_result bl_driver_tick(bl_driver *d, uint32_t now_ms)
{
    bl_event ev;

    memset(&ev, 0, sizeof(ev));
    ev.type = BL_EV_TICK;
    ev.now_ms = now_ms;
    return bl_driver_apply(d, ev, NULL);
}
