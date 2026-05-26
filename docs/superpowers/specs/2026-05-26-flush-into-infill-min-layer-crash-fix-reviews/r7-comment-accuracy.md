# R7 — Comment Accuracy & Value Review

Scope: 4-line `// NOTE:` block added at `src/slic3r/GUI/GUI_Factories.cpp:68-71`
guarding the order of entries in `FREQ_SETTINGS_BUNDLE_FFF["Flush options"]`.
Also cross-checks comments introduced by commit `17ec45d3fd` (the broader
`flush_into_infill_min_layer` feature) for accuracy and rot risk.

---

## Comment Under Review

```
// NOTE: the first three entries (flush_into_infill, flush_into_objects, flush_into_support)
// are addressed by positional index [0], [1], [2] in append_menu_items_flush_options() below
// — all three must be ConfigOptionBool, in that order. Append non-bool extras (e.g.
// flush_into_infill_min_layer) AFTER index [2] only.
```

---

## Critical (C) — none

No factual inaccuracies. Verified:

- **Bundle order at line 72**: `flush_into_infill`, `flush_into_objects`,
  `flush_into_support`, `flush_into_infill_min_layer`. Indices 0/1/2 match the
  comment.
- **Consumer at `append_menu_items_flush_options()` (line 1089)** uses
  `FREQ_SETTINGS_BUNDLE_FFF["Flush options"][0..2]` and:
    - calls `option->getBool()` (lines 1122-1126, 1130-1134, 1139-1143,
      1147-1151, 1156-1160, 1164-1168);
    - assigns `new ConfigOptionBool(!option->getBool())` (lines 1126, 1143,
      1160).
  So the "must be ConfigOptionBool" claim is load-bearing — swapping in a
  non-bool option at indices 0..2 would mis-construct a `ConfigOptionBool`
  from an int and silently corrupt the user's setting (the original crash
  vector).
- **Type definitions in `PrintConfig.cpp` lines 6899/6924/6932** confirm
  `flush_into_infill`, `flush_into_support`, `flush_into_objects` are all
  `coBool`. `flush_into_infill_min_layer` at line 6908 is `coInt` —
  precisely matching the comment's example of a "non-bool extra".

---

## High (H)

### H1. No back-reference at the consumer site (`append_menu_items_flush_options`, ~line 1089)

A contributor refactoring the menu callbacks will not see this NOTE. The
positional `[0]`/`[1]`/`[2]` accesses at lines 1122–1168 are bare integer
literals; nothing in the function body explains why those indices are safe or
that they assume `ConfigOptionBool`. A future change like "let me make the
flush-into-infill toggle reflect a tri-state" by widening the type at
`PrintConfig.cpp:6899` will pass code review on the type-def side, then crash
at runtime on `set_key_value(... new ConfigOptionBool(!option->getBool()))`.

**Suggest**: add a 1-line back-reference at the top of
`append_menu_items_flush_options()`, e.g.:
```
// Indices [0], [1], [2] are pinned to ConfigOptionBool entries of
// FREQ_SETTINGS_BUNDLE_FFF["Flush options"] (defined in this file ~line 56).
// See the NOTE above that bundle before changing this order.
```
This bi-directional pinning is the standard fix for "spooky positional coupling
between a literal table and a hand-coded indexer" — neither side is
self-sufficient without the other.

### H2. Comment does not address the analogous SLA bundle

`FREQ_SETTINGS_BUNDLE_SLA` at line 76 is currently empty, but it is iterated
identically at lines 736–742 and selected by `printer_technology()` at line
453. The comment only protects the FFF bundle. If/when SLA flush options are
added with positional consumers, the same hazard repeats. Low-likelihood but
worth a parenthetical: "FFF only — SLA bundle has no positional consumers
today." Not a blocker.

---

## Medium (M)

### M1. Comment is partly WHAT-not-WHY

