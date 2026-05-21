# W2-R8 — pr-review-toolkit:pr-test-analyzer

Agent ID: a6434f2f4c47a5ee6

## Summary
Spec's "skip automated tests" reasoning is factually accurate (verified: zero references to `WipingExtrusions`/`ToolOrdering`/`flush_into_*` anywhere in `tests/`). Complies with de-facto project convention (3/3 most-recent ConfigOption-adding commits shipped without tests).

## Findings

### Critical
None.

### High
- **H1: Raft-offset arithmetic regression risk uncovered.** The `Layer::id() - raft_layers()` math + `PrintObjectSlice.cpp:772` renumber edge is the most fragile part — covered only by manual test 2 (visual G-code preview).
- **Recommended minimum test:** config-roundtrip + invalidation test confirming `flush_into_infill_min_layer` change triggers `psWipeTower` + `psGCodeExport` invalidation (mirror existing `flush_into_infill` pattern).
- **Where:** `tests/libslic3r/test_config.cpp` — pure config layer, no slicing required.

### Medium
- **M2: Edge-case table → smoke-test gaps.** Not covered: Row 3 (Layer::id() < raft_layers), Row 4 (nullptr lookup), Row 6 (flush_into_objects=true), Row 8 (spiral vase), Row 9 (mixed bottom_shell_layers), Row 10 (3MF default load).
- **Recommended:** 3MF default-load test in `tests/libslic3r/test_3mf.cpp` — asserts old 3MF without key loads as 0. Catches `set_deserialize` regressions.

### Low
- **L3: Wipe-tower G-code regression test** would be valuable but the test harness doesn't exist — building one would 5x+ the diff. Defer to follow-up PR. ✓ correctly out-of-scope.

### Test quality issues
- Manual tests rely on "visually inspect G-code preview" — not reproducible. Specify deterministic checks (e.g., `grep -c "; CP TOOLCHANGE WIPE"` per layer band).
