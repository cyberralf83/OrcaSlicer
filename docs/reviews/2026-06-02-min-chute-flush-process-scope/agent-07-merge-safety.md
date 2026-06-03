# Agent 07 — Upstream Merge-Safety & Conventions Review

## Verdict

**APPROVE WITH CHANGES.** The reclassification (filament→process, array→scalar) is sound and the
proposal correctly identifies all reference sites. But it has **one CRITICAL compile bug** in the
described Tab.cpp placement (`is_BBL_printer` is out of scope where item 7 says to add the toggle),
and **two MEDIUM convention deviations** versus upstream process-option style (missing `def->category`;
the `this->add()` def and `.hpp` declaration stay physically in the filament/wipe-tower cluster while
the option is now a process key). None of the 7 edits sit in genuinely byte-stable upstream regions —
all four of Preset.cpp / Print.cpp / Tab.cpp-optgroup / PrintConfig.cpp def-cluster are upstream-tracked
lists/blocks that the fork must re-merge every nightly, so each insertion is a recurring conflict point.
The merge-risk is inherent to the feature's nature (it must touch shared lists) and is **acceptable** if
the edits are kept minimal and well-anchored; the fixes below reduce it.

## Merge-risk ranking (highest → lowest)

| Rank | Edit | File / region | Why it conflicts | Severity |
|------|------|---------------|------------------|----------|
| 1 | #3 ADD to `s_Preset_print_options` | Preset.cpp ≈1124-1127 | Single flat `std::vector<std::string>` initializer that upstream edits frequently (every new process option appends here). Fork already diverges here (`flush_into_infill_min_layer` at local:1125 is **not** in upstream — upstream has only infill/objects/support at 1121-1123). Inserting a fork-only line compounds an existing local divergence → re-conflicts on nearly every nightly that touches this list. | MEDIUM |
| 2 | #7 ADD optgroup line | Tab.cpp Flush-options group (local 2682-2686) | This optgroup already diverges from upstream (upstream 2679-2682 lists infill/objects/support only; local adds `flush_into_infill_min_layer` at 2684). Adding a 5th `append_single_option_line` inside a block upstream actively edits → recurring conflict. | MEDIUM |
| 3 | #5 rename in invalidation list | Print.cpp:289 | Inside the huge `else if (opt_key == ...)` chain ending `psWipeTower/psSkirtBrim` (local 368-370). Upstream edits this chain constantly. The line is fork-only (upstream:288 has only `filament_minimal_purge_on_wipe_tower`); a pure rename keeps the conflict footprint to one line. | MEDIUM |
| 4 | #2 def block | PrintConfig.cpp:2722 (filament/wipe-tower cluster) | Sits between upstream-adjacent `filament_minimal_purge_on_wipe_tower` (2711) and `filament_cooling_before_tower`. This cluster is relatively stable, but the block is fork-only so any upstream reflow of neighbours conflicts. **Classification smell:** a *process* key defined in the middle of the *filament* def cluster (see Findings). | MEDIUM |
| 5 | #1 .hpp decl | PrintConfig.hpp:1456, `GCodeConfig` macro block | Type-only change to a fork-only line wedged between two upstream `filament_*` lines (1455/1457). Low churn but classification smell (process key declared in `GCodeConfig`'s filament group). | LOW |
| 6 | #4 GCode.cpp body | GCode.cpp:880, inside `append_tcr` BBL block | Already a fork-only block; `.value` vs `.get_at` change is internal. Region is fork-authored so merge risk is low (conflicts only if upstream rewrites `append_tcr`). | LOW |
| 7 | #6 rename | Plater.cpp:16692 update-scheduled list | Fork-touched list; pure rename, one line. | LOW |

**Net divergence change:** the proposal *removes* one fork-only line from `s_Preset_filament_options`
(reducing filament-list divergence — upstream:1272 confirms the chute key is fork-only there) and
*adds* one fork-only line to `s_Preset_print_options`. Divergence count is unchanged; it just moves
between two upstream-tracked lists. No net merge-risk increase from the Preset.cpp move itself.

## Findings

