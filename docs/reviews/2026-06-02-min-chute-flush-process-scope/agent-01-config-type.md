# Agent 01 — Config System & Type Correctness Review

Scope: the `ConfigOptionFloats` (per-extruder array) → `ConfigOptionFloat` (scalar) type change and the
filament-scope → process-scope reclassification of the min-chute-flush option. All line numbers verified
against the live tree on branch `nightly-builds-with-bc`.

## Verdict

Type change and scalar `.value` read are correct and well-precedented; the only real defects are a missing
`def->category = L("Flush options")` (every sibling in that optgroup sets it) and a placement/idiom problem
with the `toggle_line` (proposal puts it in `TabPrint::toggle_options()` where the BBL local is out of the
right scope and no other option is toggled, instead of the canonical `ConfigManipulation::toggle_print_fff_options`).

## Confirmed correct

- **Type enum + literal.** PrintConfig.cpp:2722 is currently `this->add("filament_minimal_purge_on_chute", coFloats);`
  and PrintConfig.cpp:2736 is `def->set_default_value(new ConfigOptionFloats { 0. });`. Both must change to
  `coFloat` and `new ConfigOptionFloat(0.)` (or `{0.}`). `coFloat` enum exists (Config.hpp:162). The proposal's
  table item #2 specifies exactly this. Note the live literal is spelled `ConfigOptionFloats { 0. }` (spaces),
  not `ConfigOptionFloats{0.}` as the proposal text abbreviates — cosmetic only.
- **`coFloat` ↔ `ConfigOptionFloat` member match.** With the member at PrintConfig.hpp:1456 changed to
  `((ConfigOptionFloat, minimal_chute_flush_length))` and `add(..., coFloat)`, there is no `coFloats`/scalar
  mismatch. (A leftover `coFloats` against a scalar member would silently produce a default / deserialize wrong.)
- **`.value` is the correct scalar accessor.** Precedent for reading a `ConfigOptionFloat` off the full config:
  `print_config.prime_tower_width.value` (GCode.hpp:87), `config.prime_tower_width.value` (Print.cpp:1004).
  `prime_volume` (coFloat, PrintConfig.cpp:6853) and `wipe_tower_max_purge_speed` (coFloat, 6913) are the named
  scalar siblings. `full_config.minimal_chute_flush_length.value` at GCode.cpp:880 is valid and correct.
- **`full_config` resolves the member.** GCode.cpp `full_config` is a `FullPrintConfig`
  (`PRINT_CONFIG_CLASS_DERIVED_DEFINE0(FullPrintConfig, (PrintObjectConfig, PrintRegionConfig, PrintConfig))`,
  PrintConfig.hpp:1666) and `PrintConfig` derives from `(MachineEnvelopeConfig, GCodeConfig)` (1485). So the
  member declared in the `GCodeConfig` block is reachable as `full_config.minimal_chute_flush_length`.
- **Leaving the declaration in the GCodeConfig block is correct.** Preset persistence is governed by the
  `s_Preset_*_options` whitelists, NOT C++ struct membership. Proof in-tree: `filament_diameter` (Preset.cpp:1279,
  filament whitelist) and `change_filament_gcode` (Preset.cpp:1336, print whitelist) are BOTH declared in the
  same `GCodeConfig` macro block, yet land in different presets. So moving the whitelist entry alone reclassifies
  persistence; relocating the `.hpp` declaration is unnecessary (and would enlarge the upstream diff). Proposal
  claim #3 and non-goal "NOT relocating the .hpp declaration" are both validated.
- **No name collision.** `grep` for `minimal_chute_flush_length` across `src/ resources/ localization/` returns
  zero hits. New name is free.
- **Exactly 8 refs in 7 files.** Full-repo scan for `filament_minimal_purge_on_chute` returns exactly 8 matches
  (PrintConfig.hpp:1456, PrintConfig.cpp:2722, Preset.cpp:1282, GCode.cpp:880, Print.cpp:289, Plater.cpp:16692,
  Tab.cpp:4151, Tab.cpp:4362). No profile JSON, no `.po/.pot`, no 3MF. Proposal claim #5 confirmed.
- **Invalidation block is correct.** Print.cpp:289 sits inside the single large `else if` (lines 283–367) whose
  body (368–370) emplaces `psWipeTower + psSkirtBrim`, next to `prime_volume` (342), `flush_into_infill` (343),
  `flush_into_support` (344), `wipe_tower_max_purge_speed` (356). In-place rename is sufficient.
- **Byte-identical at default 0.** With the scalar default `0`, `min_chute_length=0` → `min_chute_purge=0` →
  `apply_chute_min` is false (`min_chute_purge > EPSILON` fails), so `purge_volume` reduces to the upstream
  `std::max(tcr.purge_volume, g_min_purge_volume)` / `0.f` branches. The array→scalar change does not alter the
  value at default. Claim #1 holds.

## Findings

