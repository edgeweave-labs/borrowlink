#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <borrowlink/http.h>

/* Golden vectors, built by hand from PROTOCOL.md section 9 (integers big
 * endian, section 7) and copied into the golden-vector paragraph there. */
static const uint8_t G_REQUEST[] = {
    0x01, 0x00, 0x14, 0x00, 0x0b,
    /* "http://example.com/a" */
    0x68, 0x74, 0x74, 0x70, 0x3a, 0x2f, 0x2f, 0x65, 0x78, 0x61,
    0x6d, 0x70, 0x6c, 0x65, 0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x61,
    /* "Accept: * / *" */
    0x41, 0x63, 0x63, 0x65, 0x70, 0x74, 0x3a, 0x20, 0x2a, 0x2f, 0x2a
};
static const uint8_t G_RESPONSE[] = {
    0x00, 0xc8, 0x00, 0x18,
    /* "Content-Type: text/plain" */
    0x43, 0x6f, 0x6e, 0x74, 0x65, 0x6e, 0x74, 0x2d, 0x54, 0x79, 0x70, 0x65,
    0x3a, 0x20, 0x74, 0x65, 0x78, 0x74, 0x2f, 0x70, 0x6c, 0x61, 0x69, 0x6e
};

#define URL "http://example.com/a"
#define HDR "Accept: */*"
#define SENTINEL 0xa5

static const bl_http_limits LIM = {64u, 128u, 1024u};

static void fill(uint8_t *buf, size_t n)
{
    memset(buf, SENTINEL, n);
}

static int untouched(const uint8_t *buf, size_t n)
{
    size_t i;

    for (i = 0u; i < n; ++i) {
        if (buf[i] != SENTINEL) {
            return 0;
        }
    }
    return 1;
}

static bl_result enc_req(uint8_t *out, size_t cap, size_t *len, uint8_t method,
                         const char *url, const char *headers,
                         const bl_http_limits *lim)
{
    return bl_http_encode_request(out, cap, len, method, url,
                                  url == NULL ? 0u : strlen(url), headers,
                                  headers == NULL ? 0u : strlen(headers), lim);
}

static void test_golden_request(void)
{
    uint8_t out[96];
    size_t len = 0u;
    bl_http_request_view v;

    assert(sizeof(G_REQUEST) == 5u + 20u + 11u);
    fill(out, sizeof(out));
    assert(enc_req(out, sizeof(out), &len, BL_HTTP_GET, URL, HDR, &LIM) ==
           BL_OK);
    assert(len == sizeof(G_REQUEST) && memcmp(out, G_REQUEST, len) == 0);
    assert(untouched(out + len, sizeof(out) - len));

    memset(&v, 0, sizeof(v));
    assert(bl_http_decode_request(G_REQUEST, sizeof(G_REQUEST), &v, &LIM) ==
           BL_OK);
    assert(v.method == BL_HTTP_GET);
    assert(v.url_len == 20u && memcmp(v.url, URL, 20u) == 0);
    assert(v.headers_len == 11u && memcmp(v.headers, HDR, 11u) == 0);
    assert(v.body_len == 0u);
    /* views point into the caller's input */
    assert((const uint8_t *)v.url == G_REQUEST + 5);
    assert((const uint8_t *)v.headers == G_REQUEST + 25);

    /* exact-size buffer works */
    assert(enc_req(out, sizeof(G_REQUEST), &len, BL_HTTP_GET, URL, HDR, &LIM) ==
           BL_OK);
    assert(len == sizeof(G_REQUEST));
}

static void test_golden_response(void)
{
    uint8_t out[64];
    size_t len = 0u;
    bl_http_response_view v;

    fill(out, sizeof(out));
    assert(bl_http_encode_response(out, sizeof(out), &len, 200u,
                                   "Content-Type: text/plain", 24u) == BL_OK);
    assert(len == sizeof(G_RESPONSE) && memcmp(out, G_RESPONSE, len) == 0);
    assert(untouched(out + len, sizeof(out) - len));

    memset(&v, 0, sizeof(v));
    assert(bl_http_decode_response(G_RESPONSE, sizeof(G_RESPONSE), &v, &LIM) ==
           BL_OK);
    assert(v.status == 200u);
    assert(v.headers_len == 24u &&
           memcmp(v.headers, "Content-Type: text/plain", 24u) == 0);
    assert(v.body_len == 0u);
}

