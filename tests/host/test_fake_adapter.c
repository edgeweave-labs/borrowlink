/*
 * Full fake-adapter traces: the driver (stream binding + session reducer +
 * Message parser) against a scripted Host, with no sockets or clocks.
 * "connect -> session -> idle" is the trace PROTOCOL.md 15.1 asks for; the BLE
 * "advertise" half belongs to the future GATT adapter.
 */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <bl_driver.h>
#include <fake_host.h>

#define NODE_ID UINT64_C(0x0102030405060708)

typedef struct {
    fake_host host;
    bl_driver drv;
    uint8_t tx[64];
    uint8_t wire[80];
    uint8_t rx[40];
    uint16_t pending[8];
    bl_msg_client client;
    /* what the app saw */
    int opened;
    int ends;
    bl_action last_end;
    int status_seen;
    bl_msg_header headers[8];
    int header_count;
    uint8_t payload[512];
    size_t payload_len;
    int message_ends;
    /* fault injection */
    int fail_send_after; /* -1 = never */
    int sends;
    size_t sent_bytes;
} rig;

static int rig_send(void *ctx, const uint8_t *bytes, size_t n)
{
    rig *r = (rig *)ctx;

    if (r->fail_send_after >= 0 && r->sends >= r->fail_send_after) {
        return 1;
    }
    r->sends++;
    r->sent_bytes += n;
    fh_receive(&r->host, bytes, n);
    return 0;
}

static void rig_on_open(void *ctx, const bl_action *a)
{
    rig *r = (rig *)ctx;

    r->opened++;
    assert(a->peer_node_id == r->host.node_id);
}

static void rig_on_msg(void *ctx, const bl_msg_event *ev)
{
    rig *r = (rig *)ctx;

    if (ev->kind == BL_MSG_EV_HEADER) {
        assert(r->header_count < 8);
        r->headers[r->header_count++] = ev->header;
        r->payload_len = 0u;
    } else if (ev->kind == BL_MSG_EV_PAYLOAD) {
        assert(r->payload_len + ev->size <= sizeof(r->payload));
        memcpy(r->payload + r->payload_len, ev->data, ev->size);
        r->payload_len += ev->size;
    } else if (ev->kind == BL_MSG_EV_END) {
        r->message_ends++;
        if (r->headers[r->header_count - 1].cmd == BORROWLINK_MSG_ACK) {
            assert(bl_msg_client_release_id(
                       &r->client, r->headers[r->header_count - 1].id) ==
                   BL_OK);
        }
    }
}

static void rig_on_status(void *ctx, const uint8_t *d, size_t n)
{
    rig *r = (rig *)ctx;

    (void)d;
    (void)n;
    r->status_seen++;
}

static void rig_on_end(void *ctx, const bl_action *a)
{
    rig *r = (rig *)ctx;

    r->ends++;
    r->last_end = *a;
}

static void rig_init(rig *r, int keep, uint16_t host_max_rx, uint16_t my_max_rx)
{
    bl_driver_config c;
    bl_driver_ops ops;

    memset(r, 0, sizeof(*r));
    fh_init(&r->host, host_max_rx);
    r->fail_send_after = -1;
    memset(&c, 0, sizeof(c));
    c.session.role = BL_ROLE_INITIATOR;
    c.session.profile = BORROWLINK_PROFILE_MESSAGE;
    c.session.delivery = BORROWLINK_DELIVERY_RELIABLE;
    c.session.keep_connected = (uint8_t)keep;
    c.session.node_id = NODE_ID;
    c.session.max_rx_payload = my_max_rx;
    c.session.max_message_size = 400u;
    c.tx_buf = r->tx;
    c.tx_buf_size = sizeof(r->tx);
    c.wire_buf = r->wire;
    c.wire_buf_size = sizeof(r->wire);
    c.rx_buf = r->rx;
    c.rx_buf_size = sizeof(r->rx);
    memset(&ops, 0, sizeof(ops));
    ops.send = rig_send;
    ops.on_open = rig_on_open;
    ops.on_msg = rig_on_msg;
    ops.on_status = rig_on_status;
    ops.on_end = rig_on_end;
    assert(bl_driver_init(&r->drv, &c, &ops, r) == BL_OK);
    assert(bl_msg_client_init(&r->client, r->pending, 8u) == BL_OK);
}

