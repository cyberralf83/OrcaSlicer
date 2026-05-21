# W2-SF2 — pr-review-toolkit:silent-failure-hunter (state/persistence)

Agent ID: ae855f35c3977a9e9

## Findings

### Critical
- **C1: Preset-level value never reaches the 3MF.** `obj->config.keys()` (bbs_3mf.cpp:7810) iterates only EXPLICITLY-set per-object keys. Preset-level value lives in print preset's DynamicPrintConfig and is NOT written to 3MF. Round-trip survives because active preset re-applies on reload — but switching presets or opening on another machine silently reverts to preset default. **Pre-existing framework behavior** (also true for existing `flush_into_infill`). **Fix:** document in spec.

### High
- **H1: No `print_sequence == ByObject` UI guard.** Field stays editable in ByObject mode (silent no-op). **Fix:** extend `toggle_line` predicate with `print_sequence != PrintSequence::ByObject` check.
  **NEW finding** — adds to ConfigManipulation toggle predicate.
- **H2: `entity_map` staleness verified clean.** `Print.cpp:2368-2369` clears `m_wipe_tower_data` and `m_tool_ordering` on psWipeTower entry; ToolOrdering::clear destroys all per-layer maps. Spec invalidation entries are sufficient.
- **H3: Filament-profile override list confirmed clean.** `filament_extruder_override_keys` contains no flush_*.
- **H4: Old Orca silently drops unknown key.** `set_deserialize_nothrow` falls through to `unrecogized_keys` and returns false. Substitutions usually suppressed. **Behavioural drift: old Orca opening new 3MF will purge into bottom layers.** **Fix:** add edge-case row in spec.
- **H5: `renamed_from` is preset-name-scoped, not key-scoped.** Future key rename uses `PrintConfigDef::handle_legacy` at PrintConfig.cpp:7874, not `renamed_from`. Document for future maintainers.

### Medium
- **M1: `per_layer_extruder_switches` × gate interaction undocumented.** Behaviourally correct (orthogonal) but undocumented. **Fix:** add edge-case row.
- **M2: Dirty-marker verified clean.** `s_Preset_print_options` controls SAVE serialization, not dirty detection. Dirty marker WILL fire on value change. Document: spec's Preset.cpp:1126 entry is mandatory for SAVE, not for dirty marker.

### Low
- **L1: psSkirtBrim asymmetry persists unfixed.** Already out-of-scope.
- **L2: Field-level "no effect" tooltip surfaces.** Mostly covered by H1 fix.

## Summary
Adds 1 critical (3MF preset persistence), 5 high (mostly verification/documentation), 2 medium (edge-case rows + spec clarification).