static void test_all_methods_round_trip(void)
{
    uint8_t out[96];
    size_t len;
    uint8_t m;
    bl_http_request_view v;

    for (m = BL_HTTP_GET; m <= BL_HTTP_OPTIONS; ++m) {
        assert(enc_req(out, sizeof(out), &len, m, URL, HDR, &LIM) == BL_OK);
        assert(out[0] == m);
        assert(bl_http_decode_request(out, len, &v, &LIM) == BL_OK);
        assert(v.method == m);
    }
    assert(BL_HTTP_GET == 0x01 && BL_HTTP_POST == 0x02 && BL_HTTP_PUT == 0x03 &&
           BL_HTTP_PATCH == 0x04 && BL_HTTP_DELETE == 0x05 &&
           BL_HTTP_HEAD == 0x06 && BL_HTTP_OPTIONS == 0x07);
}

static void test_reject_method(void)
{
    uint8_t out[96];
    uint8_t in[sizeof(G_REQUEST)];
    size_t len = 7u;
    bl_http_request_view v;
    uint8_t bad[] = {0x00u, 0x08u, 0x09u, 0x80u, 0xffu};
    size_t i;

    for (i = 0u; i < sizeof(bad); ++i) {
        fill(out, sizeof(out));
        assert(enc_req(out, sizeof(out), &len, bad[i], URL, HDR, &LIM) ==
               BL_ERROR_MALFORMED);
        assert(untouched(out, sizeof(out)));
        memcpy(in, G_REQUEST, sizeof(in));
        in[0] = bad[i];
        assert(bl_http_decode_request(in, sizeof(in), &v, &LIM) ==
               BL_ERROR_MALFORMED);
        assert(bl_http_request_head_size(in, sizeof(in), &len, &LIM) ==
               BL_ERROR_MALFORMED);
    }
}

static void test_reject_header_bytes(void)
{
    uint8_t out[96];
    size_t len;
    bl_http_request_view v;
    uint8_t in[96];
    /* bare CR, bare LF, LF before CR, NUL, CR at the very end, an empty line
     * (header/body split injection), a line with no name, a line with no
     * colon, a space in the name, an obs-fold continuation. */
    static const char *const bad[] = {
        "A: b\rC: d", "A: b\nC: d", "A: b\n\rC: d", "A: b\r", "A: b\n",
        "A: b\r\n\r\nC: d", "A: b\r\n\r\n", "\r\nA: b", ": novalue", "NoColon",
        "A B: c", "A: b\r\n continued"
    };
    size_t i;

    for (i = 0u; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        fill(out, sizeof(out));
        assert(enc_req(out, sizeof(out), &len, BL_HTTP_GET, URL, bad[i],
                       &LIM) == BL_ERROR_MALFORMED);
        assert(untouched(out, sizeof(out)));
        /* same bytes arriving from the wire */
        in[0] = BL_HTTP_GET;
        in[1] = 0x00;
        in[2] = 0x01;
        in[3] = 0x00;
        in[4] = (uint8_t)strlen(bad[i]);
        in[5] = 'u';
        memcpy(in + 6, bad[i], strlen(bad[i]));
        assert(bl_http_decode_request(in, 6u + strlen(bad[i]), &v, &LIM) ==
               BL_ERROR_MALFORMED);
    }

    /* NUL inside a header (explicit length, so strlen would stop early) */
    fill(out, sizeof(out));
    assert(bl_http_encode_request(out, sizeof(out), &len, BL_HTTP_GET, URL, 20u,
                                  "A: b\0c", 6u, &LIM) == BL_ERROR_MALFORMED);
    assert(untouched(out, sizeof(out)));
    assert(bl_http_encode_response(out, sizeof(out), &len, 200u, "A: b\0c",
                                   6u) == BL_ERROR_MALFORMED);
    assert(untouched(out, sizeof(out)));
    in[0] = 0x00; in[1] = 200; in[2] = 0x00; in[3] = 0x06;
    memcpy(in + 4, "A: b\0c", 6u);
    {
        bl_http_response_view r;
        assert(bl_http_decode_response(in, 10u, &r, &LIM) ==
               BL_ERROR_MALFORMED);
    }
    /* CR/LF in a response header */
    assert(bl_http_encode_response(out, sizeof(out), &len, 200u, "A: b\nC: d",
                                   9u) == BL_ERROR_MALFORMED);
}