/* Moves everything the host queued into the driver, `step` bytes at a time. */
static void pump(rig *r, size_t step)
{
    uint8_t buf[64];

    assert(step <= sizeof(buf));
    while (r->host.out_len != 0u) {
        size_t n = fh_take(&r->host, buf, step);

        assert(bl_driver_feed(&r->drv, buf, n) == BL_OK);
    }
}

static void send_publish(rig *r, uint16_t topic, const uint8_t *payload,
                         size_t n, size_t piece)
{
    bl_msg_header h;
    uint8_t hb[BORROWLINK_MSG_HEADER_MAX];
    size_t hw = 0u;
    size_t off = 0u;
    uint16_t id = 0u;

    assert(bl_msg_client_alloc_id(&r->client, &id) == BL_OK);
    memset(&h, 0, sizeof(h));
    h.cmd = BORROWLINK_MSG_PUBLISH;
    h.id = id;
    h.topic = topic;
    assert(bl_msg_header_encode(hb, sizeof(hb), &hw, &h) == BL_OK);
    /* header, then the payload in pieces: no whole-message buffer exists */
    assert(bl_driver_send(&r->drv, hb, hw, 0, 0) == BL_OK);
    while (off < n) {
        size_t p = n - off < piece ? n - off : piece;

        assert(bl_driver_send(&r->drv, payload + off, p, off + p == n, 0) ==
               BL_OK);
        off += p;
    }
    if (n == 0u) {
        assert(bl_driver_send(&r->drv, NULL, 0u, 1, 0) == BL_OK);
    }
}

static void host_send_ack(rig *r, uint16_t id, uint8_t status, size_t chunk)
{
    bl_msg_header h;
    uint8_t b[4];
    size_t w = 0u;

    memset(&h, 0, sizeof(h));
    h.cmd = BORROWLINK_MSG_ACK;
    h.id = id;
    h.status = status;
    assert(bl_msg_header_encode(b, sizeof(b), &w, &h) == BL_OK);
    fh_send_message(&r->host, b, w, chunk);
}

static void host_send_deliver(rig *r, uint16_t id, uint16_t topic,
                              const uint8_t *payload, size_t n, size_t chunk)
{
    uint8_t buf[300];
    bl_msg_header h;
    size_t w = 0u;

    memset(&h, 0, sizeof(h));
    h.cmd = BORROWLINK_MSG_DELIVER;
    h.id = id;
    h.source_node_id = UINT64_C(0x00000000000000aa);
    h.topic = topic;
    assert(bl_msg_header_encode(buf, sizeof(buf), &w, &h) == BL_OK);
    memcpy(buf + w, payload, n);
    fh_send_message(&r->host, buf, w + n, chunk);
}

static void connect_and_open(rig *r, size_t step)
{
    assert(bl_driver_start(&r->drv, 0x4242u, 100u) == BL_OK);
    assert(r->host.frame_count == 1u);
    assert(r->host.frames[0].opcode == BORROWLINK_OPCODE_HELLO);
    assert(r->host.frames[0].channel == BL_CHANNEL_CONTROL);
    assert(r->host.frames[0].seq == 0u);
    pump(r, step);
    assert(r->opened == 1);
}

/* ------------------------------------------------------------ scenarios */

