# Design: `flush_into_infill_min_layer`

**Date:** 2026-05-21
**Branch:** `nightly-builds-with-bc`
**Status:** Approved (post 18-reviewer pass: 8 in wave 1, 10 in wave 2)

## Problem

OrcaSlicer's existing per-object option **Flush into objects' infill** purges filament-change residue into internal infill on every layer where infill is present. With transparent or light-coloured outer walls printed early in the bottom shell, the mixed-colour purge becomes visible through the first solid layers.

The user wants a way to defer flush-into-infill so the bottom shell is never used as a purge target.

## Decisions

| Decision | Choice | Rationale |
|---|---|---|
| Threshold unit | Object-local **layer number** | User chose this over Z (mm) because intent maps to "skip the bottom shell" which is layer-counted. |
| Scope of gate | **Only flush-into-infill**, not flush-into-object or flush-into-support | User-specified narrow scope; smallest behavioural surface. |
| Config home | Per-object (`PrintObjectConfig`) | Matches `flush_into_infill`'s home; allows per-object tuning. |
| Semantic | "Start at object layer N (1-based, inclusive)" with **0 = disabled** sentinel | Matches codebase idiom (`raft_layers`, `enforce_support_layers` both `min=0, default=0`). 1-based layer 0 is meaningless, so 0 is unambiguously the disabled sentinel. |
| Layer numbering | **Object-local, 1-based, excluding raft, excluding stripped empty leading layers** | `Layer::id()` is 0-based and "offsetted by number of raft layers" (Layer.hpp:269). Initial ids start at `slicing_parameters().raft_layers()` (PrintObjectSlice.cpp:31). Empty leading layers are stripped and ids of remaining layers decremented (PrintObjectSlice.cpp:766-773) — strip-and-renumber preserves the invariant `first_object_layer.id() == raft_layers()`. Therefore `int(this_layer->id()) - int(slicing_parameters().raft_layers())` yields a 0-based object-local index. The `-1` in the comparison (`object_local_layer >= min_layer - 1`) converts the user's 1-based input to a 0-based index. |
| Gate placement | **Inside `WipingExtrusions::is_overriddable()`**, in the `erInternalInfill` arm after the existing `flush_into_objects` and `flush_into_infill` early-returns | Earliest review wave (wave-1) considered moving the gate out to call sites to avoid changing `is_overriddable_and_mark`'s side effect on `something_overridable`. Wave-2 reviewers (5 agents) showed that the outer-gate-only placement creates a real correctness gap: the planning pass at `collect_extruders` (ToolOrdering.cpp:680, 707) trusts that overriddable infill WILL be overridden and therefore does NOT add `region.config().sparse_infill_filament` to `lt.extruders` for sub-min layers; the execution-pass gate then denies the override; G-code generation falls back to the original filament, which the wipe tower never planned a toolchange for. Putting the gate inside `is_overriddable` ensures the planning pass sees the same eligibility decision, flipping `something_nonoverriddable=true` and correctly adding the original filament to `lt.extruders`. The original concern about scope-leak to `flush_into_objects` perimeters is addressed by ordering: the `flush_into_objects` early-return at line 1564 fires first; the role check at 1567 only allows `erInternalInfill` to reach the new gate. |

## Architecture

### Config option

New per-object `coInt` named `flush_into_infill_min_layer`. Default `0`, min `0`, max `5000`, category `"Flush options"`.

**`def->mode` is intentionally NOT set** — it inherits `comSimple` to match the parent option `flush_into_infill`, which also omits the mode field. Setting `comAdvanced` would hide the child gate row from users in Simple mode while the parent toggle stayed visible.

Tooltip explicitly notes:
- `0` = disabled (no restriction); negative values from corrupted profiles also behave as disabled.
- Counts **object layers**, not raft or build-plate Z. 1-based.
- Applies to infill purging only — has no effect on "Flush into this object" perimeter purging.
- Purge volume that would have landed on infill is **redirected to the wipe tower** — expect a taller / wider wipe tower on gated layers.
- Object-wide (not per-region) — set high enough to cover all regions' `bottom_shell_layers` for mixed-bottom-shell objects.
- No effect when **By object** print sequence is active with multiple extruders (the wipe tower is disabled in that mode).

### Gate logic

The gate is added **inside `WipingExtrusions::is_overriddable()`** at `src/libslic3r/GCode/ToolOrdering.cpp:1559`, after the existing `flush_into_objects` early-return (line 1565) and the `flush_into_infill && role == erInternalInfill` filter (line 1567). The signature is unchanged.