static void test_header_forms_accepted(void)
{
    uint8_t out[160];
    size_t len;
    bl_http_request_view v;
    static const char *const ok[] = {
        "", "A: b", "A: b\r\n", "A: b\r\nC-D: e f", "A: b\r\nC: d\r\n",
        "X-Empty:", "A: caf\xc3\xa9"
    };
    size_t i;

    for (i = 0u; i < sizeof(ok) / sizeof(ok[0]); ++i) {
        assert(enc_req(out, sizeof(out), &len, BL_HTTP_POST, URL, ok[i],
                       &LIM) == BL_OK);
        assert(bl_http_decode_request(out, len, &v, &LIM) == BL_OK);
        assert(v.headers_len == strlen(ok[i]));
    }
}

static void test_reject_bad_utf8_and_url(void)
{
    uint8_t out[96];
    size_t len;
    /* lone continuation, truncated sequence, overlong, surrogate, > U+10FFFF */
    static const char *const bad_utf8[] = {
        "A: \x80", "A: \xc3", "A: \xc0\xaf", "A: \xed\xa0\x80", "A: \xf4\x90\x80\x80"
    };
    size_t i;

    for (i = 0u; i < sizeof(bad_utf8) / sizeof(bad_utf8[0]); ++i) {
        assert(enc_req(out, sizeof(out), &len, BL_HTTP_GET, URL, bad_utf8[i],
                       &LIM) == BL_ERROR_MALFORMED);
        assert(enc_req(out, sizeof(out), &len, BL_HTTP_GET, bad_utf8[i], "",
                       &LIM) == BL_ERROR_MALFORMED);
    }
    /* url: empty, NUL, CR, LF, DEL */
    assert(enc_req(out, sizeof(out), &len, BL_HTTP_GET, "", "", &LIM) ==
           BL_ERROR_MALFORMED);
    assert(bl_http_encode_request(out, sizeof(out), &len, BL_HTTP_GET, "a\0b",
                                  3u, "", 0u, &LIM) == BL_ERROR_MALFORMED);
    assert(enc_req(out, sizeof(out), &len, BL_HTTP_GET, "http://a/\r\nX: y", "",
                   &LIM) == BL_ERROR_MALFORMED);
    assert(enc_req(out, sizeof(out), &len, BL_HTTP_GET, "http://a/\n", "",
                   &LIM) == BL_ERROR_MALFORMED);
    assert(enc_req(out, sizeof(out), &len, BL_HTTP_GET, "http://a/\x7f", "",
                   &LIM) == BL_ERROR_MALFORMED);
    /* valid multi-byte UTF-8 url */
    assert(enc_req(out, sizeof(out), &len, BL_HTTP_GET,
                   "http://a/\xe4\xb8\xad\xf0\x9f\x99\x82", "", &LIM) == BL_OK);
}

