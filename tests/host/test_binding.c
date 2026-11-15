#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <borrowlink/binding.h>

/* Golden record: channel 1 (Reliable), length 5, frame 12 34 01 aa bb
 * (the frame vector from test_wire.c). */
static const uint8_t G_RECORD[] = {
    0x01, 0x00, 0x05, 0x12, 0x34, 0x01, 0xaa, 0xbb
};
/* Control channel, length 3: an empty RESET-less frame header only. */
static const uint8_t G_CONTROL[] = {0x00, 0x00, 0x03, 0x00, 0x00, 0x13};

static void test_encode_golden(void)
{
    static const uint8_t frame[] = {0x12, 0x34, 0x01, 0xaa, 0xbb};
    uint8_t out[16];
    size_t w = 0u;
    uint8_t head[3];

    assert(bl_binding_encode(out, sizeof(out), &w, BL_CHANNEL_RELIABLE, frame,
                             sizeof(frame)) == BL_OK);
    assert(w == sizeof(G_RECORD) && memcmp(out, G_RECORD, w) == 0);
    assert(bl_binding_header_encode(head, sizeof(head), BL_CHANNEL_RELIABLE,
                                    sizeof(frame)) == BL_OK);
    assert(memcmp(head, G_RECORD, 3u) == 0);
    /* maximum frame length: 0xffff */
    assert(bl_binding_header_encode(head, sizeof(head), BL_CHANNEL_CONTROL,
                                    0xffffu) == BL_OK);
    assert(head[0] == 0u && head[1] == 0xffu && head[2] == 0xffu);
}

static void test_encode_in_place_overlap(void)
{
    uint8_t buf[16] = {0, 0, 0, 0x12, 0x34, 0x01, 0xaa, 0xbb};
    uint8_t shifted[16] = {0x12, 0x34, 0x01, 0xaa, 0xbb};
    size_t w = 0u;

    /* the frame already sits where the record wants it */
    assert(bl_binding_encode(buf, sizeof(buf), &w, BL_CHANNEL_RELIABLE,
                             buf + 3, 5u) == BL_OK);
    assert(w == 8u && memcmp(buf, G_RECORD, 8u) == 0);
    /* the frame sits at the start of the buffer */
    assert(bl_binding_encode(shifted, sizeof(shifted), &w, BL_CHANNEL_RELIABLE,
                             shifted, 5u) == BL_OK);
    assert(w == 8u && memcmp(shifted, G_RECORD, 8u) == 0);
}

static void test_encode_rejections(void)
{
    static const uint8_t frame[] = {0, 0, 1};
    uint8_t out[8];
    size_t w = 77u;
    static uint8_t big[0x10001];

    assert(bl_binding_encode(out, sizeof(out), &w, 3u, frame, 3u) ==
           BL_ERROR_ARGUMENT);
    assert(bl_binding_encode(out, sizeof(out), &w, 0u, frame, 2u) ==
           BL_ERROR_MALFORMED);
    assert(bl_binding_encode(out, 5u, &w, 0u, frame, 3u) ==
           BL_ERROR_BUFFER_TOO_SMALL);
    assert(bl_binding_encode(out, 6u, &w, 0u, frame, 3u) == BL_OK);
    w = 77u;
    assert(bl_binding_encode(out, sizeof(out), &w, 0u, big, 0x10000u) ==
           BL_ERROR_MALFORMED);
    assert(w == 77u); /* untouched on failure */
    assert(bl_binding_encode(NULL, 8u, &w, 0u, frame, 3u) == BL_ERROR_ARGUMENT);
    assert(bl_binding_encode(out, 8u, NULL, 0u, frame, 3u) == BL_ERROR_ARGUMENT);
    assert(bl_binding_encode(out, 8u, &w, 0u, NULL, 3u) == BL_ERROR_ARGUMENT);
    assert(bl_binding_header_encode(out, 2u, 0u, 3u) == BL_ERROR_BUFFER_TOO_SMALL);
    assert(bl_binding_header_encode(out, 3u, 0u, 2u) == BL_ERROR_MALFORMED);
    assert(bl_binding_header_encode(out, 3u, 0u, 0x10000u) == BL_ERROR_MALFORMED);
    assert(bl_binding_header_encode(out, 3u, 5u, 3u) == BL_ERROR_ARGUMENT);
}

