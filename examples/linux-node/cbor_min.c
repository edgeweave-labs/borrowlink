#include "cbor_min.h"

#include <string.h>

void cbor_w_init(cbor_w *w, uint8_t *buf, size_t cap)
{
    w->buf = buf;
    w->cap = cap;
    w->len = 0u;
    w->overflow = 0;
}

static void put(cbor_w *w, const void *data, size_t n)
{
    if (w->len + n > w->cap) {
        w->overflow = 1;
        return;
    }
    memcpy(w->buf + w->len, data, n);
    w->len += n;
}

static void head(cbor_w *w, unsigned major, uint64_t v)
{
    uint8_t b[9];
    size_t n = 1u;
    size_t i;

    if (v < 24u) {
        b[0] = (uint8_t)((major << 5) | (unsigned)v);
    } else if (v <= 0xffu) {
        b[0] = (uint8_t)((major << 5) | 24u);
        b[1] = (uint8_t)v;
        n = 2u;
    } else if (v <= 0xffffu) {
        b[0] = (uint8_t)((major << 5) | 25u);
        n = 3u;
    } else if (v <= 0xffffffffu) {
        b[0] = (uint8_t)((major << 5) | 26u);
        n = 5u;
    } else {
        b[0] = (uint8_t)((major << 5) | 27u);
        n = 9u;
    }
    if (n >= 3u) {
        for (i = n - 1u; i >= 1u; --i) {
            b[i] = (uint8_t)v;
            v >>= 8;
        }
    }
    put(w, b, n);
}

void cbor_w_uint(cbor_w *w, uint64_t v) { head(w, 0u, v); }
void cbor_w_array(cbor_w *w, uint64_t count) { head(w, 4u, count); }
void cbor_w_map(cbor_w *w, uint64_t count) { head(w, 5u, count); }

void cbor_w_text(cbor_w *w, const char *s)
{
    size_t n = strlen(s);

    head(w, 3u, n);
    put(w, s, n);
}

void cbor_w_bool(cbor_w *w, int v)
{
    uint8_t b = v ? 0xf5u : 0xf4u;

    put(w, &b, 1u);
}

void cbor_w_f64(cbor_w *w, double v)
{
    uint8_t b[9];
    uint64_t bits;
    size_t i;

    memcpy(&bits, &v, sizeof(bits));
    b[0] = 0xfbu;
    for (i = 0u; i < 8u; ++i) {
        b[8u - i] = (uint8_t)bits;
        bits >>= 8;
    }
    put(w, b, 9u);
}

/* ---- reader ---- */

void cbor_r_init(cbor_r *r, const uint8_t *p, size_t n)
{
    r->p = p;
    r->n = n;
    r->pos = 0u;
    r->err = 0;
}

/* Reads an item head. Returns the major type or -1; *arg is the argument. */
static int rhead(cbor_r *r, uint64_t *arg)
{
    unsigned ib;
    unsigned ai;
    size_t extra = 0u;
    size_t i;
    uint64_t v = 0u;

    if (r->err || r->pos >= r->n) {
        r->err = 1;
        return -1;
    }
    ib = r->p[r->pos++];
    ai = ib & 31u;
    if (ai < 24u) {
        v = ai;
    } else if (ai >= 24u && ai <= 27u) {
        extra = (size_t)1u << (ai - 24u);
        if (r->pos + extra > r->n) {
            r->err = 1;
            return -1;
        }
        for (i = 0u; i < extra; ++i) {
            v = (v << 8) | r->p[r->pos++];
        }
    } else {
        r->err = 1; /* indefinite lengths are not used by EdgeWeave */
        return -1;
    }
    *arg = v;
    return (int)(ib >> 5);
}

int64_t cbor_r_map(cbor_r *r)
{
    uint64_t a = 0u;

    if (rhead(r, &a) != 5 || a > 1000u) {
        r->err = 1;
        return -1;
    }
    return (int64_t)a;
}

int cbor_r_uint(cbor_r *r, uint64_t *v)
{
    uint64_t a = 0u;

    if (rhead(r, &a) != 0) {
        r->err = 1;
        return -1;
    }
    *v = a;
    return 0;
}

int cbor_r_bool(cbor_r *r, int *v)
{
    if (r->err || r->pos >= r->n) {
        r->err = 1;
        return -1;
    }
    if (r->p[r->pos] == 0xf5u || r->p[r->pos] == 0xf4u) {
        *v = r->p[r->pos++] == 0xf5u;
        return 0;
    }
    r->err = 1;
    return -1;
}

int cbor_r_text(cbor_r *r, const char **s, size_t *len)
{
    uint64_t a = 0u;

    if (rhead(r, &a) != 3 || a > r->n - r->pos) {
        r->err = 1;
        return -1;
    }
    *s = (const char *)(r->p + r->pos);
    *len = (size_t)a;
    r->pos += (size_t)a;
    return 0;
}

static int skip_depth(cbor_r *r, int depth)
{
    uint64_t a = 0u;
    int major;
    uint64_t i;

    if (depth > 16) {
        r->err = 1;
        return -1;
    }
    if (r->pos < r->n && r->p[r->pos] >= 0xf4u && r->p[r->pos] <= 0xf7u) {
        r->pos++; /* false true null undefined */
        return 0;
    }
    if (r->pos < r->n && r->p[r->pos] == 0xfbu) {
        if (r->n - r->pos < 9u) {
            r->err = 1;
            return -1;
        }
        r->pos += 9u;
        return 0;
    }
    major = rhead(r, &a);
    if (major < 0) {
        return -1;
    }
    switch (major) {
    case 0:
    case 1:
        return 0;
    case 2:
    case 3:
        if (a > r->n - r->pos) {
            r->err = 1;
            return -1;
        }
        r->pos += (size_t)a;
        return 0;
    case 4:
        for (i = 0u; i < a; ++i) {
            if (skip_depth(r, depth + 1) != 0) {
                return -1;
            }
        }
        return 0;
    case 5:
        for (i = 0u; i < a * 2u; ++i) {
            if (skip_depth(r, depth + 1) != 0) {
                return -1;
            }
        }
        return 0;
    default:
        r->err = 1;
        return -1;
    }
}

int cbor_r_skip(cbor_r *r)
{
    return skip_depth(r, 0);
}

int cbor_text_equals(const char *s, size_t len, const char *lit)
{
    return strlen(lit) == len && memcmp(s, lit, len) == 0;
}
