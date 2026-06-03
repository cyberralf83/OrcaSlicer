# Agent 05 — Rename Completeness & Collisions

Focus: exhaustive verification that the proposed rename
`filament_minimal_purge_on_chute` (ConfigOptionFloats, filament scope) →
`minimal_chute_flush_length` (ConfigOptionFloat, process scope) touches exactly the
right references, leaves no survivor of the old key/type, and collides with nothing
under the new name.

## Verdict

**ACCURATE — proposal's reference inventory is exactly correct.** The 8-reference /
7-file claim is verified against live code. There are **exactly 8 build-input
references** to the old key, in **7 files**, all enumerated by the proposal. Every other
occurrence in the tree is review-note prose under `docs/reviews/**` (cosmetic, not build
input). The new name `minimal_chute_flush_length` has **zero pre-existing hits** anywhere
(no collision), upstream `SoftFever/OrcaSlicer` has **zero** hits for either the old or
the new key (fork-only; merge-safe). There is exactly **one** runtime accessor (GCode.cpp:880)
using `.get_at(...)`; no other `.get_at` / `.values` / `ConfigOptionFloats` cast / dynamic
`option<…>("…")` lookup references the key, so the Floats→Float type change cannot leave a
survivor that crashes or reads garbage. Skipping `handle_legacy` is **correct** for a
never-shipped key.

Two non-blocking caveats found (both LOW/MEDIUM, outside the strict rename inventory but
worth flagging): a scope nuance on the `is_BBL_printer` local in `TabPrint::toggle_options()`,
and the shared display label string.

## Complete reference list

### Build-input references to the OLD key (the 8 / 7-file set — all covered by proposal)

| # | File:line | Reference | Kind | Covered by proposal item |
|---|-----------|-----------|------|--------------------------|
| 1 | `src/libslic3r/PrintConfig.hpp:1456` | `((ConfigOptionFloats, filament_minimal_purge_on_chute))` (StaticPrintConfig field decl in `GCodeConfig` macro) | typed decl | Item 1 |
| 2 | `src/libslic3r/PrintConfig.cpp:2722` | `def = this->add("filament_minimal_purge_on_chute", coFloats);` (+ label 2723, tooltip 2724-2732, default literal 2736) | def registration | Item 2 |
| 3 | `src/libslic3r/Preset.cpp:1282` | string literal in `s_Preset_filament_options` whitelist | whitelist | Item 3 |
| 4 | `src/libslic3r/GCode.cpp:880` | `(float) full_config.filament_minimal_purge_on_chute.get_at(new_filament_id)` (the ONLY runtime accessor) | accessor | Item 4 |
| 5 | `src/libslic3r/Print.cpp:289` | `\|\| opt_key == "filament_minimal_purge_on_chute"` (invalidation list) | string literal | Item 5 |
| 6 | `src/slic3r/GUI/Plater.cpp:16692` | `opt_key == "filament_minimal_purge_on_chute" \|\|` (update_scheduled list) | string literal | Item 6 |
| 7 | `src/slic3r/GUI/Tab.cpp:4151` | `append_single_option_line("filament_minimal_purge_on_chute", …)` (Filament tab) | string literal | Item 7 (remove) |
| 8 | `src/slic3r/GUI/Tab.cpp:4362` | `toggle_option("filament_minimal_purge_on_chute", is_BBL_printer)` (TabFilament::toggle_options) | string literal | Item 7 (remove) |

Total build-input references: **8** in **7 files**. Matches the proposal exactly.

### Non-build-input occurrences (cosmetic — review notes; NOT in proposal's 7-file scope, correctly excluded)

All other matches are in `docs/reviews/**` markdown (prior-round review notes). Folders:
`2026-06-02-min-chute-flush/` (~30 hits across review/silent/external .md),
`2026-06-02-min-chute-flush-integration/` (~20 hits),
`2026-06-02-min-chute-flush-rescope/` (~10 hits), and the PROPOSAL.md under review.
These are historical analysis prose, not compiled/loaded. They will become stale after the
rename but are harmless. **LOW** at most.

