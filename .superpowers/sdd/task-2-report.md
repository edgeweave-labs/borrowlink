# Task 2: Beacon codec report

Status: complete

Implementation commit SHA: `d1fdeadd1ed00901198837744a1ad45dc6421b19`

## RED evidence

- Before the API declaration, `tests/host/test_wire.c` failed to compile because `bl_beacon` and the codec functions were undeclared.
- After adding the declarations but before `src/wire.c`, the test compiled and failed at link with missing `bl_beacon_encode` and `bl_beacon_decode` symbols.

## GREEN tests

- `tests/host/run.sh` passed: protocol constants and Beacon tests under both C11 and C++17 callers.
- `git diff --check` passed.

## Changes

- Added the public `bl_beacon` type and encode/decode declarations.
- Added a fixed-size, big-endian Beacon codec with validation and caller-owned buffers.
- Decode validates into a temporary value, leaving caller output unchanged on errors.
- Registered `src/wire.c` in CMake and expanded the host runner for both test sources and language callers.

## Risks / concerns

- Decode accepts unknown flag bits for forward compatibility, while encode rejects them as specified by the brief.
- Only the host test matrix was run; an ESP-IDF build was not available in this worktree.
