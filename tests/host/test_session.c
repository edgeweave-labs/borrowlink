#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <borrowlink/session.h>

/* ------------------------------------------------------------ harness */

#define MY_NODE UINT64_C(0x0102030405060708)
#define PEER_NODE UINT64_C(0x1112131415161718)
#define XID UINT16_C(0x1234)

typedef struct {
    bl_session s;
    bl_action a;
    uint8_t tx[256];
} fx;

static void cfg_default(bl_session_config *c, uint8_t *tx, size_t tx_size)
{
    memset(c, 0, sizeof(*c));
    c->role = BL_ROLE_INITIATOR;
    c->profile = BORROWLINK_PROFILE_MESSAGE;
    c->delivery = BORROWLINK_DELIVERY_RELIABLE;
    c->keep_connected = 1u;
    c->node_id = MY_NODE;
    c->max_rx_payload = 64u;
    c->tx_buf = tx;
    c->tx_buf_size = tx_size;
}

static void fx_init(fx *f)
{
    bl_session_config c;

    memset(f, 0, sizeof(*f));
    cfg_default(&c, f->tx, sizeof(f->tx));
    assert(bl_session_init(&f->s, &c) == BL_OK);
}

static bl_result step(fx *f, const bl_event *e)
{
    return bl_session_step(&f->s, e, &f->a);
}

static bl_event ev_of(bl_event_type type)
{
    bl_event e;

    memset(&e, 0, sizeof(e));
    e.type = (uint8_t)type;
    return e;
}

static bl_result do_start(fx *f, uint16_t xid, uint32_t now)
{
    bl_event e = ev_of(BL_EV_START);

    e.xid = xid;
    e.now_ms = now;
    return step(f, &e);
}

static bl_result do_rx(fx *f, uint8_t channel, const uint8_t *frame, size_t n)
{
    bl_event e = ev_of(BL_EV_RX_FRAME);

    e.channel = channel;
    e.data = frame;
    e.size = n;
    return step(f, &e);
}

static bl_result do_tx(fx *f, const uint8_t *d, size_t n, int em, int es)
{
    bl_event e = ev_of(BL_EV_TX_DATA);

    e.data = d;
    e.size = n;
    e.end_message = (uint8_t)em;
    e.end_stream = (uint8_t)es;
    return step(f, &e);
}

static bl_result do_simple(fx *f, bl_event_type t)
{
    bl_event e = ev_of(t);

    return step(f, &e);
}

/* ACTION_DONE with the expectation that nothing follows. */
static void done_quiet(fx *f)
{
    assert(do_simple(f, BL_EV_ACTION_DONE) == BL_OK);
    assert(f->a.type == BL_ACT_NONE);
}

static size_t mk_frame(uint8_t *out, uint16_t seq, uint8_t op,
                       const uint8_t *p, size_t n)
{
    size_t w = 0u;

    assert(bl_frame_encode(out, 300u, &w, seq, op, p, n) == BL_OK);
    return w;
}

static size_t mk_accept(uint8_t *out, uint16_t xid, uint8_t delivery,
                        uint64_t node, uint16_t max_rx)
{
    uint8_t body[BORROWLINK_ACCEPT_PAYLOAD_SIZE];
    bl_accept a;

    a.version = BORROWLINK_PROTOCOL_VERSION;
    a.delivery = delivery;
    a.xid = xid;
    a.node_id = node;
    a.max_rx_payload = max_rx;
    assert(bl_accept_encode(body, sizeof(body), &a) == BL_OK);
    return mk_frame(out, 0u, BORROWLINK_OPCODE_ACCEPT, body, sizeof(body));
}

/* START + ACCEPT: leaves the session ACTIVE with tx_seq 1 and rx_seq 1. */
static void open_session(fx *f, uint16_t peer_max_rx)
{
    uint8_t fr[64];
    size_t n;

    assert(do_start(f, XID, 1000u) == BL_OK);
    assert(f->a.type == BL_ACT_SEND_FRAME);
    done_quiet(f);
    n = mk_accept(fr, XID, BORROWLINK_DELIVERY_RELIABLE, PEER_NODE,
                  peer_max_rx);
    assert(do_rx(f, BL_CHANNEL_CONTROL, fr, n) == BL_OK);
    assert(f->a.type == BL_ACT_SESSION_OPEN);
    done_quiet(f);
    assert(f->s.phase == BL_PHASE_ACTIVE);
}

/* The action is a RESET frame with the given reason, followed by a
 * SESSION_RESET report after ACTION_DONE. */
static void expect_local_reset(fx *f, uint8_t reason, bl_error_class cls,
                               uint16_t seq)
{
    bl_frame_view v;

    assert(f->a.type == BL_ACT_SEND_FRAME);
    assert(f->a.channel == BL_CHANNEL_CONTROL);
    assert(f->a.opcode == BORROWLINK_OPCODE_RESET);
    assert(f->a.reset_reason == reason);
    assert(bl_frame_decode(&v, f->a.data, f->a.size) == BL_OK);
    assert(v.seq == seq && v.payload_size == 1u && v.payload[0] == reason);
    assert(f->s.phase == BL_PHASE_IDLE);
    assert(do_simple(f, BL_EV_ACTION_DONE) == BL_OK);
    assert(f->a.type == BL_ACT_SESSION_RESET);
    assert(f->a.local == 1u && f->a.reset_reason == reason &&
           f->a.error == (uint8_t)cls);
    done_quiet(f);
    assert(f->s.last_reset_reason == reason);
}

/* ------------------------------------------------------------ init */

static void test_init_validation(void)
{
    uint8_t tx[64];
    bl_session_config c;
    bl_session s;

    cfg_default(&c, tx, sizeof(tx));
    assert(bl_session_init(&s, &c) == BL_OK);
    assert(s.phase == BL_PHASE_IDLE && s.outstanding == 0u);
    assert(bl_session_init(NULL, &c) == BL_ERROR_ARGUMENT);
    assert(bl_session_init(&s, NULL) == BL_ERROR_ARGUMENT);

    c.role = BL_ROLE_RESPONDER;
    assert(bl_session_init(&s, &c) == BL_ERROR_UNSUPPORTED_ROLE);
    c.role = 7u;
    assert(bl_session_init(&s, &c) == BL_ERROR_ARGUMENT);

    cfg_default(&c, tx, sizeof(tx));
    c.delivery = BORROWLINK_DELIVERY_REALTIME;
    assert(bl_session_init(&s, &c) == BL_ERROR_UNSUPPORTED_DELIVERY);
    c.delivery = 9u;
    assert(bl_session_init(&s, &c) == BL_ERROR_UNSUPPORTED_DELIVERY);

    cfg_default(&c, tx, sizeof(tx));
    c.node_id = 0u;
    assert(bl_session_init(&s, &c) == BL_ERROR_MALFORMED);
    cfg_default(&c, tx, sizeof(tx));
    c.profile = BORROWLINK_PROFILE_PRESENCE;
    assert(bl_session_init(&s, &c) == BL_ERROR_UNSUPPORTED_PROFILE);
    c.profile = 0x40u;
    assert(bl_session_init(&s, &c) == BL_ERROR_UNSUPPORTED_PROFILE);
    cfg_default(&c, tx, sizeof(tx));
    c.max_rx_payload = BORROWLINK_ACCEPT_PAYLOAD_SIZE - 1u;
    assert(bl_session_init(&s, &c) == BL_ERROR_ARGUMENT);
    c.max_rx_payload = BORROWLINK_ACCEPT_PAYLOAD_SIZE;
    assert(bl_session_init(&s, &c) == BL_OK);
    cfg_default(&c, tx, sizeof(tx));
    c.tx_buf = NULL;
    assert(bl_session_init(&s, &c) == BL_ERROR_ARGUMENT);
    cfg_default(&c, tx, 17u);
    assert(bl_session_init(&s, &c) == BL_ERROR_BUFFER_TOO_SMALL);
    cfg_default(&c, tx, 18u);
    assert(bl_session_init(&s, &c) == BL_OK);
}

