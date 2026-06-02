# Review 01 — Algorithm & Geometry Correctness

**Commit under review:** `e57ed0375a` "Fix interlocking beam density not applying (inverted filter axis)"
**Reviewer lens:** Algorithm & geometry correctness (re-derived from first principles)
**Date:** 2026-06-01
**Files in scope:**
- `src/libslic3r/Feature/Interlocking/InterlockingGenerator.cpp`
- `src/libslic3r/Feature/Interlocking/InterlockingGenerator.hpp`
- `src/libslic3r/PrintConfig.cpp` (tooltips)
- `src/slic3r/GUI/ConfigManipulation.cpp` (beam-density GUI block)

---

## Verdict

The fix is **correct**. The restored 5-zone filter reproduces the PLAN tables exactly (14/14
documented cases plus 20,000 random end-anchor invariant checks pass), and the axis is now
genuinely correct, not merely "changed": for the cell geometry produced by
`generateMicrostructure`, type-0 beams run along grid-Y and are separated along grid-X, so
thinning along X (axis 0) is the only choice that reduces the number of parallel beams. The
previous `4e540c4a26` version thinned along the beam *run-direction*, which left the parallel
beam count unchanged — exactly the reported symptom.

No Critical or High findings. Two Low findings (a redundant computation in unidirectional mode
and pre-existing comment imprecision). Each correctness category checked is called out
explicitly below.

---

## Geometry re-derivation (basis for all axis findings)

`generateMicrostructure` (cpp:282-310) builds `cell_area_per_mesh_per_layer`:

- **Type-0** (`[0]`): per cell, mesh0 = rectangle `x∈[0,middle], y∈[0,cell.y]`; mesh1 =
  `x∈[middle,cell.x], y∈[0,cell.y]`. The split is **along X**; each strip spans the **full Y**
  extent of the cell.
- **Type-1** (`[1]`): same polygons with x/y swapped (cpp:301-308) → split along Y, full X.

Consequences for a type-0 layer:
- Stacking cells with the **same x, increasing y** unions strips into one continuous beam that
  runs **along Y**. ⇒ beam run-direction = Y.
- Distinct beams of the same mesh are separated **along X** (each X column is its own beam).
  ⇒ to reduce the *number of parallel beams*, thin along **X**.

`toLowerCorner`/`toGridCoord` (VoxelUtils.hpp:178-193) confirm grid axis k advances by
`cell_size_[k]` per unit, so "grid X+1" is the physically adjacent cell along X, and a z step is
one `2*beam_layer_count` group. This validates `filterRow`'s "consecutive coords differ by 1"
contiguity test and the inclusion of z in the row key (different z = different beam-layer group,
correctly never merged).

Mapping in `applyMicrostructureToOutlines`:
- `layer_type = beam_layer_idx % 2` selects `cell_area_per_mesh_per_layer[layer_type]`.
- `layer_type==0` → `filtered_type0` = `filterCellsForAxis(cells, 0)` = group by (y,z), thin x.
- `layer_type==1` → `filtered_type1` = `filterCellsForAxis(cells, 1)` = group by (x,z), thin y.

So type-0 layers (X-split, beams along Y, separated along X) are thinned along X. **Correct.**
Type-1 is the mirror and equally correct.

---

## Findings

### 1. Axis correctness — CLEAN (axis is genuinely fixed, not just changed)
**Severity:** N/A (confirmation)
**File:** InterlockingGenerator.cpp:486-512

`axis==0` groups by `(y,z)` and thins along `x`; `axis==1` groups by `(x,z)` and thins along `y`.
Cross-checked against the geometry derivation above and against the buggy
`4e540c4a26` body, which for `axis==0` keyed the stripe on `cell.y()` (thin along Y =
run-direction = no change to parallel count). The pairing
`filterCellsForAxis(cells,0) ↔ layer_type 0 ↔ cell_area...[0]` is internally consistent. The
inversion is genuinely corrected.

### 2. 5-zone segment math matches PLAN tables exactly — CLEAN
**Severity:** N/A (confirmation)
**File:** InterlockingGenerator.cpp:428-461

`filterSegment` was simulated against every pattern in PLAN-feature2-max-beam-length.md
(M=4/G=4 at counts 20/25/28; M=4/G=2 at 22/20/18/12/10/8; M=2/G=4 at 20/16/14; M=1/G=2 at
22/20). All 14 patterns and beam counts match bit-for-bit, including the documented
gap-merge-with-right-end behavior (e.g. M=4/G=2/count=18 → `BBBB..BBBB....BBBB`) and last-group
truncation (M=4/G=4/count=25 → trailing `B` then right end). The "combined gap wider than G is
expected" note holds and never drops an end beam.

### 3. End-anchor / short-segment / truncation invariants — CLEAN
**Severity:** N/A (confirmation)
**File:** InterlockingGenerator.cpp:434-458

- Short segment (`count <= 2*M`) keeps all positions (cpp:435-438) — matches PLAN "ends overlap".
- For `count > 2*M`, both ends always receive M beams (cpp:441-446). Verified across 20,000
  random (M,G,count) draws: zero violations of the end-anchor and short-segment invariants.
- Middle fill is guarded by `middle_start < middle_end` (cpp:451), so counts between `2*M+1` and
  `2*(M+G)` correctly produce no middle (e.g. M=4/G=4/count=16 → `BBBB........BBBB`), with no
  negative-range iteration or out-of-bounds access.

