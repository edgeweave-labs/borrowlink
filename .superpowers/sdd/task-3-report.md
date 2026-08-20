# Task 3 Report: Common Frame Codec

## Status

PASS

## Implementation commit

`ac668c69be1ab591b7a8eb532eadd38d570fe5a7` (`feat: add BorrowLink frame codec`)

## RED evidence

After adding the frame tests and public declarations, before implementing the
codec:

```text
tests/host/run.sh
Undefined symbols for architecture arm64:
  "_bl_frame_decode" ...
  "_bl_frame_encode" ...
ld: symbol(s) not found for architecture arm64
EXIT=1
```

The failure was the expected linker failure for the missing production
symbols.

## GREEN tests

```text
tests/host/run.sh     PASS
git diff --check       PASS
```

The host harness passed both C11 and C++17 builds/runs for the protocol
constants and wire tests.

## Overlap remediation

The final review identified that writing the header before copying a payload
inside `output` was not portable. A regression test uses `payload == output +
2` with three bytes of payload.

Before the fix, `tests/host/run.sh` produced:

```text
Assertion failed: (memcmp(encoded, expected, sizeof(expected)) == 0),
function test_frame_encode_allows_overlapping_payload
EXIT=134
```

The minimal fix moves a non-empty payload with `memmove` before writing the
header. The overlap regression then passes.

Remediation commit: `010ebeb61e51cfaf3d1b214719121545635f0e88`

Additional verification:

```text
tests/host/run.sh                         PASS
ASan+UBSan C11 test_wire                  PASS
ASan+UBSan C++17 test_wire                PASS
git diff --check                          PASS
```

## Summary

- Declared `bl_frame_view`, `bl_frame_encode`, and `bl_frame_decode`.
- Implemented the fixed 3-byte big-endian header codec with caller-owned
  buffers and no heap, thread, I/O, clock, or random dependencies.
- Enforced the eight known opcodes and preserved decode output on malformed or
  unsupported input.
- Made frame encoding safe for caller-provided overlapping payload ranges.
- Added round-trip, malformed-input, strict-opcode, output-preservation, and
  short-buffer, and overlap checks.

## Concerns

None for the Task 3 scope. Session and transport behavior remain intentionally
out of scope.