static void test_limits_encode(void)
{
    uint8_t out[256];
    size_t len = 99u;
    char big[400];
    bl_http_limits lim = {20u, 11u, 0u};

    /* exactly at the limit is fine */
    assert(enc_req(out, sizeof(out), &len, BL_HTTP_GET, URL, HDR, &lim) ==
           BL_OK);

    /* one over each limit: LIMIT_EXCEEDED, buffer untouched, len not set */
    lim.max_url = 19u;
    fill(out, sizeof(out));
    len = 99u;
    assert(enc_req(out, sizeof(out), &len, BL_HTTP_GET, URL, HDR, &lim) ==
           BL_ERROR_LIMIT_EXCEEDED);
    assert(untouched(out, sizeof(out)));
    assert(len == 0u);

    lim.max_url = 20u;
    lim.max_headers = 10u;
    fill(out, sizeof(out));
    assert(enc_req(out, sizeof(out), &len, BL_HTTP_GET, URL, HDR, &lim) ==
           BL_ERROR_LIMIT_EXCEEDED);
    assert(untouched(out, sizeof(out)));

    /* 0 means none allowed */
    lim.max_url = 0u;
    lim.max_headers = 0u;
    assert(enc_req(out, sizeof(out), &len, BL_HTTP_GET, URL, "", &lim) ==
           BL_ERROR_LIMIT_EXCEEDED);
    lim.max_url = 20u;
    assert(enc_req(out, sizeof(out), &len, BL_HTTP_GET, URL, "", &lim) ==
           BL_OK);

    /* limit exceeded wins over a too-small output buffer, and nothing is
     * written in either case (sentinel directly after the allowed bytes) */
    memset(big, 'a', sizeof(big));
    lim.max_url = 10u;
    fill(out, sizeof(out));
    assert(bl_http_encode_request(out, 8u, &len, BL_HTTP_GET, big, 300u, "", 0u,
                                  &lim) == BL_ERROR_LIMIT_EXCEEDED);
    assert(untouched(out, sizeof(out)));

    /* lengths that do not fit the 16-bit fields, even with huge limits */
    {
        static char huge[70000];
        static uint8_t huge_out[70100];
        bl_http_limits big_lim = {100000u, 100000u, 100000u};

        memset(huge, 'a', sizeof(huge));
        memset(huge_out, SENTINEL, sizeof(huge_out));
        assert(bl_http_encode_request(huge_out, sizeof(huge_out), &len,
                                      BL_HTTP_GET, huge, 65536u, "", 0u,
                                      &big_lim) == BL_ERROR_LIMIT_EXCEEDED);
        assert(untouched(huge_out, sizeof(huge_out)));
        assert(bl_http_encode_response(huge_out, sizeof(huge_out), &len, 200u,
                                       huge, 65536u) ==
               BL_ERROR_LIMIT_EXCEEDED);
        assert(untouched(huge_out, sizeof(huge_out)));
        /* 65535 does fit (all 'a' is not a header line, so use a long value) */
        memcpy(huge, "A: ", 3u);
        assert(bl_http_encode_request(huge_out, sizeof(huge_out), &len,
                                      BL_HTTP_GET, URL, 20u, huge, 65535u,
                                      &big_lim) == BL_OK);
        assert(len == 5u + 20u + 65535u);
        assert(huge_out[3] == 0xff && huge_out[4] == 0xff);
    }
}

static void test_buffer_too_small_writes_nothing(void)
{
    uint8_t out[sizeof(G_REQUEST) + 8];
    size_t len = 5u;
    size_t cap;

    for (cap = 0u; cap < sizeof(G_REQUEST); ++cap) {
        /* sentinel everywhere, including past `cap`: no byte may change */
        fill(out, sizeof(out));
        assert(enc_req(out, cap, &len, BL_HTTP_GET, URL, HDR, &LIM) ==
               BL_ERROR_BUFFER_TOO_SMALL);
        assert(untouched(out, sizeof(out)));
    }
    for (cap = 0u; cap < sizeof(G_RESPONSE); ++cap) {
        fill(out, sizeof(out));
        assert(bl_http_encode_response(out, cap, &len, 200u,
                                       "Content-Type: text/plain", 24u) ==
               BL_ERROR_BUFFER_TOO_SMALL);
        assert(untouched(out, sizeof(out)));
    }
}

