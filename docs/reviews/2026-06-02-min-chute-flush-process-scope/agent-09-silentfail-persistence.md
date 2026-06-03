# Agent-09 — Silent-Failure Hunt: Persistence & GUI Wiring

Scope: move `filament_minimal_purge_on_chute` (filament-scope, `ConfigOptionFloats`) →
`minimal_chute_flush_length` (process-scope, `ConfigOptionFloat`). Hunting only for things that
compile + run but quietly do the wrong thing / nothing with **no error surfaced to the user**.

All claims verified against live code on branch `nightly-builds-with-bc`.

## Verdict

The proposal **as written is silent-failure-free** for persistence and GUI wiring — *provided all
three coordinated edits land together* (decl/def type change, whitelist swap, and both GUI edits).
The one genuine silent-failure risk is a **partial application of the proposal** (whitelist swap
without the GUI move, or vice versa), which the load path will hide. That is a process risk, not a
defect in the proposal's end state. Documented below as MEDIUM so the team treats the 7-file change
as atomic. Everything the brief feared (wrong-struct read-default, type mismatch, stale visibility,
wrong-tab no-render, compatibility clobber) is **proven safe**.

## Findings

### [MEDIUM] Whitelist limbo is real but only bites on a *partial* landing — Preset.cpp:1519-1523, 2391-2395, 2434-2443
- **Mechanism.** Preset persistence is governed *entirely* by the `s_Preset_*_options` whitelists,
  not by C++ struct membership. `PresetCollection::add_default_preset()` seeds each default preset's
  config via `config.apply_only(defaults, keys)` where `keys` is the whitelist
  (`Preset.cpp:1523`). On load, `PresetCollection::load_preset()` does
  `DynamicPrintConfig cfg(default_preset().config); cfg.apply_only(config, cfg.keys(), true);`
  (`Preset.cpp:2393-2394`) and `load_external_preset()` does the same with `cfg.keys()`
  (`Preset.cpp:2436-2443`). `cfg.keys()` is exactly the whitelist-seeded key set. **Any key present
  in an on-disk INI/3MF but absent from the relevant whitelist is silently dropped on load** — no
  log, no substitution entry, no user message. (Note: this is a *different* path from the
  unknown-key handler in `Config.cpp:580-586` / `set_deserialize_raw` throwing
  `UnknownOptionException` at `Config.cpp:624-625`. Those fire only for keys absent from the
  `ConfigDef`. `minimal_chute_flush_length` WILL be in the `ConfigDef`, so the only relevant
  silent-drop vector is the `apply_only(..., cfg.keys())` whitelist filter.)
- **How it manifests to the user.** If the proposal's whitelist edit is applied *partially* — e.g.
  `minimal_chute_flush_length` is added to the `ConfigDef` (`PrintConfig.cpp`) and shown on the
  Print tab, but the `s_Preset_print_options` add (`Preset.cpp` ≈1124-1127) is forgotten — the user
  edits the value, saves the process preset, the save quietly omits the key (not in `config.keys()`),
  and on reload the value is silently back to default `0`. Feature appears dead with **zero error**.
  Symmetric risk: if removal from `s_Preset_filament_options` is done but the `s_Preset_print_options`
  add is missed, the key belongs to *no* preset list and is unsavable everywhere.
- **Fix.** Treat the 7-file change as atomic. The single load-bearing line for persistence is the
  `s_Preset_print_options` insertion near the `flush_into_*` block (`Preset.cpp:1124-1127`,
  verified: `flush_into_infill`/`flush_into_infill_min_layer`/`flush_into_objects`/`flush_into_support`
  are exactly there). Add a one-line smoke test in the PR checklist: set value on a BBL process
  preset, save, restart, confirm it reloads non-zero.

### [LOW] At default `0`, the value silently does nothing on non-BBL printers — by design, but worth stating — GCode.cpp:883
- **Mechanism.** The GCode floor is gated by `gcodegen.is_BBL_Printer()` at runtime
  (`GCode.cpp:883`, `apply_chute_min = is_real_toolchange && min_chute_purge > EPSILON &&
  gcodegen.is_BBL_Printer()`). The proposal keeps BBL-only **UI visibility** via
  `toggle_line(..., is_BBL_printer)`. So even if a non-BBL user somehow set a non-zero value (e.g.
  via an inherited/imported process preset that is later used on a non-BBL printer), the GCode path
  silently ignores it.
- **How it manifests.** A value persisted on a process preset that is then paired with a non-BBL
  printer has **no effect and no warning**. This is the intended "BBL-only enforcement" and matches
  upstream behavior for other BBL-gated options; not a regression. Listed only for completeness.
- **Fix.** None required. The `toggle_line` hides the row on non-BBL so the user cannot normally set
  it there; the runtime gate is the backstop. Acceptable.

