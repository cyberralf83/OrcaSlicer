# R2 — Bugs, Logic Errors, Security, Code Quality

Agent: feature-dev:code-reviewer
Note: this agent had no Write tool — orchestrator persisted findings on its behalf from the returned summary.

## Critical
None.

## High
None.

## Medium
None.

## Low
None at confidence ≥ 80.

## Sub-threshold finding (confidence ~65)

**Dead code entry: `flush_into_infill_min_layer` in `Print::invalidate_state_by_config_options`**
- File: `src/libslic3r/Print.cpp:343`.
- `Print::invalidate_state_by_config_options` is documented at line 93 as accepting "PrintConfig option keys only." `flush_into_infill_min_layer` is a `PrintObjectConfig` key (`PrintConfig.hpp:1006`), so this branch is unreachable via the global-config invalidation path.
- The correct per-object invalidation lives at `PrintObject.cpp:1421`, which correctly fires both `psWipeTower` and `psGCodeExport`.
- The entry in `Print.cpp` is harmless dead code, consistent with the same pattern for the sibling `flush_into_infill` at line 342 (pre-existing). Confidence 65 — below reporting threshold; logged for completeness.

## Notes (informational, not findings)

### Fix correctness — confirmed correct and complete for all call sites
Three consumers of `FREQ_SETTINGS_BUNDLE_FFF`:
1. `GUI_Factories.cpp:453` — `create_freq_settings_popupmenu()`: iterates by key/value, order-agnostic. Unaffected.
2. `GUI_Factories.cpp:736` — menu rebuild loop: iterates by key/value, order-agnostic. Unaffected.
3. `GUI_Factories.cpp:1089-1179` — `append_menu_items_flush_options()`: uses hard-coded positional indices [0], [1], [2] and calls `option->getBool()` on each. This is the crash site. Confirmed: with the fix, index [0] = `flush_into_infill` (coBool), [1] = `flush_into_objects` (coBool), [2] = `flush_into_support` (coBool), [3] = `flush_into_infill_min_layer` (coInt, never accessed by positional index). Fix is complete.

No other positional consumers exist. `FREQ_SETTINGS_BUNDLE_SLA` is empty.

### `is_overriddable` gate (`ToolOrdering.cpp:1561-1594`) — no bugs
- `min_layer > 0` guard at line 1577 correctly excludes 0 and negative values (both treated as "allow all layers").
- `static_cast<size_t>(min_layer - 1)` at line 1589 is safe: enclosing `if` proves `min_layer >= 1`.
- `get_layer_at_printz(print_z, EPSILON)` at line 1578 can return nullptr; handled at lines 1579-1580.
- `object.slicing_parameters()` at line 1581 is a cached reference (`Print.hpp:419`), no recomputation cost.
- The `id() < raft_layers` guard at lines 1582-1586 prevents unsigned underflow.

### `ConfigManipulation.cpp:917-921` — correct by design
`flush_into_infill_min_layer` gated with `!is_global_config`, hidden in the global Print tab and only shown when `toggle_print_fff_options` is called for per-object settings (from `GUI_ObjectSettings.cpp:409`, `GUI_ObjectTableSettings.cpp:303/409`). The field's appearance in `Tab.cpp:2655` (global Print tab Multimaterial page) is intentional but always hidden by the toggle. Matches the established pattern for `flush_into_objects` (line 915) and `flush_into_infill`/`flush_into_support` (lines 899-900).

### Null-dereference risk at `GUI_Factories.cpp:1126/1134/1143/1151/1160/1168`
Pre-existing, not introduced or worsened by this fix. If both `select_object_config.option()` and `global_config.option()` returned null, `option->getBool()` would crash. In practice this cannot occur because all three keys are registered in `PrintConfigDef` with defaults and are always reachable via `global_config`. Unchanged from the pre-fix state.

*(NOTE from orchestrator: SF1 disagrees with this analysis and rates it Critical. Surfaced to Wave-2 triage.)*

### Security
No user input escapes validation. `flush_into_infill_min_layer` is an `int` read from the sliced object's `PrintObjectConfig` after slicer-internal processing. The only external-input vector is a manually edited `.3mf` file; a negative value silently behaves as 0 (allow all layers) due to the `if (min_layer > 0)` guard, and an out-of-range positive is clamped by `def->max = 5000` in the UI. No concern.

### Serialization (`Preset.cpp:1127`)
Correctly listed in `s_Preset_print_options`. Round-trip is correct.

### Invalidation (`Print.cpp:343`, `PrintObject.cpp:1421`)
Per-object invalidation is correct and sufficient. The `Print.cpp` entry is dead code (sub-threshold finding above).

## Summary
0 findings at confidence ≥ 80. 1 sub-threshold finding (dead code at `Print.cpp:343`, confidence ~65, pre-existing pattern). The fix is correct and complete. The broader `flush_into_infill_min_layer` feature has no latent bugs, logic errors, security issues, or code-quality problems meeting the reporting threshold. Recommend merge.
