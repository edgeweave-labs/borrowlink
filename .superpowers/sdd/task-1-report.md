# Task 1 Report

- Status: DONE
- Implementation commit SHA: `c13dc4a717e132a0783cf069f9369e830e4b9a2a`

## TDD evidence

- RED: after replacing `tests/host/test_protocol_constants.c`, `tests/host/run.sh` failed at compilation with undeclared draft-2 symbols including `BORROWLINK_BEACON_SIZE` (exit 1).
- GREEN: after replacing `include/borrowlink/borrowlink.h`, `tests/host/run.sh` passed for both C11 and C++17 with exit 0 and no output.
- `git diff --check`: passed.

## Change summary

- Published draft-2 beacon, profile, flag, frame, opcode, delivery, payload-size, and reset constants.
- Added the requested `bl_result` values and C++ linkage guards.
- Replaced the constants test with draft-2 assertions.

## Risks / concerns

- This task only publishes numeric schema and result values; behavior and wire encode/decode remain for later tasks.
- Legacy pre-draft-2 macro names are intentionally removed per the brief.