static void test_decode_limits_and_truncation(void)
{
    bl_http_request_view v;
    bl_http_response_view r;
    bl_http_limits lim = {20u, 11u, 4u};
    uint8_t in[sizeof(G_REQUEST) + 8];
    size_t head = 0u;
    size_t n;

    assert(bl_http_decode_request(G_REQUEST, sizeof(G_REQUEST), &v, &lim) ==
           BL_OK);
    lim.max_url = 19u;
    assert(bl_http_decode_request(G_REQUEST, sizeof(G_REQUEST), &v, &lim) ==
           BL_ERROR_LIMIT_EXCEEDED);
    /* refused from the first five bytes alone, before the head is buffered */
    assert(bl_http_request_head_size(G_REQUEST, 5u, &head, &lim) ==
           BL_ERROR_LIMIT_EXCEEDED);
    lim.max_url = 20u;
    lim.max_headers = 10u;
    assert(bl_http_decode_request(G_REQUEST, sizeof(G_REQUEST), &v, &lim) ==
           BL_ERROR_LIMIT_EXCEEDED);
    assert(bl_http_request_head_size(G_REQUEST, 5u, &head, &lim) ==
           BL_ERROR_LIMIT_EXCEEDED);
    lim.max_headers = 11u;
    assert(bl_http_request_head_size(G_REQUEST, 5u, &head, &lim) == BL_OK);
    assert(head == sizeof(G_REQUEST));

    /* truncated head: every shorter prefix is malformed, never a read past n */
    for (n = 0u; n < sizeof(G_REQUEST); ++n) {
        assert(bl_http_decode_request(G_REQUEST, n, &v, &LIM) ==
               BL_ERROR_MALFORMED);
    }
    for (n = 0u; n < sizeof(G_RESPONSE); ++n) {
        assert(bl_http_decode_response(G_RESPONSE, n, &r, &LIM) ==
               BL_ERROR_MALFORMED);
    }
    assert(bl_http_request_head_size(G_REQUEST, 4u, &head, &LIM) ==
           BL_ERROR_MALFORMED);
    assert(bl_http_response_head_size(G_RESPONSE, 3u, &head, &LIM) ==
           BL_ERROR_MALFORMED);
    assert(bl_http_response_head_size(G_RESPONSE, 4u, &head, &LIM) == BL_OK);
    assert(head == sizeof(G_RESPONSE));

    /* body bytes after the head are returned and counted against max_body */
    memcpy(in, G_REQUEST, sizeof(G_REQUEST));
    memcpy(in + sizeof(G_REQUEST), "\x01\x02\x03\x04", 4u);
    lim.max_headers = 11u;
    assert(bl_http_decode_request(in, sizeof(G_REQUEST) + 4u, &v, &lim) ==
           BL_OK);
    assert(v.body_len == 4u && v.body == in + sizeof(G_REQUEST));
    assert(bl_http_decode_request(in, sizeof(G_REQUEST) + 5u, &v, &lim) ==
           BL_ERROR_LIMIT_EXCEEDED);

    /* response header limit */
    lim.max_headers = 23u;
    assert(bl_http_decode_response(G_RESPONSE, sizeof(G_RESPONSE), &r, &lim) ==
           BL_ERROR_LIMIT_EXCEEDED);
    assert(bl_http_response_head_size(G_RESPONSE, 4u, &head, &lim) ==
           BL_ERROR_LIMIT_EXCEEDED);
    lim.max_headers = 24u;
    assert(bl_http_decode_response(G_RESPONSE, sizeof(G_RESPONSE), &r, &lim) ==
           BL_OK);
}

static void test_status_codes(void)
{
    uint8_t out[32];
    size_t len;
    bl_http_response_view r;
    static const uint16_t ok[] = {100u, 200u, 204u, 301u, 304u, 400u, 401u,
                                  404u, 418u, 429u, 500u, 502u, 503u, 504u,
                                  599u};
    static const uint16_t bad[] = {0u, 1u, 99u, 600u, 999u, 0xffffu};
    size_t i;

    /* 4xx and 5xx are ordinary responses, not errors */
    for (i = 0u; i < sizeof(ok) / sizeof(ok[0]); ++i) {
        assert(bl_http_encode_response(out, sizeof(out), &len, ok[i], "", 0u) ==
               BL_OK);
        assert(len == 4u && out[0] == (uint8_t)(ok[i] >> 8) &&
               out[1] == (uint8_t)ok[i] && out[2] == 0u && out[3] == 0u);
        assert(bl_http_decode_response(out, len, &r, &LIM) == BL_OK);
        assert(r.status == ok[i] && r.headers_len == 0u && r.body_len == 0u);
    }
    for (i = 0u; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        fill(out, sizeof(out));
        assert(bl_http_encode_response(out, sizeof(out), &len, bad[i], "",
                                       0u) == BL_ERROR_MALFORMED);
        assert(untouched(out, sizeof(out)));
        out[0] = (uint8_t)(bad[i] >> 8);
        out[1] = (uint8_t)bad[i];
        out[2] = 0u;
        out[3] = 0u;
        assert(bl_http_decode_response(out, 4u, &r, &LIM) ==
               BL_ERROR_MALFORMED);
    }
    /* a 404 with headers and the start of a body */
    {
        uint8_t in[64];

        assert(bl_http_encode_response(in, sizeof(in), &len, 404u,
                                       "Content-Length: 9", 17u) == BL_OK);
        memcpy(in + len, "not found", 9u);
        assert(bl_http_decode_response(in, len + 9u, &r, &LIM) == BL_OK);
        assert(r.status == 404u && r.body_len == 9u &&
               memcmp(r.body, "not found", 9u) == 0);
    }
}

