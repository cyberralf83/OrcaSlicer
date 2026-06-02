# Upstream-parity review: `filament_minimal_purge_on_chute`

**Date:** 2026-06-02
**Scope:** Verify the new per-filament `ConfigOptionFloats filament_minimal_purge_on_chute`
is wired per the canonical OrcaSlicer pattern, by deriving the canonical checklist from
the existing per-filament `ConfigOptionFloats filament_minimal_purge_on_wipe_tower` in
**UPSTREAM** `SoftFever/OrcaSlicer@main`.

**Verdict:** No genuine gaps. Every file/list upstream touches to register a per-filament
`ConfigOptionFloats` key, our change also touches (or correctly does not need to). The two
mechanisms diverge only where they *should*: the wipe-tower key has consumers in
`WipeTower2.cpp/hpp` + `Print.cpp` volume-budget logic; the chute key has consumers in
`GCode.cpp` (the change-filament G-code path) instead — which is the correct, by-design
difference, not an omission.

---

## Method

Upstream `main` copies of the 7 candidate source files were fetched via
`raw.githubusercontent.com/SoftFever/OrcaSlicer/main/<path>` and grepped for every
occurrence of `filament_minimal_purge_on_wipe_tower` to derive the canonical touch-set.
(The GitHub MCP `search_code` index returned 0 hits for this repo, so raw fetch + local
grep was used as the authoritative source.) Our change was read via
`git -C <repo> diff`.

### Canonical pattern — where the wipe-tower key lives upstream

| File | Line(s) (upstream main) | Role |
|---|---|---|
| `src/libslic3r/PrintConfig.cpp` | 2711–2720 | `def = this->add("filament_minimal_purge_on_wipe_tower", coFloats)` registration block (label/tooltip/sidetext/min/mode/default) |
| `src/libslic3r/PrintConfig.hpp` | 1446 | Member inside the `GCodeConfig` `PRINT_CONFIG_CLASS_DEFINE(...)` macro |
| `src/libslic3r/Preset.cpp` | 1272 | `s_Preset_filament_options` list (filament list only) |
| `src/libslic3r/Print.cpp` | 288 | `invalidate_state_by_config_options` opt_key list |
| `src/libslic3r/Print.cpp` | 3451, 3458 | **Consumer** logic: subtract/add-back from infill wipe budget (wipe-tower-specific) |
| `src/slic3r/GUI/Plater.cpp` | 16643 | `on_config_change` reslice-trigger opt_key list |
| `src/slic3r/GUI/Tab.cpp` | 4141 | `append_single_option_line` (Multimaterial > Wipe tower parameters page) |
| `src/slic3r/GUI/Tab.cpp` | 4345 | `toggle_options` BBL-gating list |
| `src/libslic3r/GCode/WipeTower2.{cpp,hpp}` | (multiple) | **Consumer** logic, wipe-tower-specific (NOT a registration touchpoint) |

URLs:
- https://raw.githubusercontent.com/SoftFever/OrcaSlicer/main/src/libslic3r/PrintConfig.cpp
- https://raw.githubusercontent.com/SoftFever/OrcaSlicer/main/src/libslic3r/PrintConfig.hpp
- https://raw.githubusercontent.com/SoftFever/OrcaSlicer/main/src/libslic3r/Preset.cpp
- https://raw.githubusercontent.com/SoftFever/OrcaSlicer/main/src/libslic3r/Print.cpp
- https://raw.githubusercontent.com/SoftFever/OrcaSlicer/main/src/slic3r/GUI/Plater.cpp
- https://raw.githubusercontent.com/SoftFever/OrcaSlicer/main/src/slic3r/GUI/Tab.cpp
- https://raw.githubusercontent.com/SoftFever/OrcaSlicer/main/src/libslic3r/GCode.cpp

---

## Parity table

Legend: **REG** = mandatory registration touchpoint for any per-filament `ConfigOptionFloats`;
**CONS** = feature-specific consumer logic (differs by mechanism); **AUTO** = handled
automatically, no per-key edit; **N/A** = upstream does not touch this for such a key.

