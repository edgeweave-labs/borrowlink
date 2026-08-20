# Task 4 Report: HELLO and ACCEPT payload codecs

## Status

Complete. Codec implementation: `d83247bb79632a3b6480ae6520595bf2eb25fefa`
(`feat: add BorrowLink handshake codec`). Review remediation:
`02e9d0d11c78cd88de9f56b17cfaefdbc88bd45c`
(`test: cover handshake validation errors`).

## RED/GREEN evidence

- RED: after adding the public declarations and handshake vectors,
  `tests/host/run.sh` failed at link time with the expected missing symbols:
  `bl_hello_encode`, `bl_hello_decode`, `bl_accept_encode`, and
  `bl_accept_decode`.
- GREEN: after the minimal codec implementation, `tests/host/run.sh` exited
  0. It compiles and runs both C11 and C++17 host checks.
- Review-remediation GREEN: the added public API negative matrix passed against
  the existing codec. The review identified missing evidence, not a production
  defect, so there was no truthful new RED-to-production-fix cycle to record.

## Implementation summary

- Added caller-owned `bl_hello` and `bl_accept` payload APIs.
- Added 64-bit big-endian helpers and shared version, delivery, xid, node ID,
  and max-payload validation.
- HELLO rejects Presence/unknown profiles and HTTP with REALTIME delivery.
- Encoders validate capacity and fields before writing; decoders assign their
  caller output only after all validation succeeds.
- Added null, short-buffer/no-write, invalid enum, zero-value, and decode
  output-preservation coverage for both handshake codecs.
- Updated the README to state draft-2 consistently.

## Verification

| Command | Result |
|---|---|
| `tests/host/run.sh` | PASS (C11 and C++17) |
| `clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude src/wire.c tests/host/test_wire.c -o /tmp/borrowlink-wire-sanitized` | PASS |
| `/tmp/borrowlink-wire-sanitized` | PASS |
| `git diff --check` | PASS |
| worktree-backed ESP-IDF build below | PASS; generated `epaper154g.bin` |

### Worktree-backed ESP-IDF evidence

The sibling project hard-codes a component directory whose basename is
`borrowlink`. To preserve that component name without modifying the sibling
checkout, this validation used a temporary source mirror plus a temporary
`borrowlink` symlink:

```sh
mirror="$(mktemp -d /tmp/borrowlink-sibling-worktree.XXXXXX)"
rsync -a --exclude build --exclude .git \
    /Users/birdyo/Projects/esp32/esp32-s3-epaper-1.54/ "$mirror/"
component_parent="$(mktemp -d /tmp/borrowlink-draft2-component.XXXXXX)"
ln -s /Users/birdyo/Projects/esp32/borrowlink/.worktrees/draft2-wire \
    "$component_parent/borrowlink"
```

The mirror's `CMakeLists.txt` was temporarily changed only to set
`EXTRA_COMPONENT_DIRS` to `"$component_parent/borrowlink"`. From that mirror:

```sh
source /Users/birdyo/Projects/esp32/.tools/esp-idf/export.sh
idf.py -B /tmp/borrowlink-draft2-worktree-verified.91Vk2n reconfigure
ninja -C /tmp/borrowlink-draft2-worktree-verified.91Vk2n -v \
    esp-idf/borrowlink/libborrowlink.a
idf.py -B /tmp/borrowlink-draft2-worktree-verified.91Vk2n build
```

`readlink /tmp/borrowlink-draft2-component.3hys7X/borrowlink` printed
`/Users/birdyo/Projects/esp32/borrowlink/.worktrees/draft2-wire`.
`compile_commands.json:4340` and `build.ninja:17839` then named
`/tmp/borrowlink-draft2-component.3hys7X/borrowlink/src/wire.c` as the source
for `__idf_borrowlink`; that symlink resolves to this worktree. The build
produced `/tmp/borrowlink-draft2-worktree-verified.91Vk2n/epaper154g.bin`.

## Self-review

Reviewed the implementation diff against `docs/PROTOCOL.md` section 8 and the
Task 4 brief. Payload layouts, sizes, big-endian offsets, invalid-length
output preservation, and requested error distinctions match the task. The
codec remains pure C11 and adds no heap allocation, threads, I/O, clock, or
randomness.

## Concern

None. The verification harness is temporary under `/tmp`; no sibling source
or main checkout file was modified.
