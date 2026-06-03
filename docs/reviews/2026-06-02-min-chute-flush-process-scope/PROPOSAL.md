# PROPOSAL UNDER REVIEW — move min-chute-flush from filament-scope to process-scope

**Repo:** /Volumes/MacMicroSD/Github/OrcaSlicer-nighty (branch `nightly-builds-with-bc`).
Fork of upstream `SoftFever/OrcaSlicer` (itself derived from Prusa PrusaSlicer). C++17.

**Status of the feature:** an unreleased, never-compiled, never-shipped fork feature. No on-disk
3MF or preset anywhere contains the old key (confirmed: zero references in `resources/profiles/**`).

## Goal

Currently the min-chute-flush option is a **per-filament** value stored in the **filament preset**.
The user has decided to move it to a **single global value stored in the process (Print Settings)
preset**, displayed beside the `flush_into_*` options, BBL-only. At its default (`0`) the emitted
G-code must remain **byte-identical to upstream**.

## Current state (filament-scoped)

- Key `filament_minimal_purge_on_chute`, type `ConfigOptionFloats` (per-extruder array), units =
  mm of filament length, default `0` (off).
- `PrintConfig.hpp:1456` — `((ConfigOptionFloats, filament_minimal_purge_on_chute))` inside the
  `GCodeConfig` macro block (next to sibling `filament_minimal_purge_on_wipe_tower` at 1455).
- `PrintConfig.cpp:2722` — `def = this->add("filament_minimal_purge_on_chute", coFloats);` with label
  "Minimal chute flush length", sidetext "mm", `min=0`, `mode=comAdvanced`, and a
  `set_default_value(new ConfigOptionFloats{0.})` literal (~line 2736).
- `Preset.cpp:1282` — listed in `s_Preset_filament_options` (filament preset whitelist).
- `GCode.cpp:879-888` — inside `append_tcr` (BBL Type1 path):
  ```cpp
  float filament_area = float((M_PI/4.f)*pow(full_config.filament_diameter.get_at(new_filament_id),2));
  const float min_chute_length   = (float) full_config.filament_minimal_purge_on_chute.get_at(new_filament_id);
  const float min_chute_purge    = min_chute_length * filament_area;            // mm filament -> mm³
  const bool  is_real_toolchange = tcr.is_tool_change && tcr.initial_tool != tcr.new_tool;
  const bool  apply_chute_min    = is_real_toolchange && min_chute_purge > EPSILON && gcodegen.is_BBL_Printer();
  float purge_volume = (tcr.purge_volume < EPSILON)
      ? (apply_chute_min ? std::max(min_chute_purge, g_min_purge_volume) : 0.f)
      : (apply_chute_min ? std::max({tcr.purge_volume, g_min_purge_volume, min_chute_purge})
                         : std::max(tcr.purge_volume, g_min_purge_volume));
  float purge_length = purge_volume / filament_area;
  ```
  (`g_min_purge_volume = 100 mm³` at GCode.cpp:92; downstream `flush_length=purge_length` at 962;
  `flush_count = std::min(g_max_flush_count, round(purge_volume / g_purge_volume_one_time))` at 969;
  `flush_unit = purge_length / flush_count` at 970.)
- `Print.cpp:289` — in the invalidation list, inside the block that emplaces
  `psWipeTower + psSkirtBrim` (lines 368-370), beside `prime_volume`/`flush_into_infill`/
  `flush_into_support`/`wipe_tower_max_purge_speed`.
- `Plater.cpp:16692` — in the `update_scheduled` config-change list.
- `Tab.cpp:4151` — `append_single_option_line("filament_minimal_purge_on_chute", "material_multimaterial#multimaterial-wipe-tower-parameters")` on the **Filament** tab (Multimaterial page, "Wipe tower parameters" group).
- `Tab.cpp:4362` — `toggle_option("filament_minimal_purge_on_chute", is_BBL_printer)` in `TabFilament::toggle_options()`.

## Revision 2 (2026-06-02) — fixes from the 10-agent review

This revision incorporates the review findings (see `FINDINGS-SUMMARY.md` and the per-agent raw
reports in this folder). Changes vs revision 1:

- **C1 (CRITICAL, 7 agents):** the BBL toggle moved from `TabPrint::toggle_options()` (where the
  `is_BBL_printer` local is out of scope → would not compile) to
  `ConfigManipulation::toggle_print_fff_options`, beside the `flush_into_*` toggles. Edit surface
  grows to **8 files** (adds `ConfigManipulation.cpp`); the rename surface is still 8 refs / 7 files.
- **M1 (MEDIUM, 2 agents):** add `def->category = L("Flush options")`.
- **M2 (MEDIUM, verified):** gate visibility on BBL **and** grey out when no prime tower, matching the
  `flush_into_*` siblings.
- **M3 (MEDIUM, 3 agents):** reword tooltip + the `GCode.cpp:873-874` comment to "global length,
  per-filament volume".
- **M4 / L4 (LOW):** explanatory comments at the decl and the UI-vs-emission gate.
- **Out of scope (L6):** do NOT add a `flush_count=std::max(1,…)` guard (that is the regression
  reverted in 4673720c01).

## Proposed changes (8 files)

| # | File | Edit | Kind |
|---|---|---|---|
| 1 | `PrintConfig.hpp:1456` | `((ConfigOptionFloats, filament_minimal_purge_on_chute))` → `((ConfigOptionFloat, minimal_chute_flush_length))`. **Stays in the `GCodeConfig` macro block** (type-only change) — add a one-line comment noting it is process-scoped via `s_Preset_print_options` despite living here. | structural |
| 2 | `PrintConfig.cpp:2722` (+ default literal ~2736) | `def = this->add("minimal_chute_flush_length", coFloat);`, **`def->category = L("Flush options");`**, and `set_default_value(new ConfigOptionFloat(0.))`. Keep label "Minimal chute flush length", sidetext "mm", `min=0`, `comAdvanced`. **Reword tooltip** (see "Tooltip / comment text" below): drop the now-false "adjacent 'Minimal purge on wipe tower'" clause; state it is a filament **length in mm (not a volume)** whose resulting purge **volume scales with each filament's diameter**; keep the "~40 mm / 100 mm³ built-in floor", "0 = disable", and "BBL chute only" notes. | structural |
| 3 | `Preset.cpp` | **Remove** `"filament_minimal_purge_on_chute"` from `s_Preset_filament_options` (line 1282); **ADD** `"minimal_chute_flush_length"` to `s_Preset_print_options` (near the `flush_into_*` entries ≈1124-1127, before the closing `};` at ~1277). | structural |
| 4 | `GCode.cpp:880` (+ comment 873-874) | `const float min_chute_length = (float) full_config.minimal_chute_flush_length.value;` (scalar; no `.get_at`). Keep `filament_area` from the **new** filament's diameter for the mm→mm³ conversion. Gating unchanged. Update the comment at **873-874 and 880**: "per-filament" → "global length; per-filament purge volume". | structural |
| 5 | `Print.cpp:289` | Rename the string literal to `"minimal_chute_flush_length"` in place (already in the correct `psWipeTower + psSkirtBrim` block; a miss fails safe via the catch-all `invalidate_all_steps()`). | rename |
| 6 | `Plater.cpp:16692` | Rename the string literal to `"minimal_chute_flush_length"` in the update-scheduled list. | rename |
| 7 | `Tab.cpp` | **Remove** the Filament-tab option line (4151) and its toggle (4362, incl. the 4360-4361 comment). **ADD** `optgroup->append_single_option_line("minimal_chute_flush_length", "multimaterial_settings_flush_options")` after `flush_into_support` (≈2686) in the Print Settings "Flush options" optgroup — **strictly before line 2687**, where `optgroup` is reassigned to the "Advanced" group. **No toggle added here** (moved to file 8). | structural |
| 8 | `ConfigManipulation.cpp` (`toggle_print_fff_options`, after line 882) | **ADD** the BBL-only visibility + prime-tower gating, beside the `flush_into_*` toggles, where `is_BBL_Printer` (used bare at 842/845) and `have_prime_tower` (854) are in scope:<br>`toggle_line("minimal_chute_flush_length", is_BBL_Printer);` // BBL-only: hide row on non-BBL<br>`toggle_field("minimal_chute_flush_length", is_BBL_Printer && have_prime_tower);` // grey when no prime tower, like flush_into_*<br>Add a one-line comment that the UI gate (`is_BBL_Printer`, vendor) intentionally differs from the G-code emission gate (`gcodegen.is_BBL_Printer()`, runtime). | structural |