static void happy_path(size_t step, int keep)
{
    rig r;
    uint8_t payload[100];
    uint8_t rpc[70];
    uint8_t got[256];
    size_t got_len = 0u;
    size_t i;

    for (i = 0u; i < sizeof(payload); ++i) {
        payload[i] = (uint8_t)(i * 3u + 1u);
    }
    for (i = 0u; i < sizeof(rpc); ++i) {
        rpc[i] = (uint8_t)(0xa0u + i);
    }
    rig_init(&r, keep, 16u, 32u);
    connect_and_open(&r, step);
    assert(r.host.hello.xid == 0x4242u);
    assert(r.host.hello.profile == BORROWLINK_PROFILE_MESSAGE);
    assert(r.host.hello.node_id == NODE_ID);
    assert(r.host.hello.max_rx_payload == 32u);
    assert(r.drv.session.peer_max_rx == 16u);

    /* node -> host: a 105-byte PUBLISH sent in 16-byte frames */
    send_publish(&r, 0x0001u, payload, sizeof(payload), 37u);
    assert(fh_messages(&r.host, 0u, got, sizeof(got), &got_len) == 1u);
    assert(got_len == 5u + sizeof(payload));
    assert(got[0] == BORROWLINK_MSG_PUBLISH && got[3] == 0x00 && got[4] == 0x01);
    assert(memcmp(got + 5, payload, sizeof(payload)) == 0);
    for (i = 1u; i < r.host.frame_count; ++i) {
        assert(r.host.frames[i].payload_size <= 16u);
        assert(r.host.frames[i].channel == BL_CHANNEL_RELIABLE);
    }
    assert(bl_msg_client_is_pending(&r.client, 1u));

    /* host -> node: ACK and a DELIVER with a 70-byte payload (3 frames) */
    host_send_ack(&r, 1u, BORROWLINK_ACK_ACCEPTED, 2u);
    host_send_deliver(&r, 9u, 0x0002u, rpc, sizeof(rpc), 29u);
    pump(&r, step);
    assert(r.message_ends == 2);
    assert(r.header_count == 2);
    assert(r.headers[0].cmd == BORROWLINK_MSG_ACK && r.headers[0].id == 1u);
    assert(r.headers[1].cmd == BORROWLINK_MSG_DELIVER &&
           r.headers[1].id == 9u && r.headers[1].topic == 0x0002u);
    assert(r.payload_len == sizeof(rpc) &&
           memcmp(r.payload, rpc, sizeof(rpc)) == 0);
    assert(r.client.count == 0u);

    /* node answers: ACK of the DELIVER, then a reply PUBLISH */
    {
        bl_msg_header h;
        uint8_t ab[4];
        size_t w = 0u;

        memset(&h, 0, sizeof(h));
        h.cmd = BORROWLINK_MSG_ACK;
        h.id = 9u;
        assert(bl_msg_header_encode(ab, sizeof(ab), &w, &h) == BL_OK);
        assert(bl_driver_send(&r.drv, ab, w, 1, 0) == BL_OK);
    }
    send_publish(&r, 0x0003u, NULL, 0u, 8u); /* zero-length payload */
    assert(fh_messages(&r.host, 3u, NULL, 0u, NULL) == 3u);
    assert(r.host.protocol_errors == 0);

    /* orderly close: node first, then the host */
    assert(r.ends == 0);
    assert(bl_driver_close(&r.drv) == BL_OK);
    assert(r.host.frames[r.host.frame_count - 1u].opcode ==
           BORROWLINK_OPCODE_DATA_END_STREAM);
    assert(r.ends == 0 && r.drv.session.phase == BL_PHASE_ACTIVE);
    fh_send(&r.host, BL_CHANNEL_RELIABLE, BORROWLINK_OPCODE_DATA_END_STREAM,
            NULL, 0u);
    pump(&r, step);
    assert(r.ends == 1);
    assert(r.last_end.type == (keep ? BL_ACT_SET_LINK_IDLE : BL_ACT_DISCONNECT));
    assert(r.drv.finished == 1u && r.drv.session.phase == BL_PHASE_IDLE);
    assert(r.host.protocol_errors == 0);

    /* the link is idle: a second session reuses it, sequence numbers restart */
    r.host.frame_count = 0u;
    r.opened = 0;
    r.header_count = 0;
    assert(bl_driver_start(&r.drv, 0x4343u, 500u) == BL_OK);
    assert(r.host.frames[0].seq == 0u && r.host.hello.xid == 0x4343u);
    pump(&r, step);
    assert(r.opened == 1);
    assert(bl_driver_send(&r.drv, (const uint8_t *)"\x03\x00\x01\x00\x01", 5u, 1,
                          0) == BL_OK);
    assert(r.host.frames[1].seq == 1u);
    assert(r.host.protocol_errors == 0);
}

