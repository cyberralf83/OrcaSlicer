# Review 03 — Configuration System Consistency

**Commit under review:** `e57ed0375a` — "Fix interlocking beam density not applying (inverted filter axis)"
**Lens:** Configuration-system consistency across the full option lifecycle for
`interlocking_beam_group_count` and `interlocking_beam_gap`.
**Scope:** READ-ONLY. No source modified. `.github/` excluded.
**Date:** 2026-06-01

---

## Method

Traced both options through every stage of their lifecycle:

1. Definition — `PrintConfig.cpp` (`add(...)`, type, min/max, default, tooltip).
2. Struct membership — `PrintConfig.hpp` (`PrintObjectConfig`).
3. Serialization/preset list — `Preset.cpp` (`s_Preset_print_options`).
4. Invalidation — `PrintObject.cpp` (`invalidate_state_by_config_options` → `posSlice`).
5. GUI wiring — `Tab.cpp` (option lines), `ConfigManipulation.cpp` (validation, toggle).
6. Consumption — `InterlockingGenerator.cpp/.hpp` (gating + filter semantics).
7. Default profiles — `resources/profiles/**`.
8. Stray references to the old even/odd/finger/snap semantics across `src/` and `resources/`.

All grep'd references accounted for:

```
PrintConfig.cpp:4247,4259        definitions
PrintConfig.hpp:1072,1073        PrintObjectConfig members
PrintObject.cpp:1160,1161        posSlice invalidation
Preset.cpp:1264,1265             s_Preset_print_options
InterlockingGenerator.cpp:46,47  consumption (config -> locals)
ConfigManipulation.cpp:552,553   GUI validation
ConfigManipulation.cpp:991,992   toggle_line (enable/disable)
Tab.cpp:2697,2698                option lines on the Other page
```

No other option list (FullPrintConfig, default presets, etc.) references these keys.
No file under `resources/profiles/**` sets either key, so no shipped profile carries a
stale finger-era value that would be re-interpreted under the new cell semantics.

---

## Findings

### F1 — User-facing tooltips say "beams" but the unit is a beam *pair* (cell)
**Severity:** Medium
**File:** `src/libslic3r/PrintConfig.cpp:4249` (group_count) and `:4261` (gap)

**What the code actually does.** The filter (`filterCellsForAxis`) keeps/drops whole
*grid cells*. `generateMicrostructure()` (InterlockingGenerator.cpp:282–309) splits each
cell along the run direction into a mesh-A sub-region of width `middle` and a mesh-B
sub-region of width `cell - middle` — i.e. **one cell contains one beam of material A and
one beam of material B** (`cell_width = beam_width + beam_width`, InterlockingGenerator.cpp:60).
The fix's own comments acknowledge this: ConfigManipulation.cpp:549 says "one cell = one
tooth of each material," and the `.hpp` doc (InterlockingGenerator.hpp:171) says "one cell =
one interlocking tooth of each material." So `beam_group_count = 2` keeps 2 *cells* = 2
A-beams **plus** 2 B-beams (4 beam segments), and `beam_gap = 2` skips 2 cells = a 2-cell-wide
hole in both materials.

**Why it matters.** The two user-visible tooltips now read:
- "Number of consecutive **interlocking beams** to keep per group" (4249)
- "Number of **interlocking beams** to skip between groups" (4261)

A user reading "beams" will expect single-beam granularity, but the actual granularity is a
pair (a cell). This is the exact ambiguity the old "finger" wording + `/2` scaling existed to
paper over. Dropping the `/2` is correct; the residual inconsistency is purely terminological:
the developer-facing comments call the unit a "cell/tooth-pair," while the user-facing tooltips
call it a "beam." For a `coInt` with no unit suffix, the tooltip is the only place the unit is
communicated, so the wording is load-bearing.

**Fix.** Make the user-facing unit match the internal unit. Either:
- Say "cells" (each cell = one A-beam + one B-beam pair), e.g.
  "Number of consecutive interlocking cells (each cell is one beam of each material) to keep
  per group," **or**
- Keep "beams" but state explicitly that a kept/skipped unit affects both materials together.

Pick whichever matches the label "Beam group count" the team prefers, but align the two
tooltips with each other and with the ConfigManipulation/`.hpp` comments so all four
descriptions name the same unit.

---

### F2 — `interlocking_beam_group_count` keeps `max = 100`, a leftover bound from the finger era
**Severity:** Low
**File:** `src/libslic3r/PrintConfig.cpp:4254`

**History.** The option was introduced (commit `c751da3686`) with **no max** on
`group_count` (only `gap` had `max = 100`). The `max = 100` on `group_count` was added in
`1d831afa88`, the same commit that introduced the finger semantics + `/2` scaling. Under that
model, `max = 100` fingers mapped to `ceil(100/2) = 50` effective cells.

