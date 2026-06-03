# Holistic correctness review — min-chute-flush filament→process scope move

**Reviewer focus:** broad logic / edge cases / inconsistencies the compile, fidelity, style, and
silent-failure reviewers might miss.
**Diff reviewed:** `git diff 40fa1e2292..HEAD -- src/` (8 files; commits `2c5727329a` backend,
`36541c7606` GUI).
**Design:** `docs/superpowers/specs/2026-06-02-min-chute-flush-process-scope-design.md`.

## Verdict

**PASS — ship it.** The implementation is correct, complete, and matches the design exactly (8 files,
all 8 references accounted for, zero remaining references to the old key in any code/resource/loc
file). I found no CRITICAL/HIGH issues. The toggle semantics, scope move, persistence path, search
indexing, per-object-override exclusion, and default-byte-identical-G-code claims all hold up under
inspection. Findings below are LOW (documentation/polish) only.

## Confirmed correct

1. **Toggle ordering/interaction is correct and benign** (`ConfigManipulation.cpp:888-889`).
   `toggle_line(key, is_BBL_Printer)` sets row visibility (`Line::toggle_visible`,
   `Tab.cpp:1442-1447`); `toggle_field(key, is_BBL_Printer && have_prime_tower)` sets the field's
   enabled/greyed state (`Field::toggle` via `toggle_option`, `Tab.cpp:1433-1440`). The two flags are
   orthogonal. Calling `toggle_field` on a row that is currently hidden just stores the disabled state
   on a not-shown control; when the row is later shown the greyed state is already correct. This is the
   exact pattern the sibling `flush_into_*` options use (`toggle_field(el, have_prime_tower)` at
   `ConfigManipulation.cpp:881-882`). No odd state. On non-BBL the row is hidden (correct); on
   BBL-without-tower the row shows greyed (correct, matching the no-op-without-tower rationale).

2. **`is_BBL_Printer` is in scope and freshly synced.** It is a `ConfigManipulation` member
   (`ConfigManipulation.hpp:27`), set immediately before the toggle call in `TabPrint::toggle_options`
   (`Tab.cpp:2834-2839`) from `preset_bundle->is_bbl_vendor()`. Visibility re-fires on printer change.

3. **No spurious "modified"/dirty marking on existing process presets.** Default presets are built from
   `static_cast<const PrintRegionConfig &>(FullPrintConfig::defaults())` with `keys = print_options()`
   (`PresetBundle.cpp:322`, `Preset.cpp:1520-1524`). In `apply_only` (`Config.cpp:461-499`) the source
   `other.option()` call is **virtual** (`Config.hpp:2866-2868`), so the static_cast to
   `PrintRegionConfig&` does NOT hide the new key — the real object is a `FullPrintConfig`
   (`PrintConfig.hpp:1668-1671`, deriving `PrintConfig` ⊃ `GCodeConfig` where the key lives at
   `PrintConfig.hpp:1458`). Thus the default print preset gets `minimal_chute_flush_length = 0`. On-disk
   presets that omit the key are loaded over the default (`Preset.cpp:2392-2396`,
   `load_external_preset` 2434-2444), so both `selected` and `edited` configs carry `0`; `config.diff`
   compares equal → no dirty. Verified zero on-disk references in `resources/profiles/**`. This is the
   same mechanism that makes `prime_volume` (also a `PrintConfig`-block scalar in
   `s_Preset_print_options`, `Preset.cpp:1115`) work today.

4. **Search box picks the option up automatically.** The searcher indexes via
   `ConfigOptionsGroup::get_option → add_key(opt_id, type, title, config_category())`
   (`OptionsGroup.cpp:632-633`), i.e. group = the optgroup title ("Flush options") and category = the
   page category — both non-empty because the option is appended to a real optgroup
   (`Tab.cpp:2687`). The `gc.group.IsEmpty() || gc.category.IsEmpty()` guard (`Search.cpp:90,388`) is
   therefore satisfied. No hardcoded search key list needs editing.

5. **UnsavedChanges / preset-compare dialog groups it correctly.** That dialog resolves
   category/group from the *searcher* (`UnsavedChangesDialog.cpp:1719,2276`), not from `def->category`,
   so the option lands under its real UI group ("Flush options").

6. **Per-object "Add settings" override menu correctly EXCLUDES it.** Two paths:
   (a) the hardcoded `FREQ_SETTINGS_BUNDLE_FFF["Flush options"]` bundle (`GUI_Factories.cpp:72`) does
   not list the key. (b) the category-driven path reads `def->category` (`GUI_Factories.cpp:288,392`)
   but first filters opt_keys to `get_options()` = `PrintRegionConfig` + `PrintObjectConfig` keys only
   (`GUI_Factories.cpp:168-185,276-278`). The new key is in `GCodeConfig` (a sibling block, NOT a base
   of those two), so it is filtered out. Correct: a global scalar must not be per-object-overridable.
   Its UI siblings `flush_into_*` ARE per-object (they live in `PrintObjectConfig`,
   `PrintConfig.hpp:1006-1010`) — the divergence is intentional and matches `prime_volume`.