static void test_happy_path_all_fragmentations(void)
{
    size_t step;

    for (step = 1u; step <= 64u; step += (step < 9u ? 1u : 11u)) {
        happy_path(step, 1);
        happy_path(step, 0);
    }
}

static void test_host_closes_first(void)
{
    rig r;

    rig_init(&r, 1, 32u, 32u);
    connect_and_open(&r, 7u);
    fh_send(&r.host, BL_CHANNEL_RELIABLE, BORROWLINK_OPCODE_DATA_END_STREAM,
            NULL, 0u);
    pump(&r, 5u);
    assert(r.ends == 0);
    assert(bl_driver_send(&r.drv, (const uint8_t *)"\x05\x00\x01\x00", 4u, 1, 0) ==
           BL_OK); /* still allowed to speak */
    assert(bl_driver_close(&r.drv) == BL_OK);
    assert(r.ends == 1 && r.last_end.type == BL_ACT_SET_LINK_IDLE);
    assert(bl_driver_send(&r.drv, NULL, 0u, 0, 1) == BL_ERROR_STATE);
}

static void test_peer_reset_and_status(void)
{
    rig r;
    uint8_t reason = BORROWLINK_RESET_NO_NETWORK;
    uint8_t status[] = {0x00, 0x01};

    rig_init(&r, 1, 32u, 32u);
    connect_and_open(&r, 3u);
    fh_send(&r.host, BL_CHANNEL_CONTROL, BORROWLINK_OPCODE_STATUS, status, 2u);
    pump(&r, 4u);
    assert(r.status_seen == 1 && r.ends == 0);
    fh_send(&r.host, BL_CHANNEL_CONTROL, BORROWLINK_OPCODE_RESET, &reason, 1u);
    pump(&r, 4u);
    assert(r.ends == 1 && r.last_end.type == BL_ACT_SESSION_RESET);
    assert(r.last_end.local == 0u && r.last_end.reset_reason == reason);
    assert(r.last_end.error == BL_ERR_TRANSPORT);
}

static void test_handshake_refused(void)
{
    rig r;
    uint8_t reason = BORROWLINK_RESET_UNSUPPORTED_PROFILE;

    rig_init(&r, 1, 32u, 32u);
    r.host.auto_accept = 0u;
    assert(bl_driver_start(&r.drv, 0x1111u, 0u) == BL_OK);
    fh_queue_frame(&r.host, BL_CHANNEL_CONTROL, 0u, BORROWLINK_OPCODE_RESET,
                   &reason, 1u);
    pump(&r, 6u);
    assert(r.opened == 0 && r.ends == 1);
    assert(r.last_end.reset_reason == BORROWLINK_RESET_UNSUPPORTED_PROFILE);
    assert(bl_driver_send(&r.drv, NULL, 0u, 1, 0) == BL_ERROR_STATE);
}

