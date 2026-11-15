#define _POSIX_C_SOURCE 200809L
/*
 * borrowlink-linux-node: the reference node for the Linux adapter, and the C
 * end of the EdgeWeave interop test.  It is also the shape of the P3 firmware
 * main loop, with the socket and clock swapped for ESP-IDF ones.
 *
 * It connects to an EdgeWeave Hub over the BorrowLink stream binding and:
 *   1. opens a session (HELLO / ACCEPT, Message profile, RELIABLE);
 *   2. publishes a capability manifest (topic 0x0001) with
 *      sense.temperature and actuator.led;
 *   3. publishes one temperature data point (topic 0x0100);
 *   4. answers `--rpcs N` RPC calls on actuator.led/set (DELIVER on 0x0002,
 *      reply PUBLISH on 0x0003);
 *   5. closes its direction and waits for the Hub to do the same.
 *
 * Exit status: 0 success, 2 usage, 3 timeout, 4 protocol/transport failure.
 */
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bl_driver.h>

#include "cbor_min.h"
#include "linux_adapter.h"

#define TOPIC_MANIFEST 0x0001u
#define TOPIC_RPC_REQUEST 0x0002u
#define TOPIC_RPC_REPLY 0x0003u
#define TOPIC_TEMP_VALUE 0x0100u

#define MY_MAX_RX 512u
#define MAX_RPC_PAYLOAD 1024u

typedef struct {
    int fd;
    bl_driver drv;
    uint8_t tx[MY_MAX_RX + 3u];
    uint8_t wire[MY_MAX_RX + 6u];
    uint8_t rx[MY_MAX_RX + 3u];
    uint16_t pending[8];
    bl_msg_client client;
    /* application */
    int opened;
    int finished;
    uint8_t end_type;
    uint8_t end_reason;
    uint8_t end_error;
    uint64_t peer_node_id;
    uint16_t peer_max_rx;
    /* the DELIVER being assembled / ready */
    bl_msg_header cur;
    uint8_t cur_payload[MAX_RPC_PAYLOAD];
    size_t cur_len;
    int cur_overflow;
    int deliver_ready;
    /* settings */
    int verbose;
} node;

static void say(const node *n, const char *fmt, ...)
{
    va_list ap;

    if (!n->verbose) {
        return;
    }
    fputs("node: ", stdout);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fputc('\n', stdout);
    fflush(stdout);
}

/* ------------------------------------------------------------ driver hooks */

static int hook_send(void *ctx, const uint8_t *bytes, size_t n)
{
    return la_send_all(((node *)ctx)->fd, bytes, n);
}

static void hook_open(void *ctx, const bl_action *a)
{
    node *nd = (node *)ctx;

    nd->opened = 1;
    nd->peer_node_id = a->peer_node_id;
    nd->peer_max_rx = a->peer_max_rx;
}

static void hook_msg(void *ctx, const bl_msg_event *ev)
{
    node *nd = (node *)ctx;

    switch (ev->kind) {
    case BL_MSG_EV_HEADER:
        nd->cur = ev->header;
        nd->cur_len = 0u;
        nd->cur_overflow = 0;
        break;
    case BL_MSG_EV_PAYLOAD:
        if (nd->cur_len + ev->size > sizeof(nd->cur_payload)) {
            nd->cur_overflow = 1;
        } else {
            memcpy(nd->cur_payload + nd->cur_len, ev->data, ev->size);
            nd->cur_len += ev->size;
        }
        break;
    case BL_MSG_EV_END:
        if (nd->cur.cmd == BORROWLINK_MSG_ACK) {
            (void)bl_msg_client_release_id(&nd->client, nd->cur.id);
            say(nd, "ack id=%u status=%u", (unsigned)nd->cur.id,
                (unsigned)nd->cur.status);
        } else if (nd->cur.cmd == BORROWLINK_MSG_DELIVER) {
            nd->deliver_ready = 1;
        }
        break;
    default:
        break;
    }
}

static void hook_end(void *ctx, const bl_action *a)
{
    node *nd = (node *)ctx;

    nd->finished = 1;
    nd->end_type = a->type;
    nd->end_reason = a->reset_reason;
    nd->end_error = a->error;
}

/* ------------------------------------------------------------ I/O pumping */

/* Read once from the socket (up to timeout_ms) and feed the driver.
 * Returns 0, LA_TIMEOUT, or -1 on failure/EOF. */
