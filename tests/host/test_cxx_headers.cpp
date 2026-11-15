// The public headers must be includable from C++17 and the types must be
// usable there (no C-only constructs).  Core is plain C compiled separately
// and linked in.
#include <cassert>
#include <cstring>

#include <borrowlink/binding.h>
#include <borrowlink/borrowlink.h>
#include <borrowlink/message.h>
#include <borrowlink/session.h>
#include <bl_driver.h>

int main()
{
    uint8_t tx[64];
    bl_session_config cfg{};
    bl_session session;
    bl_event event{};
    bl_action action{};

    cfg.role = BL_ROLE_INITIATOR;
    cfg.profile = BORROWLINK_PROFILE_MESSAGE;
    cfg.delivery = BORROWLINK_DELIVERY_RELIABLE;
    cfg.node_id = 1;
    cfg.max_rx_payload = 32;
    cfg.tx_buf = tx;
    cfg.tx_buf_size = sizeof(tx);
    assert(bl_session_init(&session, &cfg) == BL_OK);
    event.type = BL_EV_START;
    event.xid = 7;
    assert(bl_session_step(&session, &event, &action) == BL_OK);
    assert(action.type == BL_ACT_SEND_FRAME);
    assert(action.opcode == BORROWLINK_OPCODE_HELLO);

    bl_msg_header header{};
    uint8_t out[BORROWLINK_MSG_HEADER_MAX];
    size_t written = 0;
    header.cmd = BORROWLINK_MSG_ACK;
    header.id = 3;
    assert(bl_msg_header_encode(out, sizeof(out), &written, &header) == BL_OK);
    assert(written == BORROWLINK_MSG_ACK_SIZE);

    bl_msg_parser parser;
    bl_msg_parser_init(&parser);
    bl_msg_event mev;
    size_t used = 0;
    assert(bl_msg_parser_feed(&parser, out, written, 1, &used, &mev) == BL_OK);
    assert(mev.kind == BL_MSG_EV_HEADER);

    uint8_t rxbuf[64];
    bl_binding_rx rx;
    assert(bl_binding_rx_init(&rx, rxbuf, sizeof(rxbuf), 40) == BL_OK);
    assert(bl_binding_reset_reason(BL_ERROR_LIMIT_EXCEEDED) ==
           BORROWLINK_RESET_LIMIT_EXCEEDED);
    bl_driver driver;
    (void)driver;
    return 0;
}