static void test_handshake_timeout(void)
{
    rig r;
    bl_driver_config c;
    bl_driver_ops ops;

    rig_init(&r, 1, 32u, 32u);
    /* rebuild with a timeout */
    memset(&c, 0, sizeof(c));
    c.session.role = BL_ROLE_INITIATOR;
    c.session.profile = BORROWLINK_PROFILE_MESSAGE;
    c.session.node_id = NODE_ID;
    c.session.max_rx_payload = 32u;
    c.session.handshake_timeout_ms = 2000u;
    c.tx_buf = r.tx;
    c.tx_buf_size = sizeof(r.tx);
    c.wire_buf = r.wire;
    c.wire_buf_size = sizeof(r.wire);
    c.rx_buf = r.rx;
    c.rx_buf_size = sizeof(r.rx);
    memset(&ops, 0, sizeof(ops));
    ops.send = rig_send;
    ops.on_end = rig_on_end;
    assert(bl_driver_init(&r.drv, &c, &ops, &r) == BL_OK);
    r.host.auto_accept = 0u;
    assert(bl_driver_start(&r.drv, 0x2222u, 1000u) == BL_OK);
    assert(bl_driver_tick(&r.drv, 2999u) == BL_OK && r.ends == 0);
    assert(bl_driver_tick(&r.drv, 3000u) == BL_OK && r.ends == 1);
    assert(r.last_end.error == BL_ERR_TIMEOUT);
}

static void test_node_violations_are_reset_by_the_node(void)
{
    rig r;

    /* seq gap from the host */
    rig_init(&r, 1, 32u, 32u);
    connect_and_open(&r, 8u);
    fh_queue_frame(&r.host, BL_CHANNEL_RELIABLE, 5u,
                   BORROWLINK_OPCODE_DATA_END_MESSAGE, (const uint8_t *)"\x05", 1u);
    pump(&r, 8u);
    assert(r.ends == 1 && r.last_end.local == 1u);
    assert(r.last_end.reset_reason == BORROWLINK_RESET_PROTOCOL_ERROR);
    assert(r.host.frames[r.host.frame_count - 1u].opcode ==
           BORROWLINK_OPCODE_RESET);

    /* a record longer than max_rx_payload + 3 (LIMIT_EXCEEDED) */
    rig_init(&r, 1, 32u, 32u);
    connect_and_open(&r, 8u);
    {
        uint8_t big[40];

        memset(big, 0, sizeof(big));
        fh_queue_frame(&r.host, BL_CHANNEL_RELIABLE, 1u,
                       BORROWLINK_OPCODE_DATA, big, sizeof(big));
        /* the binding rx buffer holds max_rx_payload + 3 = 35 bytes */
        {
            uint8_t chunk[64];
            size_t n = fh_take(&r.host, chunk, sizeof(chunk));

            assert(bl_driver_feed(&r.drv, chunk, n) ==
                   BL_ERROR_LIMIT_EXCEEDED);
        }
    }
    assert(r.ends == 1);
    assert(r.last_end.reset_reason == BORROWLINK_RESET_LIMIT_EXCEEDED);

    /* an unknown Message command */
    rig_init(&r, 1, 32u, 32u);
    connect_and_open(&r, 8u);
    fh_send_message(&r.host, (const uint8_t *)"\x7f\x00\x00", 3u, 8u);
    pump(&r, 8u);
    assert(r.ends == 1);
    assert(r.last_end.reset_reason == BORROWLINK_RESET_PROTOCOL_ERROR);

    /* the peer ends its stream in the middle of a message */
    rig_init(&r, 1, 32u, 32u);
    connect_and_open(&r, 8u);
    fh_send(&r.host, BL_CHANNEL_RELIABLE, BORROWLINK_OPCODE_DATA_END_STREAM,
            (const uint8_t *)"\x03\x00", 2u);
    pump(&r, 8u);
    assert(r.ends == 1);
    assert(r.last_end.reset_reason == BORROWLINK_RESET_PROTOCOL_ERROR);

    /* a frame on the wrong characteristic */
    rig_init(&r, 1, 32u, 32u);
    connect_and_open(&r, 8u);
    fh_send(&r.host, BL_CHANNEL_CONTROL, BORROWLINK_OPCODE_DATA_END_MESSAGE,
            NULL, 0u);
    pump(&r, 8u);
    assert(r.ends == 1);
    assert(r.last_end.reset_reason == BORROWLINK_RESET_PROTOCOL_ERROR);
}

