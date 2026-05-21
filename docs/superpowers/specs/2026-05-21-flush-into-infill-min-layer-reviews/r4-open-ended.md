# Reviewer 4 — general-purpose (open-ended hunt with code verification)

Agent ID: a208f95f5b30b0028

## Findings

### Critical
- **C1: Scope leak — gate also blocks `flush_into_objects` perimeter overrides.** Same finding as R1 (M2), R2 (C1), R3 (M2). **Now 4 reviewers in agreement.** **Fix:** move the gate inside the `flush_into_infill` branch, after the `eec.role() != erInternalInfill` early-return.
  File: `src/libslic3r/GCode/ToolOrdering.cpp:1559-1571`

- **C2: Missing print-level invalidation in `Print.cpp:341`.** `flush_into_infill` is listed in `Print::invalidate_state_by_config_options` at Print.cpp:341 — the new key must also be there or print-preset-path changes won't invalidate psWipeTower/psGCodeExport.
  **Fix:** add `|| opt_key == "flush_into_infill_min_layer"` between lines 341-342.
  File: `src/libslic3r/Print.cpp:341` — **NEW FINDING not in other reviews**

### High
- **H1: Multi-object averaged `lt.print_z` can miss layer.** `ToolOrdering.cpp:641` averages `0.5*(zs[i]+zs[j-1])` over near-equal Zs from all objects. For mixed-raft plates, per-object `Layer::print_z` may differ from `m_layer_tools->print_z` beyond `get_layer_at_printz` EPSILON, returning nullptr → fail-closed denial → regression on layers that SHOULD be allowed. **Fix:** resolve layer once at the outer loop in `mark_wiping_extrusions` (it already does at line 1635) and pass to helper.
  File: `src/libslic3r/GCode/ToolOrdering.cpp:641, 1559`

- **H2: `s_Preset_print_options` entry required.** Same as R2 H4 — confirmed by R4. Insert adjacent to `flush_into_infill`.

- **H3: `GUI_Factories.cpp:68` Object Settings sidebar inclusion.** Right-click → Object Settings needs the new key in the "Flush options" category. Same as R2 M2.

### Medium
- **M1: UI toggle on `flush_into_infill_min_layer` must NOT be added to the existing loop** at line 895 — that loop only chains `have_prime_tower`. New field needs separate `toggle_line/toggle_field` with the combined predicate.

- **M2: Tab UI parent-child indentation.** Bare `append_single_option_line` produces flat row; users won't see the parent-child relation with `flush_into_infill`.

- **M3: `Layer::id()` size_t→int narrowing.** Cleaner: do `size_t` arithmetic with explicit underflow guard. Same as R2 H1 and SF1 H3.

### Low
- **L1: Localization.** `run_gettext.sh` extracts at packaging. Not blocking.
- **L2: Profile JSONs.** 87 profiles reference `flush_into_infill`; default 0 keeps existing behavior, no profile edits needed.
- **L3: `s_Preset_print_options` order is irrelevant for behavior** but matters for diff readability.
- **L4: gate runs uniformly across `ensure_perimeters_infills_order` and `collect_extruders`** — correct **once C1 is fixed** (gate restricted to infill arm).

### Verified non-issues
- `object.config()` returns `const PrintObjectConfig&` (Print.hpp:321) — new field accessible.
- `slicing_parameters()` is populated before ToolOrdering runs (Print.hpp:419 + update_slicing_parameters() in Print::apply).
- PRINT_CONFIG_CLASS_DEFINE macro auto-propagates to FullPrintConfig/StaticPrintConfig/DynamicPrintConfig. No aggregator updates needed.
