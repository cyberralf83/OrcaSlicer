# Impact Review 06 — GUI Interactions with existing Tab/Plater logic

**Commit:** `eef00f7032` — "Add minimum chute flush length filament option"
**Lens:** Outward impact of the new `filament_minimal_purge_on_chute` GUI wiring on existing TabFilament / Plater logic.
**Scope:** GUI only (`Tab.cpp`, `Plater.cpp`) plus the config/preset plumbing that the GUI relies on. `.github/` ignored.

## What was traced

| Area | File:line | Verdict |
|---|---|---|
| Optgroup placement | `Tab.cpp:4151` | OK |
| Mode visibility (comAdvanced) | `PrintConfig.cpp:2735` vs neighbors | OK |
| `toggle_option` enable/disable semantics | `Tab.cpp:4362`, `Tab.cpp:1433`, `Field.cpp:298` | OK |
| Interaction with BBL hide-loop | `Tab.cpp:4355-4362` | OK (disjoint keys) |
| Variant/multi-extruder indexing | `Tab.cpp:4343-4372` | OK (matches sibling) |
| Dirty / compare-with-system-preset | `Preset.cpp:1282` | OK |
| Settings search index | `Tab.cpp:4151` | OK |
| Plater `on_config_change` branch | `Plater.cpp:16692` | Low — redundant `update()` |
| Invalidation | `Print.cpp:289` | OK |

---

## Findings

### [Low] Plater.cpp:16692 — `update()` scene refresh is redundant for this key (harmless)

**Interaction.** The key was added to the `else if` branch in `Plater::on_config_change` that sets
`update_scheduled = true`, which causes `p->update()` (a full 3D scene refresh) to run in addition to
the unconditional `schedule_background_process()` / re-slice at the bottom of the function
(`Plater.cpp:16732-16738`).

**Why it is questionable.** Its sibling `filament_minimal_purge_on_wipe_tower` belongs in that branch
because it genuinely changes **wipe-tower geometry** shown in the 3D preview: it feeds
`WipeTower2.cpp:2198` (`wipe_volumes[i][j] = max(..., filament_minimal_purge_on_wipe_tower)`),
`WipeTower2.cpp:2302`, and `Print.cpp:3452/3459` (`volume_to_wipe`). The new
`filament_minimal_purge_on_chute` is consumed **only** in `GCode.cpp` at emission time
(`GCode.cpp:880` append_tcr, `GCode.cpp:7845` set_extruder) — it does not alter tower footprint, plate
layout, or any 3D-scene volume. So the extra `update()` does no useful scene work; the re-slice that
the unconditional `schedule_background_process()` triggers is what actually re-applies the value.

**Impact.** None observable: `p->update()` is idempotent and cheap relative to the re-slice it precedes;
the value still takes effect via the background process. This is a tidiness note, not a bug. Following
the sibling's pattern for consistency is also a defensible choice.

**Fix (optional).** Drop the key from this branch and rely on the unconditional
`schedule_background_process()` already at `Plater.cpp:16736`. If kept, no change needed — leave a one-line
comment that it is included for parity with the wipe-tower sibling, not because it touches the 3D scene.

---

## Points explicitly checked and found clean

**Mode visibility (comAdvanced).** `PrintConfig.cpp:2735` sets `def->mode = comAdvanced`, identical to
every neighbor in the "Wipe tower parameters" optgroup (`filament_minimal_purge_on_wipe_tower` 2719,
`filament_tower_interface_pre_extrusion_dist` 2751, `filament_tower_interface_purge_volume` 2775,
`filament_tower_interface_print_temp` 2783, `filament_tower_ironing_area` 2767). The new field shows in
Advanced + Expert and hides in Simple, exactly tracking its neighbors when the global mode toggle changes.
No stray field left visible/hidden out of step with the group.

**`toggle_option` semantics — no false hide/show, no stale field.** `Tab::toggle_option`
(`Tab.cpp:1433`) calls `field->toggle()`, and `Field::toggle` (`Field.cpp:298`) only enables/disables
(greys out) — it does **not** hide the line. So the new field is always present on the Multimaterial page;
for BBL printers it is enabled, for non-BBL it is greyed out. This is symmetric with the sibling
`filament_minimal_purge_on_wipe_tower`, which is `toggle_option(el, !is_BBL_printer)` (disabled for BBL).
`toggle_options()` runs inside `TabFilament::update()` (`Tab.cpp:4390`), which is invoked on preset switch,
printer switch, and variant-combo change, so the enabled/disabled state is recomputed every time and
cannot go stale.

**No conflict with the BBL hide-loop.** The loop at `Tab.cpp:4355-4358` disables a set of keys
(including `filament_minimal_purge_on_wipe_tower`) for BBL; the new line at `Tab.cpp:4362` enables
`filament_minimal_purge_on_chute` for BBL. The two operate on **disjoint** option keys, so running in the
same `toggle_options` pass produces no double-toggle or override. The inverse condition (`is_BBL_printer`
vs `!is_BBL_printer`) is intentional and correct: wipe-tower minimum is meaningful for non-BBL wipe-tower
printers, chute minimum is meaningful for BBL chute printers. `is_BBL_printer` is recomputed at
`Tab.cpp:4271` (`is_bbl_vendor()`) at the top of `toggle_options`, so a BBL↔non-BBL switch flips both
correctly.

**Variant / multi-extruder indexing matches the sibling.** Variant-indexed fields on this page use
`256 + idx` (`filament_adaptive_volumetric_speed` at `Tab.cpp:4347`, `long_retractions_when_ec`/
`retraction_distances_when_ec` at `Tab.cpp:4371-4372`). Both purge minimums are toggled with the default
index (single field, no per-extruder variant). The new chute toggle uses the same default-index call as
its sibling, so multi-extruder BBL variant-combo changes treat it identically — no index mismatch, no
"toggle hits the wrong extruder's field" risk relative to the established sibling behavior.

**Dirty indicator / compare-with-system-preset.** The key is in `s_Preset_filament_options`
(`Preset.cpp:1282`), the same whitelist that carries `filament_minimal_purge_on_wipe_tower`. This is the
gating list for filament preset save/diff/compare; with the key present, the modified ("dirty") marker,
discard/reset, and "compare with system preset" all include it correctly. No separate compare-exclusion
list needs the key. (This is the same whitelist gotcha noted in project memory for the seam feature — here
it is handled.)

**Settings search.** The search index is populated from `optgroup->append_single_option_line`
(`Tab.cpp:4151`) plus the config def's label/tooltip (`PrintConfig.cpp:2723-2732`). Because the option is
appended exactly like its sibling and has a non-empty label, it is findable in settings search; adding it
breaks no search-index assumption (no fixed-size array or hardcoded key list in the searcher relies on the
prior option count).

**Invalidation.** `Print.cpp:289` adds the key to `invalidate_state_by_config_options`, so editing it in
the Tab correctly invalidates and forces a re-slice — consistent with the value being applied at G-code
emission time. Without this the GUI edit would silently not re-slice; it is present and correct.

---

## Verdict

GUI integration is sound. The new field shows/hides with the global mode toggle in lockstep with its
optgroup neighbors, enables/disables correctly across preset/printer/variant switches, does not collide
with the BBL hide-loop, and participates correctly in dirty-tracking, system-preset comparison, and
settings search. The only note is **Low**: the `Plater::on_config_change` `update()` branch entry
(`Plater.cpp:16692`) is redundant because this key never affects 3D-scene/wipe-tower geometry (unlike its
sibling) — but it is harmless and arguably justified for parity. No Critical/High/Medium GUI-interaction
issues found.
