#pragma once

/*
 * BorrowLink HTTP profile (PROTOCOL.md section 9).
 *
 * Only the profile header and its two text fields are encoded here.  The body
 * is "the remaining stream bytes": the caller sends it through the session as
 * ordinary DATA chunks after the head produced by the encoders below, and
 * DATA_END_STREAM is the only end marker.  Nothing in this layer needs the whole
 * request or response in memory, and nothing allocates.
 *
 *   request head   method(1) url_len(2) headers_len(2) url headers
 *   response head  status(2) headers_len(2) headers
 *
 * Integers are big endian (section 7 byte order, same as every other field in
 * this library).  Return codes are bl_result.  BL_ERROR_LIMIT_EXCEEDED is the
 * case the session answers with RESET: LIMIT_EXCEEDED
 * (BORROWLINK_RESET_LIMIT_EXCEEDED).
 *
 * Streaming receive: the first DATA frames may be shorter than the head.  Call
 * bl_http_request_head_size() / bl_http_response_head_size() with the first
 * bytes (at least the fixed part) to learn how many bytes to collect, then
 * pass the collected bytes to the decoder.  Both checks run the caller's
 * limits against the length fields, so an oversized head is refused before any
 * of it is buffered.
 *
 * All encoders validate everything first and write nothing on failure.
 */

#include <borrowlink/borrowlink.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BORROWLINK_HTTP_REQUEST_FIXED_SIZE 5u
#define BORROWLINK_HTTP_RESPONSE_FIXED_SIZE 4u
/* url_len and headers_len are 16-bit fields. */
#define BORROWLINK_HTTP_FIELD_MAX 65535u

typedef enum {
    BL_HTTP_GET = 0x01,
    BL_HTTP_POST = 0x02,
    BL_HTTP_PUT = 0x03,
    BL_HTTP_PATCH = 0x04,
    BL_HTTP_DELETE = 0x05,
    BL_HTTP_HEAD = 0x06,
    BL_HTTP_OPTIONS = 0x07
} bl_http_method;

/* Caller-chosen ceilings.  0 means "none allowed", not "unlimited". */
typedef struct {
    size_t max_url;
    size_t max_headers;
    size_t max_body;
} bl_http_limits;

typedef struct {
    uint8_t method;
    const char *url;
    size_t url_len;
    const char *headers;
    size_t headers_len;
    const uint8_t *body; /* bytes after the head inside the input, may be empty */
    size_t body_len;
} bl_http_request_view;

typedef struct {
    uint16_t status;
    const char *headers;
    size_t headers_len;
    const uint8_t *body; /* bytes after the head inside the input, may be empty */
    size_t body_len;
} bl_http_response_view;

/* Writes the request head (5 + url_len + headers_len bytes) and sets *len.
 *   BL_ERROR_MALFORMED          unknown method, empty url, url or headers not
 *                               valid (see below)
 *   BL_ERROR_LIMIT_EXCEEDED     url_len > lim->max_url, headers_len >
 *                               lim->max_headers, or a length that does not
 *                               fit its 16-bit field
 *   BL_ERROR_BUFFER_TOO_SMALL   cap < head size
 *   BL_ERROR_ARGUMENT           NULL out/len/lim, or NULL url/headers with a
 *                               non-zero length
 * url: UTF-8, no C0 control bytes (this includes NUL, CR, LF) and no DEL.
 * headers: UTF-8; "Name: value" lines separated by CRLF, one optional CRLF
 * after the last; Name is an RFC 9110 token; no NUL, no CR or LF other than
 * the CRLF separators, no empty line.  Empty headers (length 0) are valid. */
bl_result bl_http_encode_request(uint8_t *out, size_t cap, size_t *len,
                                 uint8_t method, const char *url,
                                 size_t url_len, const char *headers,
                                 size_t headers_len, const bl_http_limits *lim);

/* `in` holds the head and, optionally, the start of the body.  Same errors as
 * the encoder, plus BL_ERROR_MALFORMED when `n` is shorter than the head the
 * length fields announce.  Views point into `in`.  Body bytes present in `in`
 * beyond the head count against lim->max_body. */
bl_result bl_http_decode_request(const uint8_t *in, size_t n,
                                 bl_http_request_view *out,
                                 const bl_http_limits *lim);

/* status 100..599; 4xx and 5xx are ordinary responses.  Writes the head
 * (4 + headers_len bytes).  No limits argument: the Host chooses what it
 * sends; the receiver applies its limits in bl_http_decode_response(). */
bl_result bl_http_encode_response(uint8_t *out, size_t cap, size_t *len,
                                  uint16_t status, const char *headers,
                                  size_t headers_len);

/* lim->max_headers and lim->max_body apply (max_url is ignored). */
bl_result bl_http_decode_response(const uint8_t *in, size_t n,
                                  bl_http_response_view *out,
                                  const bl_http_limits *lim);

/* Total head size announced by the first bytes of a request / response, after
 * the method (status) / limits checks.  `n` must cover the fixed part (5 / 4 bytes),
 * otherwise BL_ERROR_MALFORMED.  BL_ERROR_LIMIT_EXCEEDED when a length field
 * exceeds the limit. */
bl_result bl_http_request_head_size(const uint8_t *in, size_t n, size_t *head,
                                    const bl_http_limits *lim);
bl_result bl_http_response_head_size(const uint8_t *in, size_t n, size_t *head,
                                     const bl_http_limits *lim);

/* Running body total: adds `chunk` to *total and fails with
 * BL_ERROR_LIMIT_EXCEEDED (leaving *total unchanged) when the result would
 * exceed lim->max_body or overflow size_t. */
bl_result bl_http_body_add(size_t *total, size_t chunk,
                           const bl_http_limits *lim);

#ifdef __cplusplus
}
#endif