| File / list | Upstream touches for this kind of key? | We touched? | Gap? |
|---|---|---|---|
| `PrintConfig.cpp` — `this->add(...)` registration | Yes (REG) | Yes (2719–2736) | No |
| `PrintConfig.hpp` — `GCodeConfig` `PRINT_CONFIG_CLASS_DEFINE` member | Yes (REG) | Yes (1456) | No |
| `Preset.cpp` — `s_Preset_filament_options` | Yes (REG) | Yes (1282) | No |
| `Preset.cpp` — `s_Preset_print_options` | No (wipe-tower key NOT in it) | No | No — correctly not needed |
| `Preset.cpp` — `s_Preset_printer_options` / `machine_limits` / SLA lists | No | No | No — correctly not needed |
| `Print.cpp` — `invalidate_state_by_config_options` | Yes (REG) | Yes (289) | No |
| `Print.cpp` — wipe-budget consumer (lines 3451/3458) | Yes, but **wipe-tower-specific** (CONS) | No | No — chute is not a wipe-tower mechanism; consumer is in GCode.cpp instead |
| `Plater.cpp` — `on_config_change` reslice list | Yes (REG) | Yes (16692) | No |
| `Tab.cpp` — `append_single_option_line` (UI) | Yes (REG) | Yes (4151) | No |
| `Tab.cpp` — `toggle_options` enable/disable | Yes (REG) | Yes (4360–4363) | No — note BBL gating is *inverted* vs wipe-tower (intentional, see below) |
| `GCode.cpp` — flush/purge emitter (consumer) | Feature-specific (CONS); upstream wipe-tower key has **0** refs here | Yes (append_tcr + set_extruder) | No — this is where the chute mechanism *belongs* |
| `WipeTower2.cpp/.hpp` — consumer | Yes for wipe-tower key (CONS) | No | No — chute purge is not produced by the wipe tower; correctly untouched |
| `filament_extruder_override_keys` (PrintConfig.cpp:63) | No (wipe-tower key absent) | No | No — correctly not needed |
| `filament_options_with_variant` (PrintConfig.cpp:8251) | No (`minimal_purge` not present) | No | No — correctly not needed (not a per-variant key) |
| `handle_legacy` / `handle_legacy_composite` (PrintConfig.cpp:7931/8166) | No — only for renamed/removed keys & enums | No | No — brand-new key needs no legacy mapping |
| `s_keys_map_*` enum maps | No — enums only; this is `coFloats` | No | No — correctly not needed |
| cereal `serialize`/`load`/`save` macros (PrintConfig.hpp:2114–2183) | AUTO — driven by `PRINT_CONFIG_CLASS_DEFINE` + `serialization_key_ordinal`; no per-key macro | n/a | No — adding the macro member is sufficient |
| 3MF read/write (`src/libslic3r/Format/`) | AUTO — generic config (de)serialization, no hardcoded per-key list | n/a | No — correctly not needed |
| GUI option search / `key2category` (`Search.cpp`) | AUTO — `append_options(config,…)` iterates config keys, category/group pulled from the config def | n/a | No — correctly not needed |
| "compatible printers/condition" lists | N/A — unrelated to a purge float | n/a | No |
| Translations `.pot` / `.po` | AUTO — `scripts/run_gettext.sh` runs `xgettext --keyword=L …` over `localization/i18n/list.txt`, which includes `src/libslic3r/PrintConfig.cpp` (list.txt:196) | n/a | No — new `L()` strings auto-extracted on next regen, exactly as the wipe-tower strings were |

---

## Findings (severity)

**No genuine gaps.** All mandatory registration touchpoints are present and match the
canonical wipe-tower pattern one-for-one. All "beyond the obvious" candidates
(cereal, handle_legacy, s_keys_map, 3MF, GUI search/category, extruder-override and
per-variant lists, translation extraction) are either auto-handled or correctly not
required for this key class.

### Confirmations worth recording

- **Cereal serialization — auto.** `PRINT_CONFIG_CLASS_DEFINE` generates the per-class
  serialization; cereal save/load for `DynamicPrintConfig` keys off
  `serialization_key_ordinal` from the option-definition registry
  (`PrintConfig.hpp:2155–2186`). No per-key cereal macro exists for the wipe-tower key,
  so none is needed for chute. Adding the macro member (which we did) is the whole job.
- **`handle_legacy` — not needed.** It only remaps renamed/removed keys and enum values
  (`PrintConfig.cpp:7931`); the wipe-tower key has no entry. A brand-new key needs none.
- **Translations — auto.** `localization/i18n/OrcaSlicer.pot` already contains
  "Minimal purge on wipe tower" (1 hit) and does NOT yet contain "Minimal purge on chute"
  (0 hits). This is the expected steady state: the new strings are picked up automatically
  by `scripts/run_gettext.sh` (xgettext over `list.txt`, which lists `PrintConfig.cpp`).
  No manual .pot/.po edit is required — the same path by which the wipe-tower strings
  entered the catalog.
- **`s_Preset_filament_options` only.** Upstream places the wipe-tower key in exactly one
  Preset list (`s_Preset_filament_options`, Preset.cpp:1272) — NOT `s_Preset_print_options`.
  Our chute key is added to the same single list. (An early WebFetch pass mis-reported it
  as also being in `s_Preset_print_options`; direct grep of the raw file disproves that —
  the key appears exactly once in upstream Preset.cpp.)

### Intentional, correct divergences from the wipe-tower key (not gaps)

1. **Consumer location.** The wipe-tower key's slicing consumers are in
   `WipeTower2.cpp/.hpp` and the `Print.cpp` infill-wipe budget (lines 3451/3458). The
   chute key's consumer is the change-filament G-code emitter in `GCode.cpp`
   (`append_tcr` block + `set_extruder`). This is correct: a chute "poop" is produced by
   the tool-change flush, not by the wipe tower, so it must not be wired into WipeTower2
   or the wipe-budget math.
2. **Inverted BBL gating.** `Tab.cpp::toggle_options` enables the wipe-tower key for
   non-BBL printers (`!is_BBL_printer`) and the chute key for BBL printers
   (`is_BBL_printer`). Intentional — only chute-eject printers (Bambu Lab) emit purge via
   the change-filament G-code path the chute clamp lives on. Documented inline in the diff.

### Minor observations (non-blocking, informational)

- **Unit mismatch with the sibling option.** `filament_minimal_purge_on_chute` is a
  *length* (mm of filament); `filament_minimal_purge_on_wipe_tower` is a *volume* (mm³).
  The two sit adjacent in the same UI group. This is not a parity gap (the tooltip and
  sidetext call it out explicitly), but it is a UX foot-gun worth a second look in the
  functional/UX review.
- **Profile JSONs.** 501 filament profiles set `filament_minimal_purge_on_wipe_tower`
  as data. None need a chute entry — the `coFloats` default (`{0.}`, i.e. disabled) applies
  when a profile omits the key, matching the "Set to 0 to disable (default)" tooltip.
