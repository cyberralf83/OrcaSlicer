# Agent 08 — Holistic consistency review

Scope: does the whole proposal (rename filament→global, type Floats→Float, move Filament tab →
Print tab, reword tooltip/comment, units, help anchor, defaults, discoverability) hang together?
Verified against live code on branch `nightly-builds-with-bc`.

## Verdict

REQUEST CHANGES. The plan is internally coherent on the big strokes (key rename, type change,
whitelist move, default 0 = byte-identical, anchor reuse), and all 8 references it lists are exactly
the 8 that exist (no stray JSON/po/3MF — claim #5 holds). But it ships two real defects: (1) the
Tab.cpp toggle instruction references a `is_BBL_printer` local that does NOT exist in scope at the
named insertion point in `TabPrint::toggle_options()` — it must use
`m_config_manipulation.get_is_BBL_Printer()`; and (2) the proposed tooltip/comment reword is
semantically incomplete — calling the value purely "global" hides that the resulting purge VOLUME
(and even the "~40 mm" floor figure) is still per-filament because of the diameter conversion. Both
are fixable with wording the implementer must get right; neither is fatal to compilation.

## Confirmed coherent

- All 8 live references match the proposal's "Current state" inventory exactly:
  PrintConfig.hpp:1456, PrintConfig.cpp:2722 (+default 2736), Preset.cpp:1282
  (s_Preset_filament_options), GCode.cpp:880, Print.cpp:289, Plater.cpp:16692, Tab.cpp:4151,
  Tab.cpp:4362. Claim #5 (rename touches exactly these and nothing else) is TRUE — grep across
  src/resources/localization finds no profile JSON, no .po, no 3MF, no other .cpp/.hpp hit.
- No name collision: grep for `minimal_chute`/`chute_flush` (excluding the old key) returns nothing.
  `minimal_chute_flush_length` is free.
- Help anchor `multimaterial_settings_flush_options` is exactly what the four sibling `flush_into_*`
  lines already use in the same "Flush options" optgroup (Tab.cpp:2683-2686). Reusing it is coherent
  and the right call. The current Filament-tab anchor was
  `material_multimaterial#multimaterial-wipe-tower-parameters`; dropping it on the move is correct.
- Whitelist reclassification (claim #3): persistence is governed solely by `s_Preset_*_options`.
  Removing from `s_Preset_filament_options` (1282) and adding near flush_into_* in
  `s_Preset_print_options` (1124-1127) is the complete and correct mechanism. Keeping the `.hpp`
  declaration inside the `GCodeConfig` macro block is fine — `GCodeConfig` aggregates into
  `FullPrintConfig`, so `full_config.minimal_chute_flush_length` resolves regardless of which struct
  declares it. Non-goal "don't relocate the .hpp declaration" does not contradict "moves to process
  preset"; they are orthogonal (whitelist vs struct).
- Scalar read idiom (claim #2): `full_config.minimal_chute_flush_length.value` is the correct pattern
  for a `ConfigOptionFloat`; confirmed against existing reads like
  `gcodegen.config().standby_temperature_delta.value` (GCode.cpp:276) and `printer_model.value`
  (GCode.cpp:99). The downstream `min_chute_purge = min_chute_length * filament_area` then
  `purge_length = purge_volume / filament_area` is unchanged, so claim #7 (no div-by-zero / NaN; mm→mm³
  via new filament diameter intended) holds — filament_area is always > 0 (diameter > 0) and gating
  by `min_chute_purge > EPSILON` already guards the 0-input path.
- Default 0 byte-identical (claim #1): with value 0, `min_chute_purge = 0`, `apply_chute_min` is
  false, so `purge_volume`/`purge_length`/`flush_count`/`flush_unit`/`flush_length_N` all take the
  exact upstream branch. TRUE. Sensible default.
- Constants the proposal cites are correct: `g_min_purge_volume = 100.f` (GCode.cpp:92), the floor;
  `g_purge_volume_one_time = 135.f` (GCode.cpp:93, drives flush_count); `g_max_flush_count = 4`.
- Print.cpp:289 sits in the `psWipeTower + psSkirtBrim` emplace block (lines 369-370) — claim #6
  re-slice/re-export trigger is correctly placed; renaming the literal in place keeps it there.

## Findings

- [HIGH] Tab.cpp ~2835 (proposal change #7 / claim #4) — The proposal says to add
  `toggle_line("minimal_chute_flush_length", is_BBL_printer)` "where `is_BBL_printer = ... is_bbl_vendor()`
  is already computed (≈2835)." This is WRONG. In `TabPrint::toggle_options()` (live lines 2829-2881)
  the local `bool is_BBL_printer` exists ONLY inside the `if (m_preset_bundle) { ... }` block
  (lines 2833-2836) and is out of scope everywhere the new option line would go. The rest of the
  function reads BBL state via `m_config_manipulation.get_is_BBL_Printer()` (used at line 2867 for
  `wipe_tower_wall_type`). Also note `TabPrint::toggle_options()` currently calls `toggle_line`
  ZERO times — unlike `TabFilament`/`TabPrinter`. Fix: insert
  `toggle_line("minimal_chute_flush_length", m_config_manipulation.get_is_BBL_Printer());`
  AFTER line 2838 (after `set_is_BBL_Printer` has run via the `m_preset_bundle` block, so the cached
  flag is current). Re-fire on printer change is fine because `update()` calls `toggle_options()`
  (line 2914) and `is_bbl_vendor()` is re-read each call. As written, the proposal's snippet would not
  compile (undeclared `is_BBL_printer`).

- [MEDIUM] PrintConfig.cpp:2724-2732 tooltip reword (change #2) + GCode.cpp:873 comment (change #4)
  — calling the value simply "global" is semantically incomplete and mildly misleading. The stored
  value is a global filament LENGTH (mm), but the enforced purge it produces is a per-filament VOLUME:
  `min_chute_purge = length * filament_area(new_filament_id)`. Two filaments of different diameters
  fed the same mm setting get different mm³ floors. Worse, the "~40 mm (100 mm³)" equivalence the
  tooltip keeps is itself diameter-specific (100 mm³ ÷ area = ~41.6 mm only at 1.75 mm; ~12.5 mm at a
  3.0 mm-area path). The proposal already half-acknowledges this ("a single global mm value yields a
  per-filament mm³ floor", non-goal note) but the instruction to the tooltip/comment author does not
  enforce saying it. The GCode.cpp comment should read "global length, per-filament volume", NOT plain
  "global" (matches the orchestrator's hint). Proposed exact text below.

- [LOW] Units confusability label/sidetext (focus item) — On the Print tab "Flush options" group the
  new row will read "Minimal chute flush length … mm" sitting beside `flush_into_*` (booleans/ints),
  while its conceptual sibling "Minimal purge on wipe tower" (mm³, default 15) now lives on a
  different tab (Filament). Because the two are no longer adjacent, the old tooltip clause
  "(Note: the adjacent 'Minimal purge on wipe tower' is a volume in mm³, not a length.)" becomes
  factually wrong — it is no longer adjacent. The proposal already says to drop that phrasing (good),
  but the report should make explicit that KEEPING any "adjacent" reference is now a bug. Retain a
  units clarification ("this is a length in mm, not a volume in mm³") without the word "adjacent".

- [LOW] Naming coherence (focus item) — label "Minimal chute flush length" / key
  `minimal_chute_flush_length` / anchor `multimaterial_settings_flush_options` are mutually coherent:
  label and key share the chute-flush-length noun phrase; the anchor is the shared Flush-options page
  (chute flush is a flush option). No change needed. Only minor: the label still says "chute" while
  the group is generic "Flush options" — acceptable, the tooltip explains the chute mechanism.

- [LOW] Discoverability / non-BBL scenario (focus item) — A user on a non-BBL printer who enables
  `flush_into_infill` will NOT see this row (hidden by `toggle_line(..., is_BBL)`), which is intended
  (the option only does anything via the BBL chute change_filament_gcode). The proposal does not state
  this user-facing consequence anywhere, nor that the option silently has no effect on non-chute
  printers even if forced. Document it: in the tooltip (already says "Only effective on printers that
  eject purge through a chute … e.g. Bambu Lab") and as a one-line note in the changelog/PR body.
  Acceptable as intended behavior, but call it out so a non-BBL user filing "I set flush_into_infill
  but my poop is tiny" gets a documented answer.

- [LOW] Missing user-facing deliverables — The proposal lists no changelog entry, no wiki/docs note
  for the moved/renamed setting, and no range/max guidance (only `min=0`, no `max`, no suggested
  upper bound). The Tab anchor points at a wiki section
  (`multimaterial_settings_flush_options`) that does not yet describe this fork-only option; since the
  upstream wiki won't have it, the in-app tooltip is the only documentation — so the tooltip MUST be
  self-sufficient (reinforces the MEDIUM finding). Recommend adding a sensible soft upper hint in the
  tooltip (e.g. "typical values 0-200 mm").

- [LOW] Internal-section consistency check — The proposal's table labels changes #1, #2, #3, #4, #7
  as "structural" and #5, #6 as "rename", while the prose calls change #1 a "type-only change" that
  "stays in the GCodeConfig macro block." No contradiction: "structural" here means
  type/whitelist/placement change vs pure string rename; "stays in GCodeConfig" refers to .hpp struct
  membership, which is independent of preset scope (governed by whitelist). The "stays in GCodeConfig"
  vs "moves to process preset" wording could confuse a reader — recommend one clarifying sentence, but
  it is not an actual contradiction.

## Proposed tooltip text

For PrintConfig.cpp `minimal_chute_flush_length` (drops "adjacent", states global-length /
per-filament-volume, keeps floor + disable + BBL notes, no longer references the wipe-tower sibling
by adjacency):

```
def->tooltip = L("Global minimum length of filament (in mm) to purge out of the nozzle and into "
                 "the waste chute on each real tool change. This is a length, not a volume — Orca "
                 "converts it to a purge volume per filament using that filament's diameter, so the "
                 "same setting yields a slightly different volume for thicker or thinner filaments. "
                 "When most of the tool-change flush is redirected into the object's infill, the "
                 "leftover chute purge can become too small to fall free and may stick to the nozzle; "
                 "raising this guarantees enough filament to drop cleanly. A built-in floor of about "
                 "100 mm³ (roughly 40 mm of 1.75 mm filament) already applies, so values below that "
                 "have little effect. Set to 0 to disable (default). Only effective on printers that "
                 "eject purge through a chute via the change filament G-code (e.g. Bambu Lab); on "
                 "other printers this setting is hidden and has no effect.");
```

For the GCode.cpp:873-878 comment, change "per-filament minimum chute flush" → describe it as a
GLOBAL length with PER-FILAMENT volume:

```
// ORCA: Enforce a global minimum chute flush ("poop"). The option is a single global filament
// length (mm); we convert it to a purge volume (mm³) using the NEW filament's diameter, so the
// resulting floor is per-filament even though the setting is global. When most of the tool-change
// flush is diverted into object infill, tcr.purge_volume can fall to ~0, leaving a poop too small
// to drop free of the nozzle (it sticks). The floor applies only on real colour changes on BBL
// chute printers (matching the BBL-only UI); every other case keeps the original behaviour so we
// never emit spurious purge.
```

## Sources

- /Volumes/MacMicroSD/Github/OrcaSlicer-nighty/docs/reviews/2026-06-02-min-chute-flush-process-scope/PROPOSAL.md
- src/libslic3r/PrintConfig.cpp:2711-2736 (sibling mm³ wipe-tower option + current chute tooltip)
- src/libslic3r/PrintConfig.hpp:1450-1461 (GCodeConfig macro block)
- src/libslic3r/Preset.cpp:1118-1131 (s_Preset_print_options flush_into_* block), 1279-1285 (s_Preset_filament_options)
- src/libslic3r/GCode.cpp:92-94 (g_* constants), 873-888 (append_tcr chute floor), 962-976 (flush_length/flush_count)
- src/libslic3r/Print.cpp:283-370 (invalidation; psWipeTower+psSkirtBrim block)
- src/slic3r/GUI/Plater.cpp:16685-16697 (update_scheduled list)
- src/slic3r/GUI/Tab.cpp:2676-2693 (Print-tab Flush options optgroup), 2829-2881 (TabPrint::toggle_options — no toggle_line, is_BBL_printer scoped to lines 2833-2836), 4151/4353-4373 (current Filament-tab placement + toggle_option), 1442 (Tab::toggle_line signature)
- src/slic3r/GUI/ConfigManipulation.hpp:27,83-84 (get_is_BBL_Printer / set_is_BBL_Printer)
- grep across src/resources/localization: only the 8 expected references to the old key; no JSON/po/3MF