- **[MEDIUM] PrintConfig.cpp (new def block, ~2722) — missing `def->category = L("Flush options")`.**
  The proposal's item #2 lists label/sidetext/min/mode but omits `category`. Every sibling in the exact optgroup
  the option is being moved into sets it: `flush_into_infill` (7015), `flush_into_infill_min_layer` (7024),
  `flush_into_support` (7039), `flush_into_objects` (7047) all do `def->category = L("Flush options")`. A
  process option lacking a category shows blank/inconsistent provenance in the settings search and the
  "modified settings" / diff UI. Fix: add `def->category = L("Flush options");` to the new def block.
  (Note: the named scalar siblings `prime_volume`/`wipe_tower_max_purge_speed` do NOT set category, but they
  live in different optgroups — the relevant precedent is the `flush_into_*` group this option is joining.)

- **[MEDIUM] Tab.cpp `TabPrint::toggle_options()` (~2835) — wrong home / out-of-scope local for the toggle.**
  Proposal item #7 adds `toggle_line("minimal_chute_flush_length", is_BBL_printer)` "at ≈2835, where
  `is_BBL_printer` is already computed." Two problems: (1) that `is_BBL_printer` local exists ONLY inside the
  `if (m_preset_bundle) { ... }` block at lines 2833–2836; an insert outside it won't compile. (2)
  `TabPrint::toggle_options()` (2829–2881) calls NO `toggle_line` of its own — it delegates all option toggling
  to `m_config_manipulation.toggle_print_fff_options(...)` (2838). The canonical home for a BBL-gated print-tab
  toggle is `ConfigManipulation::toggle_print_fff_options` (ConfigManipulation.cpp:592), where `is_BBL_Printer`
  is already in scope and used (e.g. lines 842, 845) and where every neighbouring wipe-tower/flush toggle lives
  (`wipe_tower_max_purge_speed` toggled at 864). Fix: place `toggle_line("minimal_chute_flush_length", is_BBL_Printer);`
  in `toggle_print_fff_options` near line 842, not in `TabPrint::toggle_options()`. This also satisfies claim #4
  (re-fires on printer change, same path as `flush_into_*`).

- **[LOW] GCode.cpp comment block 873–878 — stale "per-filament" wording not fully addressed.**
  Proposal item #4 says update the comment at line 880 "per-filament" → "global", but the descriptive comment at
  873 ("Enforce a per-filament minimum chute flush ...") is the one carrying the now-incorrect framing. The value
  is now a single global mm length whose mm³ floor still scales per-filament via `filament_diameter.get_at(new_filament_id)`
  (line 879), so the comment should say roughly "global minimum chute flush length; the resulting mm³ floor still
  scales with the new filament's diameter." Cosmetic; no behavioural impact.

- **[LOW] Tab.cpp:4151 deep-wiki anchor lost on move.** The removed filament line used the help anchor
  `material_multimaterial#multimaterial-wipe-tower-parameters`; the proposal's new print-tab line uses
  `multimaterial_settings_flush_options` (consistent with the `flush_into_*` siblings at 2683–2686). Correct
  choice — just noting the anchor intentionally changes; no defect.

## Sources

- /Volumes/MacMicroSD/Github/OrcaSlicer-nighty/src/libslic3r/PrintConfig.hpp:1456, 1485, 1666
- /Volumes/MacMicroSD/Github/OrcaSlicer-nighty/src/libslic3r/PrintConfig.cpp:2722-2736 (def block), 6853 (prime_volume), 6913 (wipe_tower_max_purge_speed), 7014-7052 (flush_into_* with category)
- /Volumes/MacMicroSD/Github/OrcaSlicer-nighty/src/libslic3r/Config.hpp:162 (coFloat), 770
- /Volumes/MacMicroSD/Github/OrcaSlicer-nighty/src/libslic3r/Preset.cpp:1279 (filament_diameter), 1282 (old key), 1336 (change_filament_gcode), 1115/1124-1127/1225 (print whitelist flush entries)
- /Volumes/MacMicroSD/Github/OrcaSlicer-nighty/src/libslic3r/GCode.cpp:860-893 (append_tcr block), 873-880 (comment + read)
- /Volumes/MacMicroSD/Github/OrcaSlicer-nighty/src/libslic3r/Print.cpp:283-370 (invalidation else-if → psWipeTower+psSkirtBrim), 1004 (prime_tower_width.value)
- /Volumes/MacMicroSD/Github/OrcaSlicer-nighty/src/slic3r/GUI/Plater.cpp:16688-16696
- /Volumes/MacMicroSD/Github/OrcaSlicer-nighty/src/slic3r/GUI/Tab.cpp:2682-2686 (Flush options optgroup), 4151, 4362, 2829-2881 (TabPrint::toggle_options)
- /Volumes/MacMicroSD/Github/OrcaSlicer-nighty/src/slic3r/GUI/ConfigManipulation.cpp:47 (toggle_line), 592 (toggle_print_fff_options), 842-867 (is_BBL_Printer + flush/wipe toggles)
- /Volumes/MacMicroSD/Github/OrcaSlicer-nighty/src/libslic3r/GCode.hpp:87 (prime_tower_width.value precedent)
