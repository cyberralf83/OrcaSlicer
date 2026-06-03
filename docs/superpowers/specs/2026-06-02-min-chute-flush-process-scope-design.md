# Design — Move minimum chute-flush from filament-scope to process-scope

**Date:** 2026-06-02
**Repo:** OrcaSlicer fork `cyberralf83/OrcaSlicer`, branch `nightly-builds-with-bc` (tracks upstream
`SoftFever/OrcaSlicer` nightly; keep the diff minimal).
**Status of the feature:** unreleased, never-compiled, never-shipped fork feature. No on-disk 3MF or
preset anywhere contains the old key (verified: zero references in `resources/profiles/**`).
**Review provenance:** hardened through two multi-agent review passes (6 agents, then 10 agents). Raw
agent reports and the categorized findings live in
`docs/reviews/2026-06-02-min-chute-flush-process-scope/` (`agent-01`…`agent-10`, `FINDINGS-SUMMARY.md`,
`PROPOSAL.md` rev 2).

## Overview

The "minimum chute flush" feature enforces a floor on the BBL AMS purge ("poop") emitted at a tool
change. When most of the tool-change flush is redirected into the object's infill, the residual chute
purge can fall to ~0 — too small to drop free of the nozzle, so it sticks and causes problems. A
configurable minimum guarantees enough filament to drop cleanly.

Today the option is a **per-filament** value (`filament_minimal_purge_on_chute`, `ConfigOptionFloats`)
stored in the **filament preset**. This change makes it a **single global value**
(`minimal_chute_flush_length`, `ConfigOptionFloat`) stored in the **process (Print Settings) preset**,
displayed beside the `flush_into_*` options it relates to, BBL-only. **At its default (`0`) the emitted
G-code is byte-identical to upstream** (proven by the review).

## Goal & non-goals

**Goal:** relocate the setting's scope (filament → process), rename it, and make it a scalar — with no
change in default behavior and a clean, discoverable home next to the flush options.

**Non-goals / explicit decisions:**

- **Name:** `minimal_chute_flush_length` (drops the misleading `filament_` prefix; consistent with the
  process "flush" vocabulary, e.g. `flush_into_*`).
- **Type:** global scalar `ConfigOptionFloat` (process presets are single-valued).
- **Visibility/enforcement:** BBL-only, kept. Additionally greys out when the prime tower is off,
  matching the `flush_into_*` siblings.
- **No `handle_legacy` migration:** the feature never compiled/shipped, so nothing on disk carries the
  old key; an array→scalar alias would also be messy. Skip it.
- **Declaration stays in the `GCodeConfig` macro block:** preset membership is governed solely by the
  `s_Preset_*_options` whitelists, not by C++ struct membership (`GCodeConfig` aggregates into
  `FullPrintConfig`). Keeping it in place minimizes the upstream diff. (A one-line comment records this.)
- **Do NOT add a `flush_count = std::max(1, …)` guard** — that is the regression reverted in
  `4673720c01`. The existing ≥100 mm³ floor already makes `flush_count ≥ 1` when the feature fires.
- Translations are auto-handled on the next `run_gettext.sh`; cereal serialization is macro-generated.

## Current state (filament-scoped)

- Key `filament_minimal_purge_on_chute`, `ConfigOptionFloats` (per-extruder), units mm of filament,
  default `0`.
- `PrintConfig.hpp:1456` — `((ConfigOptionFloats, filament_minimal_purge_on_chute))` in the
  `GCodeConfig` macro block (sibling `filament_minimal_purge_on_wipe_tower` at 1455).
- `PrintConfig.cpp:2722` — `add("filament_minimal_purge_on_chute", coFloats)`; label, tooltip,
  sidetext "mm", `min=0`, `comAdvanced`; `set_default_value(new ConfigOptionFloats{0.})` (~2736).
- `Preset.cpp:1282` — in `s_Preset_filament_options`.
- `GCode.cpp:879-888` — in `append_tcr` (BBL Type1 path): reads
  `full_config.filament_minimal_purge_on_chute.get_at(new_filament_id)`, converts mm→mm³ via the new
  filament's `filament_area`, and floors `purge_volume` when
  `apply_chute_min = is_real_toolchange && min_chute_purge > EPSILON && gcodegen.is_BBL_Printer()`.
- `Print.cpp:289` — in the invalidation block that emplaces `psWipeTower + psSkirtBrim` (368-370).
- `Plater.cpp:16692` — in the `update_scheduled` list.
- `Tab.cpp:4151` — `append_single_option_line(...)` on the Filament tab; `Tab.cpp:4362` —
  `toggle_option(..., is_BBL_printer)` in `TabFilament::toggle_options()`.

## Design — changes (8 files)

