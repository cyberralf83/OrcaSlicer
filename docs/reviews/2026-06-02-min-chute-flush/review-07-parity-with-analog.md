# Review 07 — Systematic parity audit: `filament_minimal_purge_on_chute` vs `filament_minimal_purge_on_wipe_tower`

**Date:** 2026-06-02
**Lens:** Catch-all for MISSED wiring. Grep the whole repo (src/ + resources/, excluding `.github/`) for every occurrence of the analog key `filament_minimal_purge_on_wipe_tower`, build a per-location parity table against the new `filament_minimal_purge_on_chute`, and judge each absence as a genuine omission or correct-by-design.

**Scope:** Review only. No code changed.

---

## Method

```
grep -rn "filament_minimal_purge_on_wipe_tower" src/ resources/    # 509 files total
grep -rn "filament_minimal_purge_on_chute"      src/ resources/
```

- **509** total files reference the analog. **501** of those are `resources/profiles/**` and `resources/profiles_template/**` filament JSONs — profile *data*, not wiring. There are **zero** non-profile resource references (no schema/default file under `resources/` references the analog), confirmed via:
  `grep -rn "...wipe_tower" resources/ | grep -v "/profiles/" | grep -v "/profiles_template/"` → empty.
- That leaves **15 source-code call-sites across 7 files** that constitute the actual wiring. Those are the parity-relevant rows below.

Reference file-set for "what a new filament config key usually touches" in this fork, from recent feature commits:
- **`ab393cadb7`** ("hide seam at part interface"): `PrintConfig.cpp`, `PrintConfig.hpp`, `Preset.cpp`, `Tab.cpp`, `PrintObject.cpp`, `ConfigManipulation.cpp`, `GUI_Factories.cpp`, `SeamPlacer.{cpp,hpp}`, **plus a `tests/libslic3r/test_config.cpp` test**.
- **`e57ed0375a`** (interlocking beam density): `PrintConfig.cpp`, `ConfigManipulation.cpp`, `InterlockingGenerator.{cpp,hpp}`.

The canonical "config-key skeleton" (definition + serialization struct + preset whitelist + invalidation + GUI line + GUI toggle) is the part that must always be mirrored for the key to load and persist; the *consumption* site differs per feature.

---

## Parity table

