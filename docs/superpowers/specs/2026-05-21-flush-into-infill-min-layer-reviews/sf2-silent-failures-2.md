# Silent-failure hunter #2

Agent ID: aa72da26752b8e9f9

## Findings

### Critical
- **C1: `mark_wiping_extrusions` residual `volume_to_wipe` silently absorbed.** No logging when residual > 0. Users with small prime tower + `min_layer=5` could overflow silently. **Fix:** `BOOST_LOG_TRIVIAL(warning)` once per slice when residual + min_layer > 0.
- **C2: Verify invalidation lands in right block.** If new key lands in fallthrough `else { invalidate_all_steps(); }` it works by accident. Must explicitly join the `flush_into_*` sibling list to lock the contract.
  File: `src/libslic3r/PrintObject.cpp:1419-1424`
- **C3: ByObject sequence silently ignores gate.** `m_print_config_ptr` null on per-object ToolOrdering construction; lines 680/707 skip. Wipe tower is disabled in ByObject mode anyway, but UI still shows the field. **Fix:** grey out / hide when `print_sequence == ByObject`, or document.

### High
- **H1: `get_layer_at_printz` nullptr + raft accounting.** Add debug log on nullptr; verify `Layer::id()` raft semantics (already verified in our earlier exploration — id includes raft offset).
- **H2: `region.config()` shadowing of `object.config()`.** PrintRegionConfig inherits from PrintObjectConfig and can shadow per-volume. Existing `flush_into_infill` already reads from object — same blind spot inherited. **Fix:** document tooltip-only: object-scope, not per-modifier.
- **H3: UI editability vs value desync on scripted preset import.** Value persists while greyed; re-enabling silently activates. Existing behavior across Orca, not a regression.

### Medium
- **M1: 3MF round-trip key allowlist (`bbs_3mf.cpp`).** `grep flush_into Format/bbs_3mf.cpp` returned nothing — strong signal the allowlist is computed via `Preset::print_options()` at `Preset.cpp:1125`. Confirm by reading `bbs_3mf.cpp` serialization for per-object config.
  **NEEDS VERIFICATION.**
- **M2: `filament_extruder_override_keys` audit.** Confirmed: no flush_* keys in the override list. No hazard.
- **M3: UI "different from default" marker.** Tooltip should explain `0 = disabled`, `N >= 1 = enable from layer N onward`.

### Low
- **L1: `EPSILON` consistency** between line 1635 and the new gate's lookup — same source, same epsilon. No drift hazard.
- **L2: `ensure_perimeters_infills_order` re-checks via `is_overriddable`** — gate applies consistently. Confirmed.
