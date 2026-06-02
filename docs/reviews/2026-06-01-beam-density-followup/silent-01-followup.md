# Silent-failure review — commit 3f701d6143 (beam-density follow-up)

**Scope:** Silent-failure audit of commit `3f701d6143` "Address beam-density
review: clarify tooltips, restore bidirectional guard". Follow-up to the
interlocking beam-density fix.

**Files reviewed:**
- `src/libslic3r/Feature/Interlocking/InterlockingGenerator.cpp`
- `src/libslic3r/PrintConfig.cpp`

**The only logic change:** `filtered_type1_storage = filterCellsForAxis(cells, 1);`
is now guarded by `if (bidirectional)`. Everything else is comment / tooltip text.

---

## VERDICT: CLEAN. No silent failures introduced or masked by this commit.

The restored `if (bidirectional)` guard is output-neutral and provably safe; the
reworded tooltips are accurate against the algorithm and actually *surface* a
pre-existing silent no-op rather than hide it. Findings below are all
informational / Low, recorded for completeness.

---

## Finding 1 — Restored `if (bidirectional)` guard cannot drop type-1 beams (CLEAN / not a defect)

**Severity:** Low (informational — confirms the guard is safe)
**File:** `src/libslic3r/Feature/Interlocking/InterlockingGenerator.cpp:340-344`
(definition), consumed at `:366`

**Trace.** When `!bidirectional`, `filtered_type1_storage` is left empty, and
`filtered_type1` (line 344) aliases that empty set whenever `density_enabled`.
The concern is whether any code path reads `filtered_type1` while
`!bidirectional` and therefore silently drops all type-1 beams with no error.

There is exactly **one** read of `filtered_type1` in the entire file:

- Consumer loop, line 366:
  `if (layer_type == 1 && !filtered_type1.count(grid_loc)) continue;`

That read is unconditionally preceded, in the same loop body, by the guard at
lines 360-361:

```
// Skip perpendicular layers in unidirectional mode
if (!bidirectional && layer_type == 1)
    continue;
```

`generateMicrostructure()` always builds a 2-element vector
(`cell_area_per_mesh_per_layer.resize(2)`, line 286), so
`layer_type = beam_layer_idx % cell_area_per_mesh_per_layer.size()` (line 357)
is exactly `beam_layer_idx % 2`. Therefore every iteration that would reach the
line-366 read with `layer_type == 1` is `continue`d at line 361 when
`!bidirectional`. The empty `filtered_type1` set is never queried in
unidirectional mode.

The **bottom region loop** (lines 386-410) does **not** read `filtered_type1`
(or `filtered_type0`) at all — it consumes the already-populated
`structure_per_layer`, and has its own equivalent guard at line 394
(`if (!bidirectional && (beam_layer_idx % 2) == 1) continue;`). So density
filtering is confined to the consumer loop, where it is fully guarded.

**Conclusion.** No silent beam drop. Output is byte-identical to the prior
"populate unconditionally" version; the guard only removes a redundant
`filterCellsForAxis` pass in unidirectional mode, exactly as the commit message
claims ("output-neutral"). Not a silent failure.

**Note (Low, pre-existing, not introduced here):** the safety of the empty set
now rests entirely on the line-360 guard. If a future change ever reorders the
density check (line 366) above the unidirectional guard (line 360), or adds a
new reader of `filtered_type1` that is not behind the `layer_type==1` /
`bidirectional` guard, the empty set would silently drop all type-1 beams with
no log and no error. There is no assertion or comment co-locating the two as an
invariant beyond the inline comment at lines 331-335. Optional hardening: add a
`assert(bidirectional || filtered_type1.empty())`-style note, or keep the read
defensively gated. Not required for this commit.

---

## Finding 2 — Tooltip "(of each filament)" does not over-promise (CLEAN)

**Severity:** Low (informational)
**File:** `src/libslic3r/PrintConfig.cpp:4250` (group count), `:4263` (gap)

The reworded tooltips add "(of each filament)" to both the group-count and gap
counts. Verified against the algorithm: `filterCellsForAxis` thins in **cell**
units, and each surviving cell produces a beam for **both** meshes
(`for mesh_idx 0..1`, line 350) — one tooth of filament A and one of filament B
per cell. So "M consecutive beams of each filament per group" and "G beams of
each filament skipped between groups" are literally what the code does. The unit
clarification is accurate, not an over-promise. No GUI/slicer inconsistency.

---

## Finding 3 — Tooltip "very short boundaries may keep every beam" SURFACES a pre-existing silent no-op (CLEAN / improvement)

**Severity:** Low (informational — this is the *fix* for a latent silent no-op)
**File:** `src/libslic3r/PrintConfig.cpp:4252`
**Code it describes:** `InterlockingGenerator.cpp:436-440` (`filterSegment`:
`if (count <= 2 * M) { keep all }`)

Before this commit, the short-segment keep-all branch was undocumented: a user
enabling density control on a model whose two-filament boundary decomposes into
many short contiguous segments would silently get "all beams placed" (no
thinning) with no indication why. The new clause "very short boundaries may keep
every beam" explicitly states this. The hedge "may" is correct because it is
per-segment (`filterRow` splits a row into contiguous segments and applies the
5-zone rule independently), and depends on `M`. This *reduces* the GUI/behavior
gap rather than masking it. Not a silent failure; an improvement.

---

## Finding 4 — "Both ends ... always get a full group" / "middle filled left-to-right" remain accurate (CLEAN)

**Severity:** Low (informational)
**File:** `src/libslic3r/PrintConfig.cpp:4251`
**Code:** `InterlockingGenerator.cpp:437-460`

- Short segment (`count <= 2*M`): keep-all, so both ends trivially full.
- Long segment (`count > 2*M`): left `[0,M)` and right `[count-M,count)` are
  inserted; `count > 2*M` guarantees `count-M > M`, so the two end groups never
  overlap and each is a full M-group. Matches "both ends always get a full
  group."
- Middle: iterated ascending over sorted positions with stride `M+G`, keeping
  the first `M` of each stride — i.e. left-to-right, last group truncated at
  `middle_end`. Matches "middle filled left-to-right."

No inconsistency between tooltip and slicer behavior.

---

## Summary

The single logic change (the `if (bidirectional)` guard) is safe and
output-neutral: the empty `filtered_type1` set is never read in unidirectional
mode because the pre-existing `layer_type==1` continue (line 360) always fires
first, and `layer_type` is provably `beam_layer_idx % 2`. The tooltip rewordings
are accurate to the algorithm and surface (do not mask) the previously-silent
short-segment keep-all behavior. No Critical/High/Medium silent-failure issues.
One Low/optional hardening note recorded under Finding 1.
