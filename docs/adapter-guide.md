# Writing an adapter

The core (`src/`, `profiles/`, `include/`) does no I/O, reads no clock or random source and
allocates nothing. An adapter is the code that does those things for it. Two adapters exist:

| adapter | where | what it provides |
|---|---|---|
| fake | `tests/fake_adapter/` | in-memory pipes and a scripted Host, used by the unit tests |
| Linux | `examples/linux-node/linux_adapter.c` | TCP socket, `CLOCK_MONOTONIC`, `/dev/urandom` |

Everything platform-neutral that sits between the core and an adapter is in `adapters/common/bl_driver.[ch]`
(stream binding + session reducer + Message parser + executing the reducer's actions). It performs no I/O either:
it calls a `send` hook and is fed bytes. A new adapter normally reuses it unchanged.

## What the ESP-IDF stream adapter (P3) has to supply

1. **`bl_driver_ops.send(ctx, bytes, n)`** returns 0 on success. Write the bytes to the lwIP/TLS socket (blocking is
   fine; the driver does not call it from an ISR). Non-zero makes the session end with a transport error.
2. **Receive loop**: call `bl_driver_feed(d, bytes, n)` with whatever `recv()` returned (any fragmentation).
   `recv() == 0` -> `bl_driver_link_closed(d)`; `recv() < 0` or the socket event loop reports an error ->
   `bl_driver_link_lost(d)`.
3. **Start**: after the TCP/TLS connection is up, `bl_driver_start(d, xid, now_ms)`. `xid` must be non-zero and come
   from the platform RNG (`esp_random()`); `now_ms` from a monotonic clock (`esp_timer_get_time()/1000`).
4. **Time**: call `bl_driver_tick(d, now_ms)` periodically (100 ms is plenty) if `handshake_timeout_ms` is set.
5. **Identity**: `bl_session_config.node_id` is a non-zero 64-bit value generated once at provisioning and kept in
   NVS (PROTOCOL.md 5.2). The EdgeWeave manifest also carries a `boot_id` that must differ on every boot.
6. **Buffers** (all static, caller-owned; sizes for `max_rx_payload = M`, `tx_buf_size = T`):
   `tx_buf` T bytes (>= 18; usable chunk is `min(peer_max_rx, T-3)`), `wire_buf` T+3, `rx_buf` M+3, and
   `uint16_t pending[N]` for the `bl_msg_client` (N = how many PUBLISH/SUBSCRIBE requests may wait for an ACK).
   Struct sizes on x86-64: `bl_driver` 240 bytes, `bl_session` 96, `bl_binding_rx` 48, `bl_msg_parser` 20,
   `bl_msg_client` 32. Object code with `-Os`: wire 1.8 KB, session 2.6 KB, binding 1.2 KB, message 1.6 KB,
   driver 2.3 KB (x86-64, indicative only; not measured on Xtensa/RISC-V).
7. **Threading**: the driver is not thread-safe. Give it one owner task; other tasks hand it work through a queue.
   `bl_driver_send()` may be called from that task between `feed` calls (the Linux node does exactly this).
8. **Session end**: the `on_end` hook fires once per session with `SESSION_RESET`, `SET_LINK_IDLE` or `DISCONNECT`
   (see `bl_action`). Reconnect, backoff and "try another Host" are product policy; the core never retries.
9. **Transport security**: the stream binding has no bonding. EdgeWeave's token (first message, topic 0x0001) is only
   acceptable for a loopback Hub (see `docs/draft-3-proposal.md` 1.2). A real Wi-Fi node needs TLS under `send`/`recv`
   and a per-node credential; that is an adapter/product concern, not core.

## What is *not* in the driver (application layer)

The manifest, data points and RPC payloads are EdgeWeave's CBOR, not BorrowLink. `examples/linux-node/cbor_min.c`
is a ~200-line subset (unsigned ints, text, bool, float64, array/map headers, a skipping reader) that is fine to lift
into firmware; EdgeWeave's `contract/gen.py` is meant to generate the real encoders for P3.

## A GATT adapter (P4) differs in exactly these places

- `send` writes one frame per characteristic write/indication (no record header: use `bl_binding_*` only for
  byte streams) and picks the characteristic from `bl_action.channel`.
- Each received characteristic value is one frame: build a `BL_EV_RX_FRAME` with `channel` taken from the
  characteristic and call the reducer directly (skip `bl_binding_rx`).
- `BL_EV_START` is sent when the Control subscription is ready (PROTOCOL.md 6), `keep_connected` comes from the Beacon.
- `BL_EV_LINK_LOST` on disconnect; `BL_EV_LINK_CLOSED` is not used (a GATT link has no orderly half-close).
