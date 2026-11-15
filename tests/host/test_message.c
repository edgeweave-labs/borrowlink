#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <borrowlink/message.h>

/* Golden vectors, built by hand from PROTOCOL.md section 11.1 and
 * cross-checked byte for byte against the EdgeWeave Python implementation
 * (tests/interop in this repository). */
static const uint8_t G_SUBSCRIBE[] = {
    0x01, 0x00, 0x01,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x01, 0x00, 0x00, 0x3c, 0x01
};
static const uint8_t G_UNSUBSCRIBE[] = {
    0x02, 0x00, 0x01,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x01, 0x00
};
static const uint8_t G_PUBLISH[] = {
    0x03, 0x00, 0x02, 0x01, 0x00, 0xde, 0xad
};
static const uint8_t G_DELIVER[] = {
    0x04, 0x00, 0x03,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x01, 0x00, 0xbe, 0xef
};
static const uint8_t G_ACK[] = {0x05, 0x00, 0x02, 0x00};

static bl_msg_header hdr(uint8_t cmd)
{
    bl_msg_header h;

    memset(&h, 0, sizeof(h));
    h.cmd = cmd;
    return h;
}

static void test_golden_encode_decode(void)
{
    uint8_t out[32];
    size_t w = 0u;
    bl_msg_header h = hdr(BORROWLINK_MSG_SUBSCRIBE);
    bl_msg_header d;

    h.id = 1u;
    h.source_node_id = UINT64_C(0x0102030405060708);
    h.topic = 0x0100u;
    h.lease_seconds = 60u;
    h.queue_policy = BORROWLINK_QUEUE_LATEST;
    assert(bl_msg_header_encode(out, sizeof(out), &w, &h) == BL_OK);
    assert(w == sizeof(G_SUBSCRIBE) && memcmp(out, G_SUBSCRIBE, w) == 0);
    assert(bl_msg_header_decode(&d, G_SUBSCRIBE, sizeof(G_SUBSCRIBE)) == BL_OK);
    assert(memcmp(&d, &h, sizeof(h)) == 0);

    h = hdr(BORROWLINK_MSG_UNSUBSCRIBE);
    h.id = 1u;
    h.source_node_id = UINT64_C(0x0102030405060708);
    h.topic = 0x0100u;
    assert(bl_msg_header_encode(out, sizeof(out), &w, &h) == BL_OK);
    assert(w == sizeof(G_UNSUBSCRIBE) && memcmp(out, G_UNSUBSCRIBE, w) == 0);
    assert(bl_msg_header_decode(&d, G_UNSUBSCRIBE, sizeof(G_UNSUBSCRIBE)) ==
           BL_OK);
    assert(memcmp(&d, &h, sizeof(h)) == 0);

    h = hdr(BORROWLINK_MSG_PUBLISH);
    h.id = 2u;
    h.topic = 0x0100u;
    assert(bl_msg_header_encode(out, sizeof(out), &w, &h) == BL_OK);
    assert(w == BORROWLINK_MSG_PUBLISH_HEADER_SIZE &&
           memcmp(out, G_PUBLISH, w) == 0);
    /* a PUBLISH header decodes from the front of a longer buffer */
    assert(bl_msg_header_decode(&d, G_PUBLISH, sizeof(G_PUBLISH)) == BL_OK);
    assert(memcmp(&d, &h, sizeof(h)) == 0);

    h = hdr(BORROWLINK_MSG_DELIVER);
    h.id = 3u;
    h.source_node_id = UINT64_C(0x1112131415161718);
    h.topic = 0x0100u;
    assert(bl_msg_header_encode(out, sizeof(out), &w, &h) == BL_OK);
    assert(w == BORROWLINK_MSG_DELIVER_HEADER_SIZE &&
           memcmp(out, G_DELIVER, w) == 0);
    assert(bl_msg_header_decode(&d, G_DELIVER, sizeof(G_DELIVER)) == BL_OK);
    assert(memcmp(&d, &h, sizeof(h)) == 0);

    h = hdr(BORROWLINK_MSG_ACK);
    h.id = 2u;
    h.status = BORROWLINK_ACK_ACCEPTED;
    assert(bl_msg_header_encode(out, sizeof(out), &w, &h) == BL_OK);
    assert(w == sizeof(G_ACK) && memcmp(out, G_ACK, w) == 0);
    assert(bl_msg_header_decode(&d, G_ACK, sizeof(G_ACK)) == BL_OK);
    assert(memcmp(&d, &h, sizeof(h)) == 0);
}

