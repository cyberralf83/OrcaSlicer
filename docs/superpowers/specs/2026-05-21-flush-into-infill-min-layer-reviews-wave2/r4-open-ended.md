# W2-R4 — general-purpose (open-ended verification)

Agent ID: a90651d503b243630

## Findings

### Critical
- **C1: Gate over-fires under `flush_into_objects` mode.** Spec line 88 claims "flush_into_objects=true → New helper never reached" — **incorrect for fills**. `is_overriddable()` returns `true` at line 1565 whenever `flush_into_objects` is set, regardless of role. The new call-site gate (filtering on `erInternalInfill`) lives in the FILL loop where every entity is internal-infill — so the gate fires and suppresses fill-purging on bottom layers even when user enabled `flush_into_objects` (not flush_into_infill). **Fix:** gate predicate must also test `object.config().flush_into_infill && !object.config().flush_into_objects`, or restructure around the `wipe_into_infill_only` flag at line 1648.
  File: `src/libslic3r/GCode/ToolOrdering.cpp:1654, 1766`. **NEEDS VERIFICATION & FIX.**

- **C2: Missing `#include <boost/log/trivial.hpp>` in ToolOrdering.cpp.** No existing BOOST_LOG_TRIVIAL use in file. Build fails without the include. **Fix:** add to includes block.

- **C3: size_t underflow guarantee broken without explicit `int()` casts.** `Layer::id()` and `raft_layers()` both return `size_t`. `size_t - size_t` when `id() < raft_layers()` (PrintObjectSlice.cpp:772 renumber edge) wraps to giant positive. Cast to `int` afterwards would give large negative — but if implementer omits the casts, gate becomes a no-op exactly in the edge case it's meant to catch. **Fix:** spec must explicitly require `int(layer->id()) - int(object->slicing_parameters().raft_layers())`.

### High
- **H4: Mode mismatch creates invisible-child UX.** `flush_into_infill` sets no `def->mode` → defaults to `comSimple`. Spec sets new key to `comAdvanced` → users in Simple mode see parent but not gate. **Fix:** drop `def->mode = comAdvanced;` to inherit comSimple. (W2-R1 H1 confirmed by W2-R4 H4 — **2 agents**.)

- **H5: ConfigManipulation predicate diverges from sibling idiom.** Existing `toggle_line("flush_into_objects", !is_global_config)` uses only `!is_global_config`; parent fields gated separately via `toggle_field` at line 896. Spec combines them — functionally fine, stylistically divergent.

### Medium
- **M6: Spec line numbers slightly stale.** Spec refs `:1565`/`:1567` — actual `:1564-1565` and `:1567-1568`. Cosmetic.
- **M7: `bottom_shell_layers` default=3, not 0.** Spec cites it as precedent for "min=0, default=0" — overstated. Real precedents are `raft_layers` (:5045) and `enforce_support_layers` (:6020). Doesn't affect impl.

### Low
- **L8: `Preset.cpp:1126` insertion language ambiguous.** Target line vs line-after. Actionable.
- **L9: 4 call sites of `is_overriddable` (1654, 1678, 1766, 1790).** Sites 1678/1790 iterate perimeters, must NOT receive gate. Sites 1654/1766 iterate fills (every entity is infill), gate applies. Spec only says "two execution sites" — implementer could mistakenly add gate to all 4. **Fix:** spec must disambiguate.
- **L10: `enforce_support_layers` mode is `comDevelop`** — not a UI-visible precedent.
