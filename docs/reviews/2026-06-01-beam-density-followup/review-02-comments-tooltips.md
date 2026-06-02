# Review 02 — Accuracy of Comments & Tooltips

**Commit under review:** `3f701d6143` — "Address beam-density review: clarify tooltips, restore bidirectional guard"
**Lens:** Accuracy of comments and tooltips (each claim verified against the actual code, not just readability).
**Scope (READ-ONLY):** `src/libslic3r/Feature/Interlocking/InterlockingGenerator.cpp`, `src/libslic3r/PrintConfig.cpp`. `.github/` excluded.

## Method

Traced the data path that the comments and tooltips describe:

- `generateMicrostructure()` (`InterlockingGenerator.cpp:283-311`) — how a single grid cell becomes filament-A / filament-B geometry on a given beam-layer type.
- `applyMicrostructureToOutlines()` (`:313-411`) — how `layer_type` (`beam_layer_idx % 2`) selects which per-axis filtered set a cell is checked against (`:357-367`), and the bidirectional skip (`:360-361`).
- `filterCellsForAxis()` (`:413-515`) — the 5-zone per-row thinning, including the `count <= 2*M` keep-all branch (`:437`).
- Cross-checked terminology against the sibling dev comment in `ConfigManipulation.cpp:549` and the prior commit `e57ed0375a` (which already moved the units from "fingers" to plain cells).

---

## Findings

### F1 — Tooltip "(of each filament)" is ACCURATE — CLEAN
**Severity:** N/A (verification pass)
**Files:** `PrintConfig.cpp:4250` (group_count), `PrintConfig.cpp:4263` (gap)

The filter in `filterCellsForAxis` operates on **cells** (`M = beam_group_count`, `G = beam_gap` are cell counts; one row position = one cell). On a type-0 layer, `generateMicrostructure` splits each cell **along X** into two adjacent rectangles, each spanning the full cell in Y: mesh 0 (filament A) gets `x ∈ [0, middle]`, mesh 1 (filament B) gets `x ∈ [middle, cell_size.x]` (`:288-301`). So **one kept cell renders exactly one beam-slice of filament A AND one of filament B**.

Tracing a group of M kept cells: M beams of filament A and M beams of filament B — i.e. "M beams of each filament", NOT M total and NOT M pairs-as-a-single-count. The tooltip noun "consecutive interlocking beams (of each filament)" therefore matches the algorithm's cell granularity exactly. The same holds for the gap tooltip: G cells skipped = G beams of each filament skipped. **Claim is true.**

---