static void test_header_sizes(void)
{
    assert(bl_msg_header_size(BORROWLINK_MSG_SUBSCRIBE) == 16u);
    assert(bl_msg_header_size(BORROWLINK_MSG_UNSUBSCRIBE) == 13u);
    assert(bl_msg_header_size(BORROWLINK_MSG_PUBLISH) == 5u);
    assert(bl_msg_header_size(BORROWLINK_MSG_DELIVER) == 13u);
    assert(bl_msg_header_size(BORROWLINK_MSG_ACK) == 4u);
    assert(bl_msg_header_size(0x00u) == 0u);
    assert(bl_msg_header_size(0x06u) == 0u);
    assert(bl_msg_header_size(0xffu) == 0u);
}

static void test_encode_rejections(void)
{
    uint8_t out[32];
    uint8_t small[4];
    size_t w = 99u;
    bl_msg_header h = hdr(BORROWLINK_MSG_SUBSCRIBE);

    h.queue_policy = 3u;
    assert(bl_msg_header_encode(out, sizeof(out), &w, &h) == BL_ERROR_MALFORMED);
    h.queue_policy = 2u;
    assert(bl_msg_header_encode(out, sizeof(out), &w, &h) == BL_OK);
    assert(bl_msg_header_encode(out, 15u, &w, &h) == BL_ERROR_BUFFER_TOO_SMALL);
    assert(bl_msg_header_encode(out, 16u, &w, &h) == BL_OK);
    h = hdr(BORROWLINK_MSG_ACK);
    h.status = 5u;
    assert(bl_msg_header_encode(out, sizeof(out), &w, &h) == BL_ERROR_MALFORMED);
    h = hdr(0x09u);
    assert(bl_msg_header_encode(out, sizeof(out), &w, &h) == BL_ERROR_MALFORMED);
    h = hdr(BORROWLINK_MSG_ACK);
    assert(bl_msg_header_encode(small, sizeof(small), &w, &h) == BL_OK);
    assert(bl_msg_header_encode(small, 3u, &w, &h) == BL_ERROR_BUFFER_TOO_SMALL);
    assert(bl_msg_header_encode(NULL, 4u, &w, &h) == BL_ERROR_ARGUMENT);
    assert(bl_msg_header_encode(small, 4u, NULL, &h) == BL_ERROR_ARGUMENT);
    assert(bl_msg_header_encode(small, 4u, &w, NULL) == BL_ERROR_ARGUMENT);
}

static void test_decode_rejections(void)
{
    bl_msg_header h;
    uint8_t buf[32];
    size_t n;

    /* truncated at every length below the header size, for every command */
    for (n = 0u; n < sizeof(G_SUBSCRIBE); ++n) {
        assert(bl_msg_header_decode(&h, G_SUBSCRIBE, n) == BL_ERROR_MALFORMED);
    }
    for (n = 0u; n < sizeof(G_UNSUBSCRIBE); ++n) {
        assert(bl_msg_header_decode(&h, G_UNSUBSCRIBE, n) == BL_ERROR_MALFORMED);
    }
    for (n = 0u; n < BORROWLINK_MSG_PUBLISH_HEADER_SIZE; ++n) {
        assert(bl_msg_header_decode(&h, G_PUBLISH, n) == BL_ERROR_MALFORMED);
    }
    for (n = 0u; n < BORROWLINK_MSG_DELIVER_HEADER_SIZE; ++n) {
        assert(bl_msg_header_decode(&h, G_DELIVER, n) == BL_ERROR_MALFORMED);
    }
    for (n = 0u; n < sizeof(G_ACK); ++n) {
        assert(bl_msg_header_decode(&h, G_ACK, n) == BL_ERROR_MALFORMED);
    }
    /* trailing bytes on fixed-size commands */
    memcpy(buf, G_ACK, sizeof(G_ACK));
    buf[4] = 0u;
    assert(bl_msg_header_decode(&h, buf, 5u) == BL_ERROR_MALFORMED);
    memcpy(buf, G_SUBSCRIBE, sizeof(G_SUBSCRIBE));
    buf[16] = 0u;
    assert(bl_msg_header_decode(&h, buf, 17u) == BL_ERROR_MALFORMED);
    /* reserved commands and out-of-range enums */
    buf[0] = 0x00u;
    assert(bl_msg_header_decode(&h, buf, 16u) == BL_ERROR_MALFORMED);
    buf[0] = 0x06u;
    assert(bl_msg_header_decode(&h, buf, 16u) == BL_ERROR_MALFORMED);
    memcpy(buf, G_SUBSCRIBE, sizeof(G_SUBSCRIBE));
    buf[15] = 3u;
    assert(bl_msg_header_decode(&h, buf, 16u) == BL_ERROR_MALFORMED);
    memcpy(buf, G_ACK, sizeof(G_ACK));
    buf[3] = 5u;
    assert(bl_msg_header_decode(&h, buf, 4u) == BL_ERROR_MALFORMED);
    assert(bl_msg_header_decode(NULL, buf, 4u) == BL_ERROR_ARGUMENT);
    assert(bl_msg_header_decode(&h, NULL, 4u) == BL_ERROR_ARGUMENT);
}

