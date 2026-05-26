# V3 — Verification of Comments, Contract Documentation, and Back-Reference

Wave: V3 of 4. Scope: comment/contract correctness in `git diff HEAD` across `GUI_Factories.cpp`, `ToolOrdering.cpp`, `PrintConfig.cpp`.

## Summary

All comment claims verified accurate. The bidirectional contract between the producer-side comment at `FREQ_SETTINGS_BUNDLE_FFF` initializer (`GUI_Factories.cpp:68-71`) and the new consumer-side back-reference at `append_menu_items_flush_options` (`GUI_Factories.cpp:1089-1094`) mirrors correctly. Exception text quoted in the consumer comment matches `Config.hpp:274` verbatim. `wxEVT_UPDATE_UI` binding confirmed at `wxExtensions.cpp:176`. Tooltip removal of the "By object print sequence" line is justified — the gate runs in `is_overriddable` which is reached from `collect_extruders`/`mark_wiping_extrusions` independent of print sequence. All new comments follow WHY-not-WHAT.

## Critical

None.

## High

None.

## Medium

None.

## Low

### L1 — Consumer comment slightly overstates crash trigger surface
**Location:** `src/slic3r/GUI/GUI_Factories.cpp:1093-1094`
**Issue:** The comment says the throw "crashes the app on any right-click that builds the object menu." Technically the throw fires when wxWidgets dispatches the first `wxEVT_UPDATE_UI` to the bound items — which happens immediately on menu show, so the practical effect is "on right-click." The phrasing is correct in user-visible terms; just noting that the strict event-loop trigger is the `UPDATE_UI` cycle, not the right-click itself. Not worth changing.

### L2 — Contract comments use string anchors instead of line numbers
**Location:** Both contract comments
**Observation:** Both sides use string anchors ("search for FREQ_SETTINGS_BUNDLE_FFF", "append_menu_items_flush_options() below") rather than line numbers. This is correct practice — line numbers drift, identifiers do not. Positive finding.

## Detailed Verification

### 1. Bidirectional contract (PASS)
- Producer (`GUI_Factories.cpp:68-71`): names "flush_into_infill, flush_into_objects, flush_into_support" as the three required `ConfigOptionBool` entries at indices [0]/[1]/[2], constrains future additions to "AFTER index [2] only."
- Consumer (`GUI_Factories.cpp:1089-1094`): references the bundle by name, declares the same index requirement, names the throw text, and identifies the dispatch context (`wxEVT_UPDATE_UI`).
- Line-72 declaration order: `{ "flush_into_infill", "flush_into_objects", "flush_into_support", "flush_into_infill_min_layer" }` — matches producer-comment claim.
- Consumer-function handlers at lines 1126/1143/1160 call `getBool()` on indices [0]/[1]/[2] both in click handlers and (more importantly) inside `check_condition` lambdas passed to `append_menu_check_item` — these lambdas run inside the `wxEVT_UPDATE_UI` handler at `wxExtensions.cpp:176-180`. **Comment claim accurate.**

### 2. Failure-mode accuracy (PASS)
- `src/libslic3r/Config.hpp:274` defines exactly: `throw BadOptionTypeException("Calling ConfigOption::getBool on a non-boolean ConfigOption");` — comment quotes this verbatim.
- `src/slic3r/GUI/wxExtensions.cpp:175-180` binds `wxEVT_UPDATE_UI` with a lambda that calls both `enable_condition()` and `check_condition()` — the `check_condition` lambdas at lines 1135-1141, 1152-1158, 1169-1175 call `option->getBool()` on the indexed entry. Throw site and binding confirmed.

### 3. Gate code comments in `is_overriddable` (PASS)
- `ToolOrdering.cpp:1566-1568` — "Perimeters etc. on a dedicated-purge object are always overridable (upstream behavior). Infill (any role==erInternalInfill) — including infill of a dedicated-purge object — falls through to the min_layer gate below." Verified: line 1569 returns true for `flush_into_objects && role != erInternalInfill`, which preserves the upstream short-circuit for non-infill while letting infill (including on dedicated-purge objects) reach the gate at line 1582.
- `ToolOrdering.cpp:1578-1581` — "The gate lives inside is_overriddable (not at outer call sites) so collect_extruders, mark_wiping_extrusions, and ensure_perimeters_infills_order all see the same eligibility decision." This is a WHY justification for placement choice. Verified call sites: `is_overriddable` is invoked from each of these three contexts (R3 architectural review confirmed this in earlier round).
- `ToolOrdering.cpp:1589` — "slicing parameters not finalized — fail closed rather than gate against stale data." `SlicingParameters::valid` field exists at `Slicing.hpp:54` (default false). Comment correctly flags WHY fail-closed semantics.

### 4. Tooltip (PASS)
- Remaining claims verified against code:
  - "counted from 1, excluding raft" — code: `object_local_layer = this_layer->id() - raft_layers`; check `object_local_layer < min_layer - 1`. With `min_layer=1`, the comparison is `< 0` (size_t), always false → layer 1 onward qualifies. Matches "counted from 1."
  - "excluding raft" — explicit `raft_layers` subtraction at line 1597. Matches.
  - "Set to 0 to allow purging on all layers" — `if (min_layer > 0)` at line 1583 short-circuits the gate. Matches.
  - "Applies object-wide (not per-region)" — `flush_into_infill_min_layer` lives at `PrintConfig.hpp:1006` inside the `PrintObjectConfig` `PRINT_CONFIG_CLASS_DEFINE` block (started at line 918). Matches.
- Removed line ("Has no effect when By object print sequence is active with multiple extruders") — the gate is enforced in `is_overriddable`, which is invoked unconditionally from override-eligibility collection paths regardless of print sequence. The removed claim was speculative and inaccurate. Removal justified.

### 5. WHY-vs-WHAT discipline (PASS)
Every new comment in this round explains rationale, constraint, or invariant — not mechanics:
- Producer/consumer contract comments: state the invariant and the constraint for future modifications.
- Perimeters comment: explains semantic preservation versus upstream.
- Gate-placement comment: explains the consistency reason for centralization.
- Slicing-parameters-valid comment: explains the fail-closed rationale.

No WHAT-only comments introduced.

## Verdict
Comments verified accurate, bidirectional, well-anchored, and WHY-focused. No blocking issues. Two trivial low-severity notes documented; neither requires action.
