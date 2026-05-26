# R3 — Architectural Review

Scope: architectural soundness of the diff to `src/slic3r/GUI/GUI_Factories.cpp` (move `flush_into_infill_min_layer` to index [3], add 4-line warning comment) and the surrounding pattern it preserves.

The diff (HEAD):

```
-    { L("Flush options")         , { "flush_into_infill", "flush_into_objects", "flush_into_support"} }
+    // NOTE: the first three entries (flush_into_infill, flush_into_objects, flush_into_support)
+    // are addressed by positional index [0], [1], [2] in append_menu_items_flush_options() below
+    // — all three must be ConfigOptionBool, in that order. Append non-bool extras (e.g.
+    // flush_into_infill_min_layer) AFTER index [2] only.
+    { L("Flush options")         , { "flush_into_infill", "flush_into_objects", "flush_into_support", "flush_into_infill_min_layer"} }
```

The fix unblocks the crash (`getBool()` on the new `coInt` option no longer fires) and is the smallest possible change. The architectural question is whether the underlying pattern — a `std::map<std::string, std::vector<std::string>>` consumed both as an iterable bundle *and* as a fixed-index tuple — should have been refactored instead.

---

## Critical

(none)

The crash itself is resolved. No critical architectural defects introduced by *this* diff — it is strictly less fragile than HEAD~1.

---

## High

### H1. Positional contract is still an unenforced invariant; the comment is documentation, not a guard