7. **`def->category = L("Flush options")` is consistent and harmless.** Identical to
   `flush_into_objects` (`PrintConfig.cpp:7048` block sets the same category). Adds proper grouping in
   category-driven UI; does not (per #6) leak the key into per-object overrides.

8. **Mode consistency.** New option is `comAdvanced` (`PrintConfig.cpp:2737`); all four `flush_into_*`
   siblings are also `comAdvanced`. They appear/disappear together with the mode switch.

9. **Tooltip is well-formed UTF-8.** File validates as UTF-8; `mm³` = bytes `c2 b3` (U+00B3), used 13×
   in the file. No broken escapes; the `L(...)` concatenated string literal is syntactically clean
   (`PrintConfig.cpp:2725-2734`). The added clause "and only when the prime tower is enabled" matches
   the new grey-without-tower UI behavior — internally consistent.

10. **G-code math unchanged at default 0 → byte-identical.** `min_chute_length = 0` ⇒
    `min_chute_purge = 0` ⇒ `apply_chute_min = false` ⇒ both branches of `purge_volume`
    (`GCode.cpp:885-888`) collapse to the upstream `std::max(tcr.purge_volume, g_min_purge_volume)` /
    `0.f` forms. The `.value` scalar read (`GCode.cpp:881`) is valid because `FullPrintConfig ⊃
    PrintConfig ⊃ GCodeConfig`. Cereal serialization for the `ConfigOptionFloat` member is
    macro-generated. `Print.cpp:289` invalidation rename is in the correct `psWipeTower+psSkirtBrim`
    block; a miss would fail safe via `invalidate_all_steps()`.

11. **`Print.cpp:3452/3459` are the *wipe-tower* minimum** (`filament_minimal_purge_on_wipe_tower`), a
    separate feature, correctly left untouched. No collateral edits anywhere (diff is exactly 8 files).

## Findings

- **[LOW] `PrintConfig.cpp:2735` sidetext "mm" vs the now-global semantics — fine, but worth a glance.**
  The option is a length in mm of filament, sidetext `"mm"` is correct. No action; noting only that the
  field is global yet sits among per-filament-flavored neighbors — the tooltip already calls this out
  ("This is a single global value"). No fix needed.

- **[LOW] `GCode.cpp:873-875` comment wording.** The comment says "global filament length (mm)";
  slightly redundant ("global … length" then "per-filament purge volume"). Reads correctly and matches
  the design's prescribed text. Cosmetic only — no fix required.

- **[LOW] UI/runtime BBL-gate divergence is intentional but relies on vendor alignment.** UI uses
  `is_bbl_vendor()` (`Tab.cpp:2835`); emission uses `print->is_BBL_printer()`
  (`GCode.cpp:884,2029-2034`). For the normal case (a BBL printer preset) both are true, so the user
  sees the row AND the floor fires — no surprising mismatch. The inline comment
  (`ConfigManipulation.cpp:884-885`) already documents the distinction. No fix.

- **[LOW / pre-existing, out of scope] `filament_area`/`flush_count` div-by-zero in shared upstream
  code** (`GCode.cpp:889` and the noted `970/7929`) is unreachable when this feature fires (the ≥100
  mm³ floor guarantees `flush_count ≥ 1`) and is not introduced or worsened by this change. The design
  correctly declares it out of scope; the reverted `max(1,…)` guard (commit `4673720c01`) is NOT
  reintroduced. Confirmed `set_extruder` path is byte-identical to upstream.

## Sources

- Diff: `git diff 40fa1e2292..HEAD -- src/`; commits `2c5727329a`, `36541c7606`.
- `src/libslic3r/GCode.cpp:860-895, 2029-2034`
- `src/libslic3r/PrintConfig.cpp:2722-2738, 7016-7055`
- `src/libslic3r/PrintConfig.hpp:1006-1010, 1304, 1455-1458, 1485-1487, 1668-1671`
- `src/libslic3r/Preset.cpp:1115, 1128, 1283, 1447, 1520-1524, 2392-2396, 2418-2444`
- `src/libslic3r/Print.cpp:288-289, 3452-3459`
- `src/libslic3r/Config.cpp:461-499`; `src/libslic3r/Config.hpp:2572-2576, 2621-2624, 2866-2868`
- `src/libslic3r/PresetBundle.cpp:321-326`
- `src/slic3r/GUI/ConfigManipulation.cpp:39-55, 592-596, 842-889`;
  `src/slic3r/GUI/ConfigManipulation.hpp:27, 83-84`
- `src/slic3r/GUI/Tab.cpp:1433-1447, 2654, 2684-2687, 2830-2839, 8056-8068`
- `src/slic3r/GUI/OptionsGroup.cpp:620-636, 795-824`; `OptionsGroup.hpp:59`
- `src/slic3r/GUI/Search.cpp:86-103, 370-391, 418-420`; `Search.hpp:44, 96, 127`
- `src/slic3r/GUI/UnsavedChangesDialog.cpp:248-304, 1719, 2276`
- `src/slic3r/GUI/GUI_Factories.cpp:43-47, 68-72, 168-185, 270-301`
- `src/slic3r/GUI/Plater.cpp:16692`
- Design spec (above path).
