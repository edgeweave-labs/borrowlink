# BorrowLink

BorrowLink lets an IoT device hand work to a nearby paired gateway, use that gateway's network connection, and disconnect when the transaction is complete.

The protocol is currently `draft-1` and has not been implemented or validated on hardware. See [docs/PROTOCOL.md](docs/PROTOCOL.md).

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
