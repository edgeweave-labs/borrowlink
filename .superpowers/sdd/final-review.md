# Final Review: Draft-2 Wire Foundation

Reviewed `33885f9...0f98ec9` against
`docs/superpowers/plans/2026-08-21-wire-foundation.md` and
`docs/PROTOCOL.md` sections 4, 7, and 8.

## Strengths

- The public C11 API is compact, C++17-linkage-safe, and stays within the
  planned byte-codec boundary: no allocation, platform, session, transport,
  clock, random, or I/O dependency was added.
- Beacon, frame, HELLO, and ACCEPT layouts use the specified fixed sizes and
  big-endian integer encoding. Golden vectors pin each fixed-format codec.
- Trust-boundary validation happens before encoder writes. Every decoder
  parses into a local temporary and assigns caller output only after all
  validation succeeds, preserving output objects on error.
- `CMakeLists.txt` registers `src/wire.c`; recorded host C11 and C++17 checks,
  ASan/UBSan builds, and `git diff --check` pass. Recorded ESP-IDF evidence
  resolves this worktree's `src/wire.c`.
- The overlap remediation moves a payload before writing the frame header and
  uses `memmove`; its regression covers the formerly corrupting `output + 2`
  payload layout.
- README and protocol scope agree that this delivers draft-2 byte codecs while
  session policy and platform adapters remain deferred.

## Issues

### Critical

None.

### Important

None. The former overlapping-payload issue is closed by
`src/wire.c:187-192`: `memmove` completes before the header overwrites any
source bytes, and `tests/host/test_wire.c:108-123` protects that behaviour.

### Minor

- `tests/host/test_wire.c:51-81,116-122` — Beacon and frame failure tests do
  not compare the entire output object with its sentinel value, and the
  unsupported-opcode frame-decode path is not checked for non-mutation. The
  implementation currently uses temporaries correctly, but the global
  output-atomicity requirement is only fully regression-tested for HELLO and
  ACCEPT. Minimal fix: use full-struct `memcmp` sentinel checks after every
  failing Beacon/frame decode, including unknown opcode.

## Recommendations

1. Optionally add the small Beacon/frame atomicity checks while next touching
   `test_wire.c`.

## Ready to merge?

**Yes.** The overlap remediation closes the only merge-blocking finding and
introduces no new Critical or Important issue.
