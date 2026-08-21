# BorrowLink

BorrowLink lets an IoT device hand work to a nearby paired gateway, use that gateway's network connection, and disconnect when the transaction is complete.

The protocol is currently `draft-2`. See [docs/PROTOCOL.md](docs/PROTOCOL.md).

## Development

BorrowLink currently implements the draft-2 byte codec for Beacon Data,
frames, HELLO, and ACCEPT. Session policy and platform adapters are not yet
implemented.

Include the public API with:

```c
#include <borrowlink/borrowlink.h>
```

Run all host checks with:

```sh
tests/host/run.sh
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
