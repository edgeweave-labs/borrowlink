# Task 4 Fresh Re-review

## Verdict

- Spec compliance: **PASS**
- Code quality: **PASS**

Both previous Important findings and the previous Minor finding are closed.

## Spec compliance

### Critical

None.

### Important

None.

The handshake trust-boundary matrix now covers both encoders' null arguments,
short-buffer/no-write behavior, invalid version/profile/delivery values, and
zero `xid`, `node_id`, and `max_rx_payload`
(`tests/host/test_wire.c:212-301`). Both decoders cover null and short input,
invalid enums, all required zero values, and byte-for-byte preservation of the
entire caller output on every tested error
(`tests/host/test_wire.c:186-210,303-378`). The tests are invoked from `main`
(`tests/host/test_wire.c:380-389`).

The worktree-backed ESP-IDF evidence is now sufficient. The report documents
a temporary sibling mirror, a `borrowlink` symlink resolving to this exact
worktree, an explicit `EXTRA_COMPONENT_DIRS`, and build metadata naming the
symlinked worktree's `src/wire.c` for `__idf_borrowlink`; the complete firmware
build produced `epaper154g.bin`
(`.superpowers/sdd/task-4-report.md:44-76`). This closes the previous
main-checkout acceptance gap.

### Minor

None.

## Code quality

### Critical

None.

### Important

None.

The added tests directly protect the public error-handling contract for both
codec pairs without adding a framework or production abstraction. Decode
output preservation is checked through two small shared assertion helpers
(`tests/host/test_wire.c:186-210`), while the individual trust-boundary cases
remain explicit and readable (`tests/host/test_wire.c:212-378`).

The report no longer treats a main-checkout build as evidence for this diff;
it records the exact worktree component resolution and component build source
(`.superpowers/sdd/task-4-report.md:44-76`).

### Minor

None.

The README contradiction is fixed: the protocol and implemented codec scope
now consistently say draft-2 while session policy and platform adapters remain
explicitly deferred (`README.md:5-11`).

No tests were rerun for this re-review, as requested. Results above are based
on the supplied diff, current diff-touched files, and recorded verification
evidence.