### New name `minimal_chute_flush_length` — collision scan

- Whole-tree grep: hits ONLY in `PROPOSAL.md` (the proposal text itself). **Zero** hits in
  any `src/`, `resources/`, `tests/`, `localization/`, `*.json`, `*.ini`. No collision.
- `s_Preset_print_options` (the target list, Preset.cpp): does NOT already contain the new
  key — confirmed (only line 1282 in the *filament* list has the old key). No duplicate-key risk.
- Upstream `SoftFever/OrcaSlicer` code search: **0 results** for `minimal_chute_flush_length`.
  No future merge collision.

### Label string `"Minimal chute flush length"`

- Currently the label of the OLD key at `PrintConfig.cpp:2723` (already in place — the prior
  rescope round renamed the label from "Minimal purge on chute" to this). Proposal keeps it.
- Whole-tree grep for the literal: only `PrintConfig.cpp:2723` (live) + PROPOSAL.md + one
  rescope review note. No conflicting use by any other option. Safe.

### Zero-occurrence confirmations (all verified empty)

| Location checked | Old key | New key | Label |
|---|---|---|---|
| `resources/profiles/**.json` | 0 | 0 | 0 |
| `resources/**` (all) | 0 | 0 | 0 |
| `localization/*.po` / `*.pot` | 0 | 0 | 0 |
| `tests/**` | 0 | 0 | 0 |
| `src/libslic3r/Format/` (3mf / bbs_3mf) | 0 | 0 | 0 |
| `PresetBundle.cpp` | 0 | 0 | — |
| `ConfigManipulation.cpp/.hpp` | 0 | 0 | — |
| `AppConfig.cpp` | 0 | 0 | — |
| `compatible_*` lists in Preset.cpp | 0 | 0 | — |
| Dynamic lookups `option<ConfigOptionFloats>("…")` / `opt_float("…")` / `get("…")` | 0 | — | — |

(The `purgechutepileup_detection` family in `DeviceManager.cpp` / `PrintOptionsDialog.*` is
an **unrelated** BBL camera "purge chute pile-up" detection feature — not a config-option key,
not a collision. Disregard.)

## Findings

- **[INFO] Reference inventory is exact.** 8 build-input refs / 7 files, all in the proposal's
  table. No missed reference. The Floats→Float change is safe: the only place that reads the
  value is GCode.cpp:880, and proposal item 4 converts it from `.get_at(new_filament_id)` to
  `.value`. `ConfigOptionFloat` derives from `ConfigOptionSingle<double>` which exposes a public
  `T value` member (Config.hpp:307), so `full_config.minimal_chute_flush_length.value` is a valid
  scalar read. Precedent: `prime_volume` is `coFloat` / `((ConfigOptionFloat, prime_volume))`
  (PrintConfig.cpp:6853, PrintConfig.hpp:1623) and is read elsewhere as `m_config.prime_volume`.

- **[INFO] No survivor of the old type/accessor.** Grep for `.get_at` / `.values` /
  `ConfigOptionFloats`-cast / dynamic string lookups of the key returns nothing beyond the 8
  listed refs. After Floats→Float there is no second accessor that would fail to compile or
  silently index a now-scalar option.

- **[INFO] `handle_legacy` skip is correct.** `PrintConfigDef::handle_legacy`
  (PrintConfig.cpp:8049) is a pure `opt_key`/`value` *string* remapper invoked at config
  deserialization (e.g. `"wiping_volume" → "prime_volume"` at 8059). It cannot convert an array
  serialization (`coFloats` → `"0,0"`) to a scalar (`coFloat` → `"0"`); a legacy alias would
  therefore mis-parse on a real array value. Combined with the verified fact that NO on-disk
  artifact carries the old key (0 hits in resources/profiles, localization, tests, 3mf), there is
  nothing to migrate. Upstream code search confirms the key is fork-only, so it cannot arrive via
  a merged profile. Skipping is the right call.