/* ------------------------------------------------------------ handshake */

static void test_start_emits_hello_golden(void)
{
    static const uint8_t expected[] = {
        0x00, 0x00, 0x10,
        0x01, 0x03, 0x00, 0x12, 0x34,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x00, 0x40
    };
    fx f;

    fx_init(&f);
    assert(do_start(&f, XID, 5u) == BL_OK);
    assert(f.a.type == BL_ACT_SEND_FRAME);
    assert(f.a.channel == BL_CHANNEL_CONTROL);
    assert(f.a.opcode == BORROWLINK_OPCODE_HELLO);
    assert(f.a.size == sizeof(expected));
    assert(memcmp(f.a.data, expected, sizeof(expected)) == 0);
    assert(f.s.phase == BL_PHASE_HELLO_SENT);
    assert(f.s.tx_seq == 1u && f.s.rx_seq == 0u);
    done_quiet(&f);
}

static void test_start_rules(void)
{
    fx f;

    fx_init(&f);
    assert(do_start(&f, 0u, 0u) == BL_ERROR_ARGUMENT);
    assert(f.s.phase == BL_PHASE_IDLE && f.a.type == BL_ACT_NONE);
    assert(do_start(&f, XID, 0u) == BL_OK);
    /* Anything but a completion report is refused while an action is out. */
    assert(do_start(&f, XID, 0u) == BL_ERROR_BUSY);
    assert(do_tx(&f, NULL, 0u, 0, 0) == BL_ERROR_BUSY);
    assert(do_simple(&f, BL_EV_TICK) == BL_ERROR_BUSY);
    done_quiet(&f);
    assert(do_start(&f, XID, 0u) == BL_ERROR_STATE);
    /* ACTION_DONE with nothing outstanding is a caller error. */
    assert(do_simple(&f, BL_EV_ACTION_DONE) == BL_ERROR_STATE);
    assert(do_simple(&f, BL_EV_ACTION_FAILED) == BL_ERROR_STATE);
}

static void test_accept_opens_session(void)
{
    fx f;
    uint8_t fr[64];
    size_t n;

    fx_init(&f);
    assert(do_start(&f, XID, 0u) == BL_OK);
    done_quiet(&f);
    n = mk_accept(fr, XID, BORROWLINK_DELIVERY_RELIABLE, PEER_NODE, 0x0123u);
    assert(do_rx(&f, BL_CHANNEL_CONTROL, fr, n) == BL_OK);
    assert(f.a.type == BL_ACT_SESSION_OPEN);
    assert(f.a.peer_node_id == PEER_NODE && f.a.peer_max_rx == 0x0123u);
    assert(f.a.xid == XID && f.a.delivery == BORROWLINK_DELIVERY_RELIABLE);
    assert(f.s.phase == BL_PHASE_ACTIVE && f.s.rx_seq == 1u);
    /* The peer's limit (291) is clamped to what tx_buf can hold. */
    assert(f.s.tx_chunk_max == sizeof(f.tx) - 3u);
    done_quiet(&f);
}

static void test_accept_golden_frame(void)
{
    static const uint8_t accept_frame[] = {
        0x00, 0x00, 0x11,
        0x01, 0x00, 0x12, 0x34,
        0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
        0x00, 0x20
    };
    fx f;

    fx_init(&f);
    assert(do_start(&f, XID, 0u) == BL_OK);
    done_quiet(&f);
    assert(do_rx(&f, BL_CHANNEL_CONTROL, accept_frame,
                 sizeof(accept_frame)) == BL_OK);
    assert(f.a.type == BL_ACT_SESSION_OPEN);
    assert(f.a.peer_node_id == PEER_NODE && f.a.peer_max_rx == 0x20u);
}

static void test_accept_rejections(void)
{
    fx f;
    uint8_t fr[64];
    size_t n;

    /* wrong xid */
    fx_init(&f);
    assert(do_start(&f, XID, 0u) == BL_OK);
    done_quiet(&f);
    n = mk_accept(fr, 0x4321u, BORROWLINK_DELIVERY_RELIABLE, PEER_NODE, 64u);
    assert(do_rx(&f, BL_CHANNEL_CONTROL, fr, n) == BL_OK);
    expect_local_reset(&f, BORROWLINK_RESET_PROTOCOL_ERROR, BL_ERR_PROTOCOL, 1u);

    /* delivery upgrade: we asked RELIABLE, peer answers REALTIME */
    fx_init(&f);
    assert(do_start(&f, XID, 0u) == BL_OK);
    done_quiet(&f);
    n = mk_accept(fr, XID, BORROWLINK_DELIVERY_REALTIME, PEER_NODE, 64u);
    assert(do_rx(&f, BL_CHANNEL_CONTROL, fr, n) == BL_OK);
    expect_local_reset(&f, BORROWLINK_RESET_PROTOCOL_ERROR, BL_ERR_PROTOCOL, 1u);

    /* truncated ACCEPT payload */
    fx_init(&f);
    assert(do_start(&f, XID, 0u) == BL_OK);
    done_quiet(&f);
    n = mk_accept(fr, XID, BORROWLINK_DELIVERY_RELIABLE, PEER_NODE, 64u);
    n = mk_frame(fr, 0u, BORROWLINK_OPCODE_ACCEPT, fr + 3, 13u);
    assert(do_rx(&f, BL_CHANNEL_CONTROL, fr, n) == BL_OK);
    expect_local_reset(&f, BORROWLINK_RESET_PROTOCOL_ERROR, BL_ERR_PROTOCOL, 1u);

    /* ACCEPT with a zero max_rx_payload / zero node id */
    fx_init(&f);
    assert(do_start(&f, XID, 0u) == BL_OK);
    done_quiet(&f);
    memset(fr, 0, sizeof(fr));
    fr[2] = BORROWLINK_OPCODE_ACCEPT;
    fr[3] = 1u; fr[5] = 0x12u; fr[6] = 0x34u; /* node id and max_rx stay 0 */
    assert(do_rx(&f, BL_CHANNEL_CONTROL, fr, 3u + 14u) == BL_OK);
    expect_local_reset(&f, BORROWLINK_RESET_PROTOCOL_ERROR, BL_ERR_PROTOCOL, 1u);

    /* node_id bound to the bond (5.2) does not match */
    {
        uint8_t tx[64];
        bl_session_config c;

        memset(&f, 0, sizeof(f));
        cfg_default(&c, tx, sizeof(tx));
        c.expected_peer_node_id = UINT64_C(0xdeadbeef);
        assert(bl_session_init(&f.s, &c) == BL_OK);
        assert(do_start(&f, XID, 0u) == BL_OK);
        done_quiet(&f);
        n = mk_accept(fr, XID, BORROWLINK_DELIVERY_RELIABLE, PEER_NODE, 64u);
        assert(do_rx(&f, BL_CHANNEL_CONTROL, fr, n) == BL_OK);
        assert(f.a.type == BL_ACT_SEND_FRAME);
        assert(f.a.reset_reason == BORROWLINK_RESET_SECURITY_ERROR);
        assert(do_simple(&f, BL_EV_ACTION_DONE) == BL_OK);
        assert(f.a.type == BL_ACT_SESSION_RESET &&
               f.a.error == BL_ERR_SECURITY);
        done_quiet(&f);
        /* ...and a matching one passes */
        memset(&f, 0, sizeof(f));
        c.expected_peer_node_id = PEER_NODE;
        assert(bl_session_init(&f.s, &c) == BL_OK);
        assert(do_start(&f, XID, 0u) == BL_OK);
        done_quiet(&f);
        assert(do_rx(&f, BL_CHANNEL_CONTROL, fr, n) == BL_OK);
        assert(f.a.type == BL_ACT_SESSION_OPEN);
    }
}

