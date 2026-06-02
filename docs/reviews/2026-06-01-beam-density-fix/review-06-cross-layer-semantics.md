# Review 06 — Cross-Layer Semantic Consistency

**Commit under review:** `e57ed0375a` "Fix interlocking beam density not applying (inverted filter axis)"
**Files:** `src/libslic3r/Feature/Interlocking/InterlockingGenerator.cpp` / `.hpp`, `src/libslic3r/PrintConfig.cpp`, `src/slic3r/GUI/ConfigManipulation.cpp`
**Lens:** Verify the meaning of `interlocking_beam_group_count` and `interlocking_beam_gap` is consistent UI → config → algorithm, and that this change composes with adjacent interlocking features (`bidirectional`, `skip_layers`, `depth`, `boundary_avoidance` air filtering, the `density_enabled` gate, and `max=100`).

---

## Summary of established geometry (basis for findings)

From `generateMicrostructure()` (InterlockingGenerator.cpp:282–310):

- `cell_size.x == cell_size.y == 2 * beam_width` (set in `generate_interlocking_structure`, line 60).
- `middle = cell_size.x() * beam_width / (2*beam_width) = beam_width`.
- Therefore a **type-0 cell** contains exactly **two** material strips side by side along X, each `beam_width` wide and spanning the full cell in Y: one strip of mesh-0 (`[0, beam_width]`) and one of mesh-1 (`[beam_width, 2*beam_width]`).
- Stacking cells along Y (same X) joins same-material strips into one continuous beam running in Y; cells adjacent in X give **parallel** beams. So "number of parallel beams" in a type-0 layer = number of distinct X columns. **Thinning along X (axis 0) is the correct axis** to change the parallel-beam count. Type-1 is the mirror (thin along Y). The commit's central claim — that `4e540c4a26` inverted the axis and this restores the correct one — is **verified correct**.

This means: **one cell = one tooth of each material = one A-beam + one B-beam (a tooth-pair), i.e. two visible beams.** `filterCellsForAxis` keeps/drops whole cells, so its unit (M, G) is cells = tooth-pairs.

---

## Findings

### Finding 1 — Tooltip says "beams"; the algorithm counts cells (tooth-pairs) — 2× mismatch between user word and result
**Severity: Medium** (surprising-but-arguably-documented landmine; not a wrong-result bug because the algorithm is internally consistent, but the user-facing word does not denote what the user sees)
**Files:**
- `PrintConfig.cpp:4249–4252` (group_count tooltip), `:4261–4263` (gap tooltip)
- `InterlockingGenerator.cpp:414–416, 428–461` (M/G applied as cell counts)

**Why.** The group-count tooltip reads *"Number of consecutive interlocking **beams** to keep per group"* and the gap tooltip reads *"Number of interlocking **beams** to skip between groups."* The algorithm applies M and G as **cell** counts (`filterSegment`, lines 431–432: `M = beam_group_count; G = beam_gap;` indexing `positions`, where each position is one cell). One cell renders as a tooth-pair = **two** visible beams (one of each material, A then B). So with `group_count = 2`, the user keeps 2 *cells* = a contiguous band of **4** beams (A B A B); with `gap = 2`, the skipped band is **4** beams wide. The user-facing number is off by a factor of two from the literal count of "beams" a user would point at in the preview.

Notably the two *code* comments are internally honest and correct — `InterlockingGenerator.cpp:416` and the `ConfigManipulation.cpp:549` comment both say *"one cell = one tooth of each material"* / *"one cell = one interlocking tooth of each material."* The inconsistency is purely that the **tooltip** kept the word "beams" while the unit silently became "cells/tooth-pairs." This is the same conceptual gap the deleted `4e540c4a26` code tried (clumsily) to paper over with the finger→cell `/2` and even-snap; removing the `/2` was correct math, but the tooltip noun was not updated to match the new unit.

**Could a user be surprised?** Yes. A user who sets `group=1, gap=1` expecting "1 beam on, 1 beam off" (a 50%-ish comb of single beams) instead gets "1 tooth-pair on, 1 tooth-pair off" = bands of 2 beams alternating with gaps of 2 beams. The *density* is still ~50%, so the headline effect is right, but the *granularity* is 2× coarser than the wording implies. This is a real "reading the tooltip surprises you" case, which is exactly the Medium bar.

**Fix (doc-only, no code-behavior change).** Change the tooltip noun from "beams" to "cells" (or "tooth-pairs"), matching the code comments and the `interlocking_depth` / `boundary_avoidance` tooltips which already use "cells" as the unit. Suggested:
- group: *"Number of consecutive interlocking cells to keep per group (one cell = one tooth of each material). …"*
- gap: *"Number of interlocking cells to skip between groups. …"*

