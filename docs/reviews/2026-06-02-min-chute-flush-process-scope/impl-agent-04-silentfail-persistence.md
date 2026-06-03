# Silent-failure audit — persistence & GUI for `minimal_chute_flush_length`

Scope: verify the rename `filament_minimal_purge_on_chute` (per-filament `coFloats`) →
`minimal_chute_flush_length` (global scalar `coFloat`, **process** preset) does not silently drop the
value, fail to render, or read a default 0. Verified against the committed post-change files on branch
`nightly-builds-with-bc` (diff `40fa1e2292..HEAD`).

## Verdict

PASS — no code-level silent failure in persistence or GUI. The whitelist move is correct and complete,
the GUI binds the option on a real process-preset page, the toggle is correctly scoped, and the GCode
read reaches the merged value. One LOW informational item: there is no `handle_legacy` migration for the
old key, so any pre-existing user value silently drops on load (one-time, fork-only, low impact).

## Findings

- [LOW] No legacy migration for the renamed key — `src/libslic3r/PrintConfig.cpp:8051`
  (`PrintConfigDef::handle_legacy`) — **manifests as**: a user who had set the old per-filament option
  `filament_minimal_purge_on_chute` in a saved filament preset loses that value silently when the preset
  loads (the key is no longer registered, so it is dropped without warning and the new process option
  defaults to 0). No console/UI notice. — **fix** (optional): add a `handle_legacy` branch that, on
  encountering `filament_minimal_purge_on_chute`, either drops it explicitly or maps a representative
  value into `minimal_chute_flush_length`. Because the type changes from per-filament vector to a global
  scalar there is no clean 1:1 mapping; dropping is defensible. Given the feature is fork-only and recent,
  impact is minimal, but the silent drop is real. This is a migration consequence, not a defect in the
  rename itself.

## Non-issues (verified, explicitly NOT silent failures)

1. **Whitelist move is correct and complete.** `minimal_chute_flush_length` is present in
   `s_Preset_print_options` at `src/libslic3r/Preset.cpp:1128`, which sits inside that initializer
   (initializer spans lines 970–1278; closing `};` at 1278), as a plain comma-separated string element
   between `flush_into_support` and `tree_support_branch_angle`. The old key is **removed** from
   `s_Preset_filament_options` (begins line 1280) — `grep filament_minimal_purge_on_chute Preset.cpp`
   returns nothing. So the value is saved/loaded under the **process** preset, matching its new scope.
   No "removed from one list but not added to the other" split.

2. **GCode read reaches the real value, not a default 0.** `src/libslic3r/GCode.cpp:881` reads
   `full_config.minimal_chute_flush_length.value`. `full_config` is `gcodegen.m_config`
   (`FullPrintConfig&`, declared at GCode.cpp:859). Inheritance chain confirmed in PrintConfig.hpp:
   the member lives in `GCodeConfig` (`PRINT_CONFIG_CLASS_DEFINE(GCodeConfig, …)` at 1303;
   member at 1458 as `ConfigOptionFloat`) → `PrintConfig` derives from `(MachineEnvelopeConfig,
   GCodeConfig)` (1485) → `FullPrintConfig` derives from `(PrintObjectConfig, PrintRegionConfig,
   PrintConfig)` (1668). So `.minimal_chute_flush_length.value` is a reachable `ConfigOptionFloat::value`
   that the full-config merge populates from the process preset. The struct living in the `GCodeConfig`
   block does not affect preset membership — membership is whitelist-driven (see the explicit code comment
   at PrintConfig.hpp:1456–1457), and the whitelist is `s_Preset_print_options`. Type alignment is clean:
   def registered `coFloat` (PrintConfig.cpp:2722) ↔ struct `ConfigOptionFloat` ↔ default
   `ConfigOptionFloat(0.)` (PrintConfig.cpp:2738), so there is no type-mismatch parse failure that would
   silently reset to 0.

3. **GUI renders on a real process-preset page.** `src/slic3r/GUI/Tab.cpp:2687` appends
   `minimal_chute_flush_length` to the `L("Flush options")` optgroup (`param_flush`, created at Tab.cpp:2682)
   inside `TabPrint::build()` — a genuine process-preset page, alongside the sibling `flush_into_*` rows.
   The Filament-tab line was removed (TabFilament::build no longer references it; grep confirms only the
   one TabPrint occurrence). The def's `category = L("Flush options")` (PrintConfig.cpp:2724) matches the
   optgroup, so search/category resolution is consistent. No wrong-config binding, no missing row.

4. **Toggle is correctly scoped and executes for this option.**
   `src/slic3r/GUI/ConfigManipulation.cpp:888–889` (inside `toggle_print_fff_options`, the TabPrint
   update path) calls `toggle_line("minimal_chute_flush_length", is_BBL_Printer)` and
   `toggle_field(..., is_BBL_Printer && have_prime_tower)`. `is_BBL_Printer` is the
   ConfigManipulation member set from `is_bbl_vendor()` via `set_is_BBL_Printer` at Tab.cpp:2836 — the
   same preset-vendor gate used by `timelapse_type` (ConfigManipulation.cpp:967), which also lives on a
   sub-page, so the pattern is proven. `toggle_line`/`toggle_field` resolve against `m_active_page`
   (Tab.cpp:1442–1447, 1433–1440) and gracefully no-op when the Multimaterial page is not active; the
   toggle re-applies when the user navigates to that page. This is the standard OrcaSlicer behavior, not a
   silent-failure unique to this option. The row is not permanently hidden: on a BBL preset it is shown
   (greyed without a prime tower, matching the floor being a no-op without the tower). On non-BBL the line
   is hidden — intentional, since the floor never fires off-chute; this is a deliberate UI gate, not a
   silent no-op of a visible control.

5. **No leftover code/profile reference to the old key.** `grep filament_minimal_purge_on_chute` across
   `src/`, all `*.json` profiles, and `*.po/*.pot` localization returns nothing functional. The only
   remaining hits are in `docs/**` review/spec markdown (non-runtime). Every live reference was migrated:
   def (PrintConfig.cpp:2722), struct (PrintConfig.hpp:1458), invalidation list (Print.cpp:289),
   Plater on_config_change list (Plater.cpp:16692), GCode read (GCode.cpp:881), Tab (Tab.cpp:2687),
   toggle (ConfigManipulation.cpp:888–889), whitelist (Preset.cpp:1128). No dangling string that would
   silently never match.

## Sources

- `git -C /Volumes/MacMicroSD/Github/OrcaSlicer-nighty diff 40fa1e2292..HEAD -- src/`
- `src/libslic3r/Preset.cpp:970,1124-1128,1278,1280`
- `src/libslic3r/PrintConfig.cpp:2722-2738,8051`
- `src/libslic3r/PrintConfig.hpp:1303,1456-1458,1485,1668`
- `src/libslic3r/GCode.cpp:859,881`
- `src/libslic3r/Print.cpp:289`
- `src/slic3r/GUI/Tab.cpp:1433-1447,2682-2687,2836,8056-8068`
- `src/slic3r/GUI/ConfigManipulation.cpp:39-55,592,842-845,884-889,967`
- `src/slic3r/GUI/ConfigManipulation.hpp:27,83-84`
- `src/slic3r/GUI/Plater.cpp:16692`
- repo-wide grep for `filament_minimal_purge_on_chute` (only `docs/**` remain)
