#include <borrowlink/http.h>

#include <string.h>

static uint16_t bl_get_u16(const uint8_t *in)
{
    return (uint16_t)(((uint16_t)in[0] << 8) | in[1]);
}

static void bl_put_u16(uint8_t *out, uint16_t v)
{
    out[0] = (uint8_t)(v >> 8);
    out[1] = (uint8_t)v;
}

static int bl_http_method_valid(uint8_t method)
{
    return method >= (uint8_t)BL_HTTP_GET && method <= (uint8_t)BL_HTTP_OPTIONS;
}

/* Responses: 1xx..5xx.  4xx and 5xx are ordinary responses (section 9). */
static int bl_http_status_valid(uint16_t status)
{
    return status >= 100u && status <= 599u;
}

static int bl_http_is_cont(const uint8_t *s, size_t i, size_t n, uint8_t lo,
                           uint8_t hi)
{
    return i < n && s[i] >= lo && s[i] <= hi;
}

/* Strict UTF-8 (RFC 3629): no overlong forms, no surrogates, max U+10FFFF. */
static int bl_http_utf8_valid(const uint8_t *s, size_t n)
{
    size_t i = 0u;

    while (i < n) {
        uint8_t c = s[i];

        if (c < 0x80u) {
            i += 1u;
        } else if (c >= 0xc2u && c <= 0xdfu) {
            if (!bl_http_is_cont(s, i + 1u, n, 0x80u, 0xbfu)) {
                return 0;
            }
            i += 2u;
        } else if (c >= 0xe0u && c <= 0xefu) {
            uint8_t lo = (c == 0xe0u) ? 0xa0u : 0x80u;
            uint8_t hi = (c == 0xedu) ? 0x9fu : 0xbfu;

            if (!bl_http_is_cont(s, i + 1u, n, lo, hi) ||
                !bl_http_is_cont(s, i + 2u, n, 0x80u, 0xbfu)) {
                return 0;
            }
            i += 3u;
        } else if (c >= 0xf0u && c <= 0xf4u) {
            uint8_t lo = (c == 0xf0u) ? 0x90u : 0x80u;
            uint8_t hi = (c == 0xf4u) ? 0x8fu : 0xbfu;

            if (!bl_http_is_cont(s, i + 1u, n, lo, hi) ||
                !bl_http_is_cont(s, i + 2u, n, 0x80u, 0xbfu) ||
                !bl_http_is_cont(s, i + 3u, n, 0x80u, 0xbfu)) {
                return 0;
            }
            i += 4u;
        } else {
            return 0;
        }
    }
    return 1;
}

/* url: non-empty UTF-8 without C0 controls (NUL, CR, LF, ...) and DEL, so it
 * cannot smuggle a line break into the request line the Host builds. */
static int bl_http_url_valid(const uint8_t *s, size_t n)
{
    size_t i;

    if (n == 0u) {
        return 0;
    }
    for (i = 0u; i < n; ++i) {
        if (s[i] < 0x20u || s[i] == 0x7fu) {
            return 0;
        }
    }
    return bl_http_utf8_valid(s, n);
}

static int bl_http_is_token_char(uint8_t c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9')) {
        return 1;
    }
    switch (c) {
    case '!': case '#': case '$': case '%': case '&': case '\'': case '*':
    case '+': case '-': case '.': case '^': case '_': case '`': case '|':
    case '~':
        return 1;
    default:
        return 0;
    }
}

/* headers: "Name: value" lines separated by CRLF, an optional CRLF after the
 * last one.  CR and LF are legal only as that separator; NUL never; an empty
 * line (which would end the header block) is refused. */
static int bl_http_headers_valid(const uint8_t *s, size_t n)
{
    size_t pos = 0u;

    if (n == 0u) {
        return 1;
    }
    if (!bl_http_utf8_valid(s, n)) {
        return 0;
    }
    while (pos < n) {
        size_t i = pos;
        size_t name_end = 0u;

        while (i < n && s[i] != '\r' && s[i] != '\n') {
            if (s[i] == 0u) {
                return 0;
            }
            if (name_end == 0u && s[i] == ':') {
                name_end = i;
            }
            i += 1u;
        }
        /* a name of at least one token character, then ':' */
        if (name_end == 0u || name_end == pos) {
            return 0;
        }
        for (size_t k = pos; k < name_end; ++k) {
            if (!bl_http_is_token_char(s[k])) {
                return 0;
            }
        }
        if (i == n) {
            return 1;
        }
        /* s[i] is CR or LF: only CRLF is allowed */
        if (s[i] != '\r' || i + 1u >= n || s[i + 1u] != '\n') {
            return 0;
        }
        pos = i + 2u;
    }
    return 1;
}

