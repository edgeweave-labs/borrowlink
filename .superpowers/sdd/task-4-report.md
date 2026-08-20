# Task 4 Report: HELLO and ACCEPT payload codecs

## Status

Complete. Implementation commit: `d83247bb79632a3b6480ae6520595bf2eb25fefa`
(`feat: add BorrowLink handshake codec`).

## RED/GREEN evidence

- RED: after adding the public declarations and handshake vectors,
  `tests/host/run.sh` failed at link time with the expected missing symbols:
  `bl_hello_encode`, `bl_hello_decode`, `bl_accept_encode`, and
  `bl_accept_decode`.
- GREEN: after the minimal codec implementation, `tests/host/run.sh` exited
  0. It compiles and runs both C11 and C++17 host checks.

## Implementation summary

- Added caller-owned `bl_hello` and `bl_accept` payload APIs.
- Added 64-bit big-endian helpers and shared version, delivery, xid, node ID,
  and max-payload validation.
- HELLO rejects Presence/unknown profiles and HTTP with REALTIME delivery.
- Encoders validate capacity and fields before writing; decoders assign their
  caller output only after all validation succeeds.
- Updated the Development README for the draft-2 codec scope.

## Verification

| Command | Result |
|---|---|
| `tests/host/run.sh` | PASS (C11 and C++17) |
| `clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude src/wire.c tests/host/test_wire.c -o /tmp/borrowlink-wire-sanitized` | PASS |
| `/tmp/borrowlink-wire-sanitized` | PASS |
| `git diff --check` | PASS |
| `source ../.tools/esp-idf/export.sh; idf.py -B /tmp/borrowlink-draft2.rmTdaT reconfigure; idf.py -B /tmp/borrowlink-draft2.rmTdaT build` from `esp32-s3-epaper-1.54` | PASS; generated `epaper154g.bin` |

## Self-review

Reviewed the implementation diff against `docs/PROTOCOL.md` section 8 and the
Task 4 brief. Payload layouts, sizes, big-endian offsets, invalid-length
output preservation, and requested error distinctions match the task. The
codec remains pure C11 and adds no heap allocation, threads, I/O, clock, or
randomness.

## Concern

The sibling project's CMake configuration resolves its `borrowlink` component
at `/Users/birdyo/Projects/esp32/borrowlink`, not this task worktree. The
required sibling build succeeded, but its component compilation does not
directly consume this worktree's unmerged source. Host and sanitizer checks do
compile this worktree's `src/wire.c`.