- **[LOW] `is_BBL_printer` is brace-scoped inside `TabPrint::toggle_options()`** — placement
  caveat for proposal item 7. `TabPrint::toggle_options()` begins at Tab.cpp:2829. The
  `is_BBL_printer` local is declared *inside* the `if (m_preset_bundle) { … }` block
  (lines 2833-2836) and goes out of scope at the closing brace before
  `m_config_manipulation.toggle_print_fff_options(...)` runs. The proposal's "~2835" target is
  inside that block, which is fine, but if the implementer places
  `toggle_line("minimal_chute_flush_length", is_BBL_printer)` *after* the closing brace it will
  not compile (undeclared identifier). Fix: put the `toggle_line` call inside the
  `if (m_preset_bundle)` block, or hoist `is_BBL_printer` to function scope. (Note: the analogous
  `TabFilament::toggle_options()` declares `is_BBL_printer` at function scope, Tab.cpp:4269-4271 —
  hence the different pattern. Don't copy-paste the placement blindly.)

- **[LOW] Display label is shared but unique to this option.** "Minimal chute flush length" is
  used by exactly one option (the renamed one). No collision. Cosmetic only: the proposal moves
  the control to the Print Settings "Flush options" optgroup next to volume-based `flush_into_*`
  controls, while the sidetext stays `mm` (length) — consistent with the option's semantics but a
  potential user-confusion point already flagged in prior rounds (out of this agent's scope).

- **[LOW] Stale review-note prose** in `docs/reviews/**` will reference the old name/type after
  the rename. Non-build, harmless; mention only if doc hygiene is desired.

- **[INFO] Print-side wiring targets verified.** `s_Preset_print_options` flush entries are at
  Preset.cpp:1124-1127 (`flush_into_infill`, `flush_into_infill_min_layer`, `flush_into_objects`,
  `flush_into_support`) — correct insertion point for item 3. The Print invalidation siblings
  (`prime_volume` 342, `flush_into_infill` 343, `flush_into_support` 344,
  `wipe_tower_max_purge_speed` 356) confirm Print.cpp:289 already sits in the right block for
  item 5. Tab "Flush options" optgroup is at Tab.cpp:2682 with `flush_into_support` last at 2686
  — correct anchor for the new `append_single_option_line` (item 7).

## Sources

- Live repo greps (whole tree, `--exclude-dir=.git`) for `filament_minimal_purge_on_chute`,
  `minimal_chute_flush_length`, `minimal_purge_on_chute`, label string, and dynamic-lookup forms.
- `src/libslic3r/PrintConfig.cpp:2711-2736` (def block), `:8049-8123` (handle_legacy),
  `:6853` / `PrintConfig.hpp:1623` (prime_volume coFloat precedent).
- `src/libslic3r/Config.hpp:305-318` (`ConfigOptionSingle<T>::value`), `:764` (ConfigOptionFloat).
- `src/libslic3r/GCode.cpp:872-893` (the sole accessor + clamp block).
- `src/libslic3r/Preset.cpp:1124-1127` (print options flush_into), `:1282` (filament whitelist).
- `src/libslic3r/Print.cpp:289`, `:342-356` (invalidation block).
- `src/slic3r/GUI/Tab.cpp:2311` / `:2682-2686` (TabPrint::build Flush options),
  `:2829-2836` (TabPrint::toggle_options + is_BBL_printer scope),
  `:4151`, `:4265-4271`, `:4362` (TabFilament).
- `src/slic3r/GUI/Plater.cpp:16692`.
- `src/libslic3r/PresetBundle.hpp:260` (`is_bbl_vendor()`).
- Upstream code search (GitHub API, `repo:SoftFever/OrcaSlicer`):
  `filament_minimal_purge_on_chute` → 0 results; `minimal_chute_flush_length` → 0 results.
- Web search confirming upstream exposes only the sibling `filament_minimal_purge_on_wipe_tower`,
  not the chute key:
  https://github.com/OrcaSlicer/OrcaSlicer/wiki/material_multimaterial
