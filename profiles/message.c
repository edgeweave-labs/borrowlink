#include <borrowlink/message.h>

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

static uint64_t bl_get_u64(const uint8_t *in)
{
    uint64_t v = 0u;
    size_t i;

    for (i = 0u; i < 8u; ++i) {
        v = (v << 8) | in[i];
    }
    return v;
}

static void bl_put_u64(uint8_t *out, uint64_t v)
{
    size_t i;

    for (i = 0u; i < 8u; ++i) {
        out[7u - i] = (uint8_t)v;
        v >>= 8;
    }
}

size_t bl_msg_header_size(uint8_t cmd)
{
    switch (cmd) {
    case BORROWLINK_MSG_SUBSCRIBE:
        return BORROWLINK_MSG_SUBSCRIBE_SIZE;
    case BORROWLINK_MSG_UNSUBSCRIBE:
        return BORROWLINK_MSG_UNSUBSCRIBE_SIZE;
    case BORROWLINK_MSG_PUBLISH:
        return BORROWLINK_MSG_PUBLISH_HEADER_SIZE;
    case BORROWLINK_MSG_DELIVER:
        return BORROWLINK_MSG_DELIVER_HEADER_SIZE;
    case BORROWLINK_MSG_ACK:
        return BORROWLINK_MSG_ACK_SIZE;
    default:
        return 0u;
    }
}

static int bl_msg_has_payload(uint8_t cmd)
{
    return cmd == BORROWLINK_MSG_PUBLISH || cmd == BORROWLINK_MSG_DELIVER;
}

static bl_result bl_msg_validate(const bl_msg_header *h)
{
    if (bl_msg_header_size(h->cmd) == 0u) {
        return BL_ERROR_MALFORMED;
    }
    if (h->cmd == BORROWLINK_MSG_SUBSCRIBE &&
        h->queue_policy > BORROWLINK_QUEUE_FIFO_BOUNDED) {
        return BL_ERROR_MALFORMED;
    }
    if (h->cmd == BORROWLINK_MSG_ACK &&
        h->status > BORROWLINK_ACK_NOT_AUTHORIZED) {
        return BL_ERROR_MALFORMED;
    }
    return BL_OK;
}

bl_result bl_msg_header_encode(uint8_t *output, size_t output_size,
                               size_t *written, const bl_msg_header *h)
{
    size_t size;
    bl_result result;

    if (output == NULL || written == NULL || h == NULL) {
        return BL_ERROR_ARGUMENT;
    }
    result = bl_msg_validate(h);
    if (result != BL_OK) {
        return result;
    }
    size = bl_msg_header_size(h->cmd);
    if (output_size < size) {
        return BL_ERROR_BUFFER_TOO_SMALL;
    }
    output[0] = h->cmd;
    bl_put_u16(output + 1, h->id);
    switch (h->cmd) {
    case BORROWLINK_MSG_SUBSCRIBE:
        bl_put_u64(output + 3, h->source_node_id);
        bl_put_u16(output + 11, h->topic);
        bl_put_u16(output + 13, h->lease_seconds);
        output[15] = h->queue_policy;
        break;
    case BORROWLINK_MSG_UNSUBSCRIBE:
        bl_put_u64(output + 3, h->source_node_id);
        bl_put_u16(output + 11, h->topic);
        break;
    case BORROWLINK_MSG_PUBLISH:
        bl_put_u16(output + 3, h->topic);
        break;
    case BORROWLINK_MSG_DELIVER:
        bl_put_u64(output + 3, h->source_node_id);
        bl_put_u16(output + 11, h->topic);
        break;
    default: /* ACK */
        output[3] = h->status;
        break;
    }
    *written = size;
    return BL_OK;
}

bl_result bl_msg_header_decode(bl_msg_header *header, const uint8_t *input,
                               size_t input_size)
{
    bl_msg_header h;
    size_t size;
    bl_result result;

    if (header == NULL || input == NULL) {
        return BL_ERROR_ARGUMENT;
    }
    if (input_size == 0u) {
        return BL_ERROR_MALFORMED;
    }
    size = bl_msg_header_size(input[0]);
    if (size == 0u || input_size < size ||
        (!bl_msg_has_payload(input[0]) && input_size != size)) {
        return BL_ERROR_MALFORMED;
    }
    memset(&h, 0, sizeof(h));
    h.cmd = input[0];
    h.id = bl_get_u16(input + 1);
    switch (h.cmd) {
    case BORROWLINK_MSG_SUBSCRIBE:
        h.source_node_id = bl_get_u64(input + 3);
        h.topic = bl_get_u16(input + 11);
        h.lease_seconds = bl_get_u16(input + 13);
        h.queue_policy = input[15];
        break;
    case BORROWLINK_MSG_UNSUBSCRIBE:
        h.source_node_id = bl_get_u64(input + 3);
        h.topic = bl_get_u16(input + 11);
        break;
    case BORROWLINK_MSG_PUBLISH:
        h.topic = bl_get_u16(input + 3);
        break;
    case BORROWLINK_MSG_DELIVER:
        h.source_node_id = bl_get_u64(input + 3);
        h.topic = bl_get_u16(input + 11);
        break;
    default: /* ACK */
        h.status = input[3];
        break;
    }
    result = bl_msg_validate(&h);
    if (result != BL_OK) {
        return result;
    }
    *header = h;
    return BL_OK;
}

