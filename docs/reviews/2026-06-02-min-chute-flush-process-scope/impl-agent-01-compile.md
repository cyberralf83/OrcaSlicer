# Compile Review — min-chute-flush process-scope migration

Reviewer focus: will the diff compile cleanly (incl. `-Werror` paths) before the ~1h CI build?
Diff range: `40fa1e2292..HEAD` (src/ only). Branch `nightly-builds-with-bc`, macOS.

## Verdict

**WILL COMPILE.**

The migration of `filament_minimal_purge_on_chute` (`ConfigOptionFloats`, per-filament) →
`minimal_chute_flush_length` (`ConfigOptionFloat`, global/process) is type-consistent end to end. All
touched symbols resolve, the macro arity matches its neighbors, the new accessor (`.value`) is correct
for a scalar `ConfigOptionFloat`, the aggregation chain exposes the member on `FullPrintConfig`, the UI
toggle members/signatures match, and no dangling symbols, unbalanced braces, or unused-variable
`-Werror` risks were introduced. No leftover references to the old key remain anywhere in `src/`.

## Checks passed

1. **`PrintConfig.hpp:1458` macro line.** `((ConfigOptionFloat, minimal_chute_flush_length))` — 2-element
   tuple, identical format to neighbors (e.g. `((ConfigOptionFloats, filament_minimal_purge_on_wipe_tower))`
   on the line above). Lives in the `GCodeConfig` block (`PRINT_CONFIG_CLASS_DEFINE(GCodeConfig, …)` at
   hpp:1303). The two preceding `//` comment lines are outside the tuple and harmless to the BOOST_PP
   sequence. Macro generates a `ConfigOptionFloat` member.

2. **`ConfigOptionFloat` exposes `.value` (double).** `class ConfigOptionFloat : public
   ConfigOptionSingle<double>` (Config.hpp:764); `ConfigOptionSingle<T>` has `T value;` (Config.hpp:307).
   So `minimal_chute_flush_length.value` is a `double` — correct accessor, matches the old code's
   `(float)` cast target.

3. **`PrintConfig.cpp:2722-2738` registration.** `this->add("minimal_chute_flush_length", coFloat)` — enum
   is `coFloat` (scalar), matching the `ConfigOptionFloat` member (the old `coFloats`/`ConfigOptionFloats`
   pair is fully gone). `def->set_default_value(new ConfigOptionFloat(0.))` constructs the matching scalar
   type. `def->category = L("Flush options")` — `category` is `std::string` (Config.hpp:2449); `L(...)`→
   `std::string` assignment is the established pattern (same literal used at PrintConfig.cpp:7017, 7026,
   7041, 7049). `def->min/mode/sidetext/tooltip/label` all standard. No leftover `ConfigOptionFloats`
   against this key.

4. **`GCode.cpp:881`.** `full_config.minimal_chute_flush_length.value`. `full_config` is a
   `FullPrintConfig`; inheritance `FullPrintConfig → (PrintObjectConfig, PrintRegionConfig, PrintConfig)`
   (hpp:1668) and `PrintConfig → (MachineEnvelopeConfig, GCodeConfig)` (hpp:1487), and the member is in
   `GCodeConfig` (hpp:1303) → member is visible. `.value` is the correct scalar accessor; the old
   `.get_at(new_filament_id)` is gone (verified no remaining `.get_at` on this key). The explicit
   `(float)` cast of the `double value` is a C-style cast (not brace-init), so no `-Wnarrowing`.
   `g_min_purge_volume` (float, GCode.cpp:92) and `EPSILON` are pre-existing and untouched; the
   `std::max({…})` initializer-list elements (`tcr.purge_volume`, `g_min_purge_volume`, `min_chute_purge`)
   are all `float`, so the homogeneous-type `std::max(initializer_list)` overload still resolves.

