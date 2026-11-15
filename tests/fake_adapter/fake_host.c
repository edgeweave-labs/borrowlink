#include "fake_host.h"

#include <assert.h>
#include <string.h>

void fh_init(fake_host *h, uint16_t accept_max_rx)
{
    memset(h, 0, sizeof(*h));
    assert(bl_binding_rx_init(&h->rx, h->rx_buf, sizeof(h->rx_buf),
                              sizeof(h->rx_buf)) == BL_OK);
    h->auto_accept = 1u;
    h->accept_max_rx = accept_max_rx;
    h->node_id = UINT64_C(0x1112131415161718);
}

void fh_queue_raw(fake_host *h, const uint8_t *bytes, size_t n)
{
    assert(h->out_len + n <= sizeof(h->out));
    memcpy(h->out + h->out_len, bytes, n);
    h->out_len += n;
}

void fh_queue_frame(fake_host *h, uint8_t channel, uint16_t seq,
                    uint8_t opcode, const uint8_t *payload, size_t n)
{
    uint8_t frame[FH_PAYLOAD_MAX + 3u];
    uint8_t record[FH_PAYLOAD_MAX + 6u];
    size_t fw = 0u;
    size_t rw = 0u;

    assert(n <= FH_PAYLOAD_MAX);
    assert(bl_frame_encode(frame, sizeof(frame), &fw, seq, opcode, payload,
                           n) == BL_OK);
    assert(bl_binding_encode(record, sizeof(record), &rw, channel, frame, fw) ==
           BL_OK);
    fh_queue_raw(h, record, rw);
}

void fh_send(fake_host *h, uint8_t channel, uint8_t opcode,
             const uint8_t *payload, size_t n)
{
    fh_queue_frame(h, channel, h->tx_seq, opcode, payload, n);
    h->tx_seq = (uint16_t)(h->tx_seq + 1u);
}

void fh_send_message(fake_host *h, const uint8_t *payload, size_t n,
                     size_t chunk)
{
    size_t off = 0u;

    if (n == 0u) {
        fh_send(h, BL_CHANNEL_RELIABLE, BORROWLINK_OPCODE_DATA_END_MESSAGE,
                NULL, 0u);
        return;
    }
    while (off < n) {
        size_t piece = n - off < chunk ? n - off : chunk;
        int last = off + piece == n;

        fh_send(h, BL_CHANNEL_RELIABLE,
                last ? BORROWLINK_OPCODE_DATA_END_MESSAGE
                     : BORROWLINK_OPCODE_DATA,
                payload + off, piece);
        off += piece;
    }
}

static void fh_on_frame(fake_host *h, uint8_t channel, const uint8_t *raw,
                        size_t size)
{
    bl_frame_view v;
    fh_frame *f;

    if (bl_frame_decode(&v, raw, size) != BL_OK ||
        h->frame_count >= FH_MAX_FRAMES || v.payload_size > FH_PAYLOAD_MAX) {
        h->protocol_errors++;
        return;
    }
    f = &h->frames[h->frame_count++];
    f->channel = channel;
    f->seq = v.seq;
    f->opcode = v.opcode;
    f->payload_size = v.payload_size;
    memcpy(f->payload, v.payload, v.payload_size);
    /* seq per direction: HELLO is 0, everything after counts up */
    if (h->frame_count >= 2u && v.opcode != BORROWLINK_OPCODE_HELLO &&
        (uint16_t)(h->frames[h->frame_count - 2u].seq + 1u) != v.seq) {
        h->protocol_errors++;
    }
    if (v.opcode == BORROWLINK_OPCODE_HELLO) {
        if (bl_hello_decode(&h->hello, v.payload, v.payload_size) != BL_OK) {
            h->protocol_errors++;
            return;
        }
        if (h->auto_accept) {
            uint8_t body[BORROWLINK_ACCEPT_PAYLOAD_SIZE];
            bl_accept a;

            a.version = BORROWLINK_PROTOCOL_VERSION;
            a.delivery = BORROWLINK_DELIVERY_RELIABLE;
            a.xid = h->hello.xid;
            a.node_id = h->node_id;
            a.max_rx_payload = h->accept_max_rx;
            assert(bl_accept_encode(body, sizeof(body), &a) == BL_OK);
            h->tx_seq = 0u;
            fh_send(h, BL_CHANNEL_CONTROL, BORROWLINK_OPCODE_ACCEPT, body,
                    sizeof(body));
        }
    }
}

void fh_receive(fake_host *h, const uint8_t *bytes, size_t n)
{
    size_t off = 0u;

    while (off < n) {
        bl_binding_record rec;
        size_t used = 0u;
        bl_result r = bl_binding_rx_feed(&h->rx, bytes + off, n - off, &used,
                                         &rec);

        if (r != BL_OK) {
            h->protocol_errors++;
            return;
        }
        off += used;
        if (rec.ready) {
            fh_on_frame(h, rec.channel, rec.frame, rec.frame_size);
        }
    }
}

size_t fh_messages(const fake_host *h, size_t index, uint8_t *out,
                   size_t out_size, size_t *out_len)
{
    size_t count = 0u;
    size_t len = 0u;
    size_t i;

    for (i = 0u; i < h->frame_count; ++i) {
        const fh_frame *f = &h->frames[i];

        if (f->opcode > BORROWLINK_OPCODE_DATA_END_MESSAGE_STREAM) {
            continue;
        }
        if (count == index && out != NULL) {
            assert(len + f->payload_size <= out_size);
            memcpy(out + len, f->payload, f->payload_size);
        }
        len += f->payload_size;
        if (f->opcode == BORROWLINK_OPCODE_DATA_END_MESSAGE ||
            f->opcode == BORROWLINK_OPCODE_DATA_END_MESSAGE_STREAM) {
            if (count == index && out_len != NULL) {
                *out_len = len;
            }
            count++;
            len = 0u;
        }
    }
    return count;
}

size_t fh_take(fake_host *h, uint8_t *dst, size_t max)
{
    size_t n = h->out_len < max ? h->out_len : max;

    memcpy(dst, h->out, n);
    memmove(h->out, h->out + n, h->out_len - n);
    h->out_len -= n;
    return n;
}
