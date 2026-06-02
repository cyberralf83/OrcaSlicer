# Review 05 — Comment / Doc / Code Consistency (comment-rot hunt)

**Commit under review:** `e57ed0375a` — "Fix interlocking beam density not applying (inverted filter axis)"
**Lens:** Verify every comment and doc string matches the post-change code. Confirm the geometry CLAIMS in comments are actually TRUE against `generateMicrostructure`.
**Scope:** `InterlockingGenerator.cpp/.hpp`, `PrintConfig.cpp`, `ConfigManipulation.cpp`, `PLAN-feature2-max-beam-length.md`. (`.github/` excluded; no source modified — read-only review.)

---

## Method / ground truth

`generateMicrostructure()` (InterlockingGenerator.cpp:282–310) is the source of truth for cell geometry:

- **layer_type 0** (`cell_area_per_mesh_per_layer[0]`):
  - mesh 0 poly: x ∈ [0, middle], y ∈ [0, cell_size.y()]  (X-extent = `width[0]`, full Y)
  - mesh 1 poly: x ∈ [middle, cell_size.x()], y ∈ [0, cell_size.y()]  (full Y)
  - → each cell is **split along X**, both material strips **span the full cell in Y**.
  - → tiling cells, each material forms a continuous bar **running along Y**, one bar pair **per cell along X**. So type-0 beams **repeat along X**.
- **layer_type 1** (`cell_area_per_mesh_per_layer[1]`): identical but x/y swapped (lines 302–308) → split along Y, full X → beams repeat along Y. The mirror of type 0.

Call-site mapping (lines 337–342, 362–365): `filterCellsForAxis(cells, 0)` → `filtered_type0`, checked when `layer_type == 0`. So **axis 0 ↔ type 0**, **axis 1 ↔ type 1**.

`filterCellsForAxis` axis==0 branch (lines 486–498): groups by `(y, z)`, collects `x`, thins x. axis==1 (lines 499–512): groups by `(x, z)`, collects `y`, thins y.

---

## Findings

### F1 — [Medium] generateInterlockingStructure comment overstates "fail BOTH per-axis checks"
**File:** `src/libslic3r/Feature/Interlocking/InterlockingGenerator.cpp:206–211`

The comment reads:
> "Density filtering is applied per-axis inside applyMicrostructureToOutlines (one density pattern per beam direction). No pre-filter is needed here: cells in the gap zones simply **fail both per-axis checks** and produce no beam, so iterating them is harmless."

**Why it's wrong:** Filtering is independent per axis along different directions. A cell that lands in a gap for axis 0 (absent from `filtered_type0`) can still be present in `filtered_type1`, and vice-versa — the call site checks `filtered_type0` only for `layer_type == 0` and `filtered_type1` only for `layer_type == 1` (lines 362–365). So a "gap-zone" cell does **not** necessarily fail *both* checks, and does **not** necessarily "produce no beam" — it may still produce a beam in the perpendicular direction. The whole rationale of the per-axis design (and the F-series PLAN "Why per-axis filtering" section, lines 139–148) is precisely that a cell can be kept for one axis and dropped for the other. The comment contradicts that design.

The *conclusion* ("iterating all cells is harmless") is correct; only the justification is overstated. Severity Medium (not High) because it is reasoning-commentary, not load-bearing behavior, and the gist holds.

**Fix:** Reword to per-direction, e.g.:
> "...cells in a gap zone for a given beam direction simply fail that direction's per-axis check and produce no beam *of that type*; a cell dropped for one direction may still be kept for the perpendicular direction. Iterating all cells is therefore harmless."

---

### F2 — [Low] "fingers" terminology survives in the type-0 geometry comment
**File:** `src/libslic3r/Feature/Interlocking/InterlockingGenerator.cpp:418`

> "The **fingers** of a type-0 (even) beam layer repeat along grid-X..."

The commit's stated intent was to drop "finger" semantics (the old finger→cell `/2` scaling). Lines 414–416 establish the new unit vocabulary: "one cell = one interlocking tooth of each material," counts in **cell units**. Line 418 then reintroduces "fingers" — here meaning the physical beam protrusions, not the unit of counting, so it is not a *unit* contradiction. But it is mild residual terminology drift that a reader could conflate with the abandoned finger-unit concept.

**Geometry claim itself is TRUE:** "type-0 fingers repeat along grid-X" and "each cell is split along X, spanning the full cell in Y" both match `generateMicrostructure[0]` exactly (X-extent = `width[mesh]`, Y-extent = `cell_size.y()`). ✓

**Fix (optional):** s/fingers/beams/ (or "teeth") at line 418 for vocabulary consistency with lines 414–416.

---

## Verified-correct (no action)