/* ------------------------------------------------------------ parser */

typedef struct {
    int headers;
    int payloads;
    int ends;
    bl_msg_header header;
    uint8_t payload[64];
    size_t payload_size;
} seen;

/* Feeds `msg` in pieces of `step` bytes, the last piece carrying end_message. */
static bl_result run_parser(bl_msg_parser *p, const uint8_t *msg, size_t n,
                            size_t step, seen *out)
{
    size_t pos = 0u;

    memset(out, 0, sizeof(*out));
    do {
        size_t piece = n - pos < step ? n - pos : step;
        int end = pos + piece == n;
        size_t off = 0u;

        for (;;) {
            bl_msg_event ev;
            size_t used = 0u;
            bl_result r = bl_msg_parser_feed(p, msg + pos + off, piece - off,
                                             end, &used, &ev);

            if (r != BL_OK) {
                return r;
            }
            off += used;
            if (ev.kind == BL_MSG_EV_HEADER) {
                out->headers++;
                out->header = ev.header;
            } else if (ev.kind == BL_MSG_EV_PAYLOAD) {
                assert(out->payload_size + ev.size <= sizeof(out->payload));
                memcpy(out->payload + out->payload_size, ev.data, ev.size);
                out->payload_size += ev.size;
                out->payloads++;
            } else if (ev.kind == BL_MSG_EV_END) {
                out->ends++;
                break;
            } else {
                break;
            }
        }
        pos += piece;
    } while (pos < n);
    return BL_OK;
}

static void test_parser_every_split(void)
{
    static const struct {
        const uint8_t *msg;
        size_t n;
        uint8_t cmd;
        size_t payload;
    } cases[] = {
        {G_SUBSCRIBE, sizeof(G_SUBSCRIBE), BORROWLINK_MSG_SUBSCRIBE, 0u},
        {G_UNSUBSCRIBE, sizeof(G_UNSUBSCRIBE), BORROWLINK_MSG_UNSUBSCRIBE, 0u},
        {G_PUBLISH, sizeof(G_PUBLISH), BORROWLINK_MSG_PUBLISH, 2u},
        {G_DELIVER, sizeof(G_DELIVER), BORROWLINK_MSG_DELIVER, 2u},
        {G_ACK, sizeof(G_ACK), BORROWLINK_MSG_ACK, 0u},
    };
    size_t c;
    size_t step;

    for (c = 0u; c < sizeof(cases) / sizeof(cases[0]); ++c) {
        for (step = 1u; step <= cases[c].n; ++step) {
            bl_msg_parser p;
            seen s;

            bl_msg_parser_init(&p);
            assert(run_parser(&p, cases[c].msg, cases[c].n, step, &s) == BL_OK);
            assert(s.headers == 1 && s.ends == 1);
            assert(s.header.cmd == cases[c].cmd);
            assert(s.payload_size == cases[c].payload);
            if (cases[c].payload != 0u) {
                assert(memcmp(s.payload, cases[c].msg + cases[c].n - 2u, 2u) ==
                       0);
            }
        }
    }
}

