#include <borrowlink/binding.h>

#include <string.h>

bl_result bl_binding_header_encode(uint8_t *output, size_t output_size,
                                   uint8_t channel, size_t frame_size)
{
    if (output == NULL) {
        return BL_ERROR_ARGUMENT;
    }
    if (output_size < BORROWLINK_BINDING_HEADER_SIZE) {
        return BL_ERROR_BUFFER_TOO_SMALL;
    }
    if (channel > BL_CHANNEL_REALTIME) {
        return BL_ERROR_ARGUMENT;
    }
    if (frame_size < BORROWLINK_FRAME_HEADER_SIZE ||
        frame_size > BORROWLINK_BINDING_MAX_FRAME) {
        return BL_ERROR_MALFORMED;
    }
    output[0] = channel;
    output[1] = (uint8_t)(frame_size >> 8);
    output[2] = (uint8_t)frame_size;
    return BL_OK;
}

bl_result bl_binding_encode(uint8_t *output, size_t output_size,
                            size_t *written, uint8_t channel,
                            const uint8_t *frame, size_t frame_size)
{
    size_t total;
    bl_result result;

    if (output == NULL || written == NULL || frame == NULL) {
        return BL_ERROR_ARGUMENT;
    }
    if (frame_size > BORROWLINK_BINDING_MAX_FRAME) {
        return BL_ERROR_MALFORMED;
    }
    total = BORROWLINK_BINDING_HEADER_SIZE + frame_size;
    if (output_size < total) {
        return BL_ERROR_BUFFER_TOO_SMALL;
    }
    if (channel > BL_CHANNEL_REALTIME) {
        return BL_ERROR_ARGUMENT;
    }
    if (frame_size < BORROWLINK_FRAME_HEADER_SIZE) {
        return BL_ERROR_MALFORMED;
    }
    /* Move the frame first: it may start inside the header region. */
    memmove(output + BORROWLINK_BINDING_HEADER_SIZE, frame, frame_size);
    result = bl_binding_header_encode(output, output_size, channel,
                                      frame_size);
    if (result != BL_OK) {
        return result;
    }
    *written = total;
    return BL_OK;
}

bl_result bl_binding_rx_init(bl_binding_rx *rx, uint8_t *buf, size_t buf_size,
                             size_t max_frame)
{
    if (rx == NULL || buf == NULL) {
        return BL_ERROR_ARGUMENT;
    }
    if (max_frame < BORROWLINK_FRAME_HEADER_SIZE ||
        max_frame > BORROWLINK_BINDING_MAX_FRAME) {
        return BL_ERROR_ARGUMENT;
    }
    if (buf_size < max_frame) {
        return BL_ERROR_BUFFER_TOO_SMALL;
    }
    memset(rx, 0, sizeof(*rx));
    rx->buf = buf;
    rx->buf_size = buf_size;
    rx->max_frame = max_frame;
    return BL_OK;
}

bl_result bl_binding_rx_feed(bl_binding_rx *rx, const uint8_t *input,
                             size_t input_size, size_t *consumed,
                             bl_binding_record *record)
{
    size_t take;

    if (rx == NULL || consumed == NULL || record == NULL ||
        (input == NULL && input_size != 0u)) {
        return BL_ERROR_ARGUMENT;
    }
    memset(record, 0, sizeof(*record));
    *consumed = 0u;
    if (rx->failed) {
        return BL_ERROR_STATE;
    }
    /* Start of a new record: the previous one (if any) was handed out whole. */
    while (rx->head_have < BORROWLINK_BINDING_HEADER_SIZE &&
           *consumed < input_size) {
        rx->head[rx->head_have++] = input[(*consumed)++];
    }
    if (rx->head_have < BORROWLINK_BINDING_HEADER_SIZE) {
        return BL_OK;
    }
    if (rx->frame_size == 0u) {
        size_t length = ((size_t)rx->head[1] << 8) | rx->head[2];

        if (length > rx->max_frame) {
            rx->failed = 1u;
            return BL_ERROR_LIMIT_EXCEEDED;
        }
        if (length < BORROWLINK_FRAME_HEADER_SIZE ||
            rx->head[0] > BL_CHANNEL_REALTIME) {
            rx->failed = 1u;
            return BL_ERROR_MALFORMED;
        }
        rx->frame_size = length;
        rx->channel = rx->head[0];
        rx->have = 0u;
    }
    take = rx->frame_size - rx->have;
    if (take > input_size - *consumed) {
        take = input_size - *consumed;
    }
    if (take != 0u) {
        memcpy(rx->buf + rx->have, input + *consumed, take);
    }
    rx->have += take;
    *consumed += take;
    if (rx->have == rx->frame_size) {
        record->ready = 1u;
        record->channel = rx->channel;
        record->frame = rx->buf;
        record->frame_size = rx->frame_size;
        rx->head_have = 0u;
        rx->frame_size = 0u;
        rx->have = 0u;
    }
    return BL_OK;
}

uint8_t bl_binding_reset_reason(bl_result error)
{
    return error == BL_ERROR_LIMIT_EXCEEDED ? BORROWLINK_RESET_LIMIT_EXCEEDED
                                            : BORROWLINK_RESET_PROTOCOL_ERROR;
}
