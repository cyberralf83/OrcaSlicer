# W2-R6 — pr-review-toolkit:type-design-analyzer

Agent ID: a330d46e3e9a98aa2

## Ratings
- Encapsulation: 5/10 — raft-offset arithmetic leaked to callers
- Invariant Expression: 4/10 — two axes (enabled/threshold) collapsed into one int; 0 overloaded
- Invariant Usefulness: 7/10 — user-facing semantic is genuinely useful
- Invariant Enforcement: 4/10 — bounds enforced at config layer but precondition is "doc + debug-log + fail-closed"

## Findings

### High
- **H1: Signed/unsigned mismatch.** `Layer::id()` is `size_t` (Layer.hpp:127); `raft_layers()` is `size_t` (Slicing.hpp:42). Forcing callers to subtract and cast to `int object_local_layer` invents the wraparound risk that the "negative = impossible" sentinel papers over. **Fix:** change helper to take `const Layer&` (or compute internally) and hoist raft-offset arithmetic inside the helper — single source of truth, no signedness gymnastics.

### Medium
- **M2: Two-axis state collapsed.** `0` overloaded as disabled. A companion `coBool flush_into_infill_min_layer_enabled` would cleanly separate. Not pursued for now; flag as future cleanup.
- **M3: Cohesion drift on `WipingExtrusions`.** The new helper touches no member state — could be a free function in anonymous namespace. Adding as `const` member widens API without strengthening class invariants.
- **M4: `coInt` vs `coIntNullable`.** Nullable would model "unset = inherit/disabled" without overloading 0. Spec's choice is idiom-consistent (`raft_layers`/`enforce_support_layers`) but worth acknowledging.

### Low
- **L5: `max = 5000` soft invariant drift.** Sibling options have varied bounds (none, 5000). Fine.
- **L6: "Fail-closed on negative" is dead code** under spec's contract (raft layers never reach this path). Defensive code for state the type should make unrepresentable — see H1.

### Recommended improvements
- Change helper signature to `bool is_infill_overriddable_at_layer(const PrintObject&, const Layer&) const`, hoist raft-offset inside; drop int/sentinel/log dance.
- Or: free function in ToolOrdering.cpp anonymous namespace to preserve WipingExtrusions cohesion.