static bl_result bl_http_fields_fit(size_t url_len, size_t headers_len,
                                    const bl_http_limits *lim)
{
    if (url_len > lim->max_url || url_len > BORROWLINK_HTTP_FIELD_MAX ||
        headers_len > lim->max_headers ||
        headers_len > BORROWLINK_HTTP_FIELD_MAX) {
        return BL_ERROR_LIMIT_EXCEEDED;
    }
    return BL_OK;
}

bl_result bl_http_encode_request(uint8_t *out, size_t cap, size_t *len,
                                 uint8_t method, const char *url,
                                 size_t url_len, const char *headers,
                                 size_t headers_len, const bl_http_limits *lim)
{
    size_t size;
    bl_result result;

    if (len != NULL) {
        *len = 0u;
    }
    if (out == NULL || len == NULL || lim == NULL ||
        (url == NULL && url_len != 0u) ||
        (headers == NULL && headers_len != 0u)) {
        return BL_ERROR_ARGUMENT;
    }
    if (!bl_http_method_valid(method)) {
        return BL_ERROR_MALFORMED;
    }
    result = bl_http_fields_fit(url_len, headers_len, lim);
    if (result != BL_OK) {
        return result;
    }
    if (!bl_http_url_valid((const uint8_t *)url, url_len) ||
        !bl_http_headers_valid((const uint8_t *)headers, headers_len)) {
        return BL_ERROR_MALFORMED;
    }
    size = BORROWLINK_HTTP_REQUEST_FIXED_SIZE + url_len + headers_len;
    if (cap < size) {
        return BL_ERROR_BUFFER_TOO_SMALL;
    }
    out[0] = method;
    bl_put_u16(out + 1, (uint16_t)url_len);
    bl_put_u16(out + 3, (uint16_t)headers_len);
    memcpy(out + BORROWLINK_HTTP_REQUEST_FIXED_SIZE, url, url_len);
    if (headers_len != 0u) {
        memcpy(out + BORROWLINK_HTTP_REQUEST_FIXED_SIZE + url_len, headers,
               headers_len);
    }
    *len = size;
    return BL_OK;
}

bl_result bl_http_request_head_size(const uint8_t *in, size_t n, size_t *head,
                                    const bl_http_limits *lim)
{
    size_t url_len;
    size_t headers_len;
    bl_result result;

    if (in == NULL || head == NULL || lim == NULL) {
        return BL_ERROR_ARGUMENT;
    }
    if (n < BORROWLINK_HTTP_REQUEST_FIXED_SIZE || !bl_http_method_valid(in[0])) {
        return BL_ERROR_MALFORMED;
    }
    url_len = bl_get_u16(in + 1);
    headers_len = bl_get_u16(in + 3);
    result = bl_http_fields_fit(url_len, headers_len, lim);
    if (result != BL_OK) {
        return result;
    }
    *head = BORROWLINK_HTTP_REQUEST_FIXED_SIZE + url_len + headers_len;
    return BL_OK;
}

bl_result bl_http_decode_request(const uint8_t *in, size_t n,
                                 bl_http_request_view *out,
                                 const bl_http_limits *lim)
{
    bl_http_request_view view;
    size_t head;
    bl_result result;

    if (in == NULL || out == NULL || lim == NULL) {
        return BL_ERROR_ARGUMENT;
    }
    result = bl_http_request_head_size(in, n, &head, lim);
    if (result != BL_OK) {
        return result;
    }
    if (n < head) {
        return BL_ERROR_MALFORMED;
    }
    view.method = in[0];
    view.url_len = bl_get_u16(in + 1);
    view.headers_len = bl_get_u16(in + 3);
    view.url = (const char *)(in + BORROWLINK_HTTP_REQUEST_FIXED_SIZE);
    view.headers = view.url + view.url_len;
    view.body = in + head;
    view.body_len = n - head;
    if (view.body_len > lim->max_body) {
        return BL_ERROR_LIMIT_EXCEEDED;
    }
    if (!bl_http_url_valid((const uint8_t *)view.url, view.url_len) ||
        !bl_http_headers_valid((const uint8_t *)view.headers,
                               view.headers_len)) {
        return BL_ERROR_MALFORMED;
    }
    *out = view;
    return BL_OK;
}

