#pragma once

/*
 * The smallest CBOR (RFC 8949) subset the EdgeWeave payloads of this example
 * need: unsigned ints, text, bool, float64, array/map headers, and a skipping
 * reader.  It is application code, not part of BorrowLink: BorrowLink carries
 * opaque payload bytes.  Fixed buffer, no allocation.
 */

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t len;
    int overflow;
} cbor_w;

void cbor_w_init(cbor_w *w, uint8_t *buf, size_t cap);
void cbor_w_uint(cbor_w *w, uint64_t v);
void cbor_w_text(cbor_w *w, const char *s);
void cbor_w_bool(cbor_w *w, int v);
void cbor_w_f64(cbor_w *w, double v);
void cbor_w_array(cbor_w *w, uint64_t count);
void cbor_w_map(cbor_w *w, uint64_t count);

typedef struct {
    const uint8_t *p;
    size_t n;
    size_t pos;
    int err;
} cbor_r;

void cbor_r_init(cbor_r *r, const uint8_t *p, size_t n);
/* map header: returns the pair count, or -1 */
int64_t cbor_r_map(cbor_r *r);
int cbor_r_uint(cbor_r *r, uint64_t *v);
int cbor_r_bool(cbor_r *r, int *v);
/* text view (not NUL terminated) */
int cbor_r_text(cbor_r *r, const char **s, size_t *len);
/* skip one whole item (any nesting up to 16 deep) */
int cbor_r_skip(cbor_r *r);
int cbor_text_equals(const char *s, size_t len, const char *lit);
