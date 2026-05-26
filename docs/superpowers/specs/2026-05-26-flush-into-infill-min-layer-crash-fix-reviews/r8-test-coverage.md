# R8 — Test Coverage Review

PR: flush_into_infill_min_layer crash fix in `src/slic3r/GUI/GUI_Factories.cpp`
Date: 2026-05-26
Reviewer: R8 (test coverage)

## Summary

Coverage of the affected code is **zero** across all test suites. Grepping `tests/` for any of `FREQ_SETTINGS_BUNDLE`, `MenuFactory`, `object_menu`, `append_menu_items_flush_options`, `flush_into_infill`, `flush_into_infill_min_layer`, `is_overriddable`, `WipingExtrusions`, `wipe_tower`, or `ToolOrdering` returns **no hits**. Counts: 0 unit tests touching the bundle, 0 tests for the `is_overriddable` gate, 0 tests for `WipingExtrusions`, 0 tests for the option's `coInt` typing. The crash class — positional indexing into a config-key vector whose first three entries are contractually `coBool` — is exactly the kind of brittle invariant a tiny unit test could lock down at near-zero cost. No tests changed in this PR; the fix is code-only with a comment guard. CI builds the binary but does not exercise any path that constructs the menu bundle.

The single highest-value missing test: a libslic3r-level assertion that walks `FREQ_SETTINGS_BUNDLE_FFF["Flush options"]` and asserts entries [0..2] resolve in a default `DynamicPrintConfig` to `ConfigOptionBool` (`option->getBool()` does not throw). This would have failed the original buggy commit and would catch any future reorder.

Note: `FREQ_SETTINGS_BUNDLE_FFF` is a static-local in a GUI translation unit (`SLIC3R_GUI`); the test would need to either (a) move the constant to a header-visible namespace, (b) introduce a small accessor `MenuFactory::flush_options_keys()`, or (c) live in a GUI-linked smoke test target. Option (b) is cleanest and itself constitutes a small refactor that makes the contract testable.

## Critical (rating 9-10)

### C1. No regression test for the positional contract in `FREQ_SETTINGS_BUNDLE_FFF["Flush options"]` (rating: 9)

**What's missing.** The contract that the first three entries of `FREQ_SETTINGS_BUNDLE_FFF["Flush options"]` are `coBool` is enforced only by a code comment (lines 68-71 of `GUI_Factories.cpp`). The original bug existed because that contract was implicit. A future contributor reordering or inserting a `coInt`/`coFloat` option at position [0..2] will reintroduce the same crash, and the comment is easy to overlook.

**Suggested test (libslic3r_tests, ~25 lines).**
```cpp
// tests/libslic3r/test_flush_options_contract.cpp
TEST_CASE("Flush options bundle first three keys are coBool", "[Config][GUI_Contract]") {
    DynamicPrintConfig cfg = DynamicPrintConfig::full_print_config();
    // The contract enforced positionally in MenuFactory::append_menu_items_flush_options.
    // If MenuFactory::flush_options_keys() exists, use it. Otherwise hard-code the
    // three names matching the GUI_Factories.cpp comment.
    const std::vector<std::string> bool_keys =
        { "flush_into_infill", "flush_into_objects", "flush_into_support" };
    for (const auto& key : bool_keys) {
        const ConfigOption* opt = cfg.option(key);
        REQUIRE(opt != nullptr);
        // The original crash: getBool() on a non-bool throws BadOptionTypeException.
        REQUIRE_NOTHROW(opt->getBool());
    }
}
```

**What it catches.** Exactly the regression this PR fixed: any contributor inserting a non-bool option at index [0..2] (or renaming a bool one to a non-bool definition) will fail this test instantly.

**Cost.** Trivially low — links only `libslic3r`, no GUI/wxWidgets needed, runs in milliseconds.