static int pump_once(node *nd, int timeout_ms)
{
    uint8_t buf[1024];
    int n = la_recv(nd->fd, buf, sizeof(buf), timeout_ms);

    if (n == LA_TIMEOUT) {
        return LA_TIMEOUT;
    }
    if (n == 0) {
        (void)bl_driver_link_closed(&nd->drv);
        return -1;
    }
    if (n < 0) {
        (void)bl_driver_link_lost(&nd->drv);
        return -1;
    }
    if (bl_driver_feed(&nd->drv, buf, (size_t)n) != BL_OK && !nd->finished) {
        return -1;
    }
    return 0;
}

static int wait_until(node *nd, int (*done)(const node *), int timeout_ms)
{
    uint32_t start = la_now_ms();

    while (!done(nd)) {
        uint32_t used = la_now_ms() - start;
        int r;

        if (nd->finished) {
            return -1;
        }
        if (used >= (uint32_t)timeout_ms) {
            return LA_TIMEOUT;
        }
        r = pump_once(nd, (int)((uint32_t)timeout_ms - used));
        if (r == -1) {
            return -1;
        }
        (void)bl_driver_tick(&nd->drv, la_now_ms());
    }
    return 0;
}

static int is_open(const node *nd) { return nd->opened; }
static int is_done(const node *nd) { return nd->finished; }
static int has_deliver(const node *nd) { return nd->deliver_ready; }

static uint16_t g_wait_id;
static int id_acked(const node *nd)
{
    return !bl_msg_client_is_pending(&nd->client, g_wait_id);
}

/* ------------------------------------------------------------ messages */

/* Streams `header` then `payload` as ONE logical message.  The payload is
 * given in `piece`-byte slices to show that no whole-message buffer is
 * needed; the driver splits again to fit the peer's max_rx_payload. */
static int send_message(node *nd, const bl_msg_header *h, const uint8_t *payload,
                        size_t n, size_t piece, uint16_t *out_id)
{
    uint8_t hb[BORROWLINK_MSG_HEADER_MAX];
    bl_msg_header hh = *h;
    size_t hw = 0u;
    size_t off = 0u;
    uint16_t id = 0u;

    if (hh.cmd == BORROWLINK_MSG_PUBLISH) {
        if (bl_msg_client_alloc_id(&nd->client, &id) != BL_OK) {
            return -1;
        }
        hh.id = id;
    }
    if (out_id != NULL) {
        *out_id = hh.id;
    }
    if (bl_msg_header_encode(hb, sizeof(hb), &hw, &hh) != BL_OK) {
        return -1;
    }
    if (bl_driver_send(&nd->drv, hb, hw, n == 0u, 0) != BL_OK) {
        return -1;
    }
    while (off < n) {
        size_t p = n - off < piece ? n - off : piece;

        if (bl_driver_send(&nd->drv, payload + off, p, off + p == n, 0) !=
            BL_OK) {
            return -1;
        }
        off += p;
    }
    return 0;
}

static int publish_and_wait(node *nd, uint16_t topic, const uint8_t *payload,
                            size_t n, int timeout_ms)
{
    bl_msg_header h;
    uint16_t id = 0u;

    memset(&h, 0, sizeof(h));
    h.cmd = BORROWLINK_MSG_PUBLISH;
    h.topic = topic;
    if (send_message(nd, &h, payload, n, 64u, &id) != 0) {
        return -1;
    }
    g_wait_id = id;
    return wait_until(nd, id_acked, timeout_ms);
}

static int send_ack(node *nd, uint16_t id, uint8_t status)
{
    bl_msg_header h;

    memset(&h, 0, sizeof(h));
    h.cmd = BORROWLINK_MSG_ACK;
    h.id = id;
    h.status = status;
    return send_message(nd, &h, NULL, 0u, 1u, NULL);
}

/* ------------------------------------------------------------ payloads */