```cpp
bool WipingExtrusions::is_overriddable(const ExtrusionEntityCollection& eec, const PrintConfig& print_config,
                                      const PrintObject& object, const PrintRegion& region) const
{
    if (print_config.filament_soluble.get_at(m_layer_tools->extruder(eec, region)))
        return false;

    if (object.config().flush_into_objects)
        return true;

    if (!object.config().flush_into_infill || eec.role() != erInternalInfill)
        return false;

    // flush_into_infill_min_layer gate: only allow purge into infill on object-local layer >= N.
    const int min_layer = object.config().flush_into_infill_min_layer.value;
    if (min_layer > 0) {
        const Layer* this_layer = object.get_layer_at_printz(m_layer_tools->print_z, EPSILON);
        if (this_layer == nullptr)
            return false; // print_z does not map to an object layer (raft-only, support-only, etc.)
        const size_t raft_layers = object.slicing_parameters().raft_layers();
        if (this_layer->id() < raft_layers) {
            BOOST_LOG_TRIVIAL(warning) << "flush_into_infill_min_layer: Layer::id() (" << this_layer->id()
                                       << ") < raft_layers (" << raft_layers << ") for object " << object.id()
                                       << " at print_z=" << m_layer_tools->print_z << "; denying override.";
            return false;
        }
        const size_t object_local_layer = this_layer->id() - raft_layers;
        if (object_local_layer < static_cast<size_t>(min_layer - 1))
            return false;
    }

    return true;
}
```

Because the gate lives inside `is_overriddable`, it automatically applies at all five call sites without changes:

- `is_overriddable_and_mark` (ToolOrdering.hpp:48-51) — the planning pass at `collect_extruders` (ToolOrdering.cpp:680, 707). Correct membership of `lt.extruders` falls out.
- `mark_wiping_extrusions` (ToolOrdering.cpp:1654, 1678) — execution-pass marking.
- `ensure_perimeters_infills_order` (ToolOrdering.cpp:1766, 1790) — rescue pass.

The `flush_into_objects` perimeter path (line 1564-1565) returns `true` before the gate runs, so perimeter purging is intentionally unaffected. The `is_overriddable_and_mark` is a thin wrapper that calls `is_overriddable` (ToolOrdering.hpp:48-51) — no separate gate insertion needed.

**Required include in ToolOrdering.cpp:** `#include <boost/log/trivial.hpp>` — the file does not currently include it.

### Invalidation

Two invalidation paths exist for `flush_into_infill`-style keys; both must list the new key:

1. **PrintObject-level** at `src/libslic3r/PrintObject.cpp:1420` — invalidates `psWipeTower` + `psGCodeExport`. Add the new key to the existing `flush_into_*` chain.
2. **Print-level** at `src/libslic3r/Print.cpp:341` — invalidates `psWipeTower` + `psSkirtBrim`. Add the new key adjacent to `flush_into_infill`.

(`flush_into_objects` is intentionally absent from `Print.cpp:341` — pre-existing asymmetry that we do not fix here. `flush_into_infill` and the new key both go through both paths since they can be changed via the print-preset path.)

`psWipeTower` invalidation cascades through `Print::_make_wipe_tower` to clear `m_wipe_tower_data` and `m_tool_ordering` (Print.cpp:2368-2369), which destroys the per-layer `WipingExtrusions::entity_map` instances. There is no stale-state risk across an incremental re-slice.

### Preset persistence

Add `"flush_into_infill_min_layer"` to `s_Preset_print_options` at `src/libslic3r/Preset.cpp:1126`, immediately after the existing `"flush_into_infill"` entry (line 1125). This list controls preset *save serialization*, not dirty detection (which compares per-key via `DynamicPrintConfig::equals`). The entry is mandatory for the key to survive `save_current_preset`.

3MF round-trip uses `obj->config.keys()` directly (bbs_3mf.cpp:7810 writer, :2130 reader) — no allowlist update needed. Caveat: only *per-object overrides* are written to the 3MF; preset-level values rely on the active preset being applied at load time. This is the existing behaviour for `flush_into_infill` and is not changed here.

### GUI