## Non-issues (feared, proven safe)

### Wrong-struct red herring — leaving the decl in `GCodeConfig` causes NO silent read-default — SAFE
- `FullPrintConfig` is `PRINT_CONFIG_CLASS_DERIVED_DEFINE0(FullPrintConfig, (PrintObjectConfig,
  PrintRegionConfig, PrintConfig))` (`PrintConfig.hpp:1666-1669`). `PrintConfig` is
  `PRINT_CONFIG_CLASS_DERIVED_DEFINE(PrintConfig, (MachineEnvelopeConfig, GCodeConfig), ...)`
  (`PrintConfig.hpp:1483-1485`). So the inheritance chain is `FullPrintConfig ⊃ PrintConfig ⊃
  GCodeConfig`. The read site uses `FullPrintConfig& full_config = gcodegen.m_config;`
  (`GCode.cpp:859`) and `full_config.minimal_chute_flush_length.value` resolves to the member
  inherited from `GCodeConfig`. **The member is the same object the process preset writes into** — no
  shadow copy, no default fallback. The team should NOT waste effort relocating the `.hpp`
  declaration between structs; the proposal's "keep it in `GCodeConfig`" decision is correct and
  minimizes the upstream diff. Preset *membership* is the whitelist, not the struct.

### Type mismatch (scalar vs array) silent truncation/zero-fill — SAFE
- New decl is `ConfigOptionFloat` (`PrintConfig.hpp` edit #1); new def is `coFloat` with
  `set_default_value(new ConfigOptionFloat(0.))` (`PrintConfig.cpp:2722,2736` edit #2). The read is
  `full_config.minimal_chute_flush_length.value` — `ConfigOptionFloat : ConfigOptionSingle<double>`
  has a public `double value` (`Config.hpp:764, 305`), same pattern used safely at `GCode.cpp:518,
  814, 816`. No `.get_at()` remains (the only chute `.get_at` is the line being changed,
  `GCode.cpp:880`). Because the feature was never shipped, **no on-disk file contains the old array
  form**, so there is no array-string ever fed to a scalar deserializer — no truncation/zero-fill
  path exists. (Even if one did, a mismatched scalar deserialize would *fail* and either throw or be
  recorded, not silently zero-fill — `Config.cpp:657-674` only substitutes-and-continues for
  enum/bool types, never for `coFloat`.)

### Stale BBL visibility — `TabPrint::toggle_options()` DOES re-fire on printer change — SAFE
- Call chain verified end-to-end:
  - Printer combo / preset switch → `Tab::select_preset()` → printer branch calls
    `load_current_preset()` (`Tab.cpp:6215`) and `on_presets_changed()` (`Tab.cpp:5835/5844`).
  - `Tab::on_presets_changed()` iterates `m_dependent_tabs` (for ptFFF = `{TYPE_PRINT,
    TYPE_FILAMENT}`) and calls `tab->load_current_preset()` (`Tab.cpp:2141-2149`).
  - `GUI_App::load_current_presets()` independently loops every tab and calls
    `tab->load_current_preset()` (`GUI_App.cpp:8144-8151`).
  - `Tab::load_current_preset()` calls `update()` (`Tab.cpp:5704`).
  - `TabPrint::update()` calls `toggle_options()` when `m_update_cnt==0` and the active page is not
    "Dependencies" (`Tab.cpp:2912-2914`).
  - `TabPrint::toggle_options()` recomputes `is_BBL_printer =
    wxGetApp().preset_bundle->is_bbl_vendor()` *locally* every call (`Tab.cpp:2834`).
  So the proposed `toggle_line("minimal_chute_flush_length", is_BBL_printer)` inside
  `TabPrint::toggle_options()` re-evaluates on every printer change. **Row correctly hides/shows when
  switching between BBL and non-BBL printers** — no stale visibility. (One caveat outside this
  proposal's scope: `toggle_options()` early-returns if `!m_active_page` (`Tab.cpp:2831`), but
  `update()` only calls it for the active page anyway, and switching to the tab triggers
  `OnActive`→`update()`; standard upstream behavior, not a new silent failure.)

### GUI on wrong tab / wrong config — would NOT silently no-render — SAFE
- The proposal binds the option line to the **Print Settings** "Flush options" optgroup
  (`Tab.cpp:2682-2686`, verified `flush_into_infill/.../flush_into_support` live there) whose
  `m_config` is the process/print config — the same config that now owns the key via
  `s_Preset_print_options`. `append_single_option_line` resolves the field from
  `print_config_def`; the key is registered in the `ConfigDef`, so the field renders. Had it been
  left on the Filament tab while the key moved to the print whitelist, the Filament tab's `m_config`
  would not contain the key and the line could fail to populate — but the proposal explicitly
  *removes* the Filament-tab line (`Tab.cpp:4151`) and toggle (`Tab.cpp:4362`). Correct.

### ConfigManipulation / "filament overrides process" clobber — SAFE
- `ConfigManipulation::toggle_print_fff_options()` gates the `flush_into_*` *fields* on
  `have_prime_tower` (`ConfigManipulation.cpp:881-882`) but **never references
  `minimal_chute_flush_length`** (grep confirms zero hits). `TabPrint::toggle_options()` calls
  `toggle_print_fff_options()` first (`Tab.cpp:2838`), then the proposal's explicit
  `toggle_line("minimal_chute_flush_length", is_BBL_printer)` runs after — no ordering conflict, no
  silent override of the new row's visibility.
- The filament-override machinery (`filament_extruder_override_keys`, `PrintConfig.cpp:63-84`;
  `filament_options_with_variant`, `PrintConfig.cpp:8369`) does **not** contain the old or new key,
  so no "filament value overrides process value" path can clobber it. Confirmed the old key is not in
  `print_options_with_variant`/`filament_options_with_variant` either — so no stale variant-list
  reference is left behind by the rename.

### Re-slice / re-export triggers — SAFE
- `Print.cpp:289` (`filament_minimal_purge_on_chute`) sits inside the long `else if` disjunction that
  spans lines 283→367 and terminates in `steps.emplace_back(psWipeTower);
  steps.emplace_back(psSkirtBrim);` (`Print.cpp:368-370`) — the **same** group as `flush_into_infill`
  (343) and `flush_into_support` (344). Renaming the string literal in place preserves the
  invalidation. `Plater.cpp:16692` is a plain `opt_key ==` string compare in the `update_scheduled`
  block; rename preserves it. Neither is silently mis-grouped.

### Rename surface — exactly 8 references, no leftovers — SAFE
- `grep -rn "filament_minimal_purge_on_chute"` over `src/ resources/ localization/` returns exactly:
  `PrintConfig.cpp:2722`, `PrintConfig.hpp:1456`, `Print.cpp:289`, `GCode.cpp:880`,
  `Preset.cpp:1282`, `Plater.cpp:16692`, `Tab.cpp:4151`, `Tab.cpp:4362` — 8 references / 7 files,
  matching the proposal. Zero hits in `resources/`, `tests/`, `localization/`, no `.get_at` remnant,
  no profile JSON, no 3MF, no name collision with the new key (`minimal_chute_flush_length` has zero
  pre-existing references).

## Sources

- `src/libslic3r/Preset.cpp:466, 970, 1124-1127, 1279-1284, 1446-1447, 1519-1526, 2391-2396,
  2417-2443` — whitelist definitions and the `apply_only(..., cfg.keys())` load/save filter.
- `src/libslic3r/Config.cpp:573-589 (set_deserialize_nothrow), 603-674 (set_deserialize_raw),
  624-625 (UnknownOptionException), 657-674 (enum/bool-only substitution)` — unknown-key path
  (confirmed *not* the relevant vector here; whitelist filter is).
- `src/libslic3r/PrintConfig.hpp:1303-1310 (GCodeConfig), 1483-1485 (PrintConfig),
  1666-1669 (FullPrintConfig)` — aggregation chain proving GCodeConfig membership is fine.
- `src/libslic3r/PrintConfig.cpp:63-84 (filament_extruder_override_keys), 2711-2736 (def block),
  8338-8451 (options_with_variant sets)`.
- `src/libslic3r/GCode.cpp:712 (append_tcr), 859 (FullPrintConfig& full_config), 873-888 (chute
  floor + is_BBL_Printer gate), 518/814/816 (.value scalar-read precedent)`.
- `src/libslic3r/Print.cpp:283-370 (invalidation disjunction → psWipeTower+psSkirtBrim)`.
- `src/slic3r/GUI/Tab.cpp:2682-2686 (Flush options optgroup), 2829-2838 (TabPrint::toggle_options +
  local is_BBL_printer), 2883-2914 (TabPrint::update → toggle_options), 2108-2152
  (on_presets_changed → dependent tabs load_current_preset), 5695-5704 (load_current_preset →
  update), 5835-5844/6215 (select_preset printer branch), 4151/4362 (Filament-tab lines being
  removed)`.
- `src/slic3r/GUI/GUI_App.cpp:8126-8155 (load_current_presets loops all tabs → load_current_preset)`.
- `src/slic3r/GUI/ConfigManipulation.cpp:870-905 (toggle_print_fff_options; no chute ref)`.
- `src/slic3r/GUI/Plater.cpp:16688-16696 (update_scheduled block)`.
- `grep -rn`/`grep -rln` over `src/ resources/ localization/ tests/` for both keys.
