# Task 2: Beacon codec review

Reviewed scope: `.superpowers/sdd/task-2-brief.md`,
`.superpowers/sdd/task-2-report.md`, and
`.superpowers/sdd/review-6157e65..ec1938a.diff`, with the affected current
files inspected as needed. No tests were rerun.

## Spec compliance: PASS

The implementation matches the brief's required public API, six-byte
big-endian golden vector, and exact validation semantics:

- `src/wire.c:49-64` handles caller-owned encode buffers, rejects nulls and
  undersized buffers, and rejects reserved flag bits on encode.
- `src/wire.c:74-90` handles decode null/length errors before parsing, uses a
  temporary `decoded` value, and assigns the output only after validation; all
  decode errors therefore leave output unchanged.
- `src/wire.c:21-40` validates protocol version, supported profiles, Presence
  xid/attempt/known flags, non-Presence xid, and retains unknown decode flags
  for forward compatibility.
- `src/wire.c:3-12` encodes and decodes xid in big-endian order; the required
  `01 01 12 34 00 00` golden vector is asserted in `tests/host/test_wire.c:10-31`.
- `include/borrowlink/borrowlink.h:60-73` exposes the specified C API inside
  the existing C++ linkage guard; `tests/host/run.sh:10-26` exercises C11 core
  linkage plus C++17 callers; `CMakeLists.txt:1-4` registers `src/wire.c`.

### Critical

None.

### Important

None.

### Minor

None.

## Code quality: PASS

The diff is minimal and C11-only: no heap, threading, I/O, clock, random
state, or speculative API layer. The two small endian helpers and shared
validator avoid duplicated codec logic without adding public abstraction.
Null and size checks precede all dereferences, and the header remains usable
from C++ through the existing `extern "C"` guard.

### Critical

None.

### Important

None.

### Minor

None.