CLAUDE.md prefers comments explaining "why". This NOTE is roughly 60% WHAT
("indices 0/1/2 are addressed positionally") and 40% WHY ("all three must be
ConfigOptionBool ... AFTER index [2] only"). The WHY is the load-bearing
content — the rest restates the data structure. Tighter rewrite (preserves
all guard rails):

```
// Indices [0]/[1]/[2] of this list are consumed positionally by
// append_menu_items_flush_options() below as ConfigOptionBool. Adding a
// non-bool option (e.g. the coInt flush_into_infill_min_layer) into any of
// the first three slots would crash on getBool()/set_key_value. Append
// non-bool extras AFTER index [2].
```

Optional. Current text is acceptable; the gain is marginal.

### M2. "Crash" is implicit, not stated

The comment says non-bools must go after index [2], but never explicitly says
**what happens** if you violate it. A contributor weighing a refactor
benefits from knowing the failure mode (crash on a `ConfigOptionInt::getBool()`
cast / silent value corruption from `new ConfigOptionBool(!int_value)`). One
clause would suffice: "...else `getBool()` will mis-cast and `set_key_value`
will overwrite the option with the wrong concrete type."

### M3. Em-dash + non-ASCII

The comment uses a Unicode em-dash (U+2014). Skimming OrcaSlicer's existing
`// NOTE:` blocks (`ObjectDataViewModel.cpp:1009`, `IconManager.cpp:265`,
`Tab.cpp:2865`, `PartPlate.cpp:1889`) shows plain ASCII hyphens and no em-dash
usage. Not a defect, but the new comment stands out stylistically. A two-char
swap (em-dash → " — " or " - ") aligns better.

### M4. ToolOrdering.cpp comment (line 1572) — accurate but verbose

The 4-line block at `src/libslic3r/GCode/ToolOrdering.cpp:1572-1575`
("flush_into_infill_min_layer gate: ...") is factually correct and matches
the commit message verbatim. It explains placement well (the WHY of "lives
inside is_overriddable"). Two minor concerns:

- It repeats the commit message rather than condensing — risk of drift if the
  rationale evolves but the comment is not updated.
- The phrase "infill not in lt.extruders, never rescued" is jargon that
  presumes familiarity with the spec/wave-1 review thread. Future readers
  without that context will not understand "rescued". Suggest either dropping
  the parenthetical or linking the spec doc.

---

## Low (L)

### L1. `flush_into_objects` listed second in the comment, but the wider PrintConfig.hpp block orders it FIRST

`PrintConfig.hpp:1003-1006` orders the four options as:
`flush_into_objects`, `flush_into_infill`, `flush_into_infill_min_layer`,
`flush_into_support`. The GUI bundle deliberately re-orders them. The comment
implicitly justifies the GUI ordering (because the consumer's indices
correspond to UI labels "Flush into objects' infill" / "Flush into this
object" / "Flush into objects' support"). Not wrong, just worth being aware
that two orderings coexist intentionally. No action required.

### L2. Comment will become stale if `append_menu_items_flush_options()` is renamed

The comment names the consumer function explicitly. If someone renames it
(e.g. during a Plater refactor), the comment becomes a wild reference. Low
likelihood; the function name is descriptive enough that grep will find it.

### L3. Comment does not mention `set_key_value` assumption

The crash path is two-fold: (a) `getBool()` on a non-bool; (b) `set_key_value`
constructing a `new ConfigOptionBool(...)`. The comment covers the
constraint (must be ConfigOptionBool) but not the second mechanism. Minor —
the constraint covers both.

### L4. No analogous comment near `Tab.cpp:2652-2655`

The flush options also appear in the parameter Tab as
`append_single_option_line` calls. Those use names (not positional indices),
so they are safe — but a future reader scanning for "where are flush options
listed?" will hit Tab.cpp too. Not a defect; just noting that the
positional-coupling pattern is unique to `GUI_Factories.cpp`. No action.

---

## Other comments from commit 17ec45d3fd

- **PrintConfig.cpp:6911-6918 tooltip text**: This is a user-facing translated
  tooltip, not a code comment per se. All claims verified against
  ToolOrdering.cpp gate logic. "counted from 1, excluding raft" matches
  `min_layer - 1` arithmetic against `Layer::id() - raft_layers`. "Set to 0
  to allow purging on all layers" matches the `if (min_layer > 0)` guard.
  "Has no effect when By object print sequence is active with multiple
  extruders" — this claim about print-sequence behaviour is enforced in
  `ConfigManipulation.cpp:919` (UI grays out the field) but the slicer-side
  gate at ToolOrdering.cpp does NOT check `print_sequence`. The UI hides the
  control rather than the engine ignoring it. Acceptable — the user-facing
  promise holds because the value cannot be set when the sequence is
  ByObject — but a future contributor enabling the field globally (e.g. via
  CLI override) would find the tooltip lies. **Recommendation**: tighten the
  tooltip to "Hidden in the UI when By object print sequence is active with
  multiple extruders" or move the gate into the engine.

- **ToolOrdering.cpp BOOST_LOG warning string** (line 1583): the log message
  is precise and useful for debugging. The "denying override" phrasing matches
  the `return false` that follows. Good.

- **No comments at `ConfigManipulation.cpp:917`, `Tab.cpp:2655`,
  `Preset.cpp:1127`, `Print.cpp:343`, `PrintObject.cpp:1421`**. These are
  simple add-the-key lines; comments would be overkill. OK as-is.

---

## Positive Findings

- The NOTE comment **is** present and **is** accurate (counts and types
  match). It successfully encodes the lesson from the original crash.
- The comment names the consumer function, the type constraint, and a
  concrete example of "what to do instead". This is materially more useful
  than a bare "don't reorder" comment.
- The `flush_into_infill_min_layer gate:` comment in ToolOrdering.cpp is
  also accurate and explains the non-obvious "why here, not at the call
  sites" decision — the kind of WHY comment CLAUDE.md asks for.

---

## Summary

| Severity | Count |
|----------|-------|
| Critical | 0 |
| High     | 2 |
| Medium   | 4 |
| Low      | 4 |

The comment under review is **factually accurate** and serves its primary
purpose. The dominant gap is **discoverability**: a contributor editing
`append_menu_items_flush_options()` will not see the NOTE 1000 lines above.
A reciprocal one-liner at the consumer site (H1) is the single highest-value
addition.