- **[CRITICAL] Tab.cpp ≈2836 — `is_BBL_printer` is out of scope where item 7 places the toggle; won't
  compile as described.** In `TabPrint::toggle_options()` (local 2829-2836, identical to upstream
  2820-2827) `is_BBL_printer` is declared **inside** `if (m_preset_bundle) { ... }` and dies at the
  closing brace (2836). The proposal item 7 says add `toggle_line("minimal_chute_flush_length",
  is_BBL_printer)` "≈2835, where `is_BBL_printer ... is already computed`." If inserted after line 2836
  (the natural place, alongside the other `toggle_line` calls in this method), `is_BBL_printer` is
  undefined → build break (feature dead). Note: unlike `TabFilament::toggle_options()` (local 4261)
  and `TabPrinter` (local 5427) which hoist `is_BBL_printer` to method scope, `TabPrint` keeps it
  block-local. **Fix:** either place the `toggle_line` *inside* the `if (m_preset_bundle)` block at
  2835, or hoist the declaration to method scope (`bool is_BBL_printer = false; if (m_preset_bundle) {
  is_BBL_printer = ...; ... }`) and call `toggle_line` afterward. Recommend the in-block placement to
  keep the upstream diff to one added line. Document the exact insertion point in the proposal.

- **[MEDIUM] PrintConfig.cpp item 2 — new def omits `def->category = L("Flush options")`, breaking the
  convention of its new optgroup neighbours.** Every sibling in the Print-Settings "Flush options"
  optgroup sets a category: `flush_into_infill` (local 7015 / upstream 6912), `flush_into_support`
  (7039), `flush_into_objects` (7047) all do `def->category = L("Flush options")`. The proposal's def
  (item 2) lists label/tooltip/sidetext/min/mode/default but no `category`. Without it the option is
  uncategorized in the per-object setting-override picker / search grouping. (The *optgroup placement*
  itself is driven by Tab.cpp, so the UI line still appears — this is not the CRITICAL item — but it is
  a real convention break for a process option.) **Fix:** add `def->category = L("Flush options");` to
  item 2, matching the three `flush_into_*` defs.

