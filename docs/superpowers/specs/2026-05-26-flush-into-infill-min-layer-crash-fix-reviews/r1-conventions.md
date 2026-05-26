# R1 — code-reviewer (conventions/correctness)
Agent: pr-review-toolkit:code-reviewer

## Critical
- None.

## High
- None.

## Medium
- None.

## Low
- **(Style nit, confidence 75 — below 80 reporting threshold but logged for completeness)** Lines 68–71 use an em-dash (`—`, U+2014) inside the comment. The rest of `GUI_Factories.cpp` and the surrounding BBS/Orca comments are pure ASCII. This is harmless (the file is UTF-8 elsewhere, e.g. `i18n` paths), and clang-format leaves it alone, but if strict ASCII-only comments are desired, replace `—` with ` -- ` or `:`. Not worth blocking on.

## Notes (informational, not findings)
- The `Bundle` type is `std::map<std::string, std::vector<std::string>>` (confirmed via `GUI_Factories.hpp:32`). Reordering the four string literals inside the brace-enclosed initializer list keeps the same map key (`L("Flush options")`) and the same vector length (4). It is well-formed C++ and identical-language-semantics aside from element order. No other initialization-order dependency exists.
- Positional indexing is genuinely the contract here: `append_menu_items_flush_options()` at `GUI_Factories.cpp:1089-1180` hard-codes `[0]`, `[1]`, `[2]` and unconditionally calls `option->getBool()` on each, plus writes back a `new ConfigOptionBool(...)`. Re-checked all three groups (lines 1120–1135 → "Flush into objects' infill"; 1137–1152 → "Flush into this object"; 1154–1169 → "Flush into objects' support"). The new ordering `flush_into_infill`, `flush_into_objects`, `flush_into_support` matches the menu labels semantically — the previous ordering had not only the wrong type at `[1]` (Int instead of Bool, which is the crash) but also the wrong **setting** at `[1]` (`flush_into_infill_min_layer` was being toggled when the user clicked "Flush into this object"). The fix corrects both issues.
- Other references to `FREQ_SETTINGS_BUNDLE_FFF` (lines 453, 736) iterate via range-`for` and are order-agnostic. No collateral impact from reordering.
- All four new/edited lines are ≤140 cols (.clang-format `ColumnLimit: 140` honored; 96/97/90/57/134). Indentation is 4 spaces (matches `IndentWidth: 4`, `UseTab: Never`). `//` comment style and capitalized "NOTE:" prefix are consistent with neighbouring `//BBS` markers in the same block.
- The comment placement (immediately above the line whose ordering it documents, rather than inside the function that consumes the ordering) is the right call — the function-side has no way to enforce the invariant, but anyone editing the bundle here will see the warning at the point of change. Idiomatic.
- The change is in-scope per CLAUDE.md (it fixes a regression caused by the fork's own `flush_into_infill_min_layer` feature; diff from upstream remains minimal — just one literal reorder plus a 4-line comment).
- No new include, no new symbol, no API change, no translation-string change (the `L("Flush options")` key is identical). No `.po` regeneration needed.

## Summary
The fix is correct and conformant. Reordering the four strings inside a `std::map<std::string, std::vector<std::string>>` literal initializer is well-formed C++ and leaves all order-agnostic consumers (lines 453, 736) untouched while restoring the positional [0]/[1]/[2] contract that `append_menu_items_flush_options()` requires (three `ConfigOptionBool` settings in the exact order matching the menu labels). The 4-line `// NOTE:` comment is placed at the site of the invariant, uses the same `//` style as the neighbouring `//BBS` markers, stays within the 140-column limit, and is well-targeted at the next person who edits this list. One sub-threshold style nit (em-dash inside the comment) is the only thing worth even mentioning, and it is not a real issue. Recommend merge.