### F2 — Dev comment ("one cell = one tooth of each material") vs tooltip ("beams of each filament") — terminology drift
**Severity:** Medium
**Files:** `InterlockingGenerator.cpp:418`; sibling `ConfigManipulation.cpp:549` (out of this commit's diff but same feature)
**Why:** This commit standardizes the user-facing vocabulary on **"beams (of each filament)"** and renames "fingers"→"beams" in `filterCellsForAxis` (`:420`). But two unchanged dev comments still describe the *same* unit as a **"tooth of each material"**:
- `InterlockingGenerator.cpp:418` — "...are in cell units (one cell = one interlocking **tooth of each material**)."
- `ConfigManipulation.cpp:549` — "Counts are in cell units (one cell = one **tooth of each material**)."

Both are factually correct descriptions of the same geometry (one cell = one A-slice + one B-slice). The problem is purely lexical: a reader now sees three nouns for one concept across the feature — tooltip "beam (of each filament)", `filterCellsForAxis` body "beams", and these two comments "tooth of each material". Since the commit's stated goal is to "resolve a units ambiguity", leaving "tooth of each material" in place partially reintroduces the ambiguity it set out to remove. The `:418` instance is *in the very function this commit edited* (the "fingers"→"beams" line is two lines above it), so the mismatch is glaring.
**Fix:** Align nouns. Either change the two "tooth of each material" comments to "beam of each filament" (matches the tooltip and the renamed body), or add a one-line note in the tooltip-adjacent comment that "beam of each filament" and "tooth of each material" are the same unit. Lowest-churn option: edit `:418` to "(one cell = one interlocking beam of each filament)". (Out of scope to change `ConfigManipulation.cpp` here, but it should follow the same rename for consistency.)

---

### F3 — "very short boundaries may keep every beam" — accurate mechanism, loose adjective at the bound ceiling
**Severity:** Low
**File:** `PrintConfig.cpp:4252` (group_count tooltip)
**Why:** The clause maps to the `count <= 2 * M` keep-all branch in `filterSegment` (`InterlockingGenerator.cpp:437-440`): a contiguous boundary segment of ≤ 2·M cells keeps every cell because the two anchored M-groups (left end + right end) overlap. The *mechanism* is described correctly, and the dev comment at `:428-429` says the same thing ("Segments no longer than 2*M keep every beam (the ends overlap)").

The imprecision is only in the word "very short". `beam_group_count` has `max = 100` (`:4256`). At M = 100 the keep-all branch fires for segments up to **200 cells** wide — not "very short" in absolute terms. For realistic density values (default is `0` = disabled; no profile sets it, confirmed by grep of `resources/profiles`; meaningful values are small single digits) "very short" is fair. So this is a tail-case wording nit, not a misstatement: "very short" is relative to the chosen M, which the tooltip does not spell out.
**Fix:** Optional. Tighten to "boundaries shorter than two groups may keep every beam" (ties the adjective to M instead of an absolute notion of "short"). Low priority — acceptable as-is for typical use.

---

### F4 — Reworded gap-zone comment ("fails the density check for whichever layer-type is being placed") — ACCURATE, improvement over prior "fail both"
**Severity:** N/A (verification pass) — CLEAN
**File:** `InterlockingGenerator.cpp:208-209`
**Why:** Verified against the per-layer check. Each beam layer has a single `layer_type = beam_layer_idx % 2` (`:357`), and a cell is tested against only the matching filtered set for that layer: `filtered_type0` on type-0 (`:364`), `filtered_type1` on type-1 (`:366`). No single layer consults both axes. The old wording "cells in the gap zones simply fail **both** per-axis checks" was misleading because no layer ever applies both checks to a cell. The new wording "a cell in a gap zone fails the density check **for whichever layer-type is being placed**, so it produces no beam **there**" correctly scopes the failure to the current layer-type. The qualifier "there" also correctly handles the asymmetric case (a cell can be gapped for axis 0 yet kept for axis 1). **Net: more accurate than before.** Minor residual looseness — "in a gap zone" is a per-axis property, not a global one — but "for whichever layer-type is being placed" disambiguates it, so no action needed.

---

### F5 — Bidirectional-guard comment — ACCURATE after the guard restoration
**Severity:** N/A (verification pass) — CLEAN
**File:** `InterlockingGenerator.cpp:331-335` (comment), `:340-341` (the restored `if (bidirectional)` guard)
**Why:** The comment claims "The type-1 set is only consulted on type-1 (odd) layers, which are skipped entirely in unidirectional mode (the layer_type==1 continue below), so only compute it when bidirectional." Verified by enumerating every read of `filtered_type1`:
- Only consumer is `:366` (`if (layer_type == 1 && !filtered_type1.count(grid_loc))`), guarded by `layer_type == 1`.
- `layer_type == 1` is unreachable when `!bidirectional` because of the `continue` at `:360-361`.

Therefore computing `filtered_type1_storage` only when `bidirectional` is **output-neutral** (matches the commit message). In the `!bidirectional && density_enabled` case, `filtered_type1` aliases an empty set (`:344`) but is provably never read, so the "latent empty-set trap" the old comment warned about cannot fire. The `(odd)`/`(even)` parentheticals are also correct: `cell_area_per_mesh_per_layer.size()` is 2 (`:286`), so `layer_type = beam_layer_idx % 2`. **Comment matches code.**

---

### F6 — "fingers"→"beams" rename in filterCellsForAxis — geometrically correct, complete
**Severity:** N/A (verification pass) — CLEAN
**File:** `InterlockingGenerator.cpp:420`
**Why:** "The beams of a type-0 (even) beam layer repeat along grid-X (see generateMicrostructure: each cell is split along X, spanning the full cell in Y)". Confirmed: in `generateMicrostructure` type-0 cells are split along X with full-Y extent (`:288-301`), producing full-height strips that run along Y and repeat as you step along X. Thinning the *number* of parallel beams therefore means grouping by `(y, z)` and thinning along X — exactly what `axis == 0` does (`:488-500`). The type-1 mirror (group by `(x, z)`, thin along Y) is also correct (`:501-513`). The noun "beams" is geometrically accurate and now consistent with the rest of the function body and the tooltips. `grep` confirms **no remaining "finger" references** in `InterlockingGenerator.{cpp,hpp}`, `PrintConfig.cpp`, or `ConfigManipulation.cpp`.

---

## Summary

| # | Severity | Finding | Location |
|---|----------|---------|----------|
| F1 | clean | "(of each filament)" is true: M cells → M beams of EACH filament | `PrintConfig.cpp:4250,4263` |
| F2 | Medium | Dev comments still say "tooth of each material" while tooltips/body say "beam of each filament" — same unit, three nouns; reintroduces the ambiguity this commit aimed to kill | `InterlockingGenerator.cpp:418`, `ConfigManipulation.cpp:549` |
| F3 | Low | "very short" boundaries: mechanism (`count<=2*M` keep-all) correct, but adjective is loose at the M=100 ceiling (up to 200 cells) | `PrintConfig.cpp:4252` |
| F4 | clean | "fails the density check for whichever layer-type" is accurate and better than old "fail both" | `InterlockingGenerator.cpp:208` |
| F5 | clean | Bidirectional-guard comment accurate; restoration is provably output-neutral | `InterlockingGenerator.cpp:331,340` |
| F6 | clean | "fingers"→"beams" geometrically correct; rename complete (no leftover "finger") | `InterlockingGenerator.cpp:420` |

**Verdict:** Comments and tooltips are substantively accurate — every load-bearing claim ("of each filament", the gap-zone rewording, the bidirectional guard, "fingers"→"beams") checks out against the code. No Critical/High issues. One Medium (cross-file noun drift "tooth of each material" vs "beam of each filament") and one Low (the "very short" adjective at the max-bound tail) remain; both are wording polish, not correctness errors.
