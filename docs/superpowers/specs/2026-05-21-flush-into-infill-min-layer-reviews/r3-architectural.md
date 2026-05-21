# Reviewer 3 — Plan (architectural)

Agent ID: a43fc3533dec94132

## Findings

### Critical
- **C1: `something_overridable` semantic shift in collection phase.** `is_overriddable_and_mark` runs during `collect_extruders` (ToolOrdering.cpp:680, 707) BEFORE wipe-volume planning. Gating there causes layer_tools.extruders to get the original-extruder appended for sub-min layers — silently changes tool ordering and toolchange count per layer, not just where purge lands. **Fix:** split into `is_overriddable_in_principle` (collection, no layer gate) vs `is_overriddable_now` (execution, with gate). Apply gate only inside `mark_wiping_extrusions` / `ensure_perimeters_infills_order`.
  File: `src/libslic3r/GCode/ToolOrdering.cpp:1559`, `ToolOrdering.hpp:48-51`

- **C2: `ensure_perimeters_infills_order` cache coherence.** Stale entity_map entries from prior incremental psWipeTower runs could disagree with fresh gate decision on whether `is_entity_overridden` returns true. **Fix:** ensure entity_map is cleared on invalidation; assert marking and order-enforcement passes agree on layer set.

### High
- **H1: `psSkirtBrim` not invalidated.** Skirt/brim depends on tool-ordering output (cf. PrintObject.cpp:1445, 1450, 1453, 1457 cascading psSkirtBrim from object slice/perimeter/infill/support invalidation). The existing `flush_into_*` chain at line 1419-1424 doesn't invalidate psSkirtBrim either — pre-existing hole, but new option inherits it. **Fix:** add `m_print->invalidate_step(psSkirtBrim)` to the case at line 1419 (retro-fit covers the siblings too).
  File: `src/libslic3r/PrintObject.cpp:1419-1424`

- **H2: `ByObject` print sequence may silently no-op.** `ToolOrdering.cpp:382-423` (per-object constructor) skips `is_overriddable_and_mark` (`m_print_config_ptr` is null). Need to verify `mark_wiping_extrusions` is still reachable under `print_sequence == ByObject`. **Fix:** verify reachability; document in tooltip if not.

### Medium
- **M1: Multi-region per-shell-count mismatch.** Region A `bottom_shell_layers=3`, region B `=8`, single object-wide threshold silently dirties region A's first solid-infill above layer 3 even if user wanted "above all bottom shells." **Fix:** at minimum document; better — compute effective threshold as `max(min_layer, region.config().bottom_shell_layers + 1)`.

- **M2: `flush_into_objects` shares the gate** — perimeter purging at line 1564-1565 bypasses the gate. (Same as R1/R2 finding.) **Fix:** branch on role first, gate only the infill arm.

- **M3: Adaptive layer height intuition.** "Layer 5" varies in Z. Tooltip should clarify it's an object-local layer count, not a Z height.

### Low
- **L1: Verify no other serialization whitelist.** Grep for whitelist lists outside `s_Preset_print_options` that filter what survives 3MF round-trip.
- **L2: Tab.cpp UI placement.** Conventional pattern is sub-option appears indented after all siblings, not between parent and siblings.
- **L3: Profile inheritance with default 0.** Confirmed: ConfigOptionInt default 0 inherits cleanly. No fix.
- **L4: Add `if (min_layer == 0) return true;` short-circuit.** Already in design (`if (min_layer > 0) { ... }`); rephrase if clearer.

### Critical files
- src/libslic3r/GCode/ToolOrdering.{cpp,hpp}
- src/libslic3r/PrintObject.cpp
- src/libslic3r/Print.cpp
- src/libslic3r/PrintConfig.cpp