- `src/slic3r/GUI/PrintConfig.hpp` — add `((ConfigOptionInt, flush_into_infill_min_layer))` to the `PrintObjectConfig` `OPT_PTR` block at **line 1006**, immediately after `flush_into_infill` (line 1005) and before `flush_into_support` (line 1006-current). Note: the existing order in this block is `flush_into_objects`, `flush_into_infill`, `flush_into_support` (lines 1003-1006), not alphabetical.
- `src/slic3r/GUI/Tab.cpp:2655` — add `append_single_option_line("flush_into_infill_min_layer", "multimaterial_settings_flush_options#flush-into-objects-infill")` immediately after the existing `flush_into_infill` row. This also registers the key with the `OptionsSearcher` index automatically.
- `src/slic3r/GUI/GUI_Factories.cpp:68` — add `"flush_into_infill_min_layer"` to the `"Flush options"` category vector for the right-click → "Set/Edit Object Settings" surface.
- `src/slic3r/GUI/ConfigManipulation.cpp` — separate `toggle_line` call after line 911 (the existing `flush_into_objects` toggle, which uses `!is_global_config`):
  ```cpp
  toggle_line("flush_into_infill_min_layer",
              !is_global_config
              && have_prime_tower
              && config->opt_bool("flush_into_infill")
              && config->opt_enum<PrintSequence>("print_sequence") != PrintSequence::ByObject);
  ```

## Helper signature

The new option is gated **entirely inside `is_overriddable`**. **No new public helper is added.** This dispenses with several review concerns at once: type design (R6 H1), cohesion drift (R6 M3), helper naming (R2 L1, R3 H3), and call-site role-check duplication (R3 H3).

## Error handling and edge cases

| Case | Behaviour |
|---|---|
| `flush_into_infill_min_layer = 0` | Fast path: gate skipped. Behaviour identical to current code. |
| `flush_into_infill_min_layer < 0` (corrupted profile) | Treated as disabled by the `min_layer > 0` short-circuit. |
| Raft present, user enters `1` | First object layer above the raft is eligible. `Layer::id() == raft_layers()` for that layer; subtraction yields `0 >= 0` → eligible. |
| Stripped empty leading layers | Strip-and-renumber preserves `first_object_layer.id() == raft_layers()` invariant — gate semantics unchanged. |
| `Layer::id() < raft_layers()` | Impossible under normal flow; `BOOST_LOG_TRIVIAL(warning)` logs the discrepancy and gate fails closed. |
| `get_layer_at_printz` returns nullptr | print_z does not map to an object layer (raft-only or support-only z); gate fails closed (returns false). |
| `flush_into_infill = false` | Existing line-1567 check returns false first; gate never reached. |
| `flush_into_objects = true` | Existing line-1565 early-return returns true first; gate never reached. Perimeter purging on bottom layers intentionally unaffected. |
| Print Sequence **By object** with multiple extruders | UI toggle hides the field. Even if the value is set, the wipe tower is disabled in this mode (Print.cpp:1443 `extruders.size() > 1` check) so the gate is a no-op. |
| Print Sequence **By object** with a single extruder | No wipe tower regardless; the gate is moot. |
| Spiral vase mode | No infill exists; gate never reached. |
| Per-layer extruder switch at layer K, `min_layer > K` | Switch fires normally for K. Flush-into-infill purge is suppressed on layers `< min_layer` (including K); residual flows to the wipe tower as usual. Behaviour is orthogonal between the two features. |
| Multi-region object with mixed `bottom_shell_layers` | Single object-wide threshold. Tooltip directs user to set the value high enough to cover all regions. |
| Old 3MF without the key | Loads as registered default (0 = disabled) via `set_deserialize`. Behaviour-preserving. |
| Old Orca opens new 3MF with non-zero value | Old build collects the key in `ConfigSubstitutionContext::unrecogized_keys` and silently drops it (substitutions usually suppressed in the load UI). Old build prints with purge into bottom layers. Acceptable forward-compatibility footprint; documented for users who share projects across slicer versions. |
| Future renaming of the key | Use `PrintConfigDef::handle_legacy` at PrintConfig.cpp:7874 (NOT preset-level `renamed_from`, which renames preset names, not config keys). |

## Testing

Manual smoke tests:

1. Two-colour cube, `flush_into_infill = true`, `flush_into_infill_min_layer = 5`, `print_sequence = By layer`. Slice and inspect G-code preview's per-layer purge zones: no override-related `; CP TOOLCHANGE WIPE` segments inside the object on layers 1-4; resumes at layer 5+.
2. Same as #1 with `raft_layers = 3`, `flush_into_infill_min_layer = 1`. Purge eligible starting at the first object layer above the raft.
3. `flush_into_infill_min_layer = 0` with `flush_into_infill = true`. G-code byte-identical to current behaviour (run a no-min-layer baseline first).
4. `flush_into_infill = false` + `flush_into_infill_min_layer = 5`. UI field greys out. G-code identical to current.
5. `print_sequence = By object` with two extruders. UI field greys out; G-code identical to current.
6. `flush_into_objects = true`, `flush_into_infill = true`, `flush_into_infill_min_layer = 5`. Perimeter purging continues on layers 1-4 (gate doesn't apply); infill purging starts at layer 5.

No automated tests are added — `tests/libslic3r/` and `tests/fff_print/` currently have no coverage for `WipingExtrusions`/`ToolOrdering`, and recent ConfigOption-adding commits (verified by W2-R8) also shipped without tests.

## Diff footprint

8 files, ~30 lines of net additions (smaller than the prior consolidated estimate because the helper was removed):

| File | Lines added | Notes |
|---|---|---|
| `src/libslic3r/PrintConfig.cpp` | ~13 | New `def` block. |
| `src/libslic3r/PrintConfig.hpp` | 1 | OPT_PTR entry at line 1006. |
| `src/libslic3r/GCode/ToolOrdering.cpp` | ~14 | Gate body inside `is_overriddable` + `#include <boost/log/trivial.hpp>`. |
| `src/libslic3r/PrintObject.cpp` | 1 | Invalidation chain entry. |
| `src/libslic3r/Print.cpp` | 1 | Invalidation chain entry. |
| `src/libslic3r/Preset.cpp` | 1 | `s_Preset_print_options` entry. |
| `src/slic3r/GUI/Tab.cpp` | 1 | UI row. |
| `src/slic3r/GUI/GUI_Factories.cpp` | 1 | Category vector entry. |
| `src/slic3r/GUI/ConfigManipulation.cpp` | ~5 | New `toggle_line` call with 4-part predicate. |

Merge-conflict hotspots: `PrintConfig.cpp` near the flush-options block; `Preset.cpp` near `s_Preset_print_options`. Upstream rarely edits these sections, so expected conflict frequency is low.

## Out of scope (deliberate non-fixes)

These were raised during review and **not** addressed in this design, with rationale:

- **Pre-existing `psSkirtBrim` asymmetry between PrintObject-level (`PrintObject.cpp:1420`) and Print-level (`Print.cpp:341`) invalidation chains.** Sibling `flush_into_*` options have the same gap. Fixing all four widens the upstream-merge diff for a separate concern.
- **Per-region threshold (`region.config()` instead of `object.config()`)**. Would require lifting the option to `PrintRegionConfig`, breaking the symmetry with `flush_into_infill`. Tooltip note instead.
- **Z-mm threshold mode** (the rejected alternative from brainstorming). Adds a mode selector and dual fields for marginal value.
- **Wipe-tower residual-volume warning when min_layer makes the tower oversize.** Pre-existing behaviour: residual already flows to the wipe tower whenever flush-into-infill is partially declined; no new behaviour is introduced. Address separately if user complaints surface.
- **Companion `bool flush_into_infill_min_layer_enabled` to disambiguate the `0`-as-disabled sentinel.** Adds a second config field for marginal UX clarity. The codebase idiom (raft_layers, enforce_support_layers) accepts the sentinel convention.
- **Naming change to `flush_into_infill_first_layer`** (one reviewer's preference for matching `enforce_support_layers` idiom). `min_layer` is descriptive and well-understood; the rename churn isn't worth it.
- **Automated tests / wipe-tower test harness.** Would 5x the diff. Defer to follow-up if/when other slicer tests cover this surface.

## References

Review outputs (raw findings) are saved under:
- `docs/superpowers/specs/2026-05-21-flush-into-infill-min-layer-reviews/` (wave 1: 6 agents)
- `docs/superpowers/specs/2026-05-21-flush-into-infill-min-layer-reviews-wave2/` (wave 2: 10 agents)

Plus the two pre-wave reviews from earlier in the brainstorming session.

## Revision history

- 2026-05-21 v1: Initial spec after wave 1 (6 reviewers + 2 pre-wave). Gate placed at execution call sites; helper `is_infill_overriddable_at_layer` added on `WipingExtrusions`.
- 2026-05-21 v2 (this revision): Wave 2 (10 reviewers) surfaced a correctness gap where the outer-gate-only placement leaves `lt.extruders` membership stale on sub-min layers, breaking the `ensure_perimeters_infills_order` rescue contract. Gate is moved **back inside `is_overriddable`** in the `erInternalInfill` arm — the same placement my very first proposal suggested. The role-check ordering inside `is_overriddable` prevents scope leak to the `flush_into_objects` perimeter path. The new helper is removed (no API widening). Also: `BOOST_LOG_TRIVIAL(warning)` instead of `debug`; `mode` field dropped to match parent's comSimple default; `print_sequence != ByObject` added to the UI toggle predicate; explicit `size_t` arithmetic with underflow guard; missing `boost/log/trivial.hpp` include called out; edge-case table expanded.
