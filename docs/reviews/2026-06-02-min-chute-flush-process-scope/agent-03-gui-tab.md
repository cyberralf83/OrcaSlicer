# Agent 03 — GUI / Tab wiring review

Scope: Tab.cpp removal from TabFilament, addition to TabPrint "Flush options" optgroup,
toggle_line vs toggle_option, re-fire on printer change, opt_index, process-scope read/write.
Verified against LIVE code on branch `nightly-builds-with-bc`.

## Verdict

APPROVE with minor notes. The GUI/Tab portion of the proposal is **functionally correct** and will
compile, render, and behave as intended (BBL-only row that re-fires on printer change and page
activation). One MEDIUM consistency point: the BBL gate is better placed in the existing
`ConfigManipulation::toggle_print_fff_options` delegate alongside the sibling `flush_into_*` gating,
rather than directly in `TabPrint::toggle_options()`. Two LOW insertion-precision notes. No
CRITICAL/HIGH issues in this area.

## Confirmed correct

- **Removal of Filament-tab line (Tab.cpp:4151)** — standalone `append_single_option_line`. Removing
  it leaves no dangling reference; the surrounding optgroup ("Wipe tower parameters", opened at 4149)
  still has 6 other options (4150, 4152–4156), so it is not orphaned/emptied.
- **Removal of Filament-tab toggle (Tab.cpp:4362)** — VERIFIED standalone. It is a single
  `toggle_option("filament_minimal_purge_on_chute", is_BBL_printer)` and is **NOT** part of the `el`
  range-for loop list at 4355–4358 (that loop ends at 4358 and toggles `!is_BBL_printer` for a
  different set). Removing line 4362 (and its 2-line comment 4360–4361) is clean. No other reference
  to the key remains on the Filament tab.
- **Target optgroup is in TabPrint and is process-scoped** — the "Flush options" optgroup
  (`page->new_optgroup(L("Flush options")...)` at Tab.cpp:2682) is inside `TabPrint::build()`
  (starts 2311). The Print tab's `m_config` is the print/process preset config, so a process-scoped
  option bound here reads/writes the print preset. Confirmed by direct analogy: the sibling options
  `flush_into_infill`/`flush_into_infill_min_layer`/`flush_into_objects`/`flush_into_support`
  (Tab.cpp:2683–2686) are all in `s_Preset_print_options` (Preset.cpp:1124–1127). Claim #3 and the
  process-scope read/write concern: CONFIRMED correct.
- **Insertion point after flush_into_support (~2686) is correct** — `flush_into_support` is the last
  line of the "Flush options" optgroup; line 2687 immediately reassigns `optgroup` to a new
  "Advanced" optgroup. Inserting the new `append_single_option_line` strictly between 2686 and 2687
  lands it in "Flush options" (see LOW note below on precision).
- **Help anchor `multimaterial_settings_flush_options` is real and used by a sibling** — VERIFIED:
  `flush_into_objects` (Tab.cpp:2685) uses the bare anchor `"multimaterial_settings_flush_options"`.
  The proposal's choice of the same bare anchor is valid and consistent.
- **toggle_line hides the whole ROW; toggle_option only greys** — VERIFIED at definitions:
  `Tab::toggle_option` (Tab.cpp:1433) calls `field->toggle(toggle)` (enable/grey). `Tab::toggle_line`
  (Tab.cpp:1442) sets `line->toggle_visible = toggle` (consumed by `Page::update_visibility`, hides
  the row). For a BBL-only option, hiding the entire row on non-BBL printers is the better UX and
  matches how `flush_into_objects` / `flush_into_infill_min_layer` are handled (via `toggle_line` in
  the delegate, ConfigManipulation.cpp:897/899). Switching from the old `toggle_option` to
  `toggle_line` is correct and an improvement.
- **`is_BBL_printer` is computed in TabPrint::toggle_options()** — VERIFIED at Tab.cpp:2834
  (`bool is_BBL_printer = wxGetApp().preset_bundle->is_bbl_vendor();`), recomputed every call.
- **Re-fires on PRINTER change** — full chain VERIFIED: printer select → `m_dependent_tabs` loop
  (Tab.cpp:2141–2148) → Print tab `load_current_preset()` (5695) → `update()` (5704) → at
  `m_update_cnt==0`, `toggle_options()` (Tab.cpp:2914) → `is_BBL_printer` recomputed (2834). So a
  `toggle_line("minimal_chute_flush_length", is_BBL_printer)` in `TabPrint::toggle_options()` WILL
  re-fire when switching non-BBL ↔ BBL. BBL visibility is NOT stale. Claim #4: CONFIRMED.
- **Re-fires on PAGE activation** — `Tab::activate_selected_page` (6403) calls `toggle_options()`
  (6412) then `m_active_page->update_visibility(m_mode, true)` (6413), which applies the
  `toggle_visible` flag set by `toggle_line`. So opening the Multimaterial page shows/hides the row
  correctly. (Same mechanism the existing `flush_into_objects` row relies on — strong consistency
  anchor.)
- **opt_index default (-1) for a scalar option is correct** — `toggle_line`/`get_line` default
  `opt_index = -1` (Tab.hpp:395 / 104). For a single-valued `ConfigOptionFloat`, -1 matches the line
  by key with no array index. Correct.
- **No new-name collision; exactly 8 old-name references** — `minimal_chute_flush_length` appears
  nowhere in src/ or resources/. Exactly 8 `filament_minimal_purge_on_chute` references across the 7
  files the proposal lists (Print.cpp:289, PrintConfig.cpp:2722, GCode.cpp:880, Preset.cpp:1282,
  PrintConfig.hpp:1456, Tab.cpp:4151, Tab.cpp:4362, Plater.cpp:16692). No profile JSON, no .po/.mo,
  no 3MF. Claim #5: CONFIRMED for the GUI/Tab area.

