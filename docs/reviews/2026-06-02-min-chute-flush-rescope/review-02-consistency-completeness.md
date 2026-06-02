# Review 02 — Consistency & Completeness after the rescope revert

**Feature:** `filament_minimal_purge_on_chute` (per-filament, mm of filament, default 0)
**Rescope commit:** `4673720c01` — "Scope min chute flush to BBL append_tcr; revert set_extruder changes"
**Full feature range:** `eef00f7032~1 .. HEAD`
**Lens:** consistency & completeness of the wiring after the `set_extruder` clamp was reverted; tooltip accuracy; GUI vs. slicer parity; invalidation correctness.
**Scope limit:** `.github/` ignored (none touched by the feature anyway).

---

## Verdict

**No issues found in consistency/completeness.** The revert is clean, the remaining wiring is internally consistent with the BBL/`append_tcr`-only scope, the tooltip is accurate, GUI visibility matches slicer behaviour, and invalidation is correct (and matches the sibling option).

Everything below is the supporting evidence, recorded for completeness.

---

## 1. `set_extruder` is genuinely reverted to upstream — no dangling reference

- Isolated diff of the whole `GCode::set_extruder` body between `eef00f7032~1` (pre-feature, == upstream) and `HEAD` is **empty** — the function is byte-identical to upstream. Both the chute clamp block (lines that did `wipe_volume = std::max(wipe_volume, min_chute_purge)`) and the `flush_count = std::max(1, std::min(...))` change were removed and reverted to upstream `std::min(...)`.
- `grep` for `filament_minimal_purge_on_chute` across `src/` and `resources/` returns exactly 8 hits, all of them the expected wiring:
  - `PrintConfig.hpp:1456` (option declaration)
  - `PrintConfig.cpp:2722` (option definition + tooltip)
  - `Print.cpp:289` (invalidation)
  - `Preset.cpp:1282` (`s_Preset_filament_options`)
  - `Plater.cpp:16692` (`on_config_change` update list)
  - `Tab.cpp:4151` (optgroup line) and `Tab.cpp:4362` (BBL toggle)
  - `GCode.cpp:880` (the single live use, inside `append_tcr`)
- **No orphaned reference in `set_extruder`** and no reference in `append_tcr2` (the Type2 emitter has no purge/flush logic at all in its body, so there is nothing to leave dangling there).

**Conclusion:** the revert is complete; no dead reference, no half-removed code.

---

## 2. Remaining wiring is still coherent for a BBL/`append_tcr`-only feature

The option is still registered everywhere it must be for a per-filament config value that is read during G-code export:

| File | Purpose | Still needed after rescope? |
|------|---------|-----------------------------|
| `PrintConfig.hpp:1456` | `ConfigOptionFloats` member | Yes — the value is still read in `append_tcr`. |
| `PrintConfig.cpp:2722` | definition/default/bounds/tooltip | Yes. |
| `Preset.cpp:1282` (`s_Preset_filament_options`) | makes it a persisted filament-preset key | Yes — without this the per-filament value would not round-trip in presets/3MF. |
| `Print.cpp:289` | invalidation | Yes (see §5). |
| `Plater.cpp:16692` | triggers UI/state refresh on change | Yes — same group as `filament_minimal_purge_on_wipe_tower`. |
| `Tab.cpp:4151` / `4362` | shows the line + BBL enable toggle | Yes (see §4). |

Nothing in this set became **redundant** or **contradictory** after the revert. The feature is read in exactly one place (`append_tcr`), and every wiring entry above is still required for that single read to work and to be configurable/persisted. No piece is now **incomplete** either — the chain config-def → preset-key → GUI → invalidation → consumer is intact.

---

## 3. The BBL gate is internally consistent with the path confinement

- `apply_chute_min = is_real_toolchange && min_chute_purge > EPSILON && gcodegen.is_BBL_Printer()` (`GCode.cpp:883`).
- `GCode::is_BBL_Printer()` returns `m_curr_print->is_BBL_printer()` (`GCode.cpp:2028`).
- `Print::wipe_tower_type()` (`Print.hpp:1072`) is `is_BBL_printer() ? Type1 : m_config.wipe_tower_type.value`. The Type1 emitter is `append_tcr`; the Type2 emitter is `set_extruder`/`append_tcr2`.
- Therefore a BBL printer **always** routes through `append_tcr`, and `set_extruder` is unreachable for the feature's intended use. Confining the floor to `append_tcr` **and** gating it on `is_BBL_Printer()` is belt-and-braces but not contradictory: on a BBL printer both conditions are true; on a non-BBL printer the code never reaches the BBL emitter anyway, and the explicit `is_BBL_Printer()` makes the inert-ness obvious at the only call site. Consistent.

At the default value (`0`) `min_chute_purge` is `0`, so `apply_chute_min` is false and the `purge_volume` expression reduces to upstream `tcr.purge_volume < EPSILON ? 0 : std::max(tcr.purge_volume, g_min_purge_volume)`. Byte-identical-to-upstream at default holds — confirmed against `eef00f7032~1:GCode.cpp` line 873.

---

## 4. GUI visibility and slicer behaviour are now CONSISTENT

- `TabFilament::toggle_options` (`Tab.cpp:4362`): `toggle_option("filament_minimal_purge_on_chute", is_BBL_printer)`, where `is_BBL_printer = preset_bundle->is_bbl_vendor()` (`Tab.cpp:4270`).
- `toggle_option` calls `field->toggle(toggle)` (`Tab.cpp:1433`) — it **enables/disables** the field (greys it out); it does not remove the line. The adjacent wipe-tower option uses the same mechanism with the inverse predicate (`!is_BBL_printer`). So the chute option is editable only on BBL, the wipe-tower option only on non-BBL — clean inverse pair, consistent with the in-code comment.
- Slicer side: the floor is applied only when `gcodegen.is_BBL_Printer()` is true. So:
  - **BBL:** field enabled in GUI, floor active in slicer → consistent.
  - **non-BBL:** field disabled (stuck at default 0) in GUI, floor inert in slicer (both the explicit `is_BBL_Printer()` gate and the unreachable code path) → consistent.