Alternatively, if "beams" is the desired user vocabulary, multiply by 2 somewhere — but that reintroduces the even-only granularity the commit deliberately removed, so the doc fix is preferable.

---

### Finding 2 — `filterCellsForAxis(cells, 1)` is computed unconditionally even when `bidirectional == false`, where its result is provably never read; the original `if (bidirectional)` guard was dropped
**Severity: Low** (pure performance regression on a non-default path; no correctness impact; the stated justification for dropping the guard does not hold)
**File:** `InterlockingGenerator.cpp:335–342` (esp. line 339), with the consuming guard at `:358–359` and `:364–365`.

**Why.** Both `c751da3686` (original feature) and the intermediate `4e540c4a26` guarded the type-1 filter:
```cpp
filtered_type0_storage = filterCellsForAxis(cells, 0);
if (bidirectional)
    filtered_type1_storage = filterCellsForAxis(cells, 1);
```
This commit removed the guard (line 339 now runs unconditionally) and added a comment claiming it *"removes a latent empty-set trap"* (lines 330–334). That justification is incorrect: in the consumer loop, the unidirectional skip `if (!bidirectional && layer_type == 1) continue;` (line 358) executes **before** the density check `if (layer_type == 1 && !filtered_type1.count(grid_loc)) continue;` (line 364). So when `bidirectional == false`, **no code path ever reads `filtered_type1`** — leaving it empty is harmless, not a trap. The second guard at lines 392–393 likewise skips all `beam_layer_idx % 2 == 1` layers in the polygon-application loop. There is no empty-set hazard to remove.

`filterCellsForAxis` is O(N log N) over the full dilated cell set (it sorts every row). Computing it for an axis whose output is discarded is wasted work on every slice when a user runs unidirectional + density together. Not a correctness bug, and the cost is modest, so Low.

**Fix.** Restore the guard:
```cpp
if (density_enabled) {
    filtered_type0_storage = filterCellsForAxis(cells, 0);
    if (bidirectional)
        filtered_type1_storage = filterCellsForAxis(cells, 1);
}
```
and update the comment at lines 330–334 to drop the inaccurate "empty-set trap" rationale (it can simply note type-1 is unused in unidirectional mode).

---

### Finding 3 — `density_enabled` gate is consistent across all three layers (UI warning, tooltip, algorithm)
**Severity: clean (informational)**
**Files:** `InterlockingGenerator.cpp:335` (`beam_group_count > 0 && beam_gap > 0`), `ConfigManipulation.cpp:554` (`xor_bad = … (beam_group > 0) != (beam_gap > 0)`), tooltips `PrintConfig.cpp:4252, 4263`.

**Why clean.** All three agree on the rule "both must be > 0":
- Algorithm: `const bool density_enabled = beam_group_count > 0 && beam_gap > 0;` (line 335) — if either is 0, no filtering, solid comb.
- GUI: warns exactly on the XOR case (one > 0, the other 0) — `(beam_group > 0) != (beam_gap > 0)` — i.e. precisely the configurations where the user expects an effect but `density_enabled` is false. The warning text ("…the interlocking beams will be solid (no gaps)") matches the algorithm's actual behavior (the `density_enabled ? … : cells` fallback at lines 341–342 passes the unfiltered set through).
- Both tooltips state "Both this and beam {gap|group count} must be greater than 0 to enable density control."

The warning is a one-shot (`m_beam_density_xor_warned`, lines 555/562/564) that re-arms once the XOR condition clears — reasonable UX, no inconsistency with the model. No combination found where the UI claims one thing and the slicer does another for this gate.

One minor note (not a defect): the warning only fires while `interlocking_beam` is on (`xor_bad` includes `beam_interlocking_on`), which is correct — the settings are also `toggle_line`-disabled when interlocking is off (ConfigManipulation.cpp:991–992), so a stale XOR value can't mislead.

---