static void test_rx_init(void)
{
    uint8_t buf[64];
    bl_binding_rx rx;

    assert(bl_binding_rx_init(&rx, buf, sizeof(buf), 64u) == BL_OK);
    assert(bl_binding_rx_init(&rx, buf, sizeof(buf), 65u) ==
           BL_ERROR_BUFFER_TOO_SMALL);
    assert(bl_binding_rx_init(&rx, buf, sizeof(buf), 2u) == BL_ERROR_ARGUMENT);
    assert(bl_binding_rx_init(&rx, buf, sizeof(buf), 0x10000u) ==
           BL_ERROR_ARGUMENT);
    assert(bl_binding_rx_init(&rx, NULL, sizeof(buf), 8u) == BL_ERROR_ARGUMENT);
    assert(bl_binding_rx_init(NULL, buf, sizeof(buf), 8u) == BL_ERROR_ARGUMENT);
}

/* Feeds `in` in pieces of `step`, collecting every record. */
static size_t collect(bl_binding_rx *rx, const uint8_t *in, size_t n,
                      size_t step, uint8_t *channels, size_t *sizes,
                      uint8_t frames[][64], size_t max_records,
                      bl_result *result)
{
    size_t pos = 0u;
    size_t found = 0u;

    *result = BL_OK;
    while (pos < n) {
        size_t piece = n - pos < step ? n - pos : step;
        size_t off = 0u;

        while (off < piece) {
            bl_binding_record rec;
            size_t used = 0u;
            bl_result r = bl_binding_rx_feed(rx, in + pos + off, piece - off,
                                             &used, &rec);

            if (r != BL_OK) {
                *result = r;
                return found;
            }
            assert(used > 0u || off == piece);
            off += used;
            if (rec.ready) {
                assert(found < max_records);
                channels[found] = rec.channel;
                sizes[found] = rec.frame_size;
                memcpy(frames[found], rec.frame, rec.frame_size);
                found++;
            }
        }
        pos += piece;
    }
    return found;
}

static void test_rx_every_fragmentation(void)
{
    uint8_t stream[sizeof(G_RECORD) + sizeof(G_CONTROL) + sizeof(G_RECORD)];
    uint8_t buf[64];
    uint8_t channels[4];
    size_t sizes[4];
    uint8_t frames[4][64];
    bl_binding_rx rx;
    bl_result r;
    size_t step;
    size_t found;

    memcpy(stream, G_RECORD, sizeof(G_RECORD));
    memcpy(stream + sizeof(G_RECORD), G_CONTROL, sizeof(G_CONTROL));
    memcpy(stream + sizeof(G_RECORD) + sizeof(G_CONTROL), G_RECORD,
           sizeof(G_RECORD));
    for (step = 1u; step <= sizeof(stream); ++step) {
        assert(bl_binding_rx_init(&rx, buf, sizeof(buf), 61u) == BL_OK);
        found = collect(&rx, stream, sizeof(stream), step, channels, sizes,
                        frames, 4u, &r);
        assert(r == BL_OK && found == 3u);
        assert(channels[0] == BL_CHANNEL_RELIABLE && sizes[0] == 5u);
        assert(memcmp(frames[0], G_RECORD + 3, 5u) == 0);
        assert(channels[1] == BL_CHANNEL_CONTROL && sizes[1] == 3u);
        assert(memcmp(frames[1], G_CONTROL + 3, 3u) == 0);
        assert(channels[2] == BL_CHANNEL_RELIABLE && sizes[2] == 5u);
    }
}

static void test_rx_consumed_stops_at_record_end(void)
{
    uint8_t stream[sizeof(G_RECORD) * 2];
    uint8_t buf[64];
    bl_binding_rx rx;
    bl_binding_record rec;
    size_t used = 0u;

    memcpy(stream, G_RECORD, sizeof(G_RECORD));
    memcpy(stream + sizeof(G_RECORD), G_RECORD, sizeof(G_RECORD));
    assert(bl_binding_rx_init(&rx, buf, sizeof(buf), 61u) == BL_OK);
    assert(bl_binding_rx_feed(&rx, stream, sizeof(stream), &used, &rec) == BL_OK);
    assert(rec.ready == 1u && used == sizeof(G_RECORD));
    assert(rec.frame == buf && rec.frame_size == 5u);
}

static void test_rx_zero_input_and_boundary_sizes(void)
{
    uint8_t buf[300];
    uint8_t rec_bytes[3 + 255];
    bl_binding_rx rx;
    bl_binding_record rec;
    size_t used = 99u;

    assert(bl_binding_rx_init(&rx, buf, sizeof(buf), 258u) == BL_OK);
    assert(bl_binding_rx_feed(&rx, NULL, 0u, &used, &rec) == BL_OK);
    assert(used == 0u && rec.ready == 0u);
    /* exactly max_frame is accepted */
    memset(rec_bytes, 0, sizeof(rec_bytes));
    rec_bytes[0] = 1u;
    rec_bytes[1] = 0x01u;
    rec_bytes[2] = 0x02u; /* 258 */
    {
        uint8_t big[3 + 258];
        size_t n = 0u;

        memset(big, 0x11, sizeof(big));
        big[0] = 1u; big[1] = 1u; big[2] = 2u;
        assert(bl_binding_rx_init(&rx, buf, sizeof(buf), 258u) == BL_OK);
        assert(bl_binding_rx_feed(&rx, big, sizeof(big), &n, &rec) == BL_OK);
        assert(rec.ready == 1u && rec.frame_size == 258u && n == sizeof(big));
    }
}