## Decisions / non-goals

- **New key name:** `minimal_chute_flush_length` (drops the misleading `filament_` prefix).
- **Type:** global scalar `ConfigOptionFloat` (process presets are single-valued).
- **Visibility & enforcement:** BBL-only (kept), gated in `ConfigManipulation::toggle_print_fff_options`.
  `toggle_line(..., is_BBL_Printer)` hides the whole row on non-BBL; `toggle_field(..., is_BBL_Printer
  && have_prime_tower)` greys it when no prime tower (matching the `flush_into_*` siblings, which are
  no-ops without a tower — and so is the chute floor, enforced on the wipe-tower `append_tcr` path).
- **NOT doing `handle_legacy`:** the feature was never compiled/shipped (user-confirmed), so no
  on-disk file can contain the old key; an array→scalar legacy alias would also be messy. Skip it.
- **NOT relocating the `.hpp` declaration between structs:** preset membership is governed solely by
  the `s_Preset_*_options` whitelists, not by C++ struct membership (`GCodeConfig` aggregates into
  `FullPrintConfig`). Keeping the declaration in place minimizes the upstream diff.
- Translations auto-handled on next `run_gettext.sh`; cereal serialization auto-handled by the macro.

## Claims to scrutinize

1. At default `0`, the emitted G-code (purge_volume / flush_length / flush_count / flush_length_N /
   change_filament_gcode) is byte-identical to upstream.
2. `full_config.minimal_chute_flush_length.value` is a valid, correct scalar read at GCode.cpp:880.
3. Removing from `s_Preset_filament_options` + adding to `s_Preset_print_options` fully reclassifies
   persistence with no other mechanism involved.
4. `toggle_line(..., is_BBL_printer)` in `TabPrint::toggle_options()` correctly shows/hides the row and
   re-fires on printer change.
5. The rename touches exactly the 8 references in these 7 files and nothing else (no stale `.get_at`,
   no profile JSON, no localization, no 3MF, no collision on the new name).
6. `Print.cpp:289` + `Plater.cpp:16692` correctly trigger re-slice / re-export on edit.
7. No divide-by-zero / NaN / unit-confusion introduced; the mm→mm³ via the new filament's diameter is
   intended (a single global mm value yields a per-filament mm³ floor).

## Tooltip / comment text (revision 2)

**Tooltip (`PrintConfig.cpp`):**
> Minimum length of filament purged into the waste chute on a tool change, as a **length in
> millimetres of filament (not a volume)**. This is a single global value; the resulting purge
> **volume scales with each filament's diameter** (≈40 mm ≈ 100 mm³ for 1.75 mm filament). When most
> of the flush is redirected into the object's infill, the leftover chute purge can become too small
> to fall free and may stick to the nozzle — raising this guarantees enough filament to drop cleanly.
> A built-in minimum of ≈40 mm (100 mm³) already applies, so smaller values have little effect. Set to
> 0 to disable (default). Only effective on printers that eject purge through a chute via the change
> filament G-code (e.g. Bambu Lab) and only when the prime tower is enabled.

**`GCode.cpp` comment (873-874 + 880):** replace "per-filament minimum chute flush" framing with
"global minimum chute-flush **length** (mm); converted here to a per-filament purge **volume** (mm³)
via the incoming filament's cross-sectional area."

## Verification (post-build; CI compiles, see P1 in FINDINGS-SUMMARY)

1. Apply all 8 edits **atomically** (a partial landing — filament-removal without the print-list add —
   silently drops the value).
2. Build (CI). Confirm it compiles (the rev-1 toggle placement would not have).
3. Smoke test: on a BBL printer with the prime tower on, set a non-zero value → save the process preset
   → restart → reload, and confirm the value persists (round-trips through `s_Preset_print_options`).
4. Confirm the row is hidden on a non-BBL printer and greys out on a BBL printer with the tower off.
5. Confirm default 0 yields byte-identical G-code to a pre-change build (or to upstream) for a
   multi-color BBL print.
