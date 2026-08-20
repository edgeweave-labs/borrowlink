#include <borrowlink/borrowlink.h>

#include <string.h>

static uint16_t bl_read_u16_be(const uint8_t *input)
{
    return (uint16_t)(((uint16_t)input[0] << 8) | input[1]);
}

static void bl_write_u16_be(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)(value >> 8);
    output[1] = (uint8_t)value;
}

static int bl_profile_is_known(uint8_t profile)
{
    return profile <= BORROWLINK_PROFILE_MESSAGE;
}

static bl_result bl_beacon_validate(const bl_beacon *beacon, int encoding)
{
    if (beacon->version != BORROWLINK_PROTOCOL_VERSION) {
        return BL_ERROR_UNSUPPORTED_VERSION;
    }
    if (!bl_profile_is_known(beacon->profile)) {
        return BL_ERROR_UNSUPPORTED_PROFILE;
    }
    if (beacon->profile == BORROWLINK_PROFILE_PRESENCE) {
        if (beacon->xid != 0u || beacon->attempt != 0u ||
            (beacon->flags & BORROWLINK_BEACON_FLAG_KNOWN_MASK) != 0u ||
            (encoding && beacon->flags != 0u)) {
            return BL_ERROR_MALFORMED;
        }
    } else if (beacon->xid == 0u) {
        return BL_ERROR_MALFORMED;
    }
    if (encoding &&
        (beacon->flags & (uint8_t)~BORROWLINK_BEACON_FLAG_KNOWN_MASK) != 0u) {
        return BL_ERROR_MALFORMED;
    }
    return BL_OK;
}

bl_result bl_beacon_encode(uint8_t *output,
                           size_t output_size,
                           const bl_beacon *beacon)
{
    bl_result result;

    if (output == NULL || beacon == NULL) {
        return BL_ERROR_ARGUMENT;
    }
    if (output_size < BORROWLINK_BEACON_SIZE) {
        return BL_ERROR_BUFFER_TOO_SMALL;
    }
    result = bl_beacon_validate(beacon, 1);
    if (result != BL_OK) {
        return result;
    }
    output[0] = beacon->version;
    output[1] = beacon->profile;
    bl_write_u16_be(output + 2, beacon->xid);
    output[4] = beacon->attempt;
    output[5] = beacon->flags;
    return BL_OK;
}

bl_result bl_beacon_decode(bl_beacon *beacon,
                           const uint8_t *input,
                           size_t input_size)
{
    bl_beacon decoded;
    bl_result result;

    if (beacon == NULL || input == NULL) {
        return BL_ERROR_ARGUMENT;
    }
    if (input_size != BORROWLINK_BEACON_SIZE) {
        return BL_ERROR_MALFORMED;
    }
    decoded.version = input[0];
    decoded.profile = input[1];
    decoded.xid = bl_read_u16_be(input + 2);
    decoded.attempt = input[4];
    decoded.flags = input[5];
    result = bl_beacon_validate(&decoded, 0);
    if (result != BL_OK) {
        return result;
    }
    *beacon = decoded;
    return BL_OK;
}

static int bl_opcode_is_known(uint8_t opcode)
{
    switch (opcode) {
    case BORROWLINK_OPCODE_DATA:
    case BORROWLINK_OPCODE_DATA_END_MESSAGE:
    case BORROWLINK_OPCODE_DATA_END_STREAM:
    case BORROWLINK_OPCODE_DATA_END_MESSAGE_STREAM:
    case BORROWLINK_OPCODE_HELLO:
    case BORROWLINK_OPCODE_ACCEPT:
    case BORROWLINK_OPCODE_STATUS:
    case BORROWLINK_OPCODE_RESET:
        return 1;
    default:
        return 0;
    }
}

bl_result bl_frame_encode(uint8_t *output,
                          size_t output_size,
                          size_t *written,
                          uint16_t seq,
                          uint8_t opcode,
                          const uint8_t *payload,
                          size_t payload_size)
{
    size_t frame_size;

    if (output == NULL || written == NULL ||
        (payload == NULL && payload_size != 0u)) {
        return BL_ERROR_ARGUMENT;
    }
    if (!bl_opcode_is_known(opcode)) {
        return BL_ERROR_UNSUPPORTED_OPCODE;
    }
    if (payload_size > SIZE_MAX - BORROWLINK_FRAME_HEADER_SIZE) {
        return BL_ERROR_BUFFER_TOO_SMALL;
    }
    frame_size = BORROWLINK_FRAME_HEADER_SIZE + payload_size;
    if (output_size < frame_size) {
        return BL_ERROR_BUFFER_TOO_SMALL;
    }
    bl_write_u16_be(output, seq);
    output[2] = opcode;
    if (payload_size != 0u) {
        memcpy(output + BORROWLINK_FRAME_HEADER_SIZE,
               payload, payload_size);
    }
    *written = frame_size;
    return BL_OK;
}

bl_result bl_frame_decode(bl_frame_view *frame,
                          const uint8_t *input,
                          size_t input_size)
{
    bl_frame_view decoded;

    if (frame == NULL || input == NULL) {
        return BL_ERROR_ARGUMENT;
    }
    if (input_size < BORROWLINK_FRAME_HEADER_SIZE) {
        return BL_ERROR_MALFORMED;
    }
    if (!bl_opcode_is_known(input[2])) {
        return BL_ERROR_UNSUPPORTED_OPCODE;
    }
    decoded.seq = bl_read_u16_be(input);
    decoded.opcode = input[2];
    decoded.payload = input + BORROWLINK_FRAME_HEADER_SIZE;
    decoded.payload_size = input_size - BORROWLINK_FRAME_HEADER_SIZE;
    *frame = decoded;
    return BL_OK;
}