- **[MEDIUM] PrintConfig.cpp:2722 + PrintConfig.hpp:1456 — process key left physically inside the
  filament / `GCodeConfig` clusters; a classification inconsistency a maintainer/merger will trip on.**
  The proposal (decisions: "NOT relocating the .hpp declaration between structs") is *functionally*
  correct — preset membership is governed solely by the `s_Preset_*_options` whitelists, and
  `GCodeConfig` aggregates into `FullPrintConfig`, so the option will serialize and read fine as a
  process key regardless of where it's declared. But the result is a process-scoped option whose
  `.hpp` line sits between `filament_minimal_purge_on_wipe_tower` and `filament_cooling_before_tower`
  (both filament keys, both `ConfigOptionFloats`) and whose `.cpp` def sits in the same filament/
  wipe-tower def run. Process flush siblings (`flush_into_*`, `prime_volume`,
  `wipe_tower_max_purge_speed`) all live ~6800-7050 in PrintConfig.cpp, far from 2722. This is a
  maintainability/grep-ability smell, not a bug. **Decision point:** either (a) accept it explicitly
  (minimal diff, the proposal's stated choice) and add a one-line comment at both sites noting "process
  option, declared here only to minimize diff", or (b) move both the `.hpp` line and the `.cpp` def
  next to the `flush_into_*` cluster so the file reads coherently. (a) keeps the diff smaller; (b) is
  more honest to upstream conventions. Flagging so the choice is conscious, not accidental.

- **[MEDIUM] Naming: `minimal_chute_flush_length` is internally consistent but mixes two upstream word
  orders.** Process flush/tower siblings use no `filament_` prefix (good — dropping it is correct;
  upstream reserves `filament_*` for filament-preset keys, confirmed by `filament_minimal_purge_on_*`
  being filament-list-only). However the sibling it was modeled on is `minimal_purge_on_wipe_tower`
  (noun-phrase "purge"), while the new name uses "chute_flush_length". The fork's own UI calls the
  feature "chute flush", and `flush_into_*` establishes "flush" as the process-side vocabulary, so
  `*_chute_flush_*` actually aligns better with the process namespace than carrying over "purge". This
  is acceptable; just confirm the team prefers "flush" over "purge" for the process key (the tooltip
  reword in item 2 should use "flush" consistently to match). LOW-end MEDIUM.

- **[LOW] Help anchor `multimaterial_settings_flush_options` (item 7) — verify it resolves.** The three
  existing flush lines use `"multimaterial_settings_flush_options"` (with/without `#...` fragment), so
  the base anchor is already in use by upstream and is safe to reuse. No new anchor is introduced. No
  action needed beyond keeping the exact string used by `flush_into_objects` (local 2685, no fragment).

- **[LOW] Item 5 / item 7 wording vs reality — both are accurate; recording for completeness.** Print.cpp:289
  is genuinely inside the single `else if` chain that emplaces `psWipeTower + psSkirtBrim` (verified:
  chain spans local 283-368, emplace at 369-370), so the in-place rename is correct and does trigger
  re-slice/re-export on edit (claim 6 holds). The Tab.cpp Flush-options optgroup is genuinely in
  `TabPrint::build()` on the Print-Settings "Multimaterial" page (verified: `add_options_page(L(
  "Multimaterial")` at local 2646, optgroup at 2682), so item 7's placement claim is correct — the
  only defect is the scope bug above.

- **[LOW] Confirm no second `is_bbl_vendor`/`is_BBL_Printer` mismatch.** Item 7 uses `is_BBL_printer`
  (preset-bundle vendor check) for the *UI* gate while GCode.cpp:883 gates emission on
  `gcodegen.is_BBL_Printer()` (runtime printer check). These are two different predicates by design
  (UI visibility vs G-code emission), consistent with how the current filament-scoped code already
  works, so no inconsistency — but worth a one-line code comment so a future maintainer doesn't
  "unify" them.

## Cross-cutting / out-of-focus flags

- The proposal's claim 1 (byte-identical G-code at default 0) is plausible from the GCode.cpp:884-887
  expression: at `minimal_chute_flush_length == 0`, `min_chute_purge == 0`, so `apply_chute_min` is
  false and the ternary collapses to upstream's `std::max(tcr.purge_volume, g_min_purge_volume)` /
  `0.f`. This matches the current filament-scoped behaviour. (Validating actual byte-identity is
  another agent's focus; from a merge-safety view the default-off path is unchanged.)
- Scalar read `full_config.minimal_chute_flush_length.value` (item 4, claim 2) is the correct accessor
  for `ConfigOptionFloat`; `.get_at(...)` would not compile on a scalar, so dropping it is required by
  the type change — good.

## Sources

- Local files (this fork, branch `nightly-builds-with-bc`):
  - `src/libslic3r/PrintConfig.cpp` (chute def 2722-2736; sibling `filament_minimal_purge_on_wipe_tower`
    2711-2720; `flush_into_infill` 7014-7021 incl. `category`; `flush_into_support` 7038; `flush_into_objects`
    7046; `prime_volume` 6853; `wipe_tower_max_purge_speed` 6913)
  - `src/libslic3r/PrintConfig.hpp:1455-1456` (`GCodeConfig` macro block)
  - `src/libslic3r/Preset.cpp` (`s_Preset_print_options` flush block 1124-1127; `s_Preset_filament_options`
    chute entry 1282)
  - `src/libslic3r/Print.cpp:283-370` (invalidation `else if` chain; chute at 289)
  - `src/libslic3r/GCode.cpp:873-888` (`append_tcr` chute block)
  - `src/slic3r/GUI/Tab.cpp` (`TabPrint::build` Multimaterial page 2646 / Flush-options optgroup 2682-2686;
    `TabPrint::toggle_options` 2829-2836 with block-scoped `is_BBL_printer` at 2834; `TabFilament` chute
    line 4151 + toggle 4362)
- Upstream `SoftFever/OrcaSlicer@main` (fetched via raw.githubusercontent.com):
  - `src/libslic3r/PrintConfig.cpp` — `filament_minimal_purge_on_wipe_tower` 2711 (no `category`); NO
    `filament_minimal_purge_on_chute`; NO `flush_into_infill_min_layer`; `flush_into_*` 6911-6928 all set
    `def->category = L("Flush options")`.
  - `src/libslic3r/Preset.cpp` — `s_Preset_print_options` flush block 1121-1123 (infill/objects/support
    only, NO `flush_into_infill_min_layer`); `s_Preset_filament_options` 1272 has
    `filament_minimal_purge_on_wipe_tower` but NOT the chute key.
  - `src/libslic3r/Print.cpp` — invalidation chain 288/341/342/355, emplace `psWipeTower` 368.
  - `src/slic3r/GUI/Tab.cpp` — Flush-options optgroup 2679-2682 (3 lines); `TabPrint::toggle_options`
    2820-2827 with block-scoped `is_BBL_printer` at 2825 (confirms the scope bug is upstream-structural,
    not fork-introduced).