bl_result bl_http_encode_response(uint8_t *out, size_t cap, size_t *len,
                                  uint16_t status, const char *headers,
                                  size_t headers_len)
{
    size_t size;

    if (len != NULL) {
        *len = 0u;
    }
    if (out == NULL || len == NULL || (headers == NULL && headers_len != 0u)) {
        return BL_ERROR_ARGUMENT;
    }
    if (!bl_http_status_valid(status)) {
        return BL_ERROR_MALFORMED;
    }
    if (headers_len > BORROWLINK_HTTP_FIELD_MAX) {
        return BL_ERROR_LIMIT_EXCEEDED;
    }
    if (!bl_http_headers_valid((const uint8_t *)headers, headers_len)) {
        return BL_ERROR_MALFORMED;
    }
    size = BORROWLINK_HTTP_RESPONSE_FIXED_SIZE + headers_len;
    if (cap < size) {
        return BL_ERROR_BUFFER_TOO_SMALL;
    }
    bl_put_u16(out, status);
    bl_put_u16(out + 2, (uint16_t)headers_len);
    if (headers_len != 0u) {
        memcpy(out + BORROWLINK_HTTP_RESPONSE_FIXED_SIZE, headers, headers_len);
    }
    *len = size;
    return BL_OK;
}

bl_result bl_http_response_head_size(const uint8_t *in, size_t n, size_t *head,
                                     const bl_http_limits *lim)
{
    size_t headers_len;

    if (in == NULL || head == NULL || lim == NULL) {
        return BL_ERROR_ARGUMENT;
    }
    if (n < BORROWLINK_HTTP_RESPONSE_FIXED_SIZE ||
        !bl_http_status_valid(bl_get_u16(in))) {
        return BL_ERROR_MALFORMED;
    }
    headers_len = bl_get_u16(in + 2);
    if (headers_len > lim->max_headers) {
        return BL_ERROR_LIMIT_EXCEEDED;
    }
    *head = BORROWLINK_HTTP_RESPONSE_FIXED_SIZE + headers_len;
    return BL_OK;
}

bl_result bl_http_decode_response(const uint8_t *in, size_t n,
                                  bl_http_response_view *out,
                                  const bl_http_limits *lim)
{
    bl_http_response_view view;
    size_t head;
    bl_result result;

    if (in == NULL || out == NULL || lim == NULL) {
        return BL_ERROR_ARGUMENT;
    }
    result = bl_http_response_head_size(in, n, &head, lim);
    if (result != BL_OK) {
        return result;
    }
    if (n < head) {
        return BL_ERROR_MALFORMED;
    }
    view.status = bl_get_u16(in);
    view.headers_len = bl_get_u16(in + 2);
    view.headers = (const char *)(in + BORROWLINK_HTTP_RESPONSE_FIXED_SIZE);
    view.body = in + head;
    view.body_len = n - head;
    if (view.body_len > lim->max_body) {
        return BL_ERROR_LIMIT_EXCEEDED;
    }
    if (!bl_http_headers_valid((const uint8_t *)view.headers,
                               view.headers_len)) {
        return BL_ERROR_MALFORMED;
    }
    *out = view;
    return BL_OK;
}

bl_result bl_http_body_add(size_t *total, size_t chunk,
                           const bl_http_limits *lim)
{
    if (total == NULL || lim == NULL) {
        return BL_ERROR_ARGUMENT;
    }
    if (chunk > lim->max_body || *total > lim->max_body - chunk) {
        return BL_ERROR_LIMIT_EXCEEDED;
    }
    *total += chunk;
    return BL_OK;
}
