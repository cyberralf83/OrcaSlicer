# W2-R5 — feature-dev:code-explorer (adjacent paths)

Agent ID: ab48be97034f64e12

## Findings

### Critical
- **C1: `ensure_perimeters_infills_order` lacks the gate** — same finding as W2-R3 C1 (now **2 agents**). Force-override path at ToolOrdering.cpp:1780 sets the override directly. **Fix:** apply gate inside this function too. Spec already says to gate at both sites but the explorer found the same concern about the rescue function's purpose.

- **C2: `s_Preset_print_options` vs `PrintObjectConfig` scope** — reviewer flagged this as a mismatch, **BUT THIS IS A MISREAD**: `flush_into_infill` itself lives in `PrintObjectConfig` (PrintConfig.hpp:1005) AND is listed in `s_Preset_print_options` (Preset.cpp:1125). Print presets carry per-object-config defaults — that's the existing pattern. Not an issue.

### High
- **H1: ConfigManipulation toggle loop at line 895 doesn't include new key** — same as W2-R2 H2 (now **2 agents**). Spec uses a separate `toggle_line` predicate that's stronger (also gates on `flush_into_infill` value). **Verify** spec wording is preserved through implementation.

- **H2: Per-object sidebar split UI**: `SettingsFactory::get_options` unions `PrintRegionConfig::keys()` + `PrintObjectConfig::keys()`. New key appears in per-object sidebar but `flush_into_infill` (also PrintObjectConfig) does too — so they appear together. **C2 reviewer's misread leads to this H2 also being a misread.** Not an issue.

- **H3: Invalidation asymmetry between Print.cpp:341 and PrintObject.cpp:1420.** Already documented in spec as pre-existing; spec leaves it alone.

### Medium
- **M2: Search bar indexing.** `OptionsSearcher::append_options` (Search.cpp:86-90) requires the key be on a Tab optgroup. Spec adds it to `Tab.cpp:2655` via `append_single_option_line`, so this is covered. ✓

- **M3: Preset inheritance** — child with 0 inherits parent's 0; child with 5 inherits 5; explicit clear requires writing `"key": 0` in JSON. Existing behavior; document only.

### Low
- **L1: `apply_first_layer_order`** runs before `mark_wiping_extrusions`. Gate suppresses purge on layer 0; first-layer-order has no purge side effects there. Document for users.
- **L2: Wipe tower sizing not reduced by gate.** Early layers may produce larger wipe tower segments. Document user-visible consequence.

### Verified non-issues (after sanity check)
- C2 misread (print presets carry PrintObjectConfig keys).
- H2 misread (same).