## Findings

- **[MEDIUM] src/slic3r/GUI/Tab.cpp:~2835 (toggle_options) vs ConfigManipulation.cpp:881 (delegate)
  — gate placement: prefer the delegate for consistency.** The proposal adds
  `toggle_line("minimal_chute_flush_length", is_BBL_printer)` directly in `TabPrint::toggle_options()`.
  This works (it's where `is_BBL_printer` is computed, 2834). HOWEVER, every sibling "Flush options"
  visibility/enable rule lives in `ConfigManipulation::toggle_print_fff_options`
  (ConfigManipulation.cpp:881–903): `flush_into_*` are gated there at 881–882 (`toggle_field`,
  `have_prime_tower`), `flush_into_objects` at 897 (`toggle_line`, `!is_global_config`),
  `flush_into_infill_min_layer` at 899. That delegate is invoked from `TabPrint::toggle_options()` at
  line 2838 — AFTER `set_is_BBL_Printer(is_BBL_printer)` at 2835 — and the delegate already
  references the `is_BBL_Printer` member directly (e.g. lines 443, 842, 845, 960). Placing
  `toggle_line("minimal_chute_flush_length", is_BBL_Printer)` next to the `flush_into_*` block in the
  delegate would co-locate it with its visual neighbors and be where a future maintainer looks. Fix:
  add the gate in `toggle_print_fff_options` near line 882/897 using the in-scope `is_BBL_Printer`
  member, instead of in `TabPrint::toggle_options()`. (Functional outcome identical either way; this
  is consistency/maintainability, not a bug.)

- **[MEDIUM] Interaction with the sibling `have_prime_tower` gate — not addressed by the proposal.**
  The neighboring `flush_into_*` fields are gated on `have_prime_tower` (ConfigManipulation.cpp:882):
  when no prime/wipe tower is enabled, those rows grey out. The min-chute-flush feature only takes
  effect on the BBL wipe-tower path (per the GCode.cpp:880 gating in the proposal), so the option is
  meaningless without a tower. The proposal gates the new row ONLY on `is_BBL_printer`, so on a BBL
  printer with the tower disabled the row stays fully visible/editable while its sibling flush
  options grey out — a mild UX inconsistency and potential user confusion (setting a value that does
  nothing). Consider gating on `is_BBL_printer && have_prime_tower` (or at least documenting the
  intent). Severity MEDIUM because it is edge-case UX, not a functional break; it is also outside the
  strict letter of the proposal but worth flagging.

- **[LOW] src/slic3r/GUI/Tab.cpp:2686 — insertion must be strictly before line 2687.** Line 2687
  reassigns `optgroup = page->new_optgroup(L("Advanced"), ...)`. The new
  `append_single_option_line("minimal_chute_flush_length", ...)` must be inserted between the current
  2686 and 2687; if placed at/after 2687 it would land in the "Advanced" optgroup instead of "Flush
  options". The proposal's "after flush_into_support (~2686)" is right but the line number is
  precision-sensitive.

- **[LOW] Help-anchor scheme differs from the old Filament-tab anchor (cosmetic).** The old line used
  the Filament-tab anchor `material_multimaterial#...` (Tab.cpp:4151); the new line correctly switches
  to the Print-tab anchor family `multimaterial_settings_flush_options`. This is correct (matches the
  sibling at 2685), just noting the deliberate anchor change so it isn't mistaken for an inconsistency.

## Cross-area observation (outside GUI focus, for the orchestrator)

- The proposal's tooltip rewording (PROPOSAL §2) says the value "is a filament length whose resulting
  purge volume scales with each filament's diameter," and GCode.cpp uses the **new** filament's
  diameter for the mm→mm³ conversion. With a single global mm value, the resulting mm³ floor varies
  per toolchange depending on the incoming filament's diameter. That is internally consistent with
  the design, but the GUI label/sidetext stays "mm" — fine. No GUI bug; flagging only because the
  unit semantics (global mm → per-toolchange mm³) is the kind of thing a tooltip must make explicit,
  which the proposal does intend to do.

## Sources

- src/slic3r/GUI/Tab.cpp:1433 (toggle_option), 1442 (toggle_line), 2311 (TabPrint::build),
  2682–2687 (Flush options optgroup), 2829–2838 (TabPrint::toggle_options + set_is_BBL_Printer +
  delegate call), 2912–2914 (update→toggle_options), 4148–4151 (Filament Wipe-tower optgroup + old
  line), 4355–4373 (Filament toggle_options, el-loop + old toggle), 5695–5730 (load_current_preset),
  2141–2152 (m_dependent_tabs loop), 6403–6414 (activate_selected_page).
- src/slic3r/GUI/Tab.hpp:104, 394–395 (toggle_option/toggle_line/get_line default opt_index = -1).
- src/slic3r/GUI/ConfigManipulation.cpp:592 (toggle_print_fff_options), 881–903 (flush_into_* gating),
  443/842/845/960 (is_BBL_Printer member usage in scope).
- src/slic3r/GUI/ConfigManipulation.hpp:27/83/84 (is_BBL_Printer member + get/set).
- src/libslic3r/Preset.cpp:970 (s_Preset_print_options), 1124–1127 (flush_into_* whitelisted),
  1279/1282 (s_Preset_filament_options + old key).
- grep across src/ + resources/ + localization/: 0 hits for `minimal_chute_flush_length`; exactly 8
  hits for `filament_minimal_purge_on_chute` in the 7 listed files.