static size_t build_manifest(uint8_t *buf, size_t cap, const char *token,
                             const char *name, uint64_t node_id,
                             const char *boot_id, const char *board)
{
    cbor_w w;

    cbor_w_init(&w, buf, cap);
    cbor_w_map(&w, 6u);
    cbor_w_text(&w, "auth");
    cbor_w_text(&w, token);
    cbor_w_text(&w, "name");
    cbor_w_text(&w, name);
    cbor_w_text(&w, "node_id");
    cbor_w_uint(&w, node_id);
    cbor_w_text(&w, "boot_id");
    cbor_w_text(&w, boot_id);
    cbor_w_text(&w, "board");
    cbor_w_text(&w, board);
    cbor_w_text(&w, "caps");
    cbor_w_array(&w, 2u);
    /* sense.temperature: one data point "value" on topic 0x0100 */
    cbor_w_map(&w, 5u);
    cbor_w_text(&w, "id");
    cbor_w_text(&w, "temp");
    cbor_w_text(&w, "kind");
    cbor_w_text(&w, "sense.temperature");
    cbor_w_text(&w, "spec");
    cbor_w_map(&w, 0u);
    cbor_w_text(&w, "data");
    cbor_w_map(&w, 1u);
    cbor_w_text(&w, "value");
    cbor_w_uint(&w, TOPIC_TEMP_VALUE);
    cbor_w_text(&w, "methods");
    cbor_w_array(&w, 0u);
    /* actuator.led: method "set" */
    cbor_w_map(&w, 5u);
    cbor_w_text(&w, "id");
    cbor_w_text(&w, "led");
    cbor_w_text(&w, "kind");
    cbor_w_text(&w, "actuator.led");
    cbor_w_text(&w, "spec");
    cbor_w_map(&w, 1u);
    cbor_w_text(&w, "colors");
    cbor_w_array(&w, 1u);
    cbor_w_text(&w, "on");
    cbor_w_text(&w, "data");
    cbor_w_map(&w, 0u);
    cbor_w_text(&w, "methods");
    cbor_w_array(&w, 1u);
    cbor_w_text(&w, "set");
    return w.overflow ? 0u : w.len;
}

typedef struct {
    uint64_t id;
    char cap[32];
    char method[32];
    char color[16];
    int on;
    int args_ok;
} rpc_req;

/* Copies at most cap-1 bytes; longer text is cut (the node only needs a short
 * colour name, but must still read past the rest of the message). */
static int copy_text(char *dst, size_t cap, const char *s, size_t n)
{
    size_t keep = n < cap - 1u ? n : cap - 1u;

    memcpy(dst, s, keep);
    dst[keep] = '\0';
    return 0;
}

static int parse_args(cbor_r *r, rpc_req *q)
{
    int64_t pairs = cbor_r_map(r);
    int64_t i;

    if (pairs < 0) {
        return -1;
    }
    for (i = 0; i < pairs; ++i) {
        const char *k;
        size_t kl;

        if (cbor_r_text(r, &k, &kl) != 0) {
            return -1;
        }
        if (cbor_text_equals(k, kl, "color")) {
            const char *v;
            size_t vl;

            if (cbor_r_text(r, &v, &vl) != 0 ||
                copy_text(q->color, sizeof(q->color), v, vl) != 0) {
                return -1;
            }
        } else if (cbor_text_equals(k, kl, "on")) {
            if (cbor_r_bool(r, &q->on) != 0) {
                return -1;
            }
        } else if (cbor_r_skip(r) != 0) {
            return -1;
        }
    }
    q->args_ok = 1;
    return 0;
}

static int parse_rpc(const uint8_t *p, size_t n, rpc_req *q)
{
    cbor_r r;
    int64_t pairs;
    int64_t i;

    memset(q, 0, sizeof(*q));
    cbor_r_init(&r, p, n);
    pairs = cbor_r_map(&r);
    if (pairs < 0) {
        return -1;
    }
    for (i = 0; i < pairs; ++i) {
        const char *k;
        size_t kl;
        const char *v;
        size_t vl;
        uint64_t u;

        if (cbor_r_text(&r, &k, &kl) != 0) {
            return -1;
        }
        if (cbor_text_equals(k, kl, "id")) {
            if (cbor_r_uint(&r, &q->id) != 0) {
                return -1;
            }
        } else if (cbor_text_equals(k, kl, "cap")) {
            if (cbor_r_text(&r, &v, &vl) != 0 ||
                copy_text(q->cap, sizeof(q->cap), v, vl) != 0) {
                return -1;
            }
        } else if (cbor_text_equals(k, kl, "method")) {
            if (cbor_r_text(&r, &v, &vl) != 0 ||
                copy_text(q->method, sizeof(q->method), v, vl) != 0) {
                return -1;
            }
        } else if (cbor_text_equals(k, kl, "args")) {
            if (parse_args(&r, q) != 0) {
                return -1;
            }
        } else if (cbor_text_equals(k, kl, "to")) {
            if (cbor_r_uint(&r, &u) != 0) {
                return -1;
            }
        } else if (cbor_r_skip(&r) != 0) {
            return -1;
        }
    }
    return 0;
}