static void test_body_accounting(void)
{
    size_t total = 0u;
    bl_http_limits lim = {0u, 0u, 10u};

    assert(bl_http_body_add(&total, 6u, &lim) == BL_OK && total == 6u);
    assert(bl_http_body_add(&total, 4u, &lim) == BL_OK && total == 10u);
    assert(bl_http_body_add(&total, 1u, &lim) == BL_ERROR_LIMIT_EXCEEDED);
    assert(total == 10u);
    assert(bl_http_body_add(&total, 0u, &lim) == BL_OK && total == 10u);
    /* no overflow wrap-around */
    lim.max_body = SIZE_MAX;
    total = SIZE_MAX - 1u;
    assert(bl_http_body_add(&total, 2u, &lim) == BL_ERROR_LIMIT_EXCEEDED);
    assert(total == SIZE_MAX - 1u);
    assert(bl_http_body_add(&total, 1u, &lim) == BL_OK && total == SIZE_MAX);
    assert(bl_http_body_add(NULL, 1u, &lim) == BL_ERROR_ARGUMENT);
}

static void test_argument_checks(void)
{
    uint8_t out[96];
    size_t len;
    bl_http_request_view v;
    bl_http_response_view r;

    assert(enc_req(NULL, sizeof(out), &len, BL_HTTP_GET, URL, HDR, &LIM) ==
           BL_ERROR_ARGUMENT);
    assert(enc_req(out, sizeof(out), NULL, BL_HTTP_GET, URL, HDR, &LIM) ==
           BL_ERROR_ARGUMENT);
    assert(enc_req(out, sizeof(out), &len, BL_HTTP_GET, URL, HDR, NULL) ==
           BL_ERROR_ARGUMENT);
    assert(bl_http_encode_request(out, sizeof(out), &len, BL_HTTP_GET, NULL, 4u,
                                  "", 0u, &LIM) == BL_ERROR_ARGUMENT);
    assert(bl_http_encode_request(out, sizeof(out), &len, BL_HTTP_GET, URL, 20u,
                                  NULL, 3u, &LIM) == BL_ERROR_ARGUMENT);
    assert(bl_http_encode_response(NULL, 8u, &len, 200u, "", 0u) ==
           BL_ERROR_ARGUMENT);
    assert(bl_http_encode_response(out, 8u, NULL, 200u, "", 0u) ==
           BL_ERROR_ARGUMENT);
    assert(bl_http_encode_response(out, 8u, &len, 200u, NULL, 3u) ==
           BL_ERROR_ARGUMENT);
    assert(bl_http_decode_request(NULL, 8u, &v, &LIM) == BL_ERROR_ARGUMENT);
    assert(bl_http_decode_request(G_REQUEST, sizeof(G_REQUEST), NULL, &LIM) ==
           BL_ERROR_ARGUMENT);
    assert(bl_http_decode_request(G_REQUEST, sizeof(G_REQUEST), &v, NULL) ==
           BL_ERROR_ARGUMENT);
    assert(bl_http_decode_response(G_RESPONSE, sizeof(G_RESPONSE), &r, NULL) ==
           BL_ERROR_ARGUMENT);
    assert(bl_http_decode_response(NULL, 8u, &r, &LIM) == BL_ERROR_ARGUMENT);
}

/* Every prefix and every single-byte corruption of the goldens must come back
 * as a clean result code (this is what ASan/UBSan check). */
static void test_corruption_is_safe(void)
{
    uint8_t in[sizeof(G_REQUEST)];
    bl_http_request_view v;
    bl_http_response_view r;
    size_t i;
    unsigned b;

    for (i = 0u; i < sizeof(G_REQUEST); ++i) {
        for (b = 0u; b < 256u; ++b) {
            memcpy(in, G_REQUEST, sizeof(in));
            in[i] = (uint8_t)b;
            (void)bl_http_decode_request(in, sizeof(in), &v, &LIM);
        }
    }
    for (i = 0u; i < sizeof(G_RESPONSE); ++i) {
        for (b = 0u; b < 256u; ++b) {
            memcpy(in, G_RESPONSE, sizeof(G_RESPONSE));
            in[i] = (uint8_t)b;
            (void)bl_http_decode_response(in, sizeof(G_RESPONSE), &r, &LIM);
        }
    }
}

int main(void)
{
    test_golden_request();
    test_golden_response();
    test_all_methods_round_trip();
    test_reject_method();
    test_reject_header_bytes();
    test_header_forms_accepted();
    test_reject_bad_utf8_and_url();
    test_limits_encode();
    test_buffer_too_small_writes_nothing();
    test_decode_limits_and_truncation();
    test_status_codes();
    test_body_accounting();
    test_argument_checks();
    test_corruption_is_safe();
    puts("test_http: 14 tests ok");
    return 0;
}
