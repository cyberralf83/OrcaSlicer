# V1 — Gate-restructure correctness verification

Agent: feature-dev:code-reviewer
Note: this agent had no Write tool — orchestrator persisted findings on its behalf from the returned summary.

## Scope

`src/libslic3r/GCode/ToolOrdering.cpp` lines 1561-1605 — function `WipingExtrusions::is_overriddable`. Compares the post-fix HEAD code against upstream (pre-fix) behavior and against the spec test cases in `docs/superpowers/specs/2026-05-21-flush-into-infill-min-layer-design.md`.

## Upstream (pre-fix) logic

```cpp
if (soluble) return false;
if (flush_into_objects) return true;              // no role check
if (!flush_into_infill || role != erInternalInfill) return false;
[min_layer gate]
return true;
```

## Post-fix HEAD logic

```cpp
if (soluble) return false;
if (flush_into_objects && (role != erInternalInfill || !flush_into_infill))
    return true;
if (!flush_into_infill || role != erInternalInfill) return false;
[min_layer gate]
return true;
```

(The HEAD shown above reflects the *final* fix after V1's I1 was applied — i.e., the `!flush_into_infill` guard inside the `flush_into_objects` branch that preserves upstream behavior for the dedicated-purge / no-flush-into-infill case.)

## Truth table

Abbreviations: F2O = `flush_into_objects`, F2I = `flush_into_infill`, INF = `erInternalInfill`, NON = any non-infill role, ML0 = `min_layer=0`, ML_LO = `min_layer>0` with `layer<min`, ML_HI = `min_layer>0` with `layer>=min`. Soluble=true rows always return false in both versions and are omitted.

| F2O | F2I | Role | min_layer | Upstream | Post-fix | Divergence? |
|-----|-----|------|-----------|----------|----------|-------------|
| T | T | NON | * | true | true | NONE |
| T | T | INF | ML0 | true | true | NONE |
| T | T | INF | ML_LO | true | **false** | **YES — intentional fix (spec test 6)** |
| T | T | INF | ML_HI | true | true | NONE |
| T | F | NON | * | true | true | NONE |
| T | F | INF | ML0 | true | true | NONE |
| T | F | INF | ML_LO | true | true | NONE (V1 I1 fix preserves upstream) |
| T | F | INF | ML_HI | true | true | NONE |
| F | T | NON | * | false | false | NONE |
| F | T | INF | ML0 | true | true | NONE |
| F | T | INF | ML_LO | false | false | NONE (both gate) |
| F | T | INF | ML_HI | true | true | NONE |
| F | F | NON | * | false | false | NONE |
| F | F | INF | * | false | false | NONE |

Only one cell diverges from upstream: the SF2 C1 fix that satisfies spec test case 6.

## `something_overridable` propagation

`is_overriddable_and_mark` (ToolOrdering.hpp:48-51) does `something_overridable |= is_overriddable(...)`. After the restructure:

- For F2O=T objects: perimeters (NON role) return true via the first branch, so `something_overridable=true` is set before any infill entity is processed. Downstream passes (`mark_wiping_extrusions`, `ensure_perimeters_infills_order`) correctly run.
- For F2O=F, F2I=T, INF, layer>=min: returns true. `something_overridable=true`.
- For any case returning false: `something_overridable` is not set by that entity. If no entity on the layer is overridable, the downstream loops return early (lines 1631 and 1773). Correct.
- For the F2O=T, F2I=T, INF, ML_LO case (the one we now correctly deny): `something_nonoverriddable=true` in the planning pass at line 710, `sparse_infill_filament` is added to `lt.extruders`, infill is printed with its original filament. No orphaned-extruder hazard.

## Critical

None. The core restructure is correct. SF2 C1 is properly fixed. No regression for `min_layer=0` (the default).

## Important — addressed in the final fix

### I1 — F2O=T, F2I=F, role=INF, min_layer>0 — unintended gate application

In the intermediate version of the fix, this combination caused infill on dedicated-purge objects to be gated even when the user never enabled `flush_into_infill`. That broadened the gate's scope beyond what the spec covered.

**Resolution:** the final HEAD code adds `!flush_into_infill` to the `flush_into_objects` branch condition so that F2O=T, F2I=F objects bypass the gate entirely and preserve upstream behavior.

### I2 — log level for the impossible-invariant warning

The intermediate fix demoted `BOOST_LOG_TRIVIAL(warning)` to `(debug)` to address SF2 H4's log-spam concern. V1 noted that the condition is documented as "impossible under normal flow" and demotion silently disables an important tripwire in release builds.

**Resolution:** the final HEAD code restores `warning` level (per spec) and accepts the theoretical spam tradeoff for a condition that should never fire.

## Summary

The gate-logic restructure at lines 1561-1605 correctly satisfies all six spec test cases without regressing any upstream behavior. `something_overridable` propagation through `is_overriddable_and_mark` is correct for every truth-table cell. `mark_wiping_extrusions` and `ensure_perimeters_infills_order` cannot be induced into infinite loops, missing-extruder hazards, or incorrect force-overrides by the restructured logic. Two important-level findings (I1, I2) raised in V1's initial pass were addressed in the final HEAD code before commit.

## Files reviewed

- `src/libslic3r/GCode/ToolOrdering.cpp` lines 1560-1605, 1626-1762, 1771-1828, 670-726
- `src/libslic3r/GCode/ToolOrdering.hpp` lines 47-95
- `docs/superpowers/specs/2026-05-21-flush-into-infill-min-layer-design.md`
- `docs/superpowers/specs/2026-05-26-flush-into-infill-min-layer-crash-fix-reviews/sf2-silent-failures-state.md`