static void test_parser_back_to_back_messages(void)
{
    bl_msg_parser p;
    seen s;

    bl_msg_parser_init(&p);
    assert(run_parser(&p, G_PUBLISH, sizeof(G_PUBLISH), 3u, &s) == BL_OK);
    assert(s.ends == 1);
    assert(run_parser(&p, G_ACK, sizeof(G_ACK), 2u, &s) == BL_OK);
    assert(s.ends == 1 && s.header.cmd == BORROWLINK_MSG_ACK);
    assert(run_parser(&p, G_DELIVER, sizeof(G_DELIVER), 100u, &s) == BL_OK);
    assert(s.ends == 1 && s.header.cmd == BORROWLINK_MSG_DELIVER);
}

static void test_parser_empty_payload_and_large_payload(void)
{
    static uint8_t big[5u + 40u];
    bl_msg_parser p;
    seen s;
    size_t i;

    /* PUBLISH with no payload at all */
    bl_msg_parser_init(&p);
    assert(run_parser(&p, G_PUBLISH, 5u, 5u, &s) == BL_OK);
    assert(s.headers == 1 && s.payloads == 0 && s.ends == 1);

    /* payload delivered as slices while the message is still open */
    memcpy(big, G_PUBLISH, 5u);
    for (i = 5u; i < sizeof(big); ++i) {
        big[i] = (uint8_t)i;
    }
    bl_msg_parser_init(&p);
    assert(run_parser(&p, big, sizeof(big), 7u, &s) == BL_OK);
    assert(s.payload_size == 40u && s.ends == 1 && s.payloads >= 5);
    assert(memcmp(s.payload, big + 5, 40u) == 0);
}

static void test_parser_malformed(void)
{
    bl_msg_parser p;
    bl_msg_event ev;
    size_t used = 0u;
    uint8_t buf[20];
    seen s;
    size_t n;

    /* empty message */
    bl_msg_parser_init(&p);
    assert(bl_msg_parser_feed(&p, NULL, 0u, 1, &used, &ev) == BL_ERROR_MALFORMED);
    /* dead until re-initialised */
    assert(bl_msg_parser_feed(&p, G_ACK, 4u, 1, &used, &ev) == BL_ERROR_STATE);
    bl_msg_parser_init(&p);
    /* zero bytes without end: nothing happens */
    assert(bl_msg_parser_feed(&p, NULL, 0u, 0, &used, &ev) == BL_OK);
    assert(ev.kind == BL_MSG_EV_NONE && used == 0u);

    /* unknown command */
    buf[0] = 0x00u;
    assert(bl_msg_parser_feed(&p, buf, 1u, 0, &used, &ev) == BL_ERROR_MALFORMED);
    bl_msg_parser_init(&p);
    buf[0] = 0x2au;
    assert(bl_msg_parser_feed(&p, buf, 1u, 1, &used, &ev) == BL_ERROR_MALFORMED);

    /* ended inside the header, for every command and every cut */
    for (n = 1u; n < sizeof(G_SUBSCRIBE); ++n) {
        bl_msg_parser_init(&p);
        assert(run_parser(&p, G_SUBSCRIBE, n, n, &s) == BL_ERROR_MALFORMED);
    }
    for (n = 1u; n < sizeof(G_DELIVER) - 2u; ++n) {
        bl_msg_parser_init(&p);
        assert(run_parser(&p, G_DELIVER, n, n, &s) == BL_ERROR_MALFORMED);
    }
    /* extra bytes after a fixed-size command */
    memcpy(buf, G_ACK, 4u);
    buf[4] = 1u;
    bl_msg_parser_init(&p);
    assert(run_parser(&p, buf, 5u, 5u, &s) == BL_ERROR_MALFORMED);
    memcpy(buf, G_UNSUBSCRIBE, sizeof(G_UNSUBSCRIBE));
    buf[sizeof(G_UNSUBSCRIBE)] = 1u;
    bl_msg_parser_init(&p);
    assert(run_parser(&p, buf, sizeof(G_UNSUBSCRIBE) + 1u, 100u, &s) ==
           BL_ERROR_MALFORMED);
    /* bad enum values */
    memcpy(buf, G_SUBSCRIBE, sizeof(G_SUBSCRIBE));
    buf[15] = 9u;
    bl_msg_parser_init(&p);
    assert(run_parser(&p, buf, sizeof(G_SUBSCRIBE), 4u, &s) == BL_ERROR_MALFORMED);
    memcpy(buf, G_ACK, 4u);
    buf[3] = 0x7fu;
    bl_msg_parser_init(&p);
    assert(run_parser(&p, buf, 4u, 4u, &s) == BL_ERROR_MALFORMED);
    /* arguments */
    bl_msg_parser_init(&p);
    assert(bl_msg_parser_feed(NULL, buf, 1u, 0, &used, &ev) == BL_ERROR_ARGUMENT);
    assert(bl_msg_parser_feed(&p, NULL, 1u, 0, &used, &ev) == BL_ERROR_ARGUMENT);
    assert(bl_msg_parser_feed(&p, buf, 1u, 0, NULL, &ev) == BL_ERROR_ARGUMENT);
    assert(bl_msg_parser_feed(&p, buf, 1u, 0, &used, NULL) == BL_ERROR_ARGUMENT);
}