| # | File | Edit |
|---|---|---|
| 1 | `PrintConfig.hpp:1456` | `((ConfigOptionFloats, filament_minimal_purge_on_chute))` → `((ConfigOptionFloat, minimal_chute_flush_length))`. Stays in the `GCodeConfig` block (type-only change). Add a one-line comment: process-scoped via `s_Preset_print_options` despite living here. |
| 2 | `PrintConfig.cpp:2722` (+ default ~2736) | `add("minimal_chute_flush_length", coFloat)`; **`def->category = L("Flush options");`**; `set_default_value(new ConfigOptionFloat(0.))`. Keep label "Minimal chute flush length", sidetext "mm", `min=0`, `comAdvanced`. Reworded tooltip (see below). |
| 3 | `Preset.cpp` | Remove `"filament_minimal_purge_on_chute"` from `s_Preset_filament_options` (1282); add `"minimal_chute_flush_length"` to `s_Preset_print_options` near the `flush_into_*` entries (≈1124-1127, before the closing `};` ~1277). |
| 4 | `GCode.cpp:880` (+ comment 873-874) | `const float min_chute_length = (float) full_config.minimal_chute_flush_length.value;` (scalar; no `.get_at`). Keep `filament_area` from the new filament's diameter for mm→mm³. Gating unchanged. Update the comment (873-874 + 880): "per-filament" → "global length; per-filament purge volume". |
| 5 | `Print.cpp:289` | Rename the string literal in place (stays in the correct `psWipeTower + psSkirtBrim` block; a miss fails safe via the catch-all `invalidate_all_steps()`). |
| 6 | `Plater.cpp:16692` | Rename the string literal in the `update_scheduled` list. |
| 7 | `Tab.cpp` | Remove the Filament-tab option line (4151) and its toggle (4362, incl. the 4360-4361 comment). Add `optgroup->append_single_option_line("minimal_chute_flush_length", "multimaterial_settings_flush_options")` after `flush_into_support` (≈2686), **strictly before line 2687** (where `optgroup` is reassigned to "Advanced"). No toggle added here (moved to file 8). |
| 8 | `ConfigManipulation.cpp` (`toggle_print_fff_options`, after line 882) | Add, beside the `flush_into_*` toggles where `is_BBL_Printer` (used at 842/845) and `have_prime_tower` (854) are in scope:<br>`toggle_line("minimal_chute_flush_length", is_BBL_Printer);` // BBL-only: hide row on non-BBL<br>`toggle_field("minimal_chute_flush_length", is_BBL_Printer && have_prime_tower);` // grey when no prime tower, like flush_into_*<br>Comment that the UI gate (`is_BBL_Printer`, vendor) intentionally differs from the G-code emission gate (`gcodegen.is_BBL_Printer()`, runtime). |

The rename surface is **8 references in 7 files** (verified exhaustively). The edit surface is **8 files**
(the 7 above plus `ConfigManipulation.cpp` for the new toggle, which is not a rename of an existing ref).

## Tooltip / comment text

**Tooltip (`PrintConfig.cpp`):**
> Minimum length of filament purged into the waste chute on a tool change, as a **length in
> millimetres of filament (not a volume)**. This is a single global value; the resulting purge
> **volume scales with each filament's diameter** (≈40 mm ≈ 100 mm³ for 1.75 mm filament). When most
> of the flush is redirected into the object's infill, the leftover chute purge can become too small
> to fall free and may stick to the nozzle — raising this guarantees enough filament to drop cleanly.
> A built-in minimum of ≈40 mm (100 mm³) already applies, so smaller values have little effect. Set to
> 0 to disable (default). Only effective on printers that eject purge through a chute via the change
> filament G-code (e.g. Bambu Lab) and only when the prime tower is enabled.

**`GCode.cpp` comment (873-874 + 880):** "global minimum chute-flush **length** (mm); converted here to
a per-filament purge **volume** (mm³) via the incoming filament's cross-sectional area."

## Risks, edge cases, and resolved non-issues

**Confirmed safe (proven in review):**

- **Default 0 is byte-identical to upstream** — `apply_chute_min` is false at 0, and the `purge_volume`
  expression collapses to the exact upstream form; all downstream derivations match.
- **`.value` scalar read is valid** (precedent: `prime_volume`); `FullPrintConfig ⊃ PrintConfig ⊃
  GCodeConfig`, so the member resolves at slice time regardless of which preset persists it.
- **Whitelist move is the sole persistence mechanism** — no `*_options_with_variant` /
  `filament_extruder_override_keys` / `normalize()` interaction for a scalar.
- **Missed `Print.cpp` rename fails safe** (`invalidate_all_steps()`), never stale G-code.
- **`set_extruder` stays byte-identical to upstream** (no reintroduced `max(1,…)` regression).
- **BBL visibility re-fires** on printer change (`load_current_preset → update → toggle_options`).

**Out of scope (do not change):** pre-existing `flush_count==0` / `filament_area==0` div-by-zero in the
shared upstream code (`GCode.cpp:970/7929`); unreachable when the feature fires and not introduced here.

## Verification (post-build; CI compiles)

1. Apply all 8 edits **atomically** (a partial landing silently drops the value).
2. Build (CI). Confirm it compiles (the unfixed toggle placement would not have).
3. Save → restart → reload a non-zero value on a BBL printer with the prime tower on; confirm it
   persists via `s_Preset_print_options`.
4. Confirm the row is hidden on a non-BBL printer and greys out on a BBL printer with the tower off.
5. Confirm default 0 emits byte-identical G-code vs a pre-change build for a multi-color BBL print.
