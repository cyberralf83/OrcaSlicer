# W2-R3 — Plan (architectural soundness)

Agent ID: ace71c1dd57730dc4

## Findings

### Critical
- **C1: `ensure_perimeters_infills_order` is a RESCUE function.** Its purpose is force-overriding infill that escaped marking — preventing infill-first ordering violations. Gating it means gated-layer infill prints with its ORIGINAL extruder while the perimeter is still being laid down, exactly the bug this function exists to prevent. Spec's "equivalence" claim is unverified. **Fix:** clarify semantics — either accept that on gated layers infill may print before perimeter (and document), or limit the gate to suppress force-override only when alternative ordering is safe.
  File: `src/libslic3r/GCode/ToolOrdering.cpp:1761-1783`. **NEEDS VERIFICATION.**

### High
- **H1: Helper widens API where signature overload would suffice.** Cleaner: extend `is_overriddable` with default-sentinel `int object_local_layer = -1` parameter.
- **H2: `something_overridable` silent invariant break.** `is_overriddable_and_mark` sets it true during collection (no gate). Gated layers still have `something_overridable=true` but every entity gets rejected at execution — `ensure_perimeters_infills_order` doesn't short-circuit. Wasted iteration; negligible perf, but invariant claim in spec is broken.
- **H3: Helper signature mixes responsibility with caller.** Both sites do `if (entity->role() == erInternalInfill && !is_infill_overriddable_at_layer(...))`. Role check is split across caller and callee. **Fix:** fold role check inside helper (rename to `should_gate_infill_for_layer(role, object, layer)`).

### Medium
- **M1: Single chokepoint exists but is bypassed.** Both `mark_wiping_extrusions` and `ensure_perimeters_infills_order` are called from same outer loop in Print.cpp:3298/3437. Could compute `object_local_layer` ONCE per (object, layer-tools) at chokepoint. Spec computes twice. Acceptable but flag.
- **M2: `apply_first_layer_order` runs on different eligibility model.** `first_layer_print_sequence` is computed independently. Gate on layer 1 may silently exclude purge-only filaments from first-layer extruder set. **Fix:** tooltip note.

### Low
- **L1: psSkirtBrim+psGCodeExport double-invalidation.** Pre-existing asymmetry now applies to new key.
- **L2: Gate-order in `ensure_perimeters_infills_order` short-circuit chain.** Put `at_layer` check first (int compare) for short-circuit efficiency on gated layers.