**Why it matters.** Now that the `/2` is gone, `max = 100` is interpreted as **100 cells**
directly, so the effective upper bound silently doubled (50 → 100 cells). This is not a
correctness bug — `coInt` clamps to `[0,100]`, no overflow, the filter handles any positive
`M`/`G`, and a larger bound is harmless. It is only a consistency landmine: the bound was
chosen for a unit that no longer exists, and `group_count` is now bounded while it originally
was not. If the intent is to preserve the prior effective ceiling, `max` should be `50`;
otherwise leaving `100` is fine but the value is now arbitrary rather than derived.

**Fix.** Decide intentionally: keep `100` (document it as "max 100 cells") or restore the
original "no max on group count." Low priority — flagging so the bound is a deliberate choice,
not an accident of the removed scaling.

---

### F3 — Stale "finger" term survives in an internal code comment
**Severity:** Low
**File:** `src/libslic3r/Feature/Interlocking/InterlockingGenerator.cpp:418`

The comment reads: "The fingers of a type-0 (even) beam layer repeat along grid-X ...". The
commit message states the finger→cell unit was dropped, and every user-facing string was
updated, but this internal comment still uses "fingers." It is geometrically accurate (a cell
does contain finger-shaped sub-regions) and is not user-visible, so it is not a config bug.
Noting only for terminology hygiene, since the lens asked to flag any remaining "finger"
mention. (The "(even)/(odd)" wording in the `.hpp` doc at lines 166–167 is *correct* — it
refers to alternating `layer_type` parity, `beam_layer_idx % 2`, not the old beam-pairing
rule — so it is **not** a finding.)

**Fix.** Optional: reword to "sub-regions"/"teeth" for consistency with the new vocabulary.

---

## Items verified clean (no finding)

- **Struct membership** — `PrintConfig.hpp:1072–1073`: both are `ConfigOptionInt` members of
  `PrintObjectConfig` (block opens at line 917). Correct class: interlocking is a per-object
  feature, and the sibling `interlocking_*` keys live in the same block. Not touched by the
  commit, still correct.
- **Preset option list** — `Preset.cpp:1264–1265`: both present in `s_Preset_print_options`,
  grouped with the other `interlocking_*` keys. Settings will be saved/loaded with print
  presets. Unchanged by the commit, still correct.
- **Invalidation** — `PrintObject.cpp:1160–1161`: both keys are in the `opt_key == ...` chain
  that `steps.emplace_back(posSlice)`. Changing density still re-slices, which is required —
  the filter runs inside `generateInterlockingStructure()` reached from the slice step.
  Correct and unchanged.
- **Consumption** — `InterlockingGenerator.cpp:46–47`: read straight into `int beam_group_count`
  / `int beam_gap` and passed to `applyMicrostructureToOutlines`; gating is
  `density_enabled = beam_group_count > 0 && beam_gap > 0` (line 335). This exactly matches what
  both tooltips and the ConfigManipulation comment claim ("both must be > 0 to enable").
  Consistent.
- **GUI gating** — `ConfigManipulation.cpp:554`, `:991–992`: the XOR warning fires precisely
  when `(group>0) != (gap>0)`, matching the `&&` gate in the generator; `toggle_line` enables
  both fields with `use_beam_interlocking`. The odd→even snap block was fully removed (no
  partial leftover). `m_beam_density_xor_warned` reset path (line 563–564) intact. Consistent.
- **Min / default** — both `min = 0`, default `0` (density off). Matches "Set to 0 to disable."
  `min = 0` is correct since 0 is the documented disable value. Unchanged.
- **Tooltip ↔ gating semantics** — "Set to 0 ... all beams placed" is correct for *both* keys:
  with either at 0, `density_enabled` is false and every cell passes. Internally consistent.
- **Default profiles** — no `resources/profiles/**` file sets either key, so the unit change
  cannot silently re-interpret any shipped value. Clean.

---

## Verdict

The wiring is fully intact: both options are correctly typed, in `PrintObjectConfig`, in the
preset list, invalidate `posSlice`, are consumed with a gate that matches their tooltips, and
the GUI snap removal is complete with no orphaned code. No Critical/High issues.

The one substantive consistency gap is **F1 (Medium)**: the user-facing tooltips call the unit
"beams" while the implementation and every developer-facing comment call it a "cell" /
"tooth-pair" (one A-beam + one B-beam). Since the option has no unit suffix, the tooltip is the
only unit signal the user gets, so it should name the same unit as the code. F2 (stale `max=100`
bound) and F3 (leftover "finger" comment) are Low cleanup items.