### 4. Row split / segment feeding into structure_per_layer & writeback — CLEAN
**Severity:** N/A (confirmation)
**File:** InterlockingGenerator.cpp:362-371, 466-512

The filtered sets are consulted as a pure membership gate in the inner loop
(`if (layer_type==0 && !filtered_type0.count(grid_loc)) continue;`), and surviving cells produce
their polygons unchanged. Because filtering is by grid coordinate before any polygon math, beam
geometry, union, rotate-back (cpp:376-382), and the diff/union slice writeback (cpp:404-406) are
all untouched by the change. The outer loop still iterates the full `cells` set (intentional: a
cell may survive for one layer type and not the other), consistent with the per-axis design.

### 5. Rotation / grid-frame interaction — CLEAN
**Severity:** N/A (confirmation)
**File:** InterlockingGenerator.cpp:225-231, 317, 346-371

Cells are voxelized in the rotated frame (`expolygons_rotate(..., rotation)` before
`addBoundaryCells`), and the resulting structure is rotated back by `-rotation` after assembly.
`filterCellsForAxis` operates entirely in grid (rotated) coordinates, so thinning is performed
along the true beam-grid axes regardless of `interlocking_orientation`. No frame mismatch.

### 6. No empty-vector dereference in filterRow
**Severity:** N/A (confirmation)
**File:** InterlockingGenerator.cpp:466-484

`filterRow` dereferences `positions[0]` and `positions.back()` semantics without an emptiness
guard, but it is only ever called on a vector pulled from `rows[...]`, and every map entry is
created via `push_back` of at least one cell (cpp:490 / 503). A row therefore always has ≥1
element. Safe as wired; the guard is unnecessary.

### 7. `filterCellsForAxis` only invoked when density enabled (M>0 && G>0) — CLEAN
**Severity:** N/A (confirmation)
**File:** InterlockingGenerator.cpp:335-340; ConfigManipulation.cpp:551-554

The only call site is gated on `density_enabled = beam_group_count > 0 && beam_gap > 0`. This
matters because `filterSegment` with `M==0` would key out *every* beam (`pos_in_stride < 0` never
true → empty keep set), and `G==0` degenerates to keep-all. Both pathological inputs are
unreachable. The GUI XOR warning (ConfigManipulation.cpp:554-565) surfaces the
exactly-one-is-zero misconfiguration instead of silently producing a solid comb. The removal of
the odd→even snap is consistent with the algorithm now treating counts as plain cell units; the
PrintConfig tooltips (PrintConfig.cpp:4247-4263) were updated to match.

### 8. Redundant filtered_type1 computation in unidirectional mode
**Severity:** Low
**File:** InterlockingGenerator.cpp:336-339

The new code computes `filtered_type1_storage = filterCellsForAxis(cells, 1)` unconditionally
when density is enabled. In unidirectional mode (`!bidirectional`), `layer_type==1` is skipped at
cpp:358-359 *before* `filtered_type1` is ever consulted, so the type-1 filter result is computed
and never read. The pre-regression `c751da3686` version and PLAN.md guarded this with
`if (bidirectional)`. This is a pure performance cost (one extra full pass over `cells` plus map
construction) — **not** a correctness bug. The in-code comment (cpp:330-334) frames it as removing
"a latent empty-set trap," but the trap does not exist: `filtered_type1` is dereferenced only via
`.count()`, which is well-defined on an empty set, and is unreachable in unidirectional mode
anyway.
**Suggested fix (optional):** restore `if (bidirectional) filtered_type1_storage = filterCellsForAxis(cells, 1);`
to avoid the wasted pass on large unidirectional jobs.

### 9. Comment imprecision: "each cell = one tooth of each material"
**Severity:** Low
**File:** InterlockingGenerator.cpp:416, 171 (hpp); ConfigManipulation.cpp:549

The comments describe a cell as "one interlocking tooth of each material." Geometrically a cell
(`cell_size.x = 2*beam_width`) holds one mesh-A strip *and* one mesh-B strip side by side
(`middle` split at cpp:288-289), i.e. one tooth **per** material, which is what the prose intends
but reads ambiguously ("one tooth of each material" could be read as a single shared tooth). Since
density is counted in whole cells, M=1 keeps one A-strip + one B-strip per group; the unit is the
A+B *pair* (one cell), so users counting visible same-material beams see the count, not 2×. This
is cosmetic wording only and does not affect behavior.
**Suggested fix (optional):** "one cell = one A+B beam pair" for clarity.

---

## Categories explicitly confirmed clean

- **Type-0 vs type-1 finger direction vs thinned coordinate:** correct (Findings 1, geometry
  derivation).
- **5-zone math vs PLAN tables:** exact match (Finding 2, simulated).
- **Truncation / gap-merge / short-segment behavior:** correct (Findings 2, 3).
- **Filtered cells → structure_per_layer → slice writeback:** correct (Finding 4).
- **Rotation / grid-frame interaction:** correct (Finding 5).
- **Memory safety (empty rows, OOB, negative ranges):** safe (Findings 3, 6).
- **Type compatibility (`grid_coord_t == coord_t`):** confirmed; all containers line up.
- **Disabled-feature gating (M=0 / G=0):** unreachable pathologies, properly gated (Finding 7).

No Critical, High, or Medium issues found.
