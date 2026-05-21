# Reviewer 1 — feature-dev:code-reviewer (conventions + correctness)

Agent ID: a38121514bbafb633

## Findings

### Medium
- **M1: Perf regression — `get_layer_at_printz` runs per entity inside `is_overriddable`.** Called 2-3x per entity per toolchange via `mark_wiping_extrusions`, `ensure_perimeters_infills_order`, and `is_overriddable_and_mark`. O(log N) lookup multiplied across thousands of infill entities. **Fix:** lift the lookup to the outer loop in `mark_wiping_extrusions` (which already does it at line 1635) and pass `object_local_idx` down — or add an internal helper.
  File: `src/libslic3r/GCode/ToolOrdering.cpp:1559`

- **M2: Tooltip / scope ambiguity around `flush_into_objects`.** The new gate fires only after the `flush_into_objects` early-return at line 1565. If user sets `flush_into_objects = true` with `min_layer = 3`, perimeter purging on layers 1-2 is unrestricted. Option name says "infill" but tooltip says "purging into objects' infill" — a user could reasonably expect it to gate the perimeter case too. **Fix:** make tooltip explicit that it applies only to infill flushing, not the `flush_into_objects` perimeter path.
  File: `src/libslic3r/PrintConfig.cpp` (tooltip text), `src/libslic3r/GCode/ToolOrdering.cpp:1564`

### Low
- **L1: No `max` constraint.** `coInt` field with `min=0` but no upper bound. User can enter `999999`, silently suppress purging across entire print. **Fix:** add `def->max = 5000` to match `enforce_support_layers` precedent.
  File: `src/libslic3r/PrintConfig.cpp` ~6862

- **L2: `m_slicing_params.raft_layers()` read without checking `.valid` flag.** Guarded by step ordering today (psWipeTower depends on posSlice), but not by the code. **Fix:** comment-only; the step graph protects this in practice.
  File: `src/libslic3r/GCode/ToolOrdering.cpp:1559`

### Not bugs (verified by reviewer)
- Off-by-one analysis: `min_layer=1` → all layers allowed (✓); `min_layer=2` → layer 1 blocked, layer 2+ allowed (✓). Math is correct.
- `get_layer_at_printz(raft_print_z)` returning nullptr is correct behavior — raft layers live in `m_support_layers`, not `m_layers`. The nullptr guard is effectively dead code for infill (infill only exists on object layers) but harmless.
- `ensure_perimeters_infills_order` correctly re-checks `is_overriddable` at line 1766, so the gate is honored on the force-override path too.
