# BorrowLink Protocol Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the empty BorrowLink component into a namespaced, host-tested protocol-constants foundation without inventing transport behavior.

**Architecture:** Keep the ESP-IDF component header-only until real behavior exists. Publish only constants already fixed by `docs/PROTOCOL.md`; transport state machines, serialization, and request interfaces remain absent until their unresolved requirements are specified.

**Tech Stack:** C11, C++17 compatibility check, POSIX shell, ESP-IDF CMake

## Global Constraints

- Public include path is `<borrowlink/borrowlink.h>`.
- The public header has no ESP-IDF dependency and compiles as both C11 and C++17.
- Protocol version is `0x01`; HTTP-request intent is `0x01`; discovery payload size is `6`; chunk header size is `3`; final-chunk flag is `0x01`.
- Control values are: in progress `0x00`, complete `0x01`, no network `0x10`, upstream error `0x11`, upstream timeout `0x12`, rejected `0x13`.
- Do not add packed structs, byte serializers, or parsers because the protocol does not yet specify integer endianness.
- Do not add BLE, Wi-Fi, pairing, retry, gateway, request, or response implementations or stubs.
- Do not add a source file merely to make the component look non-empty.

---

### Task 1: Publish and verify the fixed protocol constants

**Files:**
- Delete: `include/borrowlink.h`
- Create: `include/borrowlink/borrowlink.h`
- Create: `tests/host/test_protocol_constants.c`
- Create: `tests/host/run.sh`
- Modify: `README.md`

**Interfaces:**
- Consumes: Numeric values fixed by `docs/PROTOCOL.md` draft-1.
- Produces: `<borrowlink/borrowlink.h>` with `BORROWLINK_PROTOCOL_VERSION`, `BORROWLINK_INTENT_HTTP_REQUEST`, `BORROWLINK_DISCOVERY_PAYLOAD_SIZE`, `BORROWLINK_CHUNK_HEADER_SIZE`, `BORROWLINK_CHUNK_FLAG_FINAL`, and the six `BORROWLINK_CONTROL_*` constants.

- [ ] **Step 1: Write the failing host check**

Create `tests/host/test_protocol_constants.c`:

```c
#include <assert.h>
#include <stdint.h>

#include <borrowlink/borrowlink.h>

int main(void)
{
    assert(BORROWLINK_PROTOCOL_VERSION == UINT8_C(0x01));
    assert(BORROWLINK_INTENT_HTTP_REQUEST == UINT8_C(0x01));
    assert(BORROWLINK_DISCOVERY_PAYLOAD_SIZE == 6u);
    assert(BORROWLINK_CHUNK_HEADER_SIZE == 3u);
    assert(BORROWLINK_CHUNK_FLAG_FINAL == UINT8_C(0x01));
    assert(BORROWLINK_CONTROL_IN_PROGRESS == UINT8_C(0x00));
    assert(BORROWLINK_CONTROL_COMPLETE == UINT8_C(0x01));
    assert(BORROWLINK_CONTROL_NO_NETWORK == UINT8_C(0x10));
    assert(BORROWLINK_CONTROL_UPSTREAM_ERROR == UINT8_C(0x11));
    assert(BORROWLINK_CONTROL_UPSTREAM_TIMEOUT == UINT8_C(0x12));
    assert(BORROWLINK_CONTROL_REJECTED == UINT8_C(0x13));
    return 0;
}
```

Create executable `tests/host/run.sh`:

```sh
#!/bin/sh
set -eu

borrowlink_test_dir="$(mktemp -d "${TMPDIR:-/tmp}/borrowlink-test.XXXXXX")"
trap 'rm -rf "$borrowlink_test_dir"' EXIT HUP INT TERM

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Iinclude \
    tests/host/test_protocol_constants.c -o "$borrowlink_test_dir/test-c"
"$borrowlink_test_dir/test-c"

"${CXX:-c++}" -x c++ -std=c++17 -Wall -Wextra -Werror -Iinclude \
    tests/host/test_protocol_constants.c -o "$borrowlink_test_dir/test-cpp"
"$borrowlink_test_dir/test-cpp"
```

- [ ] **Step 2: Run the check and verify it fails**

Run: `tests/host/run.sh`

Expected: compilation fails because `<borrowlink/borrowlink.h>` does not exist.

- [ ] **Step 3: Publish the minimal header**

Delete `include/borrowlink.h` and create `include/borrowlink/borrowlink.h`:

```c
#pragma once

#include <stdint.h>

#define BORROWLINK_PROTOCOL_VERSION UINT8_C(0x01)
#define BORROWLINK_INTENT_HTTP_REQUEST UINT8_C(0x01)

#define BORROWLINK_DISCOVERY_PAYLOAD_SIZE 6u
#define BORROWLINK_CHUNK_HEADER_SIZE 3u
#define BORROWLINK_CHUNK_FLAG_FINAL UINT8_C(0x01)

#define BORROWLINK_CONTROL_IN_PROGRESS UINT8_C(0x00)
#define BORROWLINK_CONTROL_COMPLETE UINT8_C(0x01)
#define BORROWLINK_CONTROL_NO_NETWORK UINT8_C(0x10)
#define BORROWLINK_CONTROL_UPSTREAM_ERROR UINT8_C(0x11)
#define BORROWLINK_CONTROL_UPSTREAM_TIMEOUT UINT8_C(0x12)
#define BORROWLINK_CONTROL_REJECTED UINT8_C(0x13)
```

- [ ] **Step 4: Document and run the host check**

Add this to `README.md`:

````markdown
## Development

Include the public protocol constants with:

```c
#include <borrowlink/borrowlink.h>
```

Run the host compatibility check with `tests/host/run.sh`.
````

Run: `tests/host/run.sh`

Expected: both C11 and C++17 builds exit successfully with no output.

- [ ] **Step 5: Verify ESP-IDF integration**

From the sibling `esp32-s3-epaper-1.54/` project, run a clean build directory:

```bash
source ../.tools/esp-idf/export.sh
idf.py -B /tmp/borrowlink-agent-build reconfigure
idf.py -B /tmp/borrowlink-agent-build build
```

Expected: BorrowLink appears in the component list and the firmware build completes.

- [ ] **Step 6: Commit**

Commit only the files in this task with message:

```text
feat: add BorrowLink protocol constants
```