static void test_rx_protocol_errors(void)
{
    uint8_t buf[16];
    bl_binding_rx rx;
    bl_binding_record rec;
    size_t used = 0u;
    static const uint8_t too_big[] = {0x01, 0x00, 0x11}; /* 17 > 16 */
    static const uint8_t too_short[] = {0x01, 0x00, 0x02};
    static const uint8_t zero_len[] = {0x01, 0x00, 0x00};
    static const uint8_t bad_channel[] = {0x03, 0x00, 0x03, 0, 0, 0x13};
    static const uint8_t huge[] = {0x00, 0xff, 0xff};

    assert(bl_binding_rx_init(&rx, buf, sizeof(buf), 16u) == BL_OK);
    assert(bl_binding_rx_feed(&rx, too_big, sizeof(too_big), &used, &rec) ==
           BL_ERROR_LIMIT_EXCEEDED);
    assert(bl_binding_rx_feed(&rx, G_RECORD, sizeof(G_RECORD), &used, &rec) ==
           BL_ERROR_STATE); /* dead */
    assert(bl_binding_rx_init(&rx, buf, sizeof(buf), 16u) == BL_OK);
    assert(bl_binding_rx_feed(&rx, huge, sizeof(huge), &used, &rec) ==
           BL_ERROR_LIMIT_EXCEEDED);
    assert(bl_binding_rx_init(&rx, buf, sizeof(buf), 16u) == BL_OK);
    assert(bl_binding_rx_feed(&rx, too_short, sizeof(too_short), &used, &rec) ==
           BL_ERROR_MALFORMED);
    assert(bl_binding_rx_init(&rx, buf, sizeof(buf), 16u) == BL_OK);
    assert(bl_binding_rx_feed(&rx, zero_len, sizeof(zero_len), &used, &rec) ==
           BL_ERROR_MALFORMED);
    assert(bl_binding_rx_init(&rx, buf, sizeof(buf), 16u) == BL_OK);
    assert(bl_binding_rx_feed(&rx, bad_channel, sizeof(bad_channel), &used,
                              &rec) == BL_ERROR_MALFORMED);
    /* error is raised from the 3-byte header alone, before any body arrives */
    assert(bl_binding_rx_init(&rx, buf, sizeof(buf), 16u) == BL_OK);
    {
        uint8_t partial[] = {0x01, 0x00};

        assert(bl_binding_rx_feed(&rx, partial, 2u, &used, &rec) == BL_OK);
        assert(rec.ready == 0u && used == 2u);
        assert(bl_binding_rx_feed(&rx, too_big + 2, 1u, &used, &rec) ==
               BL_ERROR_LIMIT_EXCEEDED);
    }
    assert(bl_binding_rx_feed(NULL, G_RECORD, 8u, &used, &rec) ==
           BL_ERROR_ARGUMENT);
    assert(bl_binding_rx_init(&rx, buf, sizeof(buf), 16u) == BL_OK);
    assert(bl_binding_rx_feed(&rx, NULL, 8u, &used, &rec) == BL_ERROR_ARGUMENT);
    assert(bl_binding_rx_feed(&rx, G_RECORD, 8u, NULL, &rec) == BL_ERROR_ARGUMENT);
    assert(bl_binding_rx_feed(&rx, G_RECORD, 8u, &used, NULL) == BL_ERROR_ARGUMENT);
}

static void test_reset_reason_mapping(void)
{
    assert(bl_binding_reset_reason(BL_ERROR_LIMIT_EXCEEDED) ==
           BORROWLINK_RESET_LIMIT_EXCEEDED);
    assert(bl_binding_reset_reason(BL_ERROR_MALFORMED) ==
           BORROWLINK_RESET_PROTOCOL_ERROR);
}

int main(void)
{
    test_encode_golden();
    test_encode_in_place_overlap();
    test_encode_rejections();
    test_rx_init();
    test_rx_every_fragmentation();
    test_rx_consumed_stops_at_record_end();
    test_rx_zero_input_and_boundary_sizes();
    test_rx_protocol_errors();
    test_reset_reason_mapping();
    return 0;
}
