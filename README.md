# BorrowLink

BorrowLink lets an IoT device hand work to a nearby paired gateway, use that gateway's network connection, and disconnect when the transaction is complete.

The protocol is currently `draft-1` and has not been implemented or validated on hardware. See [docs/PROTOCOL.md](docs/PROTOCOL.md).

## Development

Include the public protocol constants with:

```c
#include <borrowlink/borrowlink.h>
```

Run the host compatibility check with `tests/host/run.sh`.
