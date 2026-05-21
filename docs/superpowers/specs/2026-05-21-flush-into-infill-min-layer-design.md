# Design: `flush_into_infill_min_layer`

**Date:** 2026-05-21
**Branch:** `nightly-builds-with-bc`
**Status:** Approved (post 8-reviewer pass)

## Problem

OrcaSlicer's existing per-object option **Flush into objects' infill** purges filament-change residue into internal infill on every layer where infill is present. With transparent or light-coloured outer walls printed early in the bottom shell, the mixed-colour purge becomes visible through the first solid layers.

The user wants a way to defer flush-into-infill so the bottom shell is never used as a purge target.

## Decisions

| Decision | Choice | Rationale |
|---|---|---|
| Threshold unit | Object-local **layer number** | User chose this over Z (mm) because intent maps to "skip the bottom shell" which is layer-counted. |
| Scope of gate | **Only flush-into-infill**, not flush-into-object or flush-into-support | User-specified narrow scope; smallest behavioural surface. |
| Config home | Per-object (`PrintObjectConfig`) | Matches `flush_into_infill`'s home; allows per-object tuning. |
| Semantic | "Start at object layer N (inclusive)" with **0 = disabled** sentinel | Matches codebase idiom (`raft_layers`, `bottom_shell_layers`, `enforce_support_layers` all use `min=0, default=0`). |
| Layer numbering | **Object-local, 1-based, excluding raft** | What the user expects when looking at GCodeViewer. Internally subtract `slicing_parameters().raft_layers()` from `Layer::id()` to undo the raft offset (PrintObjectSlice.cpp:31). |
| Gate placement | Outer call sites in `mark_wiping_extrusions` and `ensure_perimeters_infills_order`, **not** inside `is_overriddable` | Avoids changing `is_overriddable_and_mark` semantics during the `collect_extruders` planning pass and avoids per-entity O(log N) layer lookups in the hot path. |

## Architecture

### Config option

New per-object `coInt` named `flush_into_infill_min_layer`. Default `0`, min `0`, max `5000`, mode `comAdvanced`, category `"Flush options"`.

Tooltip explicitly notes:
- 0 = disabled (no restriction).
- Excludes raft layers.
- Applies to infill only — has no effect on "Flush into this object" perimeter purging or on the wipe tower.
- Object-wide (not per-region) — set high enough to cover all regions' `bottom_shell_layers` for mixed-bottom-shell objects.
- No effect when Print Sequence is **By Object** (wipe tower disabled).

### Gate logic

The gate is evaluated at the **execution** sites in `WipingExtrusions`, not at the **planning** site (`collect_extruders` → `is_overriddable_and_mark`). This keeps `something_overridable` accounting and `layer_tools.extruders` membership consistent with the no-gate baseline — only the actual override-marking step changes its behaviour.

Two execution sites in `src/libslic3r/GCode/ToolOrdering.cpp`:

1. `WipingExtrusions::mark_wiping_extrusions` (around line 1635) — already resolves `this_layer` via `object->get_layer_at_printz(lt.print_z, EPSILON)`. The object-local layer index is computed once per (object, layer-tools) pair at this point.
2. `WipingExtrusions::ensure_perimeters_infills_order` (around line 1750) — same lookup pattern; same computation.

A new helper sibling to `is_overriddable` is added on `WipingExtrusions`:

```cpp
bool is_infill_overriddable_at_layer(const PrintObject& object, int object_local_layer) const;
```

It returns `true` when `flush_into_infill_min_layer == 0`, or when `object_local_layer >= min_layer - 1`. A negative `object_local_layer` (raft layer or empty leading-layer renumbering edge case) fails closed with a `BOOST_LOG_TRIVIAL(debug)` line.

Call sites filter only `erInternalInfill` entities (perimeter-purge via `flush_into_objects` is intentionally not gated).

### Invalidation

Two invalidation paths exist for `flush_into_infill`-style keys and both must list the new key:

1. **PrintObject-level** at `src/libslic3r/PrintObject.cpp:1420` — invalidates `psWipeTower` + `psGCodeExport`.
2. **Print-level** at `src/libslic3r/Print.cpp:341` — invalidates `psWipeTower` + `psSkirtBrim`.

These two paths are pre-existing for the sibling options; the asymmetry around `psSkirtBrim` (only Print-level invalidates it) is pre-existing too and not corrected here to keep the upstream-merge diff minimal.

### Preset persistence

The key is added to `s_Preset_print_options` at `src/libslic3r/Preset.cpp:1126`, between `"flush_into_infill"` and `"flush_into_objects"`, to preserve grouping. This is the only allowlist that matters — `bbs_3mf.cpp` serializes per-object config via a generic `obj->config.keys()` iteration (writer at line 7810, reader at line 2130), so no allowlist update there is needed.

### GUI

- `src/slic3r/GUI/Tab.cpp:2655` — add an `append_single_option_line` row immediately after the `flush_into_infill` row. No custom indent widget; dependency is conveyed by the greyed-out toggle below.
- `src/slic3r/GUI/GUI_Factories.cpp:68` — add the key to the `"Flush options"` category vector for the right-click → "Set/Edit Object Settings" surface.
- `src/slic3r/GUI/ConfigManipulation.cpp` — separate `toggle_line` call mirroring the per-object pattern at line 911:
  ```cpp
  toggle_line("flush_into_infill_min_layer",
              !is_global_config && have_prime_tower && config->opt_bool("flush_into_infill"));
  ```

## Error handling and edge cases