**Better variant (rating 9, slightly higher cost).** Introduce `static const std::vector<std::string>& MenuFactory::flush_options_bool_keys()` returning the first three keys directly from the bundle, and assert each is `ConfigOptionBool*` via `dynamic_cast`. This binds the test to the actual data the GUI consumes rather than a duplicated string list.

### C2. No coverage for `WipingExtrusions::is_overriddable` gate with `flush_into_infill_min_layer` (rating: 8)

**What's missing.** The broader feature (commits `17ec45d3`, `7cbade4f`) added a non-trivial layer-gate inside `ToolOrdering.cpp::is_overriddable` (lines 1572-1591) that:
- Reads `object.config().flush_into_infill_min_layer.value`
- Calls `object.get_layer_at_printz(m_layer_tools->print_z, EPSILON)` — nullable
- Subtracts `object.slicing_parameters().raft_layers()` from `Layer::id()` with a logged warning on inversion
- Compares against `min_layer - 1`

None of this is tested. The gate runs inside three callers (`collect_extruders`, `mark_wiping_extrusions`, `ensure_perimeters_infills_order`) that the comment explicitly warns must stay consistent. A regression where one caller skips the gate would silently corrupt wipe-tower volume accounting.

**Suggested tests (fff_print_tests).**
1. Default (`min_layer == 0`) — gate is a no-op; `is_overriddable` returns `true` for an internal-infill ExtrusionEntityCollection when `flush_into_infill` is on.
2. `min_layer == 3` — at object-local layer 0, 1, 2: returns `false`. At layer 3+: returns `true` (assuming `flush_into_infill` on).
3. Raft interaction — `min_layer == 1`, `raft_layers == 2`, query at the raft top: returns `false` and logs the warning (cannot assert on the log easily, but assert the return).
4. Soluble filament short-circuit still wins regardless of `min_layer`.

**What it catches.** Off-by-one drift in `min_layer - 1`, raft accounting bugs, and the "infill not in `lt.extruders`, never rescued" hazard the in-code comment explicitly warns about.

**Cost.** Moderate — needs a mocked `PrintObject` + `Layer` or a full slicing fixture; existing `fff_print_tests` has the infrastructure (`test_print.cpp`, `test_data.hpp`).

## High (rating 7-8)

### H1. No smoke test for "construct object menu" path (rating: 7)

**What's missing.** The crash bug fixed in this PR is triggered by *constructing* the right-click menu, not by any slicing operation. There is no automated path that exercises `MenuFactory::object_menu` → `append_menu_items_flush_options` → bundle lookup. Even a headless unit test that:
1. Loads default `DynamicPrintConfig`
2. Walks `FREQ_SETTINGS_BUNDLE_FFF` keys in order
3. For each key, calls `option->getBool()` if the menu treats it as bool, and just `option != nullptr` otherwise

…would have caught this in milliseconds. Today the only verification is "run the app on macOS, right-click an object, hope it doesn't crash."

**Cost.** Low. Same target as C1, just expanded.

### H2. No manual reproduction documented for reviewers (rating: 7)

**What's missing.** No `tests/manual/` doc, no commit-message repro steps. A reviewer cannot easily verify the fix without:

1. Build the app with the BBL.json profile
2. Switch printer to a BBL machine (e.g., X1 Carbon, multi-extruder setup)
3. Load any model, slice, right-click the object in the sidebar
4. Open the "Flush Options" submenu
5. Pre-fix: app crashes with `BadOptionTypeException` at the first `option->getBool()` call on `flush_into_infill_min_layer`
6. Post-fix: submenu opens with three checkable items; the `_min_layer` option is **only** visible via the parameter table (intentional, since it's a `coInt` and the submenu has no spinner UI for it).

**Suggested action.** Add these steps to the PR description (and to a follow-up `docs/manual-tests/flush-options-menu.md` if a manual-test catalog exists). At minimum, add the reproduction to the commit message of the fix.

### H3. `flush_into_infill_min_layer` per-object override is untested (rating: 7)

**What's missing.** The earliest commit (`17ec45d3`) made the option per-object (added to `PrintObjectConfig`). There's no test confirming that an object-level override actually wins over the print-level default in the slicing pipeline (i.e., the `cereal`-serialized `ModelConfig` round-trips correctly through 3MF save/load and reaches `object.config()` inside `is_overriddable`).

**Suggested test (libslic3r_tests, alongside `test_3mf.cpp`).** Save a 3MF with `flush_into_infill_min_layer = 5` on object A and `= 0` on object B, reload, assert the per-object value survives.

## Medium (rating 5-6)

### M1. No unit test for `option_throw<ConfigOptionBool>` on a `coInt` key (rating: 5)

The existing `test_config.cpp` at line 190-199 tests `option_throw` on an unknown key but not on a *type mismatch*. A `REQUIRE_THROWS_AS(cfg.option_throw<ConfigOptionBool>("flush_into_infill_min_layer", false), BadOptionTypeException)` would document the throw-on-mismatch behavior that surfaced this crash. Useful as a regression anchor.

### M2. No CMake-discovered smoke target for GUI bundle invariants (rating: 5)

A small `tests/slic3r_gui_smoke/` target that links the GUI's static settings vectors (Bundle, OBJECT_CATEGORY_SETTINGS, PART_CATEGORY_SETTINGS) and asserts each declared key resolves in `DynamicPrintConfig::full_print_config()` would catch a whole class of "removed/renamed option, forgot to update the menu" bugs. The same pattern would catch the original crash if the assertion includes type checks.

### M3. CI does not exercise sidebar interactions (rating: 5)

The fork's macOS workflows build + sign + notarize a DMG but never launch the app. A `/Applications/<app>.app/Contents/MacOS/<bin> --help` invocation is the cheapest possible smoke test (verifies the binary loads and the linker resolved all wxWidgets / TBB symbols). It would not have caught this specific crash (menu construction is only triggered on user click), but it's worth noting that the test gap is "everything past launch."

## Low (rating 3-4)

### L1. No assertion that the `coInt` option has sensible bounds (rating: 4)

`flush_into_infill_min_layer` has `min=0, max=5000`. A trivial config-validate test using out-of-range values would document the contract. Low impact — the GUI spinner already enforces it, but the 3MF reload path could in theory deliver out-of-range values.

### L2. The `_min_layer` option does not appear by name in any test data (rating: 3)

Test data 3MFs in `tests/data/` will not exercise the new option until someone explicitly authors one. Not blocking, but worth noting that a regression test for the gate (C2) will need its own minimal test fixture.

## Positive Observations

- The PR's code comment (lines 68-71 of `GUI_Factories.cpp`) is the right *immediate* mitigation — a contributor reading the bundle definition will see the contract. This is a real coverage improvement at near-zero cost.
- The in-code comment at `ToolOrdering.cpp:1572-1575` explaining why the gate lives inside `is_overriddable` (rather than at the outer call sites) is excellent — it documents the invariant that any unit test would need to enforce. Future test authors have a clear contract to assert against.
- The existing `tests/libslic3r/test_config.cpp` already has the right pattern (`DynamicPrintConfig::full_print_config()` + `opt<ConfigOptionBool>`) for C1 to drop in with no new infrastructure.
- The codebase's testing CLAUDE.md is unusually thorough; new tests for these gaps would have strong scaffolding.

## Recommended priority for follow-up

1. **(P0, ~30 min)** Add the C1 unit test, even with the duplicated string list. Lock the contract.
2. **(P1, ~2 hr)** Land the small `MenuFactory::flush_options_bool_keys()` refactor so C1 tests the actual source-of-truth, not a copy.
3. **(P1, ~4 hr)** Add C2 (`is_overriddable` gate) tests in `fff_print_tests`. These are the higher-value tests for the broader feature.
4. **(P2)** Document manual reproduction in PR description / commit message (H2).
5. **(P3)** Consider the bigger M2 smoke target if more GUI-vs-config drift is found in subsequent reviews.