/* ------------------------------------------------------------ client ids */

static void test_client_ids(void)
{
    uint16_t store[3];
    bl_msg_client c;
    uint16_t a;
    uint16_t b;
    uint16_t d;
    uint16_t e;

    assert(bl_msg_client_init(&c, store, 3u) == BL_OK);
    assert(bl_msg_client_init(&c, NULL, 3u) == BL_ERROR_ARGUMENT);
    assert(bl_msg_client_init(&c, store, 0u) == BL_ERROR_ARGUMENT);
    assert(bl_msg_client_init(&c, store, 3u) == BL_OK);
    assert(bl_msg_client_alloc_id(&c, &a) == BL_OK && a == 1u);
    assert(bl_msg_client_alloc_id(&c, &b) == BL_OK && b == 2u);
    assert(bl_msg_client_alloc_id(&c, &d) == BL_OK && d == 3u);
    assert(bl_msg_client_is_pending(&c, 2u));
    /* full: a fourth outstanding id is refused */
    assert(bl_msg_client_alloc_id(&c, &e) == BL_ERROR_LIMIT_EXCEEDED);
    assert(bl_msg_client_release_id(&c, 2u) == BL_OK);
    assert(!bl_msg_client_is_pending(&c, 2u));
    assert(bl_msg_client_release_id(&c, 2u) == BL_ERROR_UNKNOWN_ID);
    assert(bl_msg_client_release_id(&c, 99u) == BL_ERROR_UNKNOWN_ID);
    assert(bl_msg_client_alloc_id(&c, &e) == BL_OK && e == 4u); /* keeps counting up */
    assert(bl_msg_client_release_id(&c, 1u) == BL_OK);
    assert(bl_msg_client_release_id(&c, 3u) == BL_OK);
    assert(bl_msg_client_release_id(&c, 4u) == BL_OK);
    assert(c.count == 0u);
}

static void test_client_ids_skip_zero_pending_and_wrap(void)
{
    uint16_t store[4];
    bl_msg_client c;
    uint16_t id;

    assert(bl_msg_client_init(&c, store, 4u) == BL_OK);
    c.next = 0xfffeu;
    assert(bl_msg_client_alloc_id(&c, &id) == BL_OK && id == 0xfffeu);
    assert(bl_msg_client_alloc_id(&c, &id) == BL_OK && id == 0xffffu);
    assert(bl_msg_client_alloc_id(&c, &id) == BL_OK && id == 1u); /* 0 skipped */
    /* an id that is still pending is never reused after the counter wraps */
    c.next = 0xffffu;
    assert(bl_msg_client_alloc_id(&c, &id) == BL_OK && id == 2u);
    assert(bl_msg_client_is_pending(&c, 0xffffu));
    assert(c.count == 4u);
    assert(bl_msg_client_alloc_id(&c, &id) == BL_ERROR_LIMIT_EXCEEDED);
    assert(bl_msg_client_alloc_id(NULL, &id) == BL_ERROR_ARGUMENT);
    assert(bl_msg_client_alloc_id(&c, NULL) == BL_ERROR_ARGUMENT);
    assert(bl_msg_client_release_id(NULL, 1u) == BL_ERROR_ARGUMENT);
}

int main(void)
{
    test_golden_encode_decode();
    test_header_sizes();
    test_encode_rejections();
    test_decode_rejections();
    test_parser_every_split();
    test_parser_back_to_back_messages();
    test_parser_empty_payload_and_large_payload();
    test_parser_malformed();
    test_client_ids();
    test_client_ids_skip_zero_pending_and_wrap();
    return 0;
}