| Case | Behaviour |
|---|---|
| `flush_into_infill_min_layer = 0` | Fast path: helper returns true immediately. Behaviour identical to current code. |
| Raft present, user enters "1" | First object layer above the raft is eligible. Subtraction `Layer::id() - raft_layers()` correctly yields `0` for the first object layer. |
| `Layer::id() < raft_layers()` (shouldn't happen, but PrintObjectSlice.cpp:772 has `set_id(... - 1)` renumbering) | Helper logs `BOOST_LOG_TRIVIAL(debug)` and returns false (fail-closed). |
| `get_layer_at_printz` returns nullptr | Caller already does `continue` (existing pattern at ToolOrdering.cpp:1635). Helper never reached. |
| `flush_into_infill = false` | Existing `is_overriddable` check at ToolOrdering.cpp:1567 still returns false first. New helper never reached. |
| `flush_into_objects = true` | Existing early-return at ToolOrdering.cpp:1565 still returns true first. New helper never reached. Perimeter purging on bottom layers is intentionally unaffected. |
| `print_sequence == ByObject` | Wipe tower is disabled in this mode; option is effectively a no-op. Tooltip notes this. |
| Spiral vase mode | No infill exists. Helper never reached. |
| Multi-region with mixed `bottom_shell_layers` | Single object-wide threshold may not perfectly align with one region's bottom shell. Tooltip directs user to set the value high enough to cover all regions. |
| Old 3MF saved without the key | Loads as registered default (0 = disabled) via `set_deserialize`. Behaviour-preserving. |
| Existing profile JSONs in `resources/profiles/` | None reference the new key; all silently inherit 0. No profile edits needed. |

## Testing

Manual smoke tests (no automated tests added — `tests/fff_print/` doesn't currently exercise wipe-tower override paths and adding one would widen the diff further):

1. Two-colour cube, `flush_into_infill = true`, `flush_into_infill_min_layer = 5`. Slice and visually inspect the G-code preview's per-layer purge zones: no purge into infill on layers 1-4, purge resumes at layer 5+.
2. Same setup with `raft_layers = 3`, `flush_into_infill_min_layer = 1`. Purge eligible starting at the **first object layer above the raft**, not at the first raft layer.
3. Set `flush_into_infill_min_layer = 0` with `flush_into_infill = true`. Behaviour identical to current OrcaSlicer.
4. Toggle `flush_into_infill = false` while `min_layer = 5`. Field greys out in UI; option has no behavioural effect (existing `is_overriddable` check denies first). Re-enable: gate snaps back to "from layer 5+".
5. Print sequence By Object. UI shows field but slicing ignores it (wipe tower disabled). Verify no crashes; G-code matches current.

## Diff footprint

8 files, ~45 lines of net additions:

| File | Lines added | Notes |
|---|---|---|
| `src/libslic3r/PrintConfig.cpp` | ~13 | New `def` block. |
| `src/libslic3r/PrintConfig.hpp` | 1 | OPT_PTR entry. |
| `src/libslic3r/GCode/ToolOrdering.cpp` | ~12 | New helper + two call-site filters + object-local-layer computation hoisted. |
| `src/libslic3r/GCode/ToolOrdering.hpp` | 1 | Helper declaration. |
| `src/libslic3r/PrintObject.cpp` | 1 | Invalidation chain entry. |
| `src/libslic3r/Print.cpp` | 1 | Invalidation chain entry. |
| `src/libslic3r/Preset.cpp` | 1 | `s_Preset_print_options` entry. |
| `src/slic3r/GUI/Tab.cpp` | 1 | UI row. |
| `src/slic3r/GUI/GUI_Factories.cpp` | 1 | Category vector entry. |
| `src/slic3r/GUI/ConfigManipulation.cpp` | ~3 | New `toggle_line` call. |

Merge-conflict hotspots: `PrintConfig.cpp` near the flush-options block; `Preset.cpp` near `s_Preset_print_options`. Upstream rarely edits these sections, so the expected conflict frequency is low.

## Out of scope (deliberate non-fixes)

These were raised during review and **not** addressed in this design, with rationale:

- **Pre-existing `psSkirtBrim` asymmetry between PrintObject-level (`PrintObject.cpp:1420`) and Print-level (`Print.cpp:341`) invalidation chains.** Sibling `flush_into_*` options have the same gap. Fixing all four widens the upstream-merge diff for a separate concern.
- **Per-region threshold (`region.config()` instead of `object.config()`)**. Would require lifting the option to `PrintRegionConfig`, breaking the symmetry with `flush_into_infill`. Tooltip note instead.
- **Z-mm threshold mode** (the rejected alternative from brainstorming). Adds a mode selector and dual fields for marginal value.
- **Wipe-tower residual-volume warning** (raised by SF2 C1). When the gate denies purge on bottom layers, `volume_to_wipe` residual flows to the wipe tower as before — no new behaviour, no warning added. If overflow becomes a real user complaint, address separately.
- **`is_overriddable_in_principle` / `is_overriddable_now` split** (raised by R3). The chosen call-site placement achieves the same separation without a function rename or signature churn.

## References

Review outputs (raw findings) are saved under `docs/superpowers/specs/2026-05-21-flush-into-infill-min-layer-reviews/`:

- `r1-conventions-correctness.md` — feature-dev:code-reviewer
- `r2-pr-style.md` — pr-review-toolkit:code-reviewer
- `r3-architectural.md` — Plan agent
- `r4-open-ended.md` — general-purpose
- `sf1-silent-failures.md` — pr-review-toolkit:silent-failure-hunter (round 1)
- `sf2-silent-failures-2.md` — pr-review-toolkit:silent-failure-hunter (round 2)

Plus the two pre-wave reviews from earlier in the brainstorming session that established the raft-offset, `Preset.cpp:1126`, and idiomatic-default findings.
