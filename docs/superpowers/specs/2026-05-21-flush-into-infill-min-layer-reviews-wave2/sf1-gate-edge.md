# W2-SF1 — pr-review-toolkit:silent-failure-hunter (gate edge)

Agent ID: a5b3e474a7cb5afdd

## Findings

### Critical
- **C1: Empty-leading-layer renumbering at `PrintObjectSlice.cpp:772` (`set_id(... - 1)`)** shifts layer ids when empty leading slices are stripped. User input `min_layer=5` then resolves to post-renumbering layer 5, which is a DIFFERENT physical Z than pre-renumbering layer 5. Silent threshold shift. **Fix:** subtract original (pre-strip) leading-empty count, or document + one-shot log.
  File: `src/libslic3r/PrintObjectSlice.cpp:772`. **NEEDS VERIFICATION.**

- **C2: `ensure_perimeters_infills_order` rescue path interaction.** Function exists to force-override infill that was "overriddable but not overridden" — exactly the state our gate produces. Spec says to gate at this site too, which **skips** the rescue. Original comment at lines 1770-1773 warns: "could be printed before its perimeter, or not be printed at all (in case its original extruder has not been added to LayerTools)". Combined with C3 below, the spec's gate placement leaves entities un-marked AND un-rescued. **Fix:** either fall through to a different branch that ensures `lt.has_extruder(region.config().sparse_infill_filament)`, or gate at the planning pass.

### High
- **H1: Wipe-tower silently absorbs unbounded residual volume.** No validation step checks whether tower fits the build plate. **Fix:** warn when min_layer > 0 contributed to depth > 2x prime_tower_width.
  File: `src/libslic3r/Print.cpp:3421-3430`, `WipeTower.cpp:1464`.

- **H2: `m_layer_tools->print_z` averaged across objects with different rafts.** When `print_z` is the average of slightly-mismatched Zs, `get_layer_at_printz(..., EPSILON)` can return nullptr for one object and valid layer for another. Existing `continue` masks this. **Fix:** add `BOOST_LOG_TRIVIAL(debug)` when nullptr at gate sites.

- **H3: `collect_extruders` membership wrong on bottom layers.** Spec keeps `is_overriddable_and_mark` returning TRUE during planning, so `something_nonoverriddable=false` → original extruder NOT added to `lt.extruders`. At execution gate denies override. Wipe-tower planner (Print.cpp:3410-3411) iterates `layer_tools.extruders` and never sees the original extruder → `plan_toolchange` never called. Either infill goes un-printed, or wrong extruder selected. **Fix:** the gate MUST influence the planning pass too. Re-evaluate the "per-entity O(log N)" objection — caching local-layer-per-object at outer loop start is O(1) per entity.
  File: `src/libslic3r/GCode/ToolOrdering.cpp:680, 707`. **MULTI-AGENT (R3, R5) — NEEDS VERIFICATION.**

### Medium
- **M1: Debug log on negative id is mostly dead code.** Only fires if `empty_leading_count > raft_layers`. Document context + add unit test.
- **M2: `apply_first_layer_order` interaction.** If H3 is fixed (planner adds original extruder), `first_layer_print_sequence` may produce different layer-1 ordering than baseline. Add smoke test.
- **M3: First-layer purge with `purge_in_prime_tower=true`.** Volume may underflow then clamp at 0; user thinks they got clean colour but tower didn't transition. Tooltip note required.

### Low
- **L1: Negative config value from unsanitized 3MF import** hits fast-path; functionally OK. Consider clamping `[0, 5000]` on import.
- **L2: Tooltip doesn't say "negative also = disabled"** — cosmetic.

## Bottom line
**C1, C2, H3 are the same architectural mistake from three angles** — outer-gate-only leaves planning pass stale. The spec's "no new behaviour, residual flows to wipe tower" claim is incorrect for **entity-presence accounting** (extruder membership in layer_tools), only volume bookkeeping is unchanged.
