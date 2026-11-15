# BorrowLink

BorrowLink lets an IoT device hand work to a nearby paired gateway, use that gateway's network connection, and disconnect when the transaction is complete.

The protocol is currently `draft-2`. See [docs/PROTOCOL.md](docs/PROTOCOL.md).

## Development

BorrowLink implements, as a pure C11 core with caller-owned memory (no heap, no I/O,
no clock, no platform headers):

| piece | files |
|---|---|
| draft-2 byte codec: Beacon Data, frames, HELLO, ACCEPT | `include/borrowlink/borrowlink.h`, `src/wire.c` |
| session reducer `bl_session_step()`: initiator, RELIABLE, seq checks, DATA with message boundaries, close, RESET | `include/borrowlink/session.h`, `src/session.c` |
| Message profile: the five commands, streaming parser, id bookkeeping | `include/borrowlink/message.h`, `profiles/message.c` |
| stream binding (TCP framing, proposal for draft-3) | `include/borrowlink/binding.h`, `src/binding.c` |

Not yet implemented: responder role, REALTIME delivery, HTTP profile, Opaque stream, the BLE GATT adapter,
bonding. `bl_session_init()` refuses the responder role and REALTIME with explicit errors; the interfaces are in place.

Platform glue is outside the core: `adapters/common/` (platform-neutral driver), `examples/linux-node/`
(POSIX socket adapter and a reference node), `tests/fake_adapter/` (in-memory test adapter).
See [docs/adapter-guide.md](docs/adapter-guide.md). Spec questions found while implementing these, with
suggested wording, are in [docs/draft-3-proposal.md](docs/draft-3-proposal.md); PROTOCOL.md is unchanged.

Include the public API with:

```c
#include <borrowlink/session.h>   /* pulls in borrowlink.h */
#include <borrowlink/message.h>
#include <borrowlink/binding.h>
```

Run all host checks (C11 and C++17 builds, unit tests, fake-adapter traces, core purity check, then everything
again under ASan + UBSan) with:

```sh
tests/host/run.sh
```

If the machine has no libasan/libubsan, point `BL_SAN_LIBDIR` at a directory containing them
(default `/var/tmp/bl-sanlibs`), or set `BL_SKIP_SANITIZERS=1` to skip that pass knowingly.

Interoperability with EdgeWeave's Python Hub (needs a checkout of the EdgeWeave repository; it is exported with
`git archive` into a scratch directory and never modified):

```sh
tests/interop/run.sh                       # EDGEWEAVE_REPO / EDGEWEAVE_REF select the checkout and commit
BL_INTEROP_SAN=1 tests/interop/run.sh      # the C side built with ASan + UBSan
```

## ESP-IDF dependency

BorrowLink release versions are separate from the wire protocol byte version.
The first component release is `0.1.0`; the current wire protocol version is
`0x01`.

```yaml
dependencies:
  borrowlink:
    git: https://github.com/edgeweave-labs/borrowlink.git
    version: v0.1.0
```

## ESP-IDF smoke build

```bash
./scripts/bootstrap-idf.sh
export IDF_TARGET=esp32s3
source ../.tools/esp-idf-v5.5.2/export.sh
idf.py -C examples/component-smoke build
```
