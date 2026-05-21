# W2-R2 — pr-review-toolkit:code-reviewer (PR-style rigorous)

Agent ID: a6446f815b728d05b

## Findings

### Critical
- **C1: Naming has no precedent.** Codebase uses `_layers` (count) and `enforce_*_layers` (first-N) idioms — `_min_layer` appears nowhere. Sibling examples: `bottom_shell_layers`, `top_shell_layers`, `raft_layers`, `enforce_support_layers`. **Suggest:** rename to `flush_into_infill_first_layer` (semantic: "purge enabled starting at this layer, 1-based, raft excluded; 0 = disabled"). Removes the inclusive/exclusive ambiguity noted in spec.

### High
- **H1: `By Object` vs `By object` casing.** Enum label at PrintConfig.cpp:1757 is `L("By object")` lowercase. Spec tooltip uses `"By Object"` — translators search by exact msgid; will not match. **Fix:** rewrite as `By object`.
- **H2: ConfigManipulation toggle pattern.** Existing `flush_into_*` cluster at ConfigManipulation.cpp:895 uses a *loop* iterating `{"flush_into_infill", "flush_into_support", "flush_into_objects"}` with `toggle_field(have_prime_tower)`. Spec's `toggle_line` at line ~911 only gates on `flush_into_infill==true`, NOT on `have_prime_tower`. When prime tower disabled, new field stays enabled while parent greyed. **Fix:** add `have_prime_tower &&` to the predicate. **Note:** spec already says `have_prime_tower && opt_bool("flush_into_infill")` — so this may be a misread; verify spec wording is preserved during implementation.
- **H3: Print.cpp:341 placement.** Line 341-342 lists `flush_into_infill` and `flush_into_support` but **NOT `flush_into_objects`** — because `flush_into_objects` is per-object only. New key is also per-object → symmetric with `flush_into_objects`, NOT with 341 entries. Adding at 341 unnecessarily invalidates `psSkirtBrim`. **Fix:** drop the Print.cpp:341 edit; rely on PrintObject.cpp:1420 only. **NEEDS VERIFICATION.**

### Medium
- **M1: 1-based vs 0-based clarity in spec narration.** Spec mixes "user enters 1-based" and "internal index is `Layer::id() - raft_layers()` which yields 0 for first object layer." Clarify wording.
- **M2: Atomic-commit boundary.** Splits naturally into 2 commits: (a) config+invalidation+preset+GUI (no behaviour change at min_layer=0), (b) ToolOrdering gate logic. Acceptable as one commit; flag.
- **M3: Tooltip bloat.** Spec tooltip is ~3x longer than siblings. **Fix:** trim to 1-2 sentences; move caveats to wiki anchor.

### Low
- **L1: Helper naming** `is_infill_overriddable_at_layer` mixes role + gate axis. Suggest `is_layer_eligible_for_infill_override(object, object_local_layer)`.
- **L2: CLAUDE.md keep-diff-minimal violation.** Reviewer notes this is already accepted by user.