static void test_transport_failures(void)
{
    rig r;

    /* the socket refuses the HELLO */
    rig_init(&r, 1, 32u, 32u);
    r.fail_send_after = 0;
    assert(bl_driver_start(&r.drv, 0x1u, 0u) == BL_OK);
    assert(r.ends == 1 && r.last_end.error == BL_ERR_TRANSPORT);

    /* the socket dies after the handshake */
    rig_init(&r, 1, 32u, 32u);
    connect_and_open(&r, 8u);
    r.fail_send_after = r.sends;
    assert(bl_driver_send(&r.drv, (const uint8_t *)"\x05\x00\x01\x00", 4u, 1, 0) ==
           BL_OK);
    assert(r.ends == 1 && r.last_end.error == BL_ERR_TRANSPORT);
    assert(r.drv.session.phase == BL_PHASE_IDLE);

    /* the connection drops while ACTIVE */
    rig_init(&r, 1, 32u, 32u);
    connect_and_open(&r, 8u);
    assert(bl_driver_link_lost(&r.drv) == BL_OK);
    assert(r.ends == 1 && r.last_end.error == BL_ERR_TRANSPORT &&
           r.last_end.local == 0u);
    assert(bl_driver_link_lost(&r.drv) == BL_OK); /* idempotent */
    assert(r.ends == 1);
}

static void test_local_reset(void)
{
    rig r;

    rig_init(&r, 1, 32u, 32u);
    connect_and_open(&r, 8u);
    assert(bl_driver_reset(&r.drv, BORROWLINK_RESET_BUSY) == BL_OK);
    assert(r.host.frames[r.host.frame_count - 1u].opcode ==
           BORROWLINK_OPCODE_RESET);
    assert(r.ends == 1 && r.last_end.reset_reason == BORROWLINK_RESET_BUSY);
}

static void test_driver_init_validation(void)
{
    rig r;
    bl_driver_config c;
    bl_driver_ops ops;

    rig_init(&r, 1, 32u, 32u);
    memset(&c, 0, sizeof(c));
    memset(&ops, 0, sizeof(ops));
    ops.send = rig_send;
    c.session.role = BL_ROLE_INITIATOR;
    c.session.profile = BORROWLINK_PROFILE_MESSAGE;
    c.session.node_id = NODE_ID;
    c.session.max_rx_payload = 32u;
    c.tx_buf = r.tx;
    c.tx_buf_size = sizeof(r.tx);
    c.wire_buf = r.wire;
    c.wire_buf_size = sizeof(r.tx) + 2u; /* needs +3 */
    c.rx_buf = r.rx;
    c.rx_buf_size = sizeof(r.rx);
    assert(bl_driver_init(&r.drv, &c, &ops, &r) == BL_ERROR_BUFFER_TOO_SMALL);
    c.wire_buf_size = sizeof(r.tx) + 3u;
    assert(bl_driver_init(&r.drv, &c, &ops, &r) == BL_OK);
    c.rx_buf_size = 34u; /* needs max_rx + 3 = 35 */
    assert(bl_driver_init(&r.drv, &c, &ops, &r) == BL_ERROR_BUFFER_TOO_SMALL);
    ops.send = NULL;
    c.rx_buf_size = sizeof(r.rx);
    assert(bl_driver_init(&r.drv, &c, &ops, &r) == BL_ERROR_ARGUMENT);
}

int main(void)
{
    test_happy_path_all_fragmentations();
    test_host_closes_first();
    test_peer_reset_and_status();
    test_handshake_refused();
    test_handshake_timeout();
    test_node_violations_are_reset_by_the_node();
    test_transport_failures();
    test_local_reset();
    test_driver_init_validation();
    return 0;
}