static void test_handshake_frame_errors(void)
{
    fx f;
    uint8_t fr[64];
    size_t n;

    /* ACCEPT with seq 1 instead of 0 */
    fx_init(&f);
    assert(do_start(&f, XID, 0u) == BL_OK);
    done_quiet(&f);
    n = mk_accept(fr, XID, BORROWLINK_DELIVERY_RELIABLE, PEER_NODE, 64u);
    fr[1] = 1u;
    assert(do_rx(&f, BL_CHANNEL_CONTROL, fr, n) == BL_OK);
    expect_local_reset(&f, BORROWLINK_RESET_PROTOCOL_ERROR, BL_ERR_PROTOCOL, 1u);

    /* ACCEPT on the wrong characteristic */
    fx_init(&f);
    assert(do_start(&f, XID, 0u) == BL_OK);
    done_quiet(&f);
    n = mk_accept(fr, XID, BORROWLINK_DELIVERY_RELIABLE, PEER_NODE, 64u);
    assert(do_rx(&f, BL_CHANNEL_RELIABLE, fr, n) == BL_OK);
    expect_local_reset(&f, BORROWLINK_RESET_PROTOCOL_ERROR, BL_ERR_PROTOCOL, 1u);

    /* reserved opcode */
    fx_init(&f);
    assert(do_start(&f, XID, 0u) == BL_OK);
    done_quiet(&f);
    fr[0] = 0u; fr[1] = 0u; fr[2] = 0x14u;
    assert(do_rx(&f, BL_CHANNEL_CONTROL, fr, 3u) == BL_OK);
    expect_local_reset(&f, BORROWLINK_RESET_UNSUPPORTED_OPCODE,
                       BL_ERR_PROTOCOL, 1u);

    /* a frame shorter than its header */
    fx_init(&f);
    assert(do_start(&f, XID, 0u) == BL_OK);
    done_quiet(&f);
    assert(do_rx(&f, BL_CHANNEL_CONTROL, fr, 2u) == BL_OK);
    expect_local_reset(&f, BORROWLINK_RESET_PROTOCOL_ERROR, BL_ERR_PROTOCOL, 1u);

    /* DATA before ACCEPT */
    fx_init(&f);
    assert(do_start(&f, XID, 0u) == BL_OK);
    done_quiet(&f);
    n = mk_frame(fr, 0u, BORROWLINK_OPCODE_DATA_END_MESSAGE, NULL, 0u);
    assert(do_rx(&f, BL_CHANNEL_RELIABLE, fr, n) == BL_OK);
    expect_local_reset(&f, BORROWLINK_RESET_PROTOCOL_ERROR, BL_ERR_PROTOCOL, 1u);

    /* the realtime characteristic is not usable in P2 */
    fx_init(&f);
    assert(do_start(&f, XID, 0u) == BL_OK);
    done_quiet(&f);
    n = mk_frame(fr, 0u, BORROWLINK_OPCODE_DATA_END_MESSAGE, NULL, 0u);
    assert(do_rx(&f, BL_CHANNEL_REALTIME, fr, n) == BL_OK);
    expect_local_reset(&f, BORROWLINK_RESET_PROTOCOL_ERROR, BL_ERR_PROTOCOL, 1u);
}

static void test_handshake_peer_reset_and_simultaneous_hello(void)
{
    fx f;
    uint8_t fr[64];
    uint8_t reason = BORROWLINK_RESET_UNSUPPORTED_PROFILE;
    size_t n;

    fx_init(&f);
    assert(do_start(&f, XID, 0u) == BL_OK);
    done_quiet(&f);
    n = mk_frame(fr, 0u, BORROWLINK_OPCODE_RESET, &reason, 1u);
    assert(do_rx(&f, BL_CHANNEL_CONTROL, fr, n) == BL_OK);
    assert(f.a.type == BL_ACT_SESSION_RESET && f.a.local == 0u);
    assert(f.a.reset_reason == BORROWLINK_RESET_UNSUPPORTED_PROFILE);
    assert(f.a.error == BL_ERR_PROTOCOL && f.s.phase == BL_PHASE_IDLE);
    done_quiet(&f);

    /* A RESET without a reason byte counts as PROTOCOL_ERROR. */
    fx_init(&f);
    assert(do_start(&f, XID, 0u) == BL_OK);
    done_quiet(&f);
    n = mk_frame(fr, 0u, BORROWLINK_OPCODE_RESET, NULL, 0u);
    assert(do_rx(&f, BL_CHANNEL_CONTROL, fr, n) == BL_OK);
    assert(f.a.type == BL_ACT_SESSION_RESET &&
           f.a.reset_reason == BORROWLINK_RESET_PROTOCOL_ERROR);
    done_quiet(&f);

    /* Host HELLO while ours is outstanding: ignored (ours wins, 8). */
    fx_init(&f);
    assert(do_start(&f, XID, 0u) == BL_OK);
    done_quiet(&f);
    {
        uint8_t body[BORROWLINK_HELLO_PAYLOAD_SIZE];
        bl_hello h;

        h.version = BORROWLINK_PROTOCOL_VERSION;
        h.profile = BORROWLINK_PROFILE_MESSAGE;
        h.delivery = BORROWLINK_DELIVERY_RELIABLE;
        h.xid = 0x7777u;
        h.node_id = PEER_NODE;
        h.max_rx_payload = 64u;
        assert(bl_hello_encode(body, sizeof(body), &h) == BL_OK);
        n = mk_frame(fr, 0u, BORROWLINK_OPCODE_HELLO, body, sizeof(body));
    }
    assert(do_rx(&f, BL_CHANNEL_CONTROL, fr, n) == BL_OK);
    assert(f.a.type == BL_ACT_NONE && f.s.phase == BL_PHASE_HELLO_SENT);
    assert(f.s.rx_seq == 0u);
    n = mk_accept(fr, XID, BORROWLINK_DELIVERY_RELIABLE, PEER_NODE, 64u);
    assert(do_rx(&f, BL_CHANNEL_CONTROL, fr, n) == BL_OK);
    assert(f.a.type == BL_ACT_SESSION_OPEN);
}