`FREQ_SETTINGS_BUNDLE_FFF` is declared as a flat `std::map<std::string, std::vector<std::string>>` (typedef'd as `SettingsFactory::Bundle` at `GUI_Factories.hpp:32`). The container carries no type, no contract, and no validation. Three of its consumers (`GUI_Factories.cpp:457`, `:465`, `:736`, `:742`) iterate by value and tolerate any string. A fourth consumer — `append_menu_items_flush_options` at `:1089-1180` — silently *requires* that indices `[0]`, `[1]`, `[2]` are bool-typed config keys in a specific semantic order:

| Index | Required key                  | Required type | Callback assumption                  |
|-------|-------------------------------|---------------|--------------------------------------|
| [0]   | `flush_into_infill`           | `coBool`      | toggle "Flush into objects' infill"  |
| [1]   | `flush_into_objects`          | `coBool`      | toggle "Flush into this object"      |
| [2]   | `flush_into_support`          | `coBool`      | toggle "Flush into objects' support" |
| [3+]  | anything (this fix's relaxation) | any        | not consumed by this code path       |

A future contributor who alphabetises the list, inserts an extra bool at index [1] (`flush_into_brim` say), or refactors the map literal will silently break the wiring: the **labels** ("Flush into objects' infill") are hard-coded in the menu builder, but the **keys** are looked up positionally, so an off-by-one wires the wrong key to the wrong label with no compile-time or runtime error — just a "this checkbox does nothing" user report.

The comment helps a careful reader but does not encode the invariant in the type system or as a `static_assert`. A future contributor reading only line 1122 will not see the comment 1050 lines above.

Concrete alternatives, ordered by effort:

1. **Address by key, drop positional indexing entirely.** Replace each `FREQ_SETTINGS_BUNDLE_FFF["Flush options"][N]` with a string literal of the actual key. Six call sites, ~30 lines, mechanical. This is what `ConfigManipulation.cpp:899` already does for the *same three keys* in a sibling subsystem (`for (auto el : {"flush_into_infill", "flush_into_support", "flush_into_objects"})`), proving the pattern is already understood and used in the codebase. After this refactor the bundle is *only* used as an iterable, the brittle index disappears, and reorder/insert is free.

2. **Compile-time guard** — leave the bundle alone, but add at top of `append_menu_items_flush_options`:
   ```cpp
   const auto& flush = FREQ_SETTINGS_BUNDLE_FFF["Flush options"];
   assert(flush.size() >= 3
       && flush[0] == "flush_into_infill"
       && flush[1] == "flush_into_objects"
       && flush[2] == "flush_into_support");
   ```
   Cheap, narrows the failure mode from a silent mis-wire to a debug-mode assert.

3. **Tuple-shaped consumer-side data** — pull the three bool keys out into a small `static constexpr std::array<const char*, 3>` adjacent to `append_menu_items_flush_options`, leave the freq-settings bundle as a pure "what shows up in Add Settings" registry. Cleanly separates the two roles.

The fix as committed picks none of these and instead leans on a comment. For a 1000-line gap between declaration and consumer, that is the weakest of the available options.

### H2. The dual role of `FREQ_SETTINGS_BUNDLE_FFF` is the actual design smell

The bundle has two unrelated jobs welded together:

- **Role A (registry):** "what frequent settings exist for FFF, grouped by category, available in the *Add Settings* picker / context menus." Used at `:453-484`, `:736-741`. Order-insensitive, content-extensible.
- **Role B (tuple):** "the three boolean flush toggles, in display order, to wire into the *Flush Options* checkable submenu." Used at `:1122-1167`. Order-sensitive, type-constrained, fixed cardinality.

A single `std::vector<std::string>` cannot honestly express both. The 17ec45d3fd crash was inevitable the moment Role A was extended (add an int per-object option for the picker) without Role B knowing. The R3 architectural recommendation is to **separate the two roles** rather than to teach future contributors a hidden ordering rule via comment.

This is the same shape as the canonical "primitive obsession" smell — a data structure carrying meaning by position rather than by name.

---

## Medium

### M1. The smallest-viable fix left a real footgun

The PR is technically minimal — exactly the bytes needed to stop the crash. But "minimal" and "architecturally appropriate" diverge here. Six identical call sites at `:1122-1167` (three pairs of setter + getter) all carry the same positional-index pattern, and each is two lines that *could have been refactored to a string literal in the same touch*. Doing so would have:

- Made the fix self-documenting (no NOTE comment needed).
- Eliminated the new "append AFTER index [2] only" rule.
- Aligned this code with the by-key pattern already used 80 lines away in `ConfigManipulation.cpp`.
- Reduced LOC by ~6 lines net.

The cost of doing the refactor in the same PR is roughly equal to the cost of writing the cautionary comment, and the outcome is unambiguously better. The fix as committed optimises for diff size at the expense of design.

### M2. `FREQ_SETTINGS_BUNDLE_SLA` is currently empty — no parallel risk *today*, but the pattern is in place

`FREQ_SETTINGS_BUNDLE_SLA` (`:76-79`) is empty (`// BBS: remove SLA freq settings`). No SLA caller does positional indexing. The structural pattern that allowed the FFF crash is, however, structurally identical; if SLA freq settings are ever restored and a checkable-toggle menu is added for one of its categories, the same trap can recur. Worth a one-line follow-up comment in the SLA bundle, or — preferred — fixing the architecture once.

### M3. Per-object option registration in OrcaSlicer is already a "register in N places" pattern

Adding a per-object option (per the spec at `7e95dd1b7f` / `7cbade4fdc`) touches:

| File                                                  | Purpose                                                  |
|-------------------------------------------------------|----------------------------------------------------------|
| `src/libslic3r/PrintConfig.cpp:6908`                  | `add("flush_into_infill_min_layer", coInt)` + metadata   |
| `src/libslic3r/PrintConfig.hpp:1006`                  | C++ struct field in `PrintObjectConfig` BOOST_PP list    |
| `src/libslic3r/Preset.cpp:1127`                       | `is_overriddable` allow-list                             |
| `src/libslic3r/Print.cpp:343`                         | Invalidation key list                                    |
| `src/libslic3r/PrintObject.cpp:1421`                  | Invalidation key list                                    |
| `src/slic3r/GUI/Tab.cpp:2655`                         | Render in Printer/Filament tab                           |
| `src/slic3r/GUI/ConfigManipulation.cpp:917`           | Visibility/toggle rules (by string key — good)           |
| `src/slic3r/GUI/GUI_Factories.cpp:72`                 | Freq-settings bundle (by string key in literal — good)   |
| `src/libslic3r/GCode/ToolOrdering.cpp:1566+`          | Actual slicing-time consumer                             |

That is 9 touchpoints. Most are by-key (good — adding a key only requires adding the key). The single brittle point is `GUI_Factories.cpp` Role B — it is *also* by-key in the literal, but a sibling consumer reads it positionally. No other touchpoint on this list has the "list literal also serves as positional tuple" property. So the architecture overall is sane; this is one localised wart.

### M4. The four-line warning comment is well-written but in the wrong place

The comment is at the *declaration site* (`:68-71`). The dangerous consumer is at `:1122-1167`. A contributor who lands at the consumer (e.g., adding a new flush toggle) sees `FREQ_SETTINGS_BUNDLE_FFF["Flush options"][0]` with no comment nearby, and has no reason to scroll 1050 lines up. A defensive comment **at the consumer** ("// These indices are wired by position to flush_into_infill/objects/support — see line 56-72") would be at least as load-bearing. Best is both, but if forced to pick one location, the consumer wins.

---

## Low

### L1. Comment wording could be tighter
"Append non-bool extras [...] AFTER index [2] only" is technically the rule, but the actually-load-bearing rule is "do not change the order of the first three". A bool inserted at [1] would corrupt the wiring just as badly as a non-bool at [1]. The "non-bool" framing implies that *type* is what matters, when in fact *position* is what matters. Suggest: "Indices [0]-[2] are wired positionally in append_menu_items_flush_options(); do not reorder, do not insert."

### L2. Naming: "Flush options" string key is itself a fragile join
`FREQ_SETTINGS_BUNDLE_FFF["Flush options"]` (line 1122 etc.) uses a *translated* category label as a `std::map` key, where the same string appears in `L("Flush options")` at line 72. If anyone ever changes the category label (a routine UI tweak), the menu silently breaks. The wrapping `L()` happens at static-init time and just returns the source string, so it works today, but it is another implicit join that the type system does not enforce. A `static constexpr char kFlushOptionsCategory[] = "Flush options";` shared by both sites would lock this.

### L3. `append_menu_items_flush_options` has near-duplicated 16-line blocks
The three `append_menu_check_item` calls at `:1120-1169` differ only in label, index, and (implicitly) key. A helper `add_flush_toggle(menu, label, key, can_flush, select_object_config, global_config)` would collapse 50 lines to ~15 and make the by-key refactor in H1 a one-line change. Out of scope for a crash fix, but worth a follow-up.

---

## Summary

**Counts:** 0 Critical · 2 High · 4 Medium · 3 Low

**Verdict:** the fix is correct and unblocks shipping, but it is the *weakest* of the available architectural responses. The committed change preserves an implicit positional contract by documenting it; the codebase already demonstrates (at `ConfigManipulation.cpp:899`) that the cleaner pattern — address options by string-key literal — is the established convention for this exact same trio of keys. The comment is in the wrong place (1050 lines from the danger), the rule it states is slightly mis-framed (position, not type), and the bundle's dual role (Role A registry + Role B tuple) is the actual design smell.

**Top architectural concern:** H1/H2 — `FREQ_SETTINGS_BUNDLE_FFF["Flush options"]` carries two incompatible roles and the positional contract for Role B is unguarded. The next contributor who reorders/inserts a flush key, even harmlessly within the bundle, will silently mis-wire three checkboxes with no compile-time error. A 6-line follow-up replacing `FREQ_SETTINGS_BUNDLE_FFF["Flush options"][N]` with string literals in `append_menu_items_flush_options` would eliminate the hazard, remove the new NOTE comment as load-bearing, and align the file with its sibling `ConfigManipulation.cpp`. That is the recommended next step — not blocking for this fix, but high-value cleanup.

**Parallel SLA risk:** none today (`FREQ_SETTINGS_BUNDLE_SLA` is empty); structurally possible if SLA ever grows checkable-toggle menus.