5. **`ConfigManipulation.cpp:888-889`.** `toggle_line(const std::string&, const bool, int=-1)` and
   `toggle_field(...)` are both `ConfigManipulation` members (hpp:69-70). `is_BBL_Printer` is a member
   field (hpp:27) in scope throughout `toggle_print_fff_options`. `have_prime_tower` is a local declared
   at ConfigManipulation.cpp:854, in scope at 889. String-literal → `std::string` conversion for the key
   arg is implicit and fine. `is_BBL_Printer && have_prime_tower` is `bool` → matches `const bool` param.

6. **`Tab.cpp` edits.**
   - Print tab (2687): `append_single_option_line("minimal_chute_flush_length", …)` added to the
     "Flush options" optgroup — same call shape as its siblings.
   - Filament tab (4150): the `append_single_option_line("filament_minimal_purge_on_chute", …)` line was
     removed cleanly; the surrounding `optgroup->append_single_option_line(...)` lines are intact (no
     unbalanced braces).
   - Removed toggle in `TabFilament::toggle_options` (the old
     `toggle_option("filament_minimal_purge_on_chute", is_BBL_printer)` block). The local
     `is_BBL_printer` (declared Tab.cpp:4269) is **still used** at 4318, 4358, 4364 — no
     unused-variable `-Werror` risk.

7. **`Print.cpp:289`** and **`Plater.cpp:16692`** — single-token string-literal swap in existing
   `opt_key == "…"` comparison chains; trivially valid.

8. **`Preset.cpp` list edits.**
   - `s_Preset_print_options` (1128): `"minimal_chute_flush_length",` inserted between two existing
     comma-terminated entries — valid initializer list.
   - `s_Preset_filament_options` (1283): trailing `, "filament_minimal_purge_on_chute"` removed from the
     end of the line; the preceding `"filament_minimal_purge_on_wipe_tower",` keeps its comma and the
     next line continues normally. No trailing/dangling comma, no syntax error.

9. **No stale references.** `grep -rn "filament_minimal_purge_on_chute" src/` → 0 matches. All 8
   `minimal_chute_flush_length` references (PrintConfig.cpp/.hpp, Print.cpp, GCode.cpp, Preset.cpp,
   Plater.cpp, ConfigManipulation.cpp ×2, Tab.cpp) are mutually consistent.

## Findings

None at CRITICAL or HIGH severity.

- **[LOW] PrintConfig.cpp:2724 — new `def->category = L("Flush options")` where the old chute def had no
  category.** This is intentional (the option moved to the Print tab's "Flush options" group) and is the
  same value used by the other flush options. Compiles fine; noted only as a behavioral/UX delta, not a
  build risk.

- **[LOW] ConfigManipulation.cpp:888 vs GCode.cpp:884 — two different "BBL" gates.** UI uses
  `is_BBL_Printer` (preset vendor); G-code emission uses `gcodegen.is_BBL_Printer()` (runtime printer).
  Already documented in the in-code comment as intentional. No compile impact; flagging for the
  correctness/runtime reviewer to confirm the divergence is desired.

## Sources

- `git diff 40fa1e2292..HEAD -- src/`
- `src/libslic3r/PrintConfig.hpp:1303` (GCodeConfig), `:1458` (member), `:1487` (PrintConfig parents),
  `:1668` (FullPrintConfig parents)
- `src/libslic3r/PrintConfig.cpp:2722-2738`
- `src/libslic3r/Config.hpp:307` (`T value;`), `:764` (ConfigOptionFloat), `:2449` (`category`)
- `src/libslic3r/GCode.cpp:92, 880-889`
- `src/slic3r/GUI/ConfigManipulation.hpp:27, 69-70`; `ConfigManipulation.cpp:854, 881-889`
- `src/slic3r/GUI/Tab.cpp:2687, 4150, 4269, 4318, 4355-4358, 4364`
- `src/libslic3r/Preset.cpp:1128, 1283`; `src/libslic3r/Print.cpp:289`; `src/slic3r/GUI/Plater.cpp:16692`
- `grep -rn` for old and new key across `src/`