/* RpcReply: {id, ok, result?|error?} */
static size_t build_reply(uint8_t *buf, size_t cap, uint64_t id, int ok,
                          const char *error)
{
    cbor_w w;

    cbor_w_init(&w, buf, cap);
    cbor_w_map(&w, 3u);
    cbor_w_text(&w, "id");
    cbor_w_uint(&w, id);
    cbor_w_text(&w, "ok");
    cbor_w_bool(&w, ok);
    if (ok) {
        cbor_w_text(&w, "result");
        cbor_w_map(&w, 1u);
        cbor_w_text(&w, "ok");
        cbor_w_bool(&w, 1);
    } else {
        cbor_w_text(&w, "error");
        cbor_w_text(&w, error);
    }
    return w.overflow ? 0u : w.len;
}

/* ------------------------------------------------------------ main */

static void usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s --port N --token T [--host H] [--name NAME]\n"
            "          [--node-id HEX] [--board B] [--temp C] [--rpcs N]\n"
            "          [--timeout-ms MS] [--quiet]\n",
            argv0);
}

int main(int argc, char **argv)
{
    static node nd;
    const char *host = "127.0.0.1";
    const char *token = "";
    const char *name = "c-node";
    const char *board = "linux-reference";
    uint64_t node_id = 0u;
    double temp = 23.5;
    int port = 0;
    int rpcs = 1;
    int timeout_ms = 10000;
    int i;
    uint8_t manifest[768];
    uint8_t payload[128];
    char boot_id[17];
    uint8_t rnd[8];
    bl_driver_config cfg;
    bl_driver_ops ops;
    size_t mlen;
    uint16_t xid;
    int rc = 4;

    memset(&nd, 0, sizeof(nd));
    nd.verbose = 1;
    for (i = 1; i < argc; ++i) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;

        if (strcmp(a, "--quiet") == 0) {
            nd.verbose = 0;
        } else if (v == NULL) {
            usage(argv[0]);
            return 2;
        } else if (strcmp(a, "--host") == 0) {
            host = v; ++i;
        } else if (strcmp(a, "--port") == 0) {
            port = atoi(v); ++i;
        } else if (strcmp(a, "--token") == 0) {
            token = v; ++i;
        } else if (strcmp(a, "--name") == 0) {
            name = v; ++i;
        } else if (strcmp(a, "--board") == 0) {
            board = v; ++i;
        } else if (strcmp(a, "--node-id") == 0) {
            node_id = strtoull(v, NULL, 16); ++i;
        } else if (strcmp(a, "--temp") == 0) {
            temp = atof(v); ++i;
        } else if (strcmp(a, "--rpcs") == 0) {
            rpcs = atoi(v); ++i;
        } else if (strcmp(a, "--timeout-ms") == 0) {
            timeout_ms = atoi(v); ++i;
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (port <= 0 || port > 65535) {
        usage(argv[0]);
        return 2;
    }
    if (la_random(rnd, sizeof(rnd)) != 0) {
        return 4;
    }
    if (node_id == 0u) { /* "provisioning": a real node stores this in NVS */
        memcpy(&node_id, rnd, sizeof(node_id));
        node_id |= 1u;
    }
    for (i = 0; i < 8; ++i) {
        (void)snprintf(boot_id + i * 2, 3, "%02x", (unsigned)rnd[i]);
    }
    if (la_random(&xid, sizeof(xid)) != 0) {
        return 4;
    }
    if (xid == 0u) {
        xid = 1u;
    }

    nd.fd = la_connect(host, port);
    if (nd.fd < 0) {
        fprintf(stderr, "node: cannot connect to %s:%d\n", host, port);
        return 4;
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.session.role = BL_ROLE_INITIATOR;
    cfg.session.profile = BORROWLINK_PROFILE_MESSAGE;
    cfg.session.delivery = BORROWLINK_DELIVERY_RELIABLE;
    cfg.session.keep_connected = 0u; /* TCP: nothing to keep */
    cfg.session.node_id = node_id;
    cfg.session.max_rx_payload = MY_MAX_RX;
    cfg.session.max_message_size = MAX_RPC_PAYLOAD + 64u;
    cfg.session.handshake_timeout_ms = 5000u;
    cfg.tx_buf = nd.tx;
    cfg.tx_buf_size = sizeof(nd.tx);
    cfg.wire_buf = nd.wire;
    cfg.wire_buf_size = sizeof(nd.wire);
    cfg.rx_buf = nd.rx;
    cfg.rx_buf_size = sizeof(nd.rx);
    memset(&ops, 0, sizeof(ops));
    ops.send = hook_send;
    ops.on_open = hook_open;
    ops.on_msg = hook_msg;
    ops.on_end = hook_end;
    if (bl_driver_init(&nd.drv, &cfg, &ops, &nd) != BL_OK ||
        bl_msg_client_init(&nd.client, nd.pending, 8u) != BL_OK) {
        fprintf(stderr, "node: driver init failed\n");
        return 4;
    }

    say(&nd, "node_id=0x%016" PRIx64 " boot_id=%s", node_id, boot_id);
    if (bl_driver_start(&nd.drv, xid, la_now_ms()) != BL_OK) {
        rc = 4;
        goto out;
    }
    if (wait_until(&nd, is_open, 5000) != 0) {
        fprintf(stderr, "node: handshake failed (finished=%d reason=0x%02x)\n",
                nd.finished, (unsigned)nd.end_reason);
        rc = nd.finished ? 4 : 3;
        goto out;
    }
    say(&nd, "session open: hub=0x%016" PRIx64 " peer_max_rx=%u",
        nd.peer_node_id, (unsigned)nd.peer_max_rx);

    mlen = build_manifest(manifest, sizeof(manifest), token, name, node_id,
                          boot_id, board);
    if (mlen == 0u ||
        publish_and_wait(&nd, TOPIC_MANIFEST, manifest, mlen, 5000) != 0) {
        fprintf(stderr, "node: manifest was not acknowledged\n");
        rc = nd.finished ? 4 : 3;
        goto out;
    }
    say(&nd, "manifest published (%zu bytes)", mlen);

    {
        cbor_w w;

        cbor_w_init(&w, payload, sizeof(payload));
        cbor_w_f64(&w, temp);
        if (publish_and_wait(&nd, TOPIC_TEMP_VALUE, payload, w.len, 5000) != 0) {
            fprintf(stderr, "node: data point was not acknowledged\n");
            rc = nd.finished ? 4 : 3;
            goto out;
        }
        say(&nd, "data point published (sense.temperature value=%.2f)", temp);
    }

    for (i = 0; i < rpcs; ++i) {
        rpc_req q;
        size_t rl;
        int ok;
        const char *err = "";

        nd.deliver_ready = 0;
        if (wait_until(&nd, has_deliver, timeout_ms) != 0) {
            fprintf(stderr, "node: no RPC arrived (%d of %d)\n", i, rpcs);
            rc = nd.finished ? 4 : 3;
            goto out;
        }
        nd.deliver_ready = 0;
        /* a DELIVER must be acknowledged by its message_id */
        if (send_ack(&nd, nd.cur.id, BORROWLINK_ACK_ACCEPTED) != 0) {
            rc = 4;
            goto out;
        }
        if (nd.cur.topic != TOPIC_RPC_REQUEST || nd.cur_overflow ||
            parse_rpc(nd.cur_payload, nd.cur_len, &q) != 0) {
            fprintf(stderr, "node: unusable DELIVER topic=0x%04x\n",
                    (unsigned)nd.cur.topic);
            rc = 4;
            goto out;
        }
        say(&nd, "rpc from 0x%016" PRIx64 ": %s.%s(color=%s on=%d)",
            nd.cur.source_node_id, q.cap, q.method, q.color, q.on);
        ok = strcmp(q.cap, "led") == 0 && strcmp(q.method, "set") == 0 &&
             q.args_ok;
        if (!ok) {
            err = "no_such_method: this node only implements led.set";
        }
        rl = build_reply(payload, sizeof(payload), q.id, ok, err);
        if (rl == 0u ||
            publish_and_wait(&nd, TOPIC_RPC_REPLY, payload, rl, 5000) != 0) {
            fprintf(stderr, "node: RPC reply was not acknowledged\n");
            rc = nd.finished ? 4 : 3;
            goto out;
        }
        say(&nd, "rpc answered ok=%d", ok);
    }

    /* orderly end: close my direction, then wait for the Hub's */
    if (bl_driver_close(&nd.drv) != BL_OK) {
        rc = 4;
        goto out;
    }
    if (wait_until(&nd, is_done, 5000) != 0 && !nd.finished) {
        fprintf(stderr, "node: Hub did not close its direction\n");
        rc = 3;
        goto out;
    }
    rc = (nd.end_type == BL_ACT_DISCONNECT || nd.end_type == BL_ACT_SET_LINK_IDLE)
             ? 0 : 4;
    say(&nd, "session %s", rc == 0 ? "closed cleanly" : "ended abnormally");

out:
    la_close(nd.fd);
    return rc;
}
