# Task 3 Review: Common Frame Codec

## Verdict

- Spec compliance: **PASS**
- Code quality: **PASS**

The implementation stays within Task 3: it exposes only the frame view and
encode/decode API, uses a 3-byte `[seq u16 BE, opcode u8]` header, filters to
the eight defined DATA/CONTROL opcodes, and has no transport/session layer or
prohibited runtime dependency.  The public header retains C++ linkage guards.

## Findings

### Critical

None.

### Important

None.

### Minor

- **Test coverage:** the golden vector exercises only sequence `0x1234`.
  Add vectors for the `0x0000` and `0xffff` boundaries so BE sequence handling
  is pinned at both limits.  The implementation itself writes and reads both
  bytes correctly.  (`tests/host/test_wire.c:94`)
- **Test coverage:** malformed-short input confirms that decode leaves the
  view unchanged, but the unsupported-opcode path has no equivalent assertion.
  Add it to lock in the required error non-mutation guarantee.  (`tests/host/test_wire.c:116`)
- **Test coverage:** the codec validates NULL output/frame/input/written and a
  NULL payload with nonzero length, but the host checks exercise no NULL
  arguments.  Existing short-frame and short-output checks are present.
  (`src/wire.c:122`, `src/wire.c:152`; `tests/host/test_wire.c:108`)

## Checked behaviour

- Golden frame vector, caller-owned payload view, and 3-byte BE header:
  `tests/host/test_wire.c:84`, `src/wire.c:136`.
- All and only defined DATA/CONTROL opcodes are accepted:
  `src/wire.c:95`.
- Decode assigns to its caller output only after all validation succeeds:
  `src/wire.c:152`.
- C11/C++ public surface and Task 3 scope remain appropriate:
  `include/borrowlink/borrowlink.h:6`, `include/borrowlink/borrowlink.h:75`.
