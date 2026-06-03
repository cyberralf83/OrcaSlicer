# Style / Formatting Review — min-chute-flush process-scope diff

Scope: lines added/changed by `git diff 40fa1e2292..HEAD -- src/`. Pre-existing style in untouched
code is out of scope. Focus: indentation, macro-column alignment, 140-col limit, brace/spacing/quote
consistency, comment-block style, trailing whitespace, tabs-vs-spaces.

## Verdict

PASS. The new code matches the surrounding style in every changed file. No HIGH/MEDIUM/LOW issues found
in the added/changed lines.

## clang-format availability + result

`clang-format` is **NOT installed** on this host (`which clang-format` → not found; no `clang-format`
binary in PATH). Per the instructions, I assessed by eye against `.clang-format` (`IndentWidth: 4`,
`ColumnLimit: 140`, `UseTab: Never`, `AlignConsecutiveAssignments: true`, `SpaceAfterCStyleCast: true`,
`Cpp11BracedListStyle: true`, brace wrapping for classes/functions) and against the immediately
surrounding lines in each file.

Measurement method: column widths were computed as Unicode codepoints under UTF-8 (perl `length` with
`-CSD`), not raw bytes — the tooltip/comment text contains multibyte glyphs (`—` em-dash = 3 bytes,
`³` = 2 bytes, `'` curly apostrophe = 3 bytes) that would otherwise inflate a naive byte count and
produce false 140-col positives. All reported widths below are true display-column widths.

## Findings

None. All changed lines conform. Verification detail per file:

- **src/libslic3r/PrintConfig.hpp** (macro block, lines 1456–1458)
  - Field-name column for the new `((ConfigOptionFloat, minimal_chute_flush_length))` aligns at
    column 40, identical to every neighbor (`filament_minimal_purge_on_wipe_tower`,
    `filament_cooling_before_tower`, etc.). The shorter type name `ConfigOptionFloat` is correctly
    padded out to the same field column — alignment preserved.
  - New ORCA comment lines: 4-space indent (matches the macro-list indent), widths 101 and 105 cols
    (< 140). Comment style (`// ORCA: ...`) matches the file's existing inline-comment convention.

- **src/libslic3r/PrintConfig.cpp** (option def, lines 2722–2738)
  - `def = this->add("minimal_chute_flush_length", coFloat);` and the new
    `def->category = L("Flush options");` follow the standard `def->...` def-builder pattern.
  - Tooltip continuation strings indent to column 21 (aligned under the `L("` open paren), matching
    the neighboring tooltip-continuation style. Widest line is 106 cols (< 140).
  - `def->set_default_value(new ConfigOptionFloat(0.));` matches the established scalar-default
    convention (parens, no inner spaces) used at lines 744, 1166, 1250, 1262, etc. — correct change
    from the old braced-list `ConfigOptionFloats { 0. }` form now that the type is scalar.
  - The pre-existing `def->sidetext = L("mm");` + TAB + comment line is untouched by the diff; the tab
    before the comment is preserved exactly as in the original (confirmed via `sed -n '2735l'`), per
    the review note.

- **src/libslic3r/GCode.cpp** (comment block + assignment, lines 873–882)
  - Reworded `// ORCA:` comment block keeps the original 16-space indent and `//`-comment style;
    widths ≤ 106 cols (< 140).
  - Changed assignment `const float min_chute_length = (float) full_config.minimal_chute_flush_length.value;`
    (line 881, 122 cols < 140) keeps the manual `=` alignment of the surrounding declaration block
    (`min_chute_length` / `min_chute_purge` / `is_real_toolchange` / `apply_chute_min` `=` columns
    still aligned), and the C-style cast `(float)` retains the trailing space required by
    `SpaceAfterCStyleCast: true`.

- **src/slic3r/GUI/ConfigManipulation.cpp** (lines 884–889)
  - New `// ORCA:` comment block at 4-space indent, `//`-style, widths ≤ 103 cols (< 140).
  - `toggle_line(...)` / `toggle_field(...)` calls at 4-space indent match the neighboring
    `toggle_field`/`toggle_line` calls; spacing/quote style consistent.

- **src/libslic3r/Preset.cpp** (line 1128), **src/libslic3r/Print.cpp** (line 289),
  **src/slic3r/GUI/Plater.cpp** (line 16692), **src/slic3r/GUI/Tab.cpp** (lines 2687, 4151)
  - Single-token rename/add/removal lines; each matches the indentation and comma/quote style of its
    surrounding list or `||` chain. No width or alignment concerns.

Global checks across all added (`+`) lines:
- Trailing whitespace: none (`git diff | grep -E '^\+.*[ \t]+$'` → no matches).
- Stray tabs in added lines: none (only spaces for indentation; `UseTab: Never` respected).
- All added/changed lines ≤ 140 display columns (true Unicode width).

## Sources

- `git -C /Volumes/MacMicroSD/Github/OrcaSlicer-nighty diff 40fa1e2292..HEAD -- src/`
- `.clang-format` (IndentWidth 4, ColumnLimit 140, UseTab Never, AlignConsecutiveAssignments true,
  SpaceAfterCStyleCast true)
- `which clang-format` → not installed
- Per-line width measured as Unicode codepoints: `perl -CSD -ne 'length'`
- Macro field-column measured via `awk` index of field name (column 40 for all neighbors + new line)
- src/libslic3r/PrintConfig.hpp:1455–1459; src/libslic3r/PrintConfig.cpp:2719–2742;
  src/libslic3r/GCode.cpp:873–885; src/slic3r/GUI/ConfigManipulation.cpp:881–889
- Scalar-default convention cross-check: `grep 'set_default_value(new ConfigOptionFloat('
  src/libslic3r/PrintConfig.cpp` (lines 744, 1166, 1250, 1262, …)
- sidetext tab confirmation: `sed -n '2735l' src/libslic3r/PrintConfig.cpp`