| # | Location (file:line) | What the analog does there | Chute key present? | Where in diff | Verdict |
|---|---|---|---|---|---|
| 1 | `src/libslic3r/PrintConfig.cpp:2711` | `def = this->add("filament_minimal_purge_on_wipe_tower", coFloats)` — option **definition** (label, tooltip, sidetext, min, mode, default). | YES | `PrintConfig.cpp:2722` (new `add("filament_minimal_purge_on_chute", coFloats)` block, default `0.`) | **Parity OK.** Definition mirrored correctly; sensible new default of 0 (disabled). |
| 2 | `src/libslic3r/PrintConfig.hpp:1455` | `((ConfigOptionFloats, filament_minimal_purge_on_wipe_tower))` — **StaticPrintConfig field** in the `PRINT_CONFIG_CLASS_DEFINE` macro (serialization + typed access). | YES | `PrintConfig.hpp:1456` (new `ConfigOptionFloats filament_minimal_purge_on_chute` immediately after). | **Parity OK.** Same type (`ConfigOptionFloats`), correct placement. |
| 3 | `src/libslic3r/Preset.cpp:1282` | Listed in `s_Preset_filament_options` — the **filament preset whitelist**. Without this the key is dropped on save/load (the documented "Preset.cpp whitelist gotcha"). | YES | `Preset.cpp:1282` (appended on the same line, right after the analog). | **Parity OK.** This is the most commonly-missed wiring; it is present. |
| 4 | `src/libslic3r/Print.cpp:288` | `opt_key == "filament_minimal_purge_on_wipe_tower"` in `invalidate_state_by_config_options()` — marks the print **dirty / forces re-slice** when the value changes. | YES | `Print.cpp:289` (new `|| opt_key == "filament_minimal_purge_on_chute"`). | **Parity OK.** Editing the chute value now correctly invalidates and re-slices. Important because the chute clamp is applied at G-code export time. |
| 5 | `src/libslic3r/Print.cpp:3452` | `volume_to_wipe -= filament_minimal_purge_on_wipe_tower.get_at(extruder_id)` — Type-2 **prime-tower** planning: reserve the wipe-tower minimum before assigning infill-purge. Guarded by `purge_in_prime_tower && single_extruder_multi_material`. | NO | — | **Correct-by-design.** This is the wipe-tower (Type-2 / prime-tower) volume planner. The chute feature operates on the **chute poop length at change-filament G-code emit time (Type-1 path in GCode.cpp)**, not on wipe-tower depth planning. Adding the chute key here would wrongly inflate the prime tower. |
| 6 | `src/libslic3r/Print.cpp:3459` | `volume_to_wipe += filament_minimal_purge_on_wipe_tower.get_at(extruder_id)` — add the reserved wipe-tower minimum back after infill marking (same Type-2 block). | NO | — | **Correct-by-design.** Same prime-tower planner as #5. Not the chute path. |
| 7 | `src/libslic3r/GCode/WipeTower2.hpp:175` | `float filament_minimal_purge_on_wipe_tower = 0.f;` — per-filament param cached in `WipeTower2::FilamentParameters`. | NO | — | **Correct-by-design.** `WipeTower2` is the Type-2 tower generator. The chute clamp does not flow through `WipeTower2`; it reads the config directly in `GCode.cpp::append_tcr`. No need to cache a chute param in the tower's filament struct. |
| 8 | `src/libslic3r/GCode/WipeTower2.cpp:1343` | `m_filpar[idx].filament_minimal_purge_on_wipe_tower = config...get_at(idx)` — populates the cached param (#7). | NO | — | **Correct-by-design.** Populates the Type-2 cache only. N/A to chute. |
| 9 | `src/libslic3r/GCode/WipeTower2.cpp:2185` | Comment: "The purging for other printers is determined by filament_minimal_purge_on_wipe_tower." | NO | — | **Correct-by-design** (comment only). No behavior. Note it confirms the wipe-tower minimum is the *non-SEMM* purge knob; the chute knob is the analog for chute-eject printers. |
| 10 | `src/libslic3r/GCode/WipeTower2.cpp:2195` | Comment for the preview wipe-volume computation. | NO | — | **Correct-by-design** (comment only). |
| 11 | `src/libslic3r/GCode/WipeTower2.cpp:2198` | `wipe_volumes[i][j] = max(wipe_volumes[i][j]*scale, ...wipe_tower.get_at(j))` — floors the **preview** wipe matrix at the wipe-tower minimum. | NO | — | **Correct-by-design.** This drives the wipe-tower preview/volume matrix. The chute poop is not a wipe-tower extrusion and must not appear in this matrix. |
| 12 | `src/libslic3r/GCode/WipeTower2.cpp:2302` | `volume_left_to_wipe = max(m_filpar[...].filament_minimal_purge_on_wipe_tower, wipe_volume_total - volume_to_save)` — floors per-toolchange wipe volume during `save_on_last_wipe()`. | NO | — | **Correct-by-design.** Type-2 tower depth/volume bookkeeping. N/A to chute. |
| 13 | `src/slic3r/GUI/Plater.cpp:16691` | `opt_key == "filament_minimal_purge_on_wipe_tower"` in `on_config_change()` — triggers prime-tower/wipe-tower **GUI/preview refresh** on change. | YES | `Plater.cpp:16692` (new `|| opt_key == "filament_minimal_purge_on_chute"`). | **Parity OK.** Changing the chute value refreshes the relevant GUI state alongside the wipe-tower/prime-tower keys. |
| 14 | `src/slic3r/GUI/Tab.cpp:4150` | `optgroup->append_single_option_line("filament_minimal_purge_on_wipe_tower", ...)` — adds the **GUI control** to the Filament → Multimaterial → "Wipe tower parameters" group. | YES | `Tab.cpp:4151` (new `append_single_option_line("filament_minimal_purge_on_chute", ...)` directly after, same help anchor). | **Parity OK.** Control is rendered. (Minor cosmetic note below re: group title.) |
| 15 | `src/slic3r/GUI/Tab.cpp:4355` | Analog is in the `for (auto el : {...}) toggle_option(el, !is_BBL_printer)` loop — the wipe-tower minimum is **shown only for non-BBL** printers. | YES (inverse) | `Tab.cpp:4362` (new standalone `toggle_option("filament_minimal_purge_on_chute", is_BBL_printer)`). | **Parity OK — intentionally inverse.** The wipe-tower minimum is hidden for BBL; the chute minimum is shown **only** for BBL (chute-eject). `is_BBL_printer` is in scope (defined at `Tab.cpp:4271`). The accompanying comment documents the inversion. Correct by design. |

### New-only chute consumption site (no analog row — the feature's actual effect)

| Location | What it does | Note |
|---|---|---|
| `src/libslic3r/GCode.cpp:880` (+ surrounding `append_tcr` block, lines ~873–972) | Reads `filament_minimal_purge_on_chute.get_at(new_filament_id)`, converts mm→mm³ via `filament_area`, and floors `purge_volume` for **real tool changes only** (`tcr.is_tool_change && tcr.initial_tool != tcr.new_tool`); also forces `flush_count >= 1`. | This is the deliberate design choice: the chute clamp lives in the **Type-1 change-filament G-code path** in `GCode.cpp`, NOT in `WipeTower2`/Print.cpp Type-2 planning (rows 5–12). The `is_real_toolchange` guard prevents spurious purge on finish-layer/priming/same-tool TCRs. This is the analog of how the wipe-tower minimum lives in the Type-2 planner — each minimum lives in its own purge path. |

---

## Genuine omissions

After auditing all 15 source call-sites: every absence in the wipe-tower (#5–#12) and the inverse GUI toggle (#15) is **correct-by-design**, consistent with the feature's stated architecture (chute clamp in Type-1 `GCode.cpp::append_tcr`, not Type-2 `WipeTower2`/Print.cpp). All 6 mandatory "config-key skeleton" sites (definition, struct field, preset whitelist, invalidation, GUI line, GUI toggle, plus the Plater refresh) are present and correct.

**No genuine omissions in the C++ wiring or in the parity-relevant code paths.**

### One non-blocking gap vs. the fork's recent-feature file-set norm

| Severity | Item | Detail |
|---|---|---|
| **Low** | No unit test added | The most recent comparable config-key feature in this fork (`ab393cadb7`, hide-seam) added a `tests/libslic3r/test_config.cpp` case for the new key. The analog `filament_minimal_purge_on_wipe_tower` itself has **no** test (`grep tests/` → empty), so there is no analog test to mirror, and a test is not required for the key to load/function. This is a convention gap, not a parity gap. Flagging only because the audit was asked to cross-check against the recent-feature file-set. Not required for correctness. |

### Cosmetic observations (not omissions)

- **GUI group placement (Low/cosmetic):** the chute control is added to the optgroup titled *"Wipe tower parameters"* (`Tab.cpp:4149`), and uses the wipe-tower help anchor `material_multimaterial#multimaterial-wipe-tower-parameters`. Functionally it sits next to its analog and is BBL-gated to only appear when relevant, so this is acceptable; a future polish could relabel/relocate, but it is purely cosmetic and not a wiring defect.
- **Profiles (None):** the 501 profile JSONs that set the analog do **not** need the chute key — profiles only specify overrides, and the new key defaults to `0` (disabled). Absence from profiles is correct.

---

## Verdict

**No parity gaps found.** All 6 mandatory config-key wiring sites are mirrored (definition, StaticPrintConfig field, `Preset.cpp` filament whitelist, `Print.cpp` invalidation, `Tab.cpp` GUI line, `Plater.cpp` refresh), and the GUI toggle is correctly inverted for BBL. Every wipe-tower / `WipeTower2` absence (#5–#12) is correct-by-design because the chute clamp deliberately lives in the Type-1 `GCode.cpp::append_tcr` path, not the Type-2 tower planner. The only deviation from the fork's recent-feature norm is the absence of a unit test (Low; the analog has none to mirror).