### Finding 4 — Per-row segmentation re-anchors at every air-filtering hole, locally raising density near boundaries; documented but a landmine
**Severity: Low** (intended behavior per the comment, but the composition with air filtering / depth is non-obvious and can defeat the user's density target near edges)
**Files:** `InterlockingGenerator.cpp:463–484` (`filterRow` segment splitting), interacting with air filtering at `:195–204` and interface dilation (`interlocking_depth`) feeding the cell set via `getShellVoxels`.

**Why.** `filterRow` splits each (perpendicular-coord, z) row into maximal runs of consecutive grid coordinates (`positions[i] != positions[i-1] + 1`, line 473) and applies the 5-zone pattern **independently per segment**, with `filterSegment` always keeping `M` beams at *each* end (lines 441–446) and the whole segment if `count <= 2*M` (lines 435–438).

Air filtering (lines 199–201) erases cells mid-row before this runs, and `interlocking_depth` dilation can leave thin/ragged interface cell runs. The net effect:
- Every hole punched by air filtering **fragments** a row into more segments; each new fragment gets its own pair of fully-anchored ends. So density is **higher** (more beams kept) immediately around every air-filtered hole and around every geometric boundary than in a long clean run. With many small fragments (`count <= 2*M`), filtering is effectively disabled for those fragments (all kept).
- This is *deliberate* ("so every disjoint boundary segment gets its own anchored ends," comment lines 463–465, and the structural-integrity rationale that boundaries must be anchored). It is also why the pre-filter in `generateInterlockingStructure` was removed (lines 206–211) so the air-filtered set is what gets segmented.

The landmine: a user who sets a low density (small M, large G) expecting a uniformly sparse interface will instead see near-solid beams hugging every boundary and every air-avoidance hole, with sparsity only in large interior runs. On small or thin interfaces (see Finding 5) the result can be ~100% solid despite a low M/G. Nothing here is *wrong* — the anchoring is defensible for strength — but the cross-feature interaction (`boundary_avoidance` holes × per-segment re-anchoring) is invisible from the tooltip.

**Fix.** Tooltip/doc note only: mention that ends of every interface region are always fully populated for adhesion, so achieved density is highest near boundaries and on small interfaces. No algorithm change recommended (the anchoring is intentional and the air-filter→segment ordering is correct now that the pre-filter is gone).

---

### Finding 5 — `max = 100` (cells) composes poorly with typical interface sizes; large values silently become no-ops
**Severity: Low** (bounds nit; outcome is "no effect," not a wrong result)
**Files:** `PrintConfig.cpp:4254, 4265` (`def->max = 100` for both), `InterlockingGenerator.cpp:435` (`if (count <= 2 * M) keep all`).

**Why.** M and G are in cells, and a cell is `2*beam_width` wide (`beam_width` default 0.8 mm → cell ≈ 1.6 mm). A row of cells spans the interface width in that direction. For a 30 mm-wide interface that is ~19 cells; for many real parts a perpendicular run is well under 100 cells. Because `filterSegment` keeps the entire segment when `count <= 2*M` (line 435), any `M >= 50` makes *every* segment ≤ `2*M` for sub-160 mm interfaces, so density filtering becomes a silent no-op (solid comb) even though `density_enabled` is true and the user picked a large number on purpose. Similarly a large `G` with a modest `M` pushes `middle_start = M+G` past `middle_end = count-M-G` (line 451), collapsing the middle fill so only the two anchored end-groups remain — which for short rows again means "keep almost everything."

So the upper half of the `0..100` range is mostly meaningless for typical geometry, and there is no feedback telling the user their value did nothing. Not a correctness bug (output is well-defined), hence Low.

**Fix (optional).** Either lower `max` to a value that is meaningful at cell scale (e.g. 20), or add `sidetext`/tooltip text clarifying the unit is cells (~`2*beam_width` each) so users can reason about how many fit across their interface. The Finding 1 tooltip fix ("cells") already mitigates most of the confusion.

---

## Verdict

The **core semantic fix is correct**: the filter axis now matches `generateMicrostructure`'s cell geometry (verified against the cell split math), the cell-unit conversion (dropping the `/2`) is sound, the `density_enabled` gate is consistent across UI warning / tooltip / algorithm (Finding 3, clean), and the code comments accurately describe the cell/tooth model.

Two real issues remain. **Finding 1 (Medium)** is the only cross-layer *disagreement*: the tooltips still say "beams" while the unit is cells/tooth-pairs (2× the visible-beam granularity) — the code comments are right, the user-facing text was not updated to match. **Finding 2 (Low)** is a dropped `if (bidirectional)` guard whose removal is justified by an incorrect "empty-set trap" rationale; it is a harmless perf regression. Findings 4 and 5 (Low) are documented-but-surprising compositions with air filtering and with small interfaces. None cause a wrong slice; the change is safe to ship, but the Finding 1 tooltip wording should be corrected to keep UI and algorithm honest, and restoring the Finding 2 guard is cheap.
