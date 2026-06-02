# Review 04 — GUI Wiring & Visibility Logic

Feature: new per-filament option `filament_minimal_purge_on_chute` (coFloats, mm, default 0).

Scope of this review: GUI plumbing only — `src/slic3r/GUI/Tab.cpp` and `src/slic3r/GUI/Plater.cpp`. Anything under `.github/` ignored. Backend (`GCode.cpp`, `Print.cpp`, `PrintConfig.cpp`) consulted only to validate visibility/mode assumptions; not reviewed for correctness here.

## What was reviewed

- `Tab.cpp:4151` — `optgroup->append_single_option_line("filament_minimal_purge_on_chute", ...)` appended to the "Wipe tower parameters" optgroup in `TabFilament::build()`, immediately after the `filament_minimal_purge_on_wipe_tower` analog.
- `Tab.cpp:4360-4362` — new `toggle_option("filament_minimal_purge_on_chute", is_BBL_printer)` in `TabFilament::toggle_options()`, under the `if (m_active_page->title() == L("Multimaterial"))` block.
- `Plater.cpp:16692` — opt_key added to the `else if (...) { update_scheduled = true; }` branch in `Plater::on_config_change`, alongside `filament_minimal_purge_on_wipe_tower`.

## Verification performed

- **`is_BBL_printer` scope**: defined at `Tab.cpp:4269` (`bool is_BBL_printer = ... is_bbl_vendor()`) inside `TabFilament::toggle_options()`, before the `L("Multimaterial")` block at 4353. The new call at 4362 is in the same function body and after the declaration. In scope and correctly computed (same source as the analog loop at 4358). OK.
- **`toggle_option` semantics**: `Tab::toggle_option` (`Tab.cpp:1433-1440`) resolves the field and calls `field->toggle(toggle)`. `Field::toggle` (`Field.cpp:298`) is `en && !readonly ? enable() : disable()`. For a `coFloats`/TextCtrl field, `disable()` (`Field.cpp:1035`) calls `window->Disable()` + `SetEditable(false)`. So `toggle_option(..., false)` **greys out / disables** the field — it does **NOT** hide the row. Row hiding is done by `toggle_line` (`Tab.cpp:1442-1446`, sets `line->toggle_visible`), which is not used here. This matches the analog loop at 4355-4358, which also uses `toggle_option` (disable), not `toggle_line` (hide).
- **Inverse logic**: analog loop disables `filament_minimal_purge_on_wipe_tower` for BBL via `toggle_option(el, !is_BBL_printer)`; new line enables the chute option for BBL via `toggle_option(..., is_BBL_printer)`. The inverse is intentional and correct per the option's backend applicability (chute purge only on BBL change-filament G-code path; wipe-tower minimum only on non-BBL wipe-tower setups). The bool means "enabled/editable", and the inverse correctly makes exactly one of the two options editable per printer family. OK.
- **Non-BBL rendering**: option is appended unconditionally in `build()` (4151), so the row always exists. On non-BBL it is present but disabled (greyed). On BBL it is present and editable. No case where it is editable when it should not be, or absent when it should be present. OK.
- **Multi- vs single-extruder BBL**: the toggle keys only on `is_BBL_printer`, with no `nozzle_diameter`/extruder-count gate (unlike `long_retractions_when_ec` at 4368-4372). So the option is editable on both single- and multi-extruder BBL printers. See Note below for whether that breadth is intended.
- **Mode visibility**: `filament_minimal_purge_on_chute` is `comAdvanced` (`PrintConfig.cpp:2733`), identical to the analog `filament_minimal_purge_on_wipe_tower` (`comAdvanced`). Consistent — appears in Advanced mode and above. OK.
- **Preset whitelist**: `filament_minimal_purge_on_chute` added to `s_Preset_filament_options` (`Preset.cpp:1282`). This is the known fork gotcha; it is handled, so the value persists/serializes with the filament preset. OK (outside strict GUI scope but it is what makes the field actually save).
- **Plater on_config_change**: adding the opt_key to the `update_scheduled = true` branch (16692) mirrors the analog at 16691. The branch only sets `update_scheduled` (triggers a deferred 3D scene/preview refresh). Correct, consistent, not redundant, not harmful — changing the value should refresh the wipe/preview just like the wipe-tower minimum does.
- **Missing analog plumbing**: searched `ConfigManipulation.cpp` for both keys — neither the new option nor its analog appears there, so there is no `ConfigManipulation` toggle the new option is missing. No additional GUI plumbing that the analog has and this lacks.

## Findings

### Critical (90-100)
None.

### High (80-89)
None. The option is visible (and correctly editable) for both single- and multi-extruder BBL printers, and visible-but-disabled for non-BBL. No common-printer visibility defect.

### Medium (51-75)
None rising to report threshold.

### Low / Notes (informational, below report threshold)

- **N1 — "Wipe tower parameters" group label for a chute-only option.** `Tab.cpp:4151`. The option is placed in the optgroup titled "Wipe tower parameters", but its tooltip explicitly says it applies to chute-ejecting printers (BBL) "not on wipe-tower-only setups". On BBL the rest of that group's wipe-tower fields are disabled while this one is the only editable entry, which is mildly incongruent with the group heading. Functionally fine (placement next to the analog is the most discoverable spot, and OrcaSlicer keeps these grouped). Confidence this is a real defect: low — leaving as-is is defensible. If desired, no code change is strictly needed; only a cosmetic grouping/label consideration.

- **N2 — BBL toggle is not gated on multi-extruder / multi-material.** `Tab.cpp:4362`. The option is a tool-change purge minimum, yet it is enabled on all BBL printers including single-extruder ones where no tool change occurs. The analog `filament_minimal_purge_on_wipe_tower` is likewise not gated on extruder count, so this is consistent with existing behavior and not a regression. Whether a single-extruder BBL should show an editable tool-change purge field is a product question, not a wiring bug. No action required for parity.

## Verdict

GUI wiring is correct and consistent with the existing `filament_minimal_purge_on_wipe_tower` analog. `is_BBL_printer` is in scope and correctly computed; the inverse toggle is intentional and works as a *disable/enable* (not show/hide — the task's "does false hide the row" premise does not match `toggle_option`'s actual semantics, but the resulting behavior is correct either way). The option renders for all printers (disabled on non-BBL, editable on BBL, single- and multi-extruder alike), mode matches the analog, and the Plater `on_config_change` addition is correct and harmless. No issues at or above the report threshold.
