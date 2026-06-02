# Review 07 — Edge Cases & Regression/Restoration Fidelity

**Commit under review:** `e57ed0375a` — "Fix interlocking beam density not applying (inverted filter axis)"
**Baselines compared:**
- Original (correct) algorithm: `c751da3686:src/libslic3r/Feature/Interlocking/InterlockingGenerator.cpp`
- Buggy interim rewrite: `4e540c4a26`
- Spec: `PLAN-feature2-max-beam-length.md`

**Lens:** (A) faithful-restoration diff, (B) edge/boundary values.
**Method:** code-level diff (comments stripped), full call-chain trace, and an exhaustive C++ simulation of `filterSegment`/`filterRow` over M∈[1,20], G∈[1,20], count∈[1,200], plus every PLAN table row.

---

## Verdict

**CLEAN with one intentional, documented, behavior-neutral deviation.** The restored
`filterCellsForAxis` is code-identical to the original c751da3686 (the bug-free version),
the deleted-by-4e54 includes/algorithm are correctly re-added, no PLAN table row is
violated, and there is no reachable input that crashes, removes all beams, or produces
empty output. The single deviation from c751da3686 (dropping the `if (bidirectional)`
guard around the type-1 filter) is a deliberate, commented change that has **zero effect
on output** — only a small perf cost in unidirectional mode.

---

## (A) Faithful-Restoration Diff

### A1. `filterCellsForAxis` body — IDENTICAL to original. CLEAN.

Stripping comments and blank lines, the function body at HEAD
(`src/libslic3r/Feature/Interlocking/InterlockingGenerator.cpp:411-513`) diffs byte-for-byte
against `c751da3686` (70 code lines each, `diff` empty). The `filterSegment` 5-zone logic,
the `filterRow` contiguous-segment split, the `2*M` short-segment guard, the
`middle_start/middle_end` bounds, the `stride = M+G` middle stride, and both axis branches
(axis 0 → group by (y,z), thin x; axis 1 → group by (x,z), thin y) all match exactly. The
4e54 "global stripe + inverted axis + ceiling /2 scaling" rewrite is fully reverted.

### A2. Includes `<map>` / `<set>` — restored. CLEAN.

4e54 deleted `#include <map>` and `#include <set>` (lines 9-10). HEAD restores both, matching
c751da3686 exactly (`InterlockingGenerator.cpp:8-10`). `std::map` and `std::set` are used by
the restored function, so this is required and correct.

### A3. `generateInterlockingStructure` pre-filter — correctly NOT carried over. CLEAN.

4e54 added a pre-filter block in `generateInterlockingStructure` that ran
`filterCellsForAxis(has_all_meshes, ...)` (with an `if (bidirectional)` union) before
`applyMicrostructureToOutlines`. The original c751da3686 had **no** such pre-filter. HEAD has
no pre-filter either — it replaces that block with an explanatory comment
(`InterlockingGenerator.cpp:206-211`) and calls `applyMicrostructureToOutlines(has_all_meshes,
layer_regions)` directly (line 212). This matches the original. Good: had the 4e54 pre-filter
survived alongside the restored per-axis filter, cells would have been filtered twice (once
unioned, once per-axis), corrupting the result. It did not survive. CLEAN.

### A4. `if (bidirectional)` guard around the type-1 filter — DROPPED. Documented, behavior-neutral. (Low / informational)

**File:** `src/libslic3r/Feature/Interlocking/InterlockingGenerator.cpp:337-340`

- **Original c751da3686 (lines 321-324):**
  ```cpp
  if (density_enabled) {
      filtered_type0_storage = filterCellsForAxis(cells, 0);
      if (bidirectional)
          filtered_type1_storage = filterCellsForAxis(cells, 1);
  }
  ```
- **HEAD (lines 337-340):**
  ```cpp
  if (density_enabled) {
      filtered_type0_storage = filterCellsForAxis(cells, 0);
      filtered_type1_storage = filterCellsForAxis(cells, 1);   // guard removed
  }
  ```

This is the **only** behavioral difference between HEAD and c751da3686 for this feature, and
it is deliberate: the HEAD comment (lines 330-334) states the type-1 set is "unused in
unidirectional mode … but populating it removes a latent empty-set trap."

