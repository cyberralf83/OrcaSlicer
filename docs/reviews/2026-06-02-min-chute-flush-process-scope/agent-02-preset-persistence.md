# Agent-02 — Preset Persistence & Whitelist Review

Scope: persistence/whitelist correctness of moving `filament_minimal_purge_on_chute`
(`ConfigOptionFloats`, filament-scope) → `minimal_chute_flush_length` (`ConfigOptionFloat`,
process/print-scope). Verified against LIVE code on branch `nightly-builds-with-bc`.

## Verdict

APPROVE with one MEDIUM caveat. The persistence reclassification is correct and complete:
removing the key from `s_Preset_filament_options` and adding it to `s_Preset_print_options`
is the *sole* mechanism that governs which preset the value is saved into / loaded from, and
the scalar type is safe in the print list (it is unsafe in the filament list — which the
proposal correctly avoids). No second list (`*_options_with_variant`,
`filament_extruder_override_keys`) needs touching. Existing project 3MFs load cleanly; dirty
detection works once the key is in the print whitelist. The one caveat is a C++ scoping gotcha
in the proposed `TabPrint::toggle_options()` insertion point.

## Confirmed correct

- **Whitelist is the persistence authority.** `PresetCollection::add_default_preset()` does
  `m_presets.back().config.apply_only(defaults, keys)` (Preset.cpp:1523), where `keys` is the
  per-type whitelist passed from `PresetBundle` ctor (PresetBundle.cpp:322 `Preset::print_options()`,
  :323 `Preset::filament_options()`). So adding `minimal_chute_flush_length` to
  `s_Preset_print_options` pulls it (from `FullPrintConfig::defaults()`) into the print preset
  config, and removing it from `s_Preset_filament_options` drops it from filament presets. This is
  the complete reclassification — claim 3 holds.
- **Exact insertion target verified.** `s_Preset_print_options` opens at Preset.cpp:970 and closes
  with `};` at Preset.cpp:1277. The `flush_into_*` entries are at 1124-1127 (`flush_into_infill`,
  `flush_into_infill_min_layer`, `flush_into_objects`, `flush_into_support`). Inserting the new key
  anywhere between 971 and 1276 lands inside the initializer; the proposal's "≈1124-1127" is inside.
- **Old key removed from the right line.** `filament_minimal_purge_on_chute` is in
  `s_Preset_filament_options` at Preset.cpp:1282 (sibling of `filament_minimal_purge_on_wipe_tower`,
  which is correctly NOT touched).
- **No second list to update.** The old key is absent from `print_options_with_variant`
  (PrintConfig.cpp:8338 — contains only `print_extruder_id` + `print_extruder_variant`, all else
  commented), `filament_options_with_variant` (8369), `printer_options_with_variant_1/2`,
  and `filament_extruder_override_keys` (PrintConfig.cpp:63). The new key needs adding to NONE of
  them (a single global scalar has no per-extruder variant). Confirmed by grep: zero matches.
- **`printer_options()` does not double-register.** It aggregates printer + machine_limits +
  nozzle_options only (Preset.cpp:1456-1465), NOT print_options. No duplicate registration.
- **`Preset::normalize()` scalar safety confirmed.** normalize() (Preset.cpp:442) iterates
  `Preset::filament_options()` and only resizes vector options — the guard at 474 is
  `if (opt != nullptr && opt->is_vector())`. A scalar `coFloat` would be skipped even if left in
  the filament list, but leaving it there would still be wrong (it would mean the value persists in
  filament presets). The proposal removes it from the filament list, so normalize never sees it.
  Correct. (Claim from the brief — "a scalar must NOT be in the filament list" — confirmed: it must
  not be, and the proposal removes it.)
- **3MF / preset load is non-rejecting.** `Preset::remove_invalid_keys()` (Preset.cpp:489) merely
  *erases* unknown keys and returns a string for logging — no throw, no load failure. Project 3MF
  config import (`_extract_project_config_from_archive` → `config.load_from_json(...,
  config_substitutions, ...)`, bbs_3mf.cpp:2646) and the gcode-legacy path
  (`load_from_gcode_string_legacy`, bbs_3mf.cpp:2626) route through the substitution-aware /
  nothrow deserialize that records `unrecogized_keys` (Config.cpp:580-587) rather than throwing.
  An existing 3MF that lacks `minimal_chute_flush_length` simply leaves it at its `0` default; a
  (hypothetical, never-shipped) 3MF carrying the old `filament_minimal_purge_on_chute` would be
  silently ignored. No validation rejects the load. Claim 4/forward-compat holds.