- **filterCellsForAxis header comment, cpp:414–427** — "5-zone pattern", "beam_group_count (M) and beam_gap (G) are in cell units", "M beams at left end, G gap, middle filled L→R with repeating M-beam groups separated by G gaps (last group truncated), G gap, M beams at right end", "Segments no longer than 2*M keep every beam (ends overlap)". Each clause checked against `filterSegment` (lines 428–461): short-circuit is `count <= 2*M` ✓; left end `[0,M)` ✓; right end `[count-M,count)` ✓; middle `[M+G, count-M-G)` stride `M+G`, keep when `pos_in_stride < M` ✓ (truncation falls out of the loop bound). All TRUE.
- **filterRow comment, cpp:463–465** — "split into contiguous segments (consecutive grid coords differ by exactly 1)... each segment gets its own anchored ends." Matches the `positions[i] != positions[i-1] + 1` split (line 473). ✓
- **axis branch comments, cpp:487 / cpp:500** — "type-0 beams repeat along x: group by (y,z), thin along x" and "type-1 beams repeat along y: group by (x,z), thin along y." Both match the code grouping/keys and are TRUE against `generateMicrostructure`. ✓
- **generateInterlockingStructure, cpp:206 phrase "one density pattern per beam direction"** — two `filterCellsForAxis` calls, one per direction. ✓ (Only the "fail both" clause is faulted — see F1.)
- **Per-layer-type filtering comment, cpp:330–334** — "a cell may pass for one beam direction but not the other"; "type-1 set unused in unidirectional mode (gated by the layer_type==1 continue), populating it removes a latent empty-set trap." Matches lines 335–342 (both populated unconditionally) and the `!bidirectional && layer_type == 1` continue (line 358). ✓
- **hpp doc block, InterlockingGenerator.hpp:162–172** — accurately describes the 5-zone per-row algorithm and units: "axis 0 = even/type-0 beams, grouped by (y,z) and thinned along x; axis 1 = odd/type-1 beams, grouped by (x,z) and thinned along y", "split into contiguous segments", "beam_group_count beams at each end (anchored), beam_gap-wide gaps, middle filled L→R with repeating groups (last truncated)", "Counts are in cell units." Every clause matches the cpp. No leftover `cell_M`/`cell_G`/`ceil`/stripe text (the old `(pos-min)%(cell_M+cell_G)<cell_M` doc was fully replaced). ✓
- **hpp member comments, InterlockingGenerator.hpp:197–200** — `beam_group_count` "Number of consecutive beams per group... 0 = disabled"; `beam_gap` "Number of empty cells between beam groups... 0 = disabled. Max 100." Consistent with PrintConfig (`min 0`, `max 100`) and the density gate. ✓
- **PrintConfig.cpp tooltips, 4249–4252 / 4261–4263** — beam_group_count: "Number of consecutive interlocking beams to keep per group. Both ends of each boundary segment always get a full group, with the middle filled left-to-right. Set to 0 to disable... Both this and beam gap must be > 0." beam_gap: "Number of interlocking beams to skip between groups. Set to 0 for no gaps... Both this and beam group count must be > 0." All claims match the algorithm and the `group>0 && gap>0` gate. **No leftover "finger" / "Odd values are snapped up to the next even number" / pairing text** — the snap claims were correctly removed in both tooltips. ✓
- **ConfigManipulation.cpp comment block, 545–550** — "Beam density control thins the interlocking beams (keep N beams, skip a gap, repeat). Both must be > 0... gated on `group > 0 && gap > 0`. Setting only one silently produces a solid comb, so warn... Counts are in cell units... no rounding/snapping is applied." Matches the surviving `xor_bad` warn logic (lines 551–565) and the removal of the odd→even snap block. The `group>0 && gap>0` claim matches `density_enabled` (cpp:335). No stale snap/pairing language. ✓
- **No stale tokens** in source: `grep -i "stripe|finger|/2|ceiling|cell_M|cell_G|snap|paired"` over the three source files returns only cpp:418 (F2) and ConfigManipulation:550 (the *correct* "no rounding/snapping" statement). Tab.cpp:2697–2698 and PrintObject.cpp:1160–1161 reference the keys only (no stale prose). ✓

---

## PLAN-feature2-max-beam-length.md (weighted LOW per brief)

The PLAN's *algorithm* description matches the implementation (5-zone, segment splitting, per-axis, M/G semantics, short-segment overlap), so it is not actively misleading about behavior. Two drift items:

### F3 — [Low] PLAN title still says "(DEFERRED)"; feature is implemented
**File:** `PLAN-feature2-max-beam-length.md:1`
> "# Feature 2: Beam Density Control (DEFERRED)"

The feature is now shipped (settings, `filterCellsForAxis`, warn logic all present). The "DEFERRED" tag and the present-tense "Files to Modify" / "Implementation Strategy" framing read as if work is pending. Stale, but it's a planning doc, not user/dev-facing API doc. **Fix:** retitle to "(IMPLEMENTED in e57ed0375a)" or move under a done/ folder.

### F4 — [Low] PLAN call-site snippet gates type-1 on `bidirectional`; code populates unconditionally
**File:** `PLAN-feature2-max-beam-length.md:263–266` (and notes at 309)
PLAN shows:
```cpp
if (bidirectional)
    filtered_type1_storage = filterCellsForAxis(cells, 1); // only needed in bidirectional mode
```
Implemented code (cpp:337–340) populates **both** sets unconditionally when `density_enabled`, with the cpp comment (330–334) explaining this is deliberate to "remove a latent empty-set trap." The PLAN snippet therefore describes a slightly different (and now-rejected) call shape. Harmless drift; the shipped code's own comment documents the divergence. **Fix:** update the PLAN snippet to match, or annotate that the gate was dropped intentionally.

(Minor: PLAN tooltip text at lines 15/21 says "unlimited (current behavior)" / "empty cells", whereas shipped tooltips say "all beams placed" / "interlocking beams". Cosmetic, not tracked as a separate finding.)

---

## Verdict

The new comments and doc strings are substantially accurate and the load-bearing **geometry claims are TRUE** against `generateMicrostructure` (type-0 split-along-X/full-Y/repeat-along-X, type-1 the mirror; axis↔type mapping; 5-zone segment math; cell-unit semantics). All stale "stripe / finger-unit / `/2` / ceiling / `cell_M`/`cell_G` / odd→even snap" language was removed from the tooltips, the hpp doc block, and the ConfigManipulation comment. One genuine comment-rot bug remains: **F1 (Medium)** — the `generateInterlockingStructure` comment claims gap-zone cells "fail both per-axis checks," which contradicts the per-axis-independent design it is describing. Everything else is Low (residual "fingers" wording, PLAN "DEFERRED"/call-site drift).