static void test_handshake_timeout(void)
{
    uint8_t tx[64];
    bl_session_config c;
    fx f;

    memset(&f, 0, sizeof(f));
    cfg_default(&c, tx, sizeof(tx));
    c.handshake_timeout_ms = 1000u;
    assert(bl_session_init(&f.s, &c) == BL_OK);
    /* a tick before START changes nothing */
    {
        bl_event e = ev_of(BL_EV_TICK);

        e.now_ms = 99999u;
        assert(step(&f, &e) == BL_OK && f.a.type == BL_ACT_NONE);
        /* START close to the 32-bit wrap */
        assert(do_start(&f, XID, 0xfffffe00u) == BL_OK);
        done_quiet(&f);
        e.now_ms = 0xfffffe00u + 999u;
        assert(step(&f, &e) == BL_OK && f.a.type == BL_ACT_NONE);
        e.now_ms = 0xfffffe00u + 1000u; /* wraps past 0 */
        assert(step(&f, &e) == BL_OK);
        assert(f.a.type == BL_ACT_SESSION_RESET);
        assert(f.a.error == BL_ERR_TIMEOUT && f.a.local == 1u);
        assert(f.s.phase == BL_PHASE_IDLE && f.s.last_error == BL_ERR_TIMEOUT);
        done_quiet(&f);
    }
    /* once ACTIVE, ticks are inert */
    memset(&f, 0, sizeof(f));
    assert(bl_session_init(&f.s, &c) == BL_OK);
    {
        uint8_t fr[64];
        size_t n;
        bl_event e = ev_of(BL_EV_TICK);

        assert(do_start(&f, XID, 0u) == BL_OK);
        done_quiet(&f);
        n = mk_accept(fr, XID, BORROWLINK_DELIVERY_RELIABLE, PEER_NODE, 64u);
        assert(do_rx(&f, BL_CHANNEL_CONTROL, fr, n) == BL_OK);
        done_quiet(&f);
        e.now_ms = 50000u;
        assert(step(&f, &e) == BL_OK && f.a.type == BL_ACT_NONE);
        assert(f.s.phase == BL_PHASE_ACTIVE);
    }
}

/* ------------------------------------------------------------ transmit */