- **Dirty detection works in print scope.** `PresetCollection::dirty_options()` /
  `is_dirty()` compare via `config.diff()` / `deep_diff()` (Preset.cpp:3343, 3432), which iterate
  the *config's actual keys*, not the whitelist. Once the key is in the print preset config, it is
  compared. A `coFloat` is handled by `deep_diff`'s `default:` branch (Preset.cpp:3388) after the
  `*this_opt != *other_opt` gate (3357), and trivially by non-deep `diff()`. Save-diff path
  (Preset.cpp:657-693) handles scalars via `if (opt_dst->is_scalar() ...) opt_dst->set(opt_src)`
  (676-677), bypassing the vector-nil logic. So edit → dirty flag → save writes the scalar correctly
  into the user/process preset. Claim 5 (no stale state) holds.
- **GCode read is scope-independent and type-correct.** `GCodeConfig` → `PrintConfig`
  (PrintConfig.hpp:1485) → `FullPrintConfig` (PrintConfig.hpp:1668). `full_config` is a
  `FullPrintConfig`, so `minimal_chute_flush_length` resolves regardless of any whitelist. The
  member lives in `FullPrintConfig::defaults()` purely by struct membership, so the slicing read at
  GCode.cpp:880 is unaffected by the whitelist move. `ConfigOptionFloat : ConfigOptionSingle<double>`
  exposes public `double value` (Config.hpp:307/764), so `... .minimal_chute_flush_length.value`
  cast to `(float)` is valid. Claim 2 holds.
- **Reference count is exact.** Full-repo grep finds exactly 8 occurrences of the old key across the
  7 listed files (PrintConfig.cpp:2722, PrintConfig.hpp:1456, Preset.cpp:1282, Print.cpp:289,
  GCode.cpp:880, Plater.cpp:16692, Tab.cpp:4151, Tab.cpp:4362) and ZERO occurrences of the new name
  anywhere (no collision). No profile JSON, no `.po`, no tests reference either name. Claim 5 holds.
- **Invalidation/update-scheduled renames land in the right buckets.** Print.cpp:289 is inside the
  `else if (...) { steps.emplace_back(psWipeTower); steps.emplace_back(psSkirtBrim); }` block
  (Print.cpp:283-370) in `invalidate_state_by_config_options`, which matches opt keys by name
  irrespective of scope. Plater.cpp:16692 is inside the `update_scheduled = true` block
  (16688-16697) in the config-change handler, also scope-agnostic. Both renames preserve correct
  behavior. Claim 6 holds.

## Findings

- **[MEDIUM] Tab.cpp ~2835 — `is_BBL_printer` is block-scoped; the proposed `toggle_line` call must
  go INSIDE the `if (m_preset_bundle){…}` block or it won't compile.**
  In `TabPrint::toggle_options()` (Tab.cpp:2829), `bool is_BBL_printer` is declared at line 2834
  *inside* the `if (m_preset_bundle)` block (2833-2836) and goes out of scope at the closing brace
  on line 2836. The proposal says to add `toggle_line("minimal_chute_flush_length", is_BBL_printer)`
  "≈2835". That is only valid if the new call is placed *between lines 2835 and 2836* (still inside
  the block). If an implementer instead groups it with the other `toggle_*` calls further down
  (after 2836, e.g. near 2838+, by analogy to how `TabFilament::toggle_options()` keeps its
  `is_BBL_printer` in scope for the whole function at 4269-4362), it will fail to compile
  (`is_BBL_printer` undeclared). Fix: either place the call on a new line immediately before the
  `}` at 2836, or hoist `is_BBL_printer` to function scope (declare before the `if`, like
  `TabFilament` does). Recommend hoisting for clarity. — Not a defect in the proposal's intent, but
  the stated line ≈2835 is ambiguous and the naive placement breaks the build.

