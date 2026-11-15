/*
 * Reads one command per line from stdin and prints the C codecs' answer, so a
 * test in another language can compare bytes with its own implementation:
 *
 *   beacon   <ver> <profile> <xid> <attempt> <flags>        -> ok <hex> | err NAME
 *   beacon_d <hex>                                          -> ok k=v... | err NAME
 *   frame    <seq> <opcode> <hex|->      frame_d <hex>
 *   hello    <ver> <profile> <delivery> <xid> <node> <max_rx>   hello_d <hex>
 *   accept   <ver> <delivery> <xid> <node> <max_rx>             accept_d <hex>
 *   msg      <cmd> <id> <source> <topic> <lease> <policy> <status> <hex|->
 *   msg_d    <hex>          (whole message in one feed)
 *   msg_split <step> <hex>  (same message fed `step` bytes at a time)
 *   record   <channel> <frame hex>      record_d <hex>
 *
 * Numbers are decimal or 0x hex.  Only for tests; not part of the library.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <borrowlink/binding.h>
#include <borrowlink/message.h>
#include <borrowlink/session.h>

static const char *rname(bl_result r)
{
    switch (r) {
    case BL_OK: return "OK";
    case BL_ERROR_ARGUMENT: return "ARGUMENT";
    case BL_ERROR_BUFFER_TOO_SMALL: return "BUFFER_TOO_SMALL";
    case BL_ERROR_MALFORMED: return "MALFORMED";
    case BL_ERROR_UNSUPPORTED_VERSION: return "UNSUPPORTED_VERSION";
    case BL_ERROR_UNSUPPORTED_PROFILE: return "UNSUPPORTED_PROFILE";
    case BL_ERROR_UNSUPPORTED_DELIVERY: return "UNSUPPORTED_DELIVERY";
    case BL_ERROR_UNSUPPORTED_OPCODE: return "UNSUPPORTED_OPCODE";
    case BL_ERROR_LIMIT_EXCEEDED: return "LIMIT_EXCEEDED";
    default: return "OTHER";
    }
}

static void put_hex(const uint8_t *b, size_t n)
{
    size_t i;

    if (n == 0u) {
        fputs("-", stdout);
    }
    for (i = 0u; i < n; ++i) {
        printf("%02x", b[i]);
    }
}

static size_t get_hex(const char *s, uint8_t *out, size_t cap)
{
    size_t n = 0u;

    if (strcmp(s, "-") == 0) {
        return 0u;
    }
    while (s[0] != '\0' && s[1] != '\0' && n < cap) {
        char pair[3] = {s[0], s[1], 0};

        out[n++] = (uint8_t)strtoul(pair, NULL, 16);
        s += 2;
    }
    return n;
}

static uint64_t num(const char *s) { return strtoull(s, NULL, 0); }

static void out_result(bl_result r, const uint8_t *b, size_t n)
{
    if (r != BL_OK) {
        printf("err %s\n", rname(r));
        return;
    }
    fputs("ok ", stdout);
    put_hex(b, n);
    fputc('\n', stdout);
}

static void do_cmd(char *argv[], int argc)
{
    static uint8_t buf[70000];
    uint8_t in[70000];
    size_t n;
    bl_result r;
    const char *c = argv[0];

    if (strcmp(c, "beacon") == 0 && argc == 6) {
        bl_beacon b;

        b.version = (uint8_t)num(argv[1]);
        b.profile = (uint8_t)num(argv[2]);
        b.xid = (uint16_t)num(argv[3]);
        b.attempt = (uint8_t)num(argv[4]);
        b.flags = (uint8_t)num(argv[5]);
        r = bl_beacon_encode(buf, 6u, &b);
        out_result(r, buf, 6u);
    } else if (strcmp(c, "beacon_d") == 0 && argc == 2) {
        bl_beacon b;

        n = get_hex(argv[1], in, sizeof(in));
        r = bl_beacon_decode(&b, in, n);
        if (r != BL_OK) {
            printf("err %s\n", rname(r));
        } else {
            printf("ok v=%u p=%u x=%u a=%u f=%u\n", b.version, b.profile,
                   b.xid, b.attempt, b.flags);
        }
    } else if (strcmp(c, "frame") == 0 && argc == 4) {
        size_t w = 0u;

        n = get_hex(argv[3], in, sizeof(in));
        r = bl_frame_encode(buf, sizeof(buf), &w, (uint16_t)num(argv[1]),
                            (uint8_t)num(argv[2]), in, n);
        out_result(r, buf, w);
    } else if (strcmp(c, "frame_d") == 0 && argc == 2) {
        bl_frame_view v;

        n = get_hex(argv[1], in, sizeof(in));
        r = bl_frame_decode(&v, in, n);
        if (r != BL_OK) {
            printf("err %s\n", rname(r));
        } else {
            printf("ok s=%u o=%u p=", v.seq, v.opcode);
            put_hex(v.payload, v.payload_size);
            fputc('\n', stdout);
        }
    } else if (strcmp(c, "hello") == 0 && argc == 7) {
        bl_hello h;

        h.version = (uint8_t)num(argv[1]);
        h.profile = (uint8_t)num(argv[2]);
        h.delivery = (uint8_t)num(argv[3]);
        h.xid = (uint16_t)num(argv[4]);
        h.node_id = num(argv[5]);
        h.max_rx_payload = (uint16_t)num(argv[6]);
        r = bl_hello_encode(buf, BORROWLINK_HELLO_PAYLOAD_SIZE, &h);
        out_result(r, buf, BORROWLINK_HELLO_PAYLOAD_SIZE);
    } else if (strcmp(c, "hello_d") == 0 && argc == 2) {
        bl_hello h;

        n = get_hex(argv[1], in, sizeof(in));
        r = bl_hello_decode(&h, in, n);
        if (r != BL_OK) {
            printf("err %s\n", rname(r));
        } else {
            printf("ok v=%u p=%u d=%u x=%u n=%llu m=%u\n", h.version,
                   h.profile, h.delivery, h.xid, (unsigned long long)h.node_id,
                   h.max_rx_payload);
        }
    } else if (strcmp(c, "accept") == 0 && argc == 6) {
        bl_accept a;

        a.version = (uint8_t)num(argv[1]);
        a.delivery = (uint8_t)num(argv[2]);
        a.xid = (uint16_t)num(argv[3]);
        a.node_id = num(argv[4]);
        a.max_rx_payload = (uint16_t)num(argv[5]);
        r = bl_accept_encode(buf, BORROWLINK_ACCEPT_PAYLOAD_SIZE, &a);
        out_result(r, buf, BORROWLINK_ACCEPT_PAYLOAD_SIZE);
    } else if (strcmp(c, "accept_d") == 0 && argc == 2) {
        bl_accept a;

        n = get_hex(argv[1], in, sizeof(in));
        r = bl_accept_decode(&a, in, n);
        if (r != BL_OK) {
            printf("err %s\n", rname(r));
        } else {
            printf("ok v=%u d=%u x=%u n=%llu m=%u\n", a.version, a.delivery,
                   a.xid, (unsigned long long)a.node_id, a.max_rx_payload);
        }
    } else if (strcmp(c, "msg") == 0 && argc == 9) {
        bl_msg_header h;
        size_t w = 0u;

        memset(&h, 0, sizeof(h));
        h.cmd = (uint8_t)num(argv[1]);
        h.id = (uint16_t)num(argv[2]);
        h.source_node_id = num(argv[3]);
        h.topic = (uint16_t)num(argv[4]);
        h.lease_seconds = (uint16_t)num(argv[5]);
        h.queue_policy = (uint8_t)num(argv[6]);
        h.status = (uint8_t)num(argv[7]);
        n = get_hex(argv[8], in, sizeof(in));
        r = bl_msg_header_encode(buf, sizeof(buf), &w, &h);
        if (r == BL_OK && (h.cmd == BORROWLINK_MSG_PUBLISH ||
                           h.cmd == BORROWLINK_MSG_DELIVER)) {
            memcpy(buf + w, in, n);
            w += n;
        }
        out_result(r, buf, w);
    } else if ((strcmp(c, "msg_d") == 0 && argc == 2) ||
               (strcmp(c, "msg_split") == 0 && argc == 3)) {
        bl_msg_parser p;
        size_t step;
        size_t pos = 0u;
        uint8_t payload[70000];
        size_t plen = 0u;
        bl_msg_header h;
        int have = 0;
        int ended = 0;

        r = BL_OK;
        n = get_hex(argv[argc - 1], in, sizeof(in));
        step = argc == 3 ? (size_t)num(argv[1]) : n + 1u;
        if (step == 0u) {
            step = 1u;
        }
        bl_msg_parser_init(&p);
        memset(&h, 0, sizeof(h));
        do {
            size_t piece = n - pos < step ? n - pos : step;
            int end = pos + piece == n;
            size_t off = 0u;

            for (;;) {
                bl_msg_event ev;
                size_t used = 0u;

                r = bl_msg_parser_feed(&p, in + pos + off, piece - off, end,
                                       &used, &ev);
                if (r != BL_OK) {
                    break;
                }
                off += used;
                if (ev.kind == BL_MSG_EV_HEADER) {
                    h = ev.header;
                    have = 1;
                } else if (ev.kind == BL_MSG_EV_PAYLOAD) {
                    memcpy(payload + plen, ev.data, ev.size);
                    plen += ev.size;
                } else if (ev.kind == BL_MSG_EV_END) {
                    ended = 1;
                    break;
                } else {
                    break;
                }
            }
            if (r != BL_OK) {
                break;
            }
            pos += piece;
        } while (pos < n);
        if (r != BL_OK || !have || !ended) {
            printf("err %s\n", r != BL_OK ? rname(r) : "MALFORMED");
        } else {
            printf("ok c=%u id=%u s=%llu t=%u l=%u q=%u st=%u p=", h.cmd,
                   h.id, (unsigned long long)h.source_node_id, h.topic,
                   h.lease_seconds, h.queue_policy, h.status);
            put_hex(payload, plen);
            fputc('\n', stdout);
        }
    } else if (strcmp(c, "record") == 0 && argc == 3) {
        size_t w = 0u;

        n = get_hex(argv[2], in, sizeof(in));
        r = n == 0u ? BL_ERROR_ARGUMENT
                    : bl_binding_encode(buf, sizeof(buf), &w,
                                        (uint8_t)num(argv[1]), in, n);
        out_result(r, buf, w);
    } else if (strcmp(c, "record_d") == 0 && argc == 2) {
        static uint8_t rxbuf[70000];
        bl_binding_rx rx;
        bl_binding_record rec;
        size_t used = 0u;

        n = get_hex(argv[1], in, sizeof(in));
        (void)bl_binding_rx_init(&rx, rxbuf, sizeof(rxbuf), 65535u);
        r = bl_binding_rx_feed(&rx, in, n, &used, &rec);
        if (r != BL_OK) {
            printf("err %s\n", rname(r));
        } else if (!rec.ready || used != n) {
            printf("err INCOMPLETE\n");
        } else {
            printf("ok ch=%u f=", rec.channel);
            put_hex(rec.frame, rec.frame_size);
            fputc('\n', stdout);
        }
    } else {
        printf("err USAGE\n");
    }
}

int main(void)
{
    static char line[200000];

    while (fgets(line, sizeof(line), stdin) != NULL) {
        char *argv[16];
        int argc = 0;
        char *tok = strtok(line, " \t\r\n");

        while (tok != NULL && argc < 16) {
            argv[argc++] = tok;
            tok = strtok(NULL, " \t\r\n");
        }
        if (argc > 0) {
            do_cmd(argv, argc);
            fflush(stdout);
        }
    }
    return 0;
}