static void test_tx_chunking_and_boundaries(void)
{
    static const uint8_t msg[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    fx f;
    bl_frame_view v;
    size_t off = 0u;
    uint16_t seq = 1u;

    fx_init(&f);
    open_session(&f, 4u);
    while (off < sizeof(msg)) {
        assert(do_tx(&f, msg + off, sizeof(msg) - off, 1, 0) == BL_OK);
        assert(f.a.type == BL_ACT_SEND_FRAME);
        assert(f.a.channel == BL_CHANNEL_RELIABLE);
        assert(bl_frame_decode(&v, f.a.data, f.a.size) == BL_OK);
        assert(v.seq == seq);
        assert(f.a.consumed == v.payload_size);
        assert(memcmp(v.payload, msg + off, v.payload_size) == 0);
        if (off + f.a.consumed < sizeof(msg)) {
            assert(v.opcode == BORROWLINK_OPCODE_DATA && v.payload_size == 4u);
            assert(f.s.tx_msg_open == 1u);
        } else {
            assert(v.opcode == BORROWLINK_OPCODE_DATA_END_MESSAGE);
            assert(v.payload_size == 2u && f.s.tx_msg_open == 0u);
        }
        off += f.a.consumed;
        seq++;
        done_quiet(&f);
    }
}

static void test_tx_opcodes_and_zero_length(void)
{
    static const uint8_t b[] = {0xaa};
    fx f;
    bl_frame_view v;

    fx_init(&f);
    open_session(&f, 64u);
    assert(do_tx(&f, NULL, 0u, 1, 0) == BL_OK); /* empty message */
    assert(bl_frame_decode(&v, f.a.data, f.a.size) == BL_OK);
    assert(v.opcode == BORROWLINK_OPCODE_DATA_END_MESSAGE &&
           v.payload_size == 0u && v.seq == 1u && f.a.consumed == 0u);
    assert(f.a.size == 3u);
    done_quiet(&f);
    assert(do_tx(&f, b, 1u, 0, 0) == BL_OK);
    assert(bl_frame_decode(&v, f.a.data, f.a.size) == BL_OK);
    assert(v.opcode == BORROWLINK_OPCODE_DATA && v.payload_size == 1u);
    done_quiet(&f);
    assert(do_tx(&f, b, 1u, 1, 1) == BL_OK);
    assert(bl_frame_decode(&v, f.a.data, f.a.size) == BL_OK);
    assert(v.opcode == BORROWLINK_OPCODE_DATA_END_MESSAGE_STREAM);
    assert(f.s.tx_ended == 1u);
    done_quiet(&f);
    /* Nothing more may be sent after the direction is closed. */
    assert(do_tx(&f, b, 1u, 0, 0) == BL_ERROR_STATE);
    assert(f.a.type == BL_ACT_NONE);
}

static void test_tx_rules(void)
{
    static const uint8_t b[] = {1};
    fx f;

    fx_init(&f);
    assert(do_tx(&f, b, 1u, 1, 0) == BL_ERROR_STATE); /* IDLE */
    assert(do_start(&f, XID, 0u) == BL_OK);
    done_quiet(&f);
    assert(do_tx(&f, b, 1u, 1, 0) == BL_ERROR_STATE); /* HELLO_SENT */
    fx_init(&f);
    open_session(&f, 64u);
    assert(do_tx(&f, NULL, 5u, 1, 0) == BL_ERROR_ARGUMENT);
    assert(f.s.tx_seq == 1u); /* a refused event consumes no sequence number */
}

static void test_tx_chunk_limited_by_tx_buffer(void)
{
    uint8_t tx[20];
    uint8_t big[64];
    bl_session_config c;
    fx f;
    bl_frame_view v;

    memset(&f, 0, sizeof(f));
    memset(big, 7, sizeof(big));
    cfg_default(&c, tx, sizeof(tx));
    assert(bl_session_init(&f.s, &c) == BL_OK);
    {
        uint8_t fr[64];
        size_t n;

        assert(do_start(&f, XID, 0u) == BL_OK);
        done_quiet(&f);
        n = mk_accept(fr, XID, BORROWLINK_DELIVERY_RELIABLE, PEER_NODE, 1000u);
        assert(do_rx(&f, BL_CHANNEL_CONTROL, fr, n) == BL_OK);
        done_quiet(&f);
    }
    assert(f.s.tx_chunk_max == sizeof(tx) - 3u);
    assert(do_tx(&f, big, sizeof(big), 1, 1) == BL_OK);
    assert(f.a.consumed == sizeof(tx) - 3u);
    assert(bl_frame_decode(&v, f.a.data, f.a.size) == BL_OK);
    assert(v.opcode == BORROWLINK_OPCODE_DATA); /* flags wait for the last chunk */
    assert(f.a.size <= sizeof(tx));
}

static void test_tx_seq_wraps(void)
{
    fx f;
    bl_frame_view v;

    fx_init(&f);
    open_session(&f, 64u);
    f.s.tx_seq = 0xffffu;
    assert(do_tx(&f, NULL, 0u, 1, 0) == BL_OK);
    assert(bl_frame_decode(&v, f.a.data, f.a.size) == BL_OK && v.seq == 0xffffu);
    done_quiet(&f);
    assert(do_tx(&f, NULL, 0u, 1, 0) == BL_OK);
    assert(bl_frame_decode(&v, f.a.data, f.a.size) == BL_OK && v.seq == 0u);
}

/* ------------------------------------------------------------ receive */

static size_t data_frame(uint8_t *out, uint16_t seq, uint8_t op,
                         const uint8_t *p, size_t n)
{
    return mk_frame(out, seq, op, p, n);
}

static void test_rx_data_and_boundaries(void)
{
    static const uint8_t a[] = {1, 2, 3};
    static const uint8_t b[] = {4, 5};
    uint8_t fr[64];
    fx f;
    size_t n;

    fx_init(&f);
    open_session(&f, 64u);
    n = data_frame(fr, 1u, BORROWLINK_OPCODE_DATA, a, sizeof(a));
    assert(do_rx(&f, BL_CHANNEL_RELIABLE, fr, n) == BL_OK);
    assert(f.a.type == BL_ACT_DELIVER_DATA && f.a.size == 3u);
    assert(f.a.end_message == 0u && f.a.end_stream == 0u);
    assert(memcmp(f.a.data, a, 3u) == 0);
    assert(f.s.rx_msg_open == 1u);
    done_quiet(&f);
    n = data_frame(fr, 2u, BORROWLINK_OPCODE_DATA_END_MESSAGE, b, sizeof(b));
    assert(do_rx(&f, BL_CHANNEL_RELIABLE, fr, n) == BL_OK);
    assert(f.a.type == BL_ACT_DELIVER_DATA && f.a.end_message == 1u);
    assert(f.s.rx_msg_open == 0u && f.s.rx_msg_bytes == 0u);
    done_quiet(&f);
    /* zero-length message */
    n = data_frame(fr, 3u, BORROWLINK_OPCODE_DATA_END_MESSAGE, NULL, 0u);
    assert(do_rx(&f, BL_CHANNEL_RELIABLE, fr, n) == BL_OK);
    assert(f.a.type == BL_ACT_DELIVER_DATA && f.a.size == 0u &&
           f.a.end_message == 1u);
    done_quiet(&f);
}

static void test_rx_seq_checks(void)
{
    uint8_t fr[64];
    fx f;
    size_t n;

    /* gap */
    fx_init(&f);
    open_session(&f, 64u);
    n = data_frame(fr, 2u, BORROWLINK_OPCODE_DATA_END_MESSAGE, NULL, 0u);
    assert(do_rx(&f, BL_CHANNEL_RELIABLE, fr, n) == BL_OK);
    expect_local_reset(&f, BORROWLINK_RESET_PROTOCOL_ERROR, BL_ERR_PROTOCOL, 1u);

    /* duplicate / old frame */
    fx_init(&f);
    open_session(&f, 64u);
    n = data_frame(fr, 1u, BORROWLINK_OPCODE_DATA_END_MESSAGE, NULL, 0u);
    assert(do_rx(&f, BL_CHANNEL_RELIABLE, fr, n) == BL_OK);
    done_quiet(&f);
    assert(do_rx(&f, BL_CHANNEL_RELIABLE, fr, n) == BL_OK); /* same seq again */
    expect_local_reset(&f, BORROWLINK_RESET_PROTOCOL_ERROR, BL_ERR_PROTOCOL, 1u);

    /* wrap 0xffff -> 0 */
    fx_init(&f);
    open_session(&f, 64u);
    f.s.rx_seq = 0xffffu;
    n = data_frame(fr, 0xffffu, BORROWLINK_OPCODE_DATA_END_MESSAGE, NULL, 0u);
    assert(do_rx(&f, BL_CHANNEL_RELIABLE, fr, n) == BL_OK);
    assert(f.a.type == BL_ACT_DELIVER_DATA);
    done_quiet(&f);
    n = data_frame(fr, 0u, BORROWLINK_OPCODE_DATA_END_MESSAGE, NULL, 0u);
    assert(do_rx(&f, BL_CHANNEL_RELIABLE, fr, n) == BL_OK);
    assert(f.a.type == BL_ACT_DELIVER_DATA && f.s.rx_seq == 1u);
}

static void test_rx_protocol_violations_when_active(void)
{
    uint8_t fr[64];
    uint8_t body[BORROWLINK_HELLO_PAYLOAD_SIZE] = {0};
    fx f;
    size_t n;

    /* DATA on the Control characteristic */
    fx_init(&f);
    open_session(&f, 64u);
    n = data_frame(fr, 1u, BORROWLINK_OPCODE_DATA, NULL, 0u);
    assert(do_rx(&f, BL_CHANNEL_CONTROL, fr, n) == BL_OK);
    expect_local_reset(&f, BORROWLINK_RESET_PROTOCOL_ERROR, BL_ERR_PROTOCOL, 1u);

    /* STATUS on the Reliable Data characteristic */
    fx_init(&f);
    open_session(&f, 64u);
    n = mk_frame(fr, 1u, BORROWLINK_OPCODE_STATUS, body, 1u);
    assert(do_rx(&f, BL_CHANNEL_RELIABLE, fr, n) == BL_OK);
    expect_local_reset(&f, BORROWLINK_RESET_PROTOCOL_ERROR, BL_ERR_PROTOCOL, 1u);

    /* HELLO / ACCEPT in the middle of a session */
    fx_init(&f);
    open_session(&f, 64u);
    n = mk_frame(fr, 1u, BORROWLINK_OPCODE_HELLO, body, sizeof(body));
    assert(do_rx(&f, BL_CHANNEL_CONTROL, fr, n) == BL_OK);
    expect_local_reset(&f, BORROWLINK_RESET_PROTOCOL_ERROR, BL_ERR_PROTOCOL, 1u);
    fx_init(&f);
    open_session(&f, 64u);
    n = mk_accept(fr, XID, BORROWLINK_DELIVERY_RELIABLE, PEER_NODE, 64u);
    fr[1] = 1u; /* seq 1 */
    assert(do_rx(&f, BL_CHANNEL_CONTROL, fr, n) == BL_OK);
    expect_local_reset(&f, BORROWLINK_RESET_PROTOCOL_ERROR, BL_ERR_PROTOCOL, 1u);

    /* empty STATUS */
    fx_init(&f);
    open_session(&f, 64u);
    n = mk_frame(fr, 1u, BORROWLINK_OPCODE_STATUS, NULL, 0u);
    assert(do_rx(&f, BL_CHANNEL_CONTROL, fr, n) == BL_OK);
    expect_local_reset(&f, BORROWLINK_RESET_PROTOCOL_ERROR, BL_ERR_PROTOCOL, 1u);

    /* reserved opcode */
    fx_init(&f);
    open_session(&f, 64u);
    fr[0] = 0u; fr[1] = 1u; fr[2] = 0xf0u;
    assert(do_rx(&f, BL_CHANNEL_CONTROL, fr, 3u) == BL_OK);
    expect_local_reset(&f, BORROWLINK_RESET_UNSUPPORTED_OPCODE,
                       BL_ERR_PROTOCOL, 1u);
}

static void test_rx_status_and_peer_reset(void)
{
    static const uint8_t status[] = {0x00, 0x41, 0x42};
    uint8_t fr[64];
    uint8_t reason = BORROWLINK_RESET_NO_NETWORK;
    fx f;
    size_t n;

    fx_init(&f);
    open_session(&f, 64u);
    n = mk_frame(fr, 1u, BORROWLINK_OPCODE_STATUS, status, sizeof(status));
    assert(do_rx(&f, BL_CHANNEL_CONTROL, fr, n) == BL_OK);
    assert(f.a.type == BL_ACT_STATUS && f.a.size == 3u &&
           memcmp(f.a.data, status, 3u) == 0);
    assert(f.s.phase == BL_PHASE_ACTIVE);
    done_quiet(&f);
    n = mk_frame(fr, 2u, BORROWLINK_OPCODE_RESET, &reason, 1u);
    assert(do_rx(&f, BL_CHANNEL_CONTROL, fr, n) == BL_OK);
    assert(f.a.type == BL_ACT_SESSION_RESET && f.a.local == 0u);
    assert(f.a.reset_reason == BORROWLINK_RESET_NO_NETWORK &&
           f.a.error == BL_ERR_TRANSPORT);
    assert(f.s.phase == BL_PHASE_IDLE);
    done_quiet(&f);
    /* stray frames after the end are ignored */
    n = data_frame(fr, 3u, BORROWLINK_OPCODE_DATA_END_MESSAGE, NULL, 0u);
    assert(do_rx(&f, BL_CHANNEL_RELIABLE, fr, n) == BL_OK);
    assert(f.a.type == BL_ACT_NONE);
}

static void test_rx_limits(void)
{
    uint8_t tx[64];
    uint8_t fr[200];
    uint8_t blob[100];
    bl_session_config c;
    fx f;
    size_t n;

    memset(blob, 1, sizeof(blob));

    /* single frame above max_rx_payload */
    fx_init(&f);
    open_session(&f, 64u);
    n = data_frame(fr, 1u, BORROWLINK_OPCODE_DATA, blob, 65u);
    assert(do_rx(&f, BL_CHANNEL_RELIABLE, fr, n) == BL_OK);
    expect_local_reset(&f, BORROWLINK_RESET_LIMIT_EXCEEDED, BL_ERR_LIMIT, 1u);

    /* exactly max_rx_payload is fine */
    fx_init(&f);
    open_session(&f, 64u);
    n = data_frame(fr, 1u, BORROWLINK_OPCODE_DATA, blob, 64u);
    assert(do_rx(&f, BL_CHANNEL_RELIABLE, fr, n) == BL_OK);
    assert(f.a.type == BL_ACT_DELIVER_DATA);

    /* a logical message above max_message_size, spread over frames */
    memset(&f, 0, sizeof(f));
    cfg_default(&c, tx, sizeof(tx));
    c.max_message_size = 100u;
    assert(bl_session_init(&f.s, &c) == BL_OK);
    {
        uint8_t acc[64];
        size_t an;

        assert(do_start(&f, XID, 0u) == BL_OK);
        done_quiet(&f);
        an = mk_accept(acc, XID, BORROWLINK_DELIVERY_RELIABLE, PEER_NODE, 64u);
        assert(do_rx(&f, BL_CHANNEL_CONTROL, acc, an) == BL_OK);
        done_quiet(&f);
    }
    n = data_frame(fr, 1u, BORROWLINK_OPCODE_DATA, blob, 60u);
    assert(do_rx(&f, BL_CHANNEL_RELIABLE, fr, n) == BL_OK);
    done_quiet(&f);
    n = data_frame(fr, 2u, BORROWLINK_OPCODE_DATA_END_MESSAGE, blob, 40u);
    assert(do_rx(&f, BL_CHANNEL_RELIABLE, fr, n) == BL_OK); /* 100 == limit */
    assert(f.a.type == BL_ACT_DELIVER_DATA);
    done_quiet(&f);
    n = data_frame(fr, 3u, BORROWLINK_OPCODE_DATA, blob, 60u); /* counter reset */
    assert(do_rx(&f, BL_CHANNEL_RELIABLE, fr, n) == BL_OK);
    assert(f.a.type == BL_ACT_DELIVER_DATA);
    done_quiet(&f);
    n = data_frame(fr, 4u, BORROWLINK_OPCODE_DATA, blob, 41u);
    assert(do_rx(&f, BL_CHANNEL_RELIABLE, fr, n) == BL_OK); /* 101 */
    expect_local_reset(&f, BORROWLINK_RESET_LIMIT_EXCEEDED, BL_ERR_LIMIT, 1u);
}

/* ------------------------------------------------------------ completion */

static void close_both(int keep, int local_first)
{
    uint8_t tx[64];
    uint8_t fr[64];
    uint8_t acc[64];
    bl_session_config c;
    fx f;
    size_t n;

    memset(&f, 0, sizeof(f));
    cfg_default(&c, tx, sizeof(tx));
    c.keep_connected = (uint8_t)keep;
    assert(bl_session_init(&f.s, &c) == BL_OK);
    assert(do_start(&f, XID, 0u) == BL_OK);
    done_quiet(&f);
    n = mk_accept(acc, XID, BORROWLINK_DELIVERY_RELIABLE, PEER_NODE, 64u);
    assert(do_rx(&f, BL_CHANNEL_CONTROL, acc, n) == BL_OK);
    done_quiet(&f);
    n = data_frame(fr, 1u, BORROWLINK_OPCODE_DATA_END_STREAM, NULL, 0u);

    if (local_first) {
        assert(do_tx(&f, NULL, 0u, 0, 1) == BL_OK);
        assert(f.a.type == BL_ACT_SEND_FRAME);
        assert(f.a.opcode == BORROWLINK_OPCODE_DATA_END_STREAM);
        assert(do_simple(&f, BL_EV_ACTION_DONE) == BL_OK);
        assert(f.a.type == BL_ACT_NONE && f.s.phase == BL_PHASE_ACTIVE);
        assert(do_rx(&f, BL_CHANNEL_RELIABLE, fr, n) == BL_OK);
        assert(f.a.type == BL_ACT_DELIVER_DATA && f.a.end_stream == 1u);
        assert(f.a.msg_truncated == 0u);
        assert(do_simple(&f, BL_EV_ACTION_DONE) == BL_OK);
    } else {
        assert(do_rx(&f, BL_CHANNEL_RELIABLE, fr, n) == BL_OK);
        assert(f.a.type == BL_ACT_DELIVER_DATA && f.a.end_stream == 1u);
        done_quiet(&f);
        assert(f.s.phase == BL_PHASE_ACTIVE && f.s.rx_ended == 1u);
        /* after the peer closed it may still send */
        assert(do_tx(&f, NULL, 0u, 1, 0) == BL_OK);
        done_quiet(&f);
        assert(do_tx(&f, NULL, 0u, 0, 1) == BL_OK);
        assert(do_simple(&f, BL_EV_ACTION_DONE) == BL_OK);
    }
    assert(f.a.type == (keep ? BL_ACT_SET_LINK_IDLE : BL_ACT_DISCONNECT));
    assert(f.s.phase == BL_PHASE_IDLE && f.s.last_error == BL_ERR_NONE &&
           f.s.last_reset_reason == 0u);
    done_quiet(&f);

    /* idle again: a new session starts from sequence 0 */
    assert(do_start(&f, 0x5555u, 0u) == BL_OK);
    assert(f.a.opcode == BORROWLINK_OPCODE_HELLO);
    assert(f.a.data[0] == 0u && f.a.data[1] == 0u);
    assert(f.s.tx_seq == 1u && f.s.rx_seq == 0u && f.s.tx_ended == 0u &&
           f.s.rx_ended == 0u);
}

static void test_completion_returns_to_idle(void)
{
    close_both(1, 1);
    close_both(1, 0);
    close_both(0, 1);
    close_both(0, 0);
}

static void test_data_after_peer_end_is_violation(void)
{
    uint8_t fr[64];
    fx f;
    size_t n;

    fx_init(&f);
    open_session(&f, 64u);
    n = data_frame(fr, 1u, BORROWLINK_OPCODE_DATA_END_STREAM, NULL, 0u);
    assert(do_rx(&f, BL_CHANNEL_RELIABLE, fr, n) == BL_OK);
    done_quiet(&f);
    n = data_frame(fr, 2u, BORROWLINK_OPCODE_DATA, NULL, 0u);
    assert(do_rx(&f, BL_CHANNEL_RELIABLE, fr, n) == BL_OK);
    expect_local_reset(&f, BORROWLINK_RESET_PROTOCOL_ERROR, BL_ERR_PROTOCOL, 1u);
}

static void test_end_stream_inside_a_message_is_flagged(void)
{
    static const uint8_t part[] = {9, 9};
    uint8_t fr[64];
    fx f;
    size_t n;

    fx_init(&f);
    open_session(&f, 64u);
    n = data_frame(fr, 1u, BORROWLINK_OPCODE_DATA_END_STREAM, part, 2u);
    assert(do_rx(&f, BL_CHANNEL_RELIABLE, fr, n) == BL_OK);
    assert(f.a.type == BL_ACT_DELIVER_DATA && f.a.end_stream == 1u);
    assert(f.a.msg_truncated == 1u);
}

/* ------------------------------------------------------------ resets etc. */

static void test_local_reset(void)
{
    fx f;

    fx_init(&f);
    assert(do_simple(&f, BL_EV_LOCAL_RESET) == BL_ERROR_STATE);
    open_session(&f, 64u);
    {
        bl_event e = ev_of(BL_EV_LOCAL_RESET);

        assert(step(&f, &e) == BL_ERROR_ARGUMENT); /* reason 0 is not a reason */
        e.reason = BORROWLINK_RESET_REJECTED;
        assert(step(&f, &e) == BL_OK);
    }
    expect_local_reset(&f, BORROWLINK_RESET_REJECTED, BL_ERR_PROTOCOL, 1u);
    assert(do_tx(&f, NULL, 0u, 1, 0) == BL_ERROR_STATE);

    /* abort during the handshake */
    fx_init(&f);
    assert(do_start(&f, XID, 0u) == BL_OK);
    done_quiet(&f);
    {
        bl_event e = ev_of(BL_EV_LOCAL_RESET);

        e.reason = BORROWLINK_RESET_SECURITY_ERROR;
        assert(step(&f, &e) == BL_OK);
    }
    expect_local_reset(&f, BORROWLINK_RESET_SECURITY_ERROR, BL_ERR_SECURITY, 1u);
}

static void test_action_failed_and_link_lost(void)
{
    uint8_t fr[64];
    fx f;
    size_t n;

    /* transport refuses HELLO */
    fx_init(&f);
    assert(do_start(&f, XID, 0u) == BL_OK);
    assert(do_simple(&f, BL_EV_ACTION_FAILED) == BL_OK);
    assert(f.a.type == BL_ACT_SESSION_RESET && f.a.error == BL_ERR_TRANSPORT);
    assert(f.s.phase == BL_PHASE_IDLE);
    assert(do_simple(&f, BL_EV_ACTION_FAILED) == BL_OK); /* the report itself */
    assert(f.a.type == BL_ACT_NONE);

    /* link lost during the handshake */
    fx_init(&f);
    assert(do_start(&f, XID, 0u) == BL_OK);
    done_quiet(&f);
    assert(do_simple(&f, BL_EV_LINK_LOST) == BL_OK);
    assert(f.a.type == BL_ACT_SESSION_RESET && f.a.local == 0u &&
           f.a.error == BL_ERR_TRANSPORT);
    done_quiet(&f);

    /* link lost while an action is outstanding is allowed */
    fx_init(&f);
    open_session(&f, 64u);
    assert(do_tx(&f, NULL, 0u, 1, 0) == BL_OK);
    assert(do_simple(&f, BL_EV_LINK_LOST) == BL_OK);
    assert(f.a.type == BL_ACT_SESSION_RESET);
    assert(f.s.outstanding == BL_ACT_SESSION_RESET);
    done_quiet(&f);
    /* link lost when idle: nothing to report */
    assert(do_simple(&f, BL_EV_LINK_LOST) == BL_OK);
    assert(f.a.type == BL_ACT_NONE);

    /* a stalled RESET send still ends the session */
    fx_init(&f);
    open_session(&f, 64u);
    n = data_frame(fr, 7u, BORROWLINK_OPCODE_DATA, NULL, 0u);
    assert(do_rx(&f, BL_CHANNEL_RELIABLE, fr, n) == BL_OK);
    assert(f.a.opcode == BORROWLINK_OPCODE_RESET);
    assert(do_simple(&f, BL_EV_ACTION_FAILED) == BL_OK);
    assert(f.a.type == BL_ACT_SESSION_RESET && f.a.error == BL_ERR_TRANSPORT);
}

static void test_idle_behaviour(void)
{
    uint8_t fr[64];
    uint8_t body[BORROWLINK_HELLO_PAYLOAD_SIZE];
    bl_hello h;
    fx f;
    size_t n;

    fx_init(&f);
    /* stray non-HELLO frames are ignored */
    n = data_frame(fr, 0u, BORROWLINK_OPCODE_DATA_END_MESSAGE, NULL, 0u);
    assert(do_rx(&f, BL_CHANNEL_RELIABLE, fr, n) == BL_OK);
    assert(f.a.type == BL_ACT_NONE);
    /* a Host-initiated HELLO is refused: the responder role is reserved */
    h.version = BORROWLINK_PROTOCOL_VERSION;
    h.profile = BORROWLINK_PROFILE_MESSAGE;
    h.delivery = BORROWLINK_DELIVERY_RELIABLE;
    h.xid = 9u;
    h.node_id = PEER_NODE;
    h.max_rx_payload = 64u;
    assert(bl_hello_encode(body, sizeof(body), &h) == BL_OK);
    n = mk_frame(fr, 0u, BORROWLINK_OPCODE_HELLO, body, sizeof(body));
    assert(do_rx(&f, BL_CHANNEL_CONTROL, fr, n) == BL_OK);
    expect_local_reset(&f, BORROWLINK_RESET_REJECTED, BL_ERR_PROTOCOL, 0u);
}

static void test_argument_errors(void)
{
    bl_event e = ev_of(BL_EV_TICK);
    bl_action a;
    fx f;
    uint8_t fr[8] = {0};

    fx_init(&f);
    assert(bl_session_step(NULL, &e, &a) == BL_ERROR_ARGUMENT);
    assert(bl_session_step(&f.s, NULL, &a) == BL_ERROR_ARGUMENT);
    assert(bl_session_step(&f.s, &e, NULL) == BL_ERROR_ARGUMENT);
    e.type = 0u;
    assert(bl_session_step(&f.s, &e, &a) == BL_ERROR_ARGUMENT);
    e.type = 200u;
    assert(bl_session_step(&f.s, &e, &a) == BL_ERROR_ARGUMENT);
    assert(do_rx(&f, 3u, fr, 3u) == BL_ERROR_ARGUMENT);
    assert(do_rx(&f, BL_CHANNEL_CONTROL, NULL, 3u) == BL_ERROR_ARGUMENT);
}

static void test_reset_error_classes(void)
{
    assert(bl_reset_error_class(BORROWLINK_RESET_NO_NETWORK) == BL_ERR_TRANSPORT);
    assert(bl_reset_error_class(BORROWLINK_RESET_UPSTREAM_FAILED) ==
           BL_ERR_TRANSPORT);
    assert(bl_reset_error_class(BORROWLINK_RESET_UPSTREAM_TIMEOUT) ==
           BL_ERR_TIMEOUT);
    assert(bl_reset_error_class(BORROWLINK_RESET_REJECTED) == BL_ERR_PROTOCOL);
    assert(bl_reset_error_class(BORROWLINK_RESET_UNSUPPORTED_PROFILE) ==
           BL_ERR_PROTOCOL);
    assert(bl_reset_error_class(BORROWLINK_RESET_PROTOCOL_ERROR) ==
           BL_ERR_PROTOCOL);
    assert(bl_reset_error_class(BORROWLINK_RESET_BUSY) == BL_ERR_TRANSPORT);
    assert(bl_reset_error_class(BORROWLINK_RESET_SECURITY_ERROR) ==
           BL_ERR_SECURITY);
    assert(bl_reset_error_class(BORROWLINK_RESET_LIMIT_EXCEEDED) == BL_ERR_LIMIT);
    assert(bl_reset_error_class(BORROWLINK_RESET_UNSUPPORTED_OPCODE) ==
           BL_ERR_PROTOCOL);
    assert(bl_reset_error_class(0xeeu) == BL_ERR_PROTOCOL);
}

/* ------------------------------------------------------------ determinism */

static uint32_t rng_state;
static unsigned seen_actions[16];

static uint32_t rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

/* Feeds a pseudo-random event stream, always honouring the ACTION_DONE rule,
 * and records every action as bytes into `log`.  Used twice: the results
 * must be identical (no hidden inputs) and the run must stay clean under the
 * sanitizers. */
static size_t random_run(uint8_t *log, size_t log_size, uint32_t seed,
                         int poison, unsigned rounds)
{
    uint8_t tx[96];
    uint8_t scratch[96];
    uint8_t payload[80];
    bl_session_config c;
    bl_session s;
    bl_action a;
    size_t logn = 0u;
    unsigned i;

    rng_state = seed;
    if (poison) {
        memset(tx, 0xa5, sizeof(tx));
        memset(&s, 0x5a, sizeof(s));
    }
    cfg_default(&c, tx, sizeof(tx));
    c.max_message_size = 200u;
    c.handshake_timeout_ms = 50u;
    c.keep_connected = (uint8_t)(seed & 1u);
    assert(bl_session_init(&s, &c) == BL_OK);

    for (i = 0u; i < rounds; ++i) {
        bl_event e;
        bl_result r;
        size_t n;
        uint32_t pick = rng() % 100u;

        memset(&e, 0, sizeof(e));
        for (n = 0u; n < sizeof(payload); ++n) {
            payload[n] = (uint8_t)rng();
        }
        if (pick < 12u) {
            e.type = BL_EV_START;
            e.xid = (uint16_t)(rng() % 3u);
            e.now_ms = i;
        } else if (pick < 55u) {
            /* a mostly valid frame for the current rx sequence */
            uint8_t ops[] = {0x00, 0x01, 0x02, 0x03, 0x10, 0x11, 0x12, 0x13,
                             0x14};
            uint8_t op = ops[rng() % sizeof(ops)];
            size_t plen = rng() % 30u;
            uint16_t seq = (uint16_t)(s.rx_seq + (rng() % 8u == 0u ? 1u : 0u));

            if (op == BORROWLINK_OPCODE_ACCEPT && rng() % 2u) {
                bl_accept ac;

                ac.version = 1u;
                ac.delivery = 0u;
                ac.xid = s.xid;
                ac.node_id = PEER_NODE;
                ac.max_rx_payload = (uint16_t)(1u + rng() % 90u);
                (void)bl_accept_encode(payload, sizeof(payload), &ac);
                plen = BORROWLINK_ACCEPT_PAYLOAD_SIZE;
            }
            if (op >= 0x14u) { /* reserved opcode: build it by hand */
                scratch[0] = (uint8_t)(seq >> 8);
                scratch[1] = (uint8_t)seq;
                scratch[2] = op;
                memcpy(scratch + 3, payload, plen);
                n = 3u + plen;
            } else {
                assert(bl_frame_encode(scratch, sizeof(scratch), &n, seq, op,
                                       payload, plen) == BL_OK);
            }
            e.type = BL_EV_RX_FRAME;
            e.channel = (uint8_t)(op >= 0x10u ? 0u : 1u);
            if (rng() % 10u == 0u) {
                e.channel = (uint8_t)(rng() % 3u);
            }
            e.data = scratch;
            e.size = (rng() % 20u == 0u) ? (n > 1u ? n - 1u : 0u) : n;
            if (e.size < 3u && rng() % 2u) {
                e.size = 3u;
            }
        } else if (pick < 80u) {
            e.type = BL_EV_TX_DATA;
            e.size = rng() % 100u;
            if (e.size > sizeof(payload)) {
                e.size = sizeof(payload);
            }
            e.data = payload;
            e.end_message = (uint8_t)(rng() % 2u);
            e.end_stream = (uint8_t)(rng() % 5u == 0u);
        } else if (pick < 85u) {
            e.type = BL_EV_LOCAL_RESET;
            e.reason = (uint8_t)(rng() % 0x1au);
        } else if (pick < 90u) {
            e.type = BL_EV_TICK;
            e.now_ms = i + (rng() % 100u);
        } else if (pick < 93u) {
            e.type = BL_EV_LINK_LOST;
        } else if (pick < 97u) {
            e.type = BL_EV_ACTION_FAILED;
        } else {
            e.type = BL_EV_ACTION_DONE;
        }
        r = bl_session_step(&s, &e, &a);
        if (logn + 4u + sizeof(a) < log_size) {
            log[logn++] = (uint8_t)r;
            log[logn++] = a.type;
            log[logn++] = a.reset_reason;
            log[logn++] = a.error;
            if (a.type == BL_ACT_SEND_FRAME) {
                size_t k;

                for (k = 0u; k < a.size && logn < log_size; ++k) {
                    log[logn++] = a.data[k];
                }
            }
        }
        seen_actions[a.type & 15u]++;
        /* invariants */
        assert(s.phase <= BL_PHASE_ACTIVE);
        assert(a.type == BL_ACT_NONE || r == BL_OK);
        if (a.type == BL_ACT_SEND_FRAME) {
            assert(a.size >= 3u && a.size <= sizeof(tx));
        }
        assert(a.type == BL_ACT_NONE || s.outstanding == a.type);
        /* an adapter obeying the rule reports completion right away most of
         * the time; sometimes it lags to exercise BUSY */
        while (s.outstanding != 0u && rng() % 8u != 0u) {
            bl_event d;

            memset(&d, 0, sizeof(d));
            d.type = rng() % 9u == 0u ? BL_EV_ACTION_FAILED : BL_EV_ACTION_DONE;
            r = bl_session_step(&s, &d, &a);
            assert(r == BL_OK);
            seen_actions[a.type & 15u]++;
            if (logn + 3u < log_size) {
                log[logn++] = a.type;
                log[logn++] = a.reset_reason;
                log[logn++] = a.error;
            }
        }
    }
    return logn;
}

static void test_deterministic_and_robust(void)
{
    static uint8_t log1[400000];
    static uint8_t log2[400000];
    size_t n1;
    size_t n2;
    uint32_t seed;

    for (seed = 1u; seed <= 6u; ++seed) {
        n1 = random_run(log1, sizeof(log1), seed * 2654435761u, 0, 20000u);
        n2 = random_run(log2, sizeof(log2), seed * 2654435761u, 1, 20000u);
        assert(n1 == n2 && n1 > 1000u);
        assert(memcmp(log1, log2, n1) == 0);
    }
    /* the random walk really reached every kind of action */
    for (n1 = BL_ACT_SEND_FRAME; n1 <= BL_ACT_DISCONNECT; ++n1) {
        assert(seen_actions[n1] > 0u);
    }
}

int main(void)
{
    test_init_validation();
    test_start_emits_hello_golden();
    test_start_rules();
    test_accept_opens_session();
    test_accept_golden_frame();
    test_accept_rejections();
    test_handshake_frame_errors();
    test_handshake_peer_reset_and_simultaneous_hello();
    test_handshake_timeout();
    test_tx_chunking_and_boundaries();
    test_tx_opcodes_and_zero_length();
    test_tx_rules();
    test_tx_chunk_limited_by_tx_buffer();
    test_tx_seq_wraps();
    test_rx_data_and_boundaries();
    test_rx_seq_checks();
    test_rx_protocol_violations_when_active();
    test_rx_status_and_peer_reset();
    test_rx_limits();
    test_completion_returns_to_idle();
    test_data_after_peer_end_is_violation();
    test_end_stream_inside_a_message_is_flagged();
    test_local_reset();
    test_action_failed_and_link_lost();
    test_idle_behaviour();
    test_argument_errors();
    test_reset_error_classes();
    test_deterministic_and_robust();
    return 0;
}
