# W2-R1 — feature-dev:code-reviewer (conventions correctness)

Agent ID: a171ea6e2d1630c77

## Findings

### High
- **H1: `mode = comAdvanced` mismatches sibling style.** `flush_into_infill` (PrintConfig.cpp:6854), `flush_into_objects` (:6871), `flush_into_support` (:6863) **all omit** `def->mode` → visible at every UI tier. Adding comAdvanced to the new child hides it in Simple/Standard modes while parent is visible. **Fix:** drop `def->mode = comAdvanced`.
- **H2: `BOOST_LOG_TRIVIAL(debug)` wrong severity.** "Shouldn't happen" guard-trip class matches `SupportMaterial.cpp:1733` which uses `assert`. Codebase uses `trace` for layer-related guards (Layer.cpp:180/194/273, GCode.cpp:5451) and `warning` for unexpected state (GCodeProcessor.cpp:3280/3313). **Fix:** use `warning` (or `trace` + assert).

### Medium
- **M3: OPT_PTR insertion position misdescribed in spec.** Actual order at PrintConfig.hpp:1003-1006 is `flush_into_objects → flush_into_infill → flush_into_support` — NOT alphabetical. Spec says "between flush_into_infill and flush_into_objects" which is literally wrong for that file. **Fix:** insert at line 1006 (after `flush_into_infill`, before `flush_into_support`). Update spec wording.
- **M4: Helper should be `private:` not `public:`.** Matches `first_nonsoluble_extruder_on_layer` / `last_nonsoluble_extruder_on_layer` precedent in WipingExtrusions. **Fix:** move declaration to private section after line 72.

### Low
- **L5: Explicit `static_cast<int>` for raft-offset subtraction.** Matches PrintObjectSlice.cpp:31 idiom (`auto id = int(...raft_layers())`). **Fix:** explicit casts in the computation.

### Not flagged (verified)
- All other fields in the new `def` block follow conventions.
- Translation discipline is correct.
- Field ordering in the def block matches siblings.