- No **visible-but-inert** mismatch beyond the standard "disabled field" convention, and no **hidden-but-active** mismatch. Even a non-zero value carried in a 3MF from another printer would be ignored on a non-BBL printer because the slicer gate requires `is_BBL_Printer()`. This is exactly the GUI-vs-execution mismatch the rescope commit set out to fix, and it is resolved.

(Minor note, not a finding: the GUI determines BBL via `is_bbl_vendor()` while the slicer uses `Print::is_BBL_printer()`. This is the established pair used throughout this fork — e.g. the sibling wipe-tower option toggles on the same `is_BBL_printer` GUI flag — so it introduces no new inconsistency.)

---

## 5. Print.cpp invalidation is correct and not over-/under-invalidating

- `filament_minimal_purge_on_chute` sits in the same `else if` branch as `filament_minimal_purge_on_wipe_tower` (`Print.cpp:288–289`), which enqueues `psWipeTower` + `psSkirtBrim` (`Print.cpp:368–369`).
- `PrintStep` order (`Print.hpp:79–89`): `psWipeTower(0) → psSkirtBrim(1) → psGCodeExport(2) → psConflictCheck(3)`. Invalidating `psWipeTower` cascades to all later steps, including `psGCodeExport` — which is the step where `append_tcr` actually consumes the option. So the change **is** picked up on re-slice: **not under-invalidating.**
- **Over-invalidating?** Strictly, the option only influences `psGCodeExport`, so re-running `psWipeTower` is marginally broader than necessary. But this is the identical pattern used by the sibling `filament_minimal_purge_on_wipe_tower` and by every other flush/purge-related filament key in this list (`flush_volumes_matrix`, `prime_volume`, `flush_into_infill`, etc.). Matching the established convention is the consistent choice; there is no behavioural bug and no inconsistency to flag. Not reported.

---

## 6. Tooltip accuracy (`PrintConfig.cpp:2723`)

Tooltip text claims: a built-in minimum of "about 40 mm (100 mm³)" already applies, and the option is "only effective on printers that eject purge through a chute via the change filament G-code (e.g. Bambu Lab)."

- **"100 mm³":** `g_min_purge_volume = 100.f` (`GCode.cpp:92`). In the `apply_chute_min` branch the `purge_volume` is `std::max(min_chute_purge, g_min_purge_volume)` (zero-purge case) or `std::max({tcr.purge_volume, g_min_purge_volume, min_chute_purge})`, so `g_min_purge_volume = 100` mm³ always participates and dominates whenever the user's chute value is below 100 mm³. "Values below that have little effect" is therefore accurate.
- **"about 40 mm":** for 1.75 mm filament, area = π/4·1.75² ≈ 2.405 mm²; 100 mm³ / 2.405 ≈ 41.6 mm ≈ "about 40 mm." Accurate.
- **"only effective on … Bambu Lab":** now genuinely accurate. Previously the GUI was BBL-only but the slicer (`set_extruder`) was not; after this rescope the slicer floor is gated on `is_BBL_Printer()`, so the claim matches actual behaviour. The earlier inaccuracy is resolved.

No tooltip correction needed.

---

## 7. Division-by-zero check on the reverted `flush_count` (sanity, not a finding)

The revert restored upstream `flush_count = std::min(g_max_flush_count, (int)std::round(purge_volume / g_purge_volume_one_time))` (dropping the fork's `std::max(1, …)`). `flush_unit = purge_length / flush_count` divides by `flush_count`, so `flush_count == 0` would be a div-by-zero. With the floor active, `purge_volume ≥ g_min_purge_volume = 100` mm³ and `g_purge_volume_one_time = 135` mm³, so `round(100/135) = round(0.74) = 1` → `flush_count ≥ 1`. The feature can never drive `flush_count` to 0. The residual `(0, 67.5)` mm³ → `flush_count == 0` window is **pre-existing upstream behaviour**, unreachable via this feature (the floor lifts purge_volume to ≥100 before this line). The `std::max(1, …)` was the fork's own addition and removing it is the correct way to stay byte-identical to upstream; doing so does not reintroduce any feature-caused div-by-zero. Consistent with the rescope's stated goal.

---

## Files reviewed

- `/Volumes/MacMicroSD/Github/OrcaSlicer-nighty/src/libslic3r/GCode.cpp` (`append_tcr` ~840–985; `set_extruder` ~7820–7945; `is_BBL_Printer` 2028; constants 92–94)
- `/Volumes/MacMicroSD/Github/OrcaSlicer-nighty/src/libslic3r/PrintConfig.cpp` (2722–2735)
- `/Volumes/MacMicroSD/Github/OrcaSlicer-nighty/src/libslic3r/PrintConfig.hpp` (1456)
- `/Volumes/MacMicroSD/Github/OrcaSlicer-nighty/src/libslic3r/Print.cpp` (288–289, 368–369) and `Print.hpp` (79–89, 1070–1072)
- `/Volumes/MacMicroSD/Github/OrcaSlicer-nighty/src/libslic3r/Preset.cpp` (1282)
- `/Volumes/MacMicroSD/Github/OrcaSlicer-nighty/src/slic3r/GUI/Plater.cpp` (16692)
- `/Volumes/MacMicroSD/Github/OrcaSlicer-nighty/src/slic3r/GUI/Tab.cpp` (1433, 4265–4373)