**Why it is output-neutral:** `filtered_type1` is consulted only at line 364
(`if (layer_type == 1 && !filtered_type1.count(grid_loc)) continue;`). In unidirectional mode,
line 358-359 (`if (!bidirectional && layer_type == 1) continue;`) always fires first, so line
364 is never reached. Whether `filtered_type1_storage` is empty (original) or populated (HEAD)
is therefore unobservable in the output. In the original, `filtered_type1` aliased the empty
`filtered_type1_storage` via the ternary at line 342 — also never consulted. Both produce
identical slices.

- **Severity:** Low (informational). Not a regression; a documented robustness improvement
  that costs one extra `filterCellsForAxis` pass over `cells` in unidirectional + density-on
  mode. The "latent empty-set trap" it guards against is real defensive value should a future
  edit ever consult `filtered_type1` unconditionally.
- **Fix:** None required. If exact perf parity with the original were desired, the
  `if (bidirectional)` guard could be restored — but the current form is safer and the author
  documented the tradeoff. Leave as-is.

### A5. GUI odd→even snap removal — fully removed, consistent. CLEAN.

`src/slic3r/GUI/ConfigManipulation.cpp`: the entire odd-value snap block (the `% 2` test +
`beam_group + 1` / `beam_gap + 1` `set_key_value`/`apply` re-entrancy) is gone. Confirmed no
residual `beam_group % 2`, `beam_gap % 2`, `beam_group + 1`, or `beam_gap + 1` remain. The
non-silent XOR warning (`xor_bad = beam_interlocking_on && (beam_group > 0) != (beam_gap > 0)`,
line 554) and its `m_beam_density_xor_warned` latch (lines 555-564) are intact. This is
correct and required: counts are now plain cell units, so odd values are meaningful and must
not be snapped. Tooltips in `PrintConfig.cpp` (lines ~4248, ~4261) were updated to drop the
"odd values are snapped" wording, matching. CLEAN.

### A6. Config bounds — consistent. CLEAN.

`PrintConfig.cpp`: both `interlocking_beam_group_count` and `interlocking_beam_gap` are `coInt`
with `min=0, max=100`. `min=0` is the disable sentinel and aligns with the
`density_enabled = group > 0 && gap > 0` gate. No bounds regression.

**Summary of every behavioral difference HEAD vs c751da3686 for this feature:** exactly one —
A4 (type-1 filter now unconditional), which is output-neutral.

---

## (B) Edge / Boundary Values

All traces below were confirmed against a faithful C++ port of the HEAD `filterSegment` /
`filterRow` (identical to original).

### B1. `count = 0, 1, 2` — safe.

- `filterSegment` with `count=0`: `0 <= 2*M` (any M≥0) → keep-all loop over an empty vector →
  returns empty. No crash, no division. (Only reachable if a segment were empty, which cannot
  happen — see B7.)
- `count=1`, `count=2` (M=4,G=2): `count <= 2*M` → keep all → `B`, `BB`. Correct.

### B2. `M=1, G=1` — smallest meaningful density. Safe, sensible.

`count=1→B`, `2→BB` (`2<=2*M`), `5→B.B.B` (3 beams), `10→B.B.B.B..B` (5 beams). The last
middle gap merging with the right gap (`..` before the final `B`) is the PLAN-sanctioned
"gap merge," not a bug. Ends always anchored.

### B3. `M` huge vs small segment (`M=100, G=2, count=5`) — keep-all, no crash.

`5 <= 2*100` → returns all 5 → `BBBBB`. The max config value M=100 against any short row simply
keeps everything; no out-of-range indexing (the `count-M` loop is never entered because the
short-segment branch returns first).

### B4. `group >> interface` (`M=50, G=2, count=10`) — keep-all. Safe.

`10 <= 100` → `BBBBBBBBBB`. No beams removed; behaves as "density too coarse for this segment,"
exactly per PLAN ("ends overlap, all beams").

### B5. `equal M=G` (`M=3, G=3, count=20`) — `BBB...BBB...BB...BBB`, 11 beams.

Ends anchored, one truncated middle group. Matches the algorithm; no degeneracy when M==G.

### B6. PLAN gap-merge & truncation tables — ALL match exactly.

Simulation output equals every PLAN row:

| Case | Expected (PLAN) | Sim | Beams |
|------|-----------------|-----|-------|
| M4 G4 c20 | `BBBB....BBBB....BBBB` | match | 12 |
| M4 G4 c25 | `BBBB....BBBB....B....BBBB` | match | 13 |
| M4 G4 c28 | `BBBB....BBBB....BBBB....BBBB` | match | 16 |
| M4 G2 c22 | `BBBB..BBBB..BBBB..BBBB` | match | 16 |
| M4 G2 c20 | `BBBB..BBBB..BB..BBBB` | match | 14 |
| **M4 G2 c18** | `BBBB..BBBB....BBBB` | **match** | 12 |
| M2 G4 c20 | `BB....BB....BB....BB` | match | 8 |
| M2 G4 c16 | `BB....BB......BB` | match | 6 |
| M2 G4 c14 | `BB....BB....BB` | match | 6 |
| M1 G2 c22 | `B..B..B..B..B..B..B..B` | match | 8 |
| M1 G2 c20 | `B..B..B..B..B..B...B` | match | 7 |
| M4 G2 c12 | `BBBB....BBBB` | match | 8 |
| M4 G2 c10 | `BBBB..BBBB` | match | 8 |
| M4 G2 c8  | `BBBBBBBB` | match | 8 |

The flagged `M=4,G=2,count=18` gap-merge case produces `BBBB..BBBB....BBBB` (4-wide merged
gap) — correct, no middle beams lost.

### B7. Empty-row safety in `filterRow` — cannot occur. CLEAN.

`filterRow` dereferences `positions[0]` unconditionally (line 471). It is only ever called from
the two axis loops, iterating a `std::map` whose entries are created exclusively via
`rows[...].push_back(...)`. A map key therefore exists only if ≥1 element was pushed, so every
`positions` vector has size ≥1. `positions[0]` is always valid. No empty-vector UB.

### B8. `density_enabled` correctly prevents `M==0` / `G==0` from reaching the filter — CRITICAL path, verified safe. CLEAN.

`filterCellsForAxis` has exactly two call sites (lines 338, 339), **both inside**
`if (density_enabled)` where `density_enabled = beam_group_count > 0 && beam_gap > 0`
(line 335). Therefore the function is never entered with M==0 or G==0. This matters because:
- With `G==0` (so `M>0`): middle-fill `stride = M+G = M` (nonzero, safe) — but this path is
  unreachable anyway.
- With `M==0`: `count <= 2*M == 0` is false for any non-empty segment, so it would fall through
  to the end loops (harmless) and the middle fill, where `stride = M+G = 0+G = G` (nonzero if
  G>0). The genuinely dangerous combination **M==0 AND G==0** would give `stride = 0` →
  modulo-by-zero in the middle-fill loop. That combination is doubly impossible: it fails
  `density_enabled`, and the only segment that could reach the middle fill needs
  `count > 2*M = 0` with `middle_start (=G) < middle_end`. The gate makes the whole concern
  moot. CLEAN.

### B9. "Remove ALL beams" / empty-output risk — exhaustively ruled out. CLEAN.

Exhaustive simulation over M∈[1,20] × G∈[1,20] × count∈[1,200] (160,000 segments): **zero**
non-empty segments produced an empty keep-set; minimum beams kept = 1 in every case. The
anchored ends guarantee ≥M beams survive any long segment and all beams survive any short one.
No reachable density configuration can erase a boundary segment. (And `generateInterlockingStructure`
returns early when `has_all_meshes.empty()`, so `filterCellsForAxis` is never even called on an
empty cell set.)

### B10. Negative coordinates / modulo — N/A. CLEAN.

The original 4e54 stripe code needed a `if (pos_in_stride < 0) pos_in_stride += stride;`
fixup because it took `perp - min_perp` on raw grid coords. The restored algorithm computes
`pos_in_stride = (i - middle_start) % stride` on **vector indices** `i` (always ≥
`middle_start` ≥ 0), so the result is never negative — no sign-of-modulo hazard. CLEAN.

---

## Conclusion

- **(A) Restoration fidelity:** Faithful. The active algorithm, includes, and absence of a
  pre-filter all match the original c751da3686 exactly. One intentional, documented,
  output-neutral deviation (A4: type-1 filter unconditional). GUI snap correctly removed;
  XOR warning preserved.
- **(B) Edge cases:** No crash, no division-by-zero, no empty/all-removed output across the
  entire reachable parameter space. Every PLAN table row reproduced exactly. M==0/G==0 is
  correctly gated out of `filterCellsForAxis` by `density_enabled`.

**No Critical, High, or Medium findings. One Low/informational finding (A4).**