- **[LOW] Tab.cpp — `toggle_line` only affects the row when the Multimaterial page is the active
  page.** `Tab::toggle_line()` (Tab.cpp:1442) early-returns if `!m_active_page` and no-ops if
  `get_line()` returns nullptr (line not on the current page). This is the normal pattern and
  matches existing flush options behavior, so functionally fine. Note for the implementer: the
  existing `flush_into_*` lines have NO toggle (always visible), so adding a BBL-gated `toggle_line`
  for the new key changes only the new row — exactly the intended BBL-only visibility. No issue,
  just confirming the row hides correctly on non-BBL and re-fires on printer change because
  `toggle_options()` is re-invoked on preset/printer switch (Tab.cpp:515, 2914, 3102). Claim 4 holds.

- **[LOW / out-of-focus] PrintConfig.cpp:2724-2732 tooltip rewording must drop the
  "adjacent 'Minimal purge on wipe tower'" phrasing.** The live tooltip explicitly references the
  adjacent wipe-tower option (a Filament-tab neighbor). After the move to the Print Settings "Flush
  options" optgroup, that neighbor is no longer adjacent, so the parenthetical at 2725-2726 becomes
  misleading. The proposal already calls for this reword (change #2); flagging only to confirm it is
  required, not optional, to avoid a misleading-inconsistency. Cosmetic/UX only.

- **[INFO] No `handle_legacy` alias is needed.** Confirmed the feature was never shipped (zero
  references in `resources/profiles/**`, zero in localization, zero in tests) and the unknown-key
  load path is non-rejecting, so skipping a legacy array→scalar alias is safe. An alias would in any
  case be awkward (vector→scalar). Agree with the proposal's non-goal.

## Sources

- src/libslic3r/Preset.cpp: 442-487 (normalize), 489-503 (remove_invalid_keys), 634-700 (Preset::save
  diff/scalar path), 970/1124-1127/1277 (s_Preset_print_options bounds + flush_into_*),
  1279-1285 (s_Preset_filament_options incl. old key at 1282), 1446-1465 (option accessors +
  printer_options aggregation), 1467-1477/1519-1526 (PresetCollection ctor + add_default_preset
  apply_only), 3342-3410 (deep_diff), 3416-3440 (is_dirty / dirty_options).
- src/libslic3r/PresetBundle.cpp: 322-325 (collections built from print_options()/filament_options()).
- src/libslic3r/PrintConfig.cpp: 63-74 (filament_extruder_override_keys), 2722-2736 (def + default
  literal + tooltip), 8338-8367 (print_options_with_variant), 8369+ (filament_options_with_variant),
  8801-8813 (set_num_filaments resize).
- src/libslic3r/PrintConfig.hpp: 305-308/764 (ConfigOptionSingle.value / ConfigOptionFloat),
  512/684-687 (variant/override list externs), 1452-1459 (GCodeConfig macro block w/ key at 1456),
  1483-1485/1666-1669 (PrintConfig/FullPrintConfig inheritance chain).
- src/libslic3r/Config.cpp: 577-589 (set_deserialize_nothrow → handle_legacy → unrecognized-key
  recording), 603-626 (set_deserialize_raw unknown-key throw — only on strict path).
- src/libslic3r/GCode.cpp: 875-888 (append_tcr read site).
- src/libslic3r/Print.cpp: 94 (invalidate_state_by_config_options sig), 283-370 (psWipeTower+
  psSkirtBrim block incl. key at 289).
- src/slic3r/GUI/Plater.cpp: 16688-16697 (update_scheduled block incl. key at 16692).
- src/slic3r/GUI/Tab.cpp: 1433-1447 (toggle_option/toggle_line), 2675-2694 (Flush options optgroup),
  2829-2836 (TabPrint::toggle_options + block-scoped is_BBL_printer), 4150-4151/4355/4362 (filament
  tab references).
- src/slic3r/GUI/Tab.hpp: 394-395 (toggle_option/toggle_line default opt_index = -1).
- src/libslic3r/Format/bbs_3mf.cpp: 2617-2653 (project/print config extraction → non-throwing load).
- Full-repo grep: exactly 8 old-key refs / 0 new-key refs across src, tests, resources, localization.