/* ------------------------------------------------------------ parser */

void bl_msg_parser_init(bl_msg_parser *parser)
{
    if (parser != NULL) {
        memset(parser, 0, sizeof(*parser));
    }
}

static bl_result bl_msg_parser_fail(bl_msg_parser *p)
{
    p->failed = 1u;
    return BL_ERROR_MALFORMED;
}

bl_result bl_msg_parser_feed(bl_msg_parser *p, const uint8_t *input,
                             size_t input_size, int end_message,
                             size_t *consumed, bl_msg_event *event)
{
    size_t take = 0u;
    bl_result result;

    if (p == NULL || consumed == NULL || event == NULL ||
        (input == NULL && input_size != 0u)) {
        return BL_ERROR_ARGUMENT;
    }
    memset(event, 0, sizeof(*event));
    *consumed = 0u;
    if (p->failed) {
        return BL_ERROR_STATE;
    }
    if (!p->header_done) {
        if (p->have == 0u && input_size == 0u) {
            if (end_message) {
                return bl_msg_parser_fail(p); /* empty message */
            }
            return BL_OK;
        }
        if (p->have == 0u) {
            p->need = (uint8_t)bl_msg_header_size(input[0]);
            if (p->need == 0u) {
                return bl_msg_parser_fail(p);
            }
        }
        while (p->have < p->need && take < input_size) {
            p->buf[p->have++] = input[take++];
        }
        *consumed = take;
        if (p->have < p->need) {
            if (end_message) {
                return bl_msg_parser_fail(p); /* ended inside the header */
            }
            return BL_OK;
        }
        result = bl_msg_header_decode(&event->header, p->buf, p->have);
        if (result != BL_OK) {
            return bl_msg_parser_fail(p);
        }
        p->header_done = 1u;
        event->kind = BL_MSG_EV_HEADER;
        return BL_OK;
    }
    if (input_size != 0u) {
        if (!bl_msg_has_payload(p->buf[0])) {
            return bl_msg_parser_fail(p); /* bytes after a fixed-size command */
        }
        event->kind = BL_MSG_EV_PAYLOAD;
        event->data = input;
        event->size = input_size;
        *consumed = input_size;
        return BL_OK;
    }
    if (end_message) {
        event->kind = BL_MSG_EV_END;
        memset(p, 0, sizeof(*p));
    }
    return BL_OK;
}

/* ------------------------------------------------------------ client ids */

bl_result bl_msg_client_init(bl_msg_client *client, uint16_t *pending,
                             size_t capacity)
{
    if (client == NULL || pending == NULL || capacity == 0u ||
        capacity > 0xfffeu) {
        return BL_ERROR_ARGUMENT;
    }
    client->pending = pending;
    client->capacity = capacity;
    client->count = 0u;
    client->next = 1u;
    return BL_OK;
}

int bl_msg_client_is_pending(const bl_msg_client *client, uint16_t id)
{
    size_t i;

    for (i = 0u; i < client->count; ++i) {
        if (client->pending[i] == id) {
            return 1;
        }
    }
    return 0;
}

bl_result bl_msg_client_alloc_id(bl_msg_client *client, uint16_t *id)
{
    uint32_t tries;
    uint16_t candidate;

    if (client == NULL || id == NULL) {
        return BL_ERROR_ARGUMENT;
    }
    if (client->count >= client->capacity) {
        return BL_ERROR_LIMIT_EXCEEDED;
    }
    candidate = client->next;
    for (tries = 0u; tries < 0x10000u; ++tries) {
        if (candidate != 0u && !bl_msg_client_is_pending(client, candidate)) {
            client->pending[client->count++] = candidate;
            client->next = (uint16_t)(candidate + 1u);
            *id = candidate;
            return BL_OK;
        }
        candidate = (uint16_t)(candidate + 1u);
    }
    return BL_ERROR_LIMIT_EXCEEDED;
}

bl_result bl_msg_client_release_id(bl_msg_client *client, uint16_t id)
{
    size_t i;

    if (client == NULL) {
        return BL_ERROR_ARGUMENT;
    }
    for (i = 0u; i < client->count; ++i) {
        if (client->pending[i] == id) {
            client->pending[i] = client->pending[client->count - 1u];
            client->count--;
            return BL_OK;
        }
    }
    return BL_ERROR_UNKNOWN_ID;
}
