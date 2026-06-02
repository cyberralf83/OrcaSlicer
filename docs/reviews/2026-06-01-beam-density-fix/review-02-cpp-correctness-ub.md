# Review 02 — C++ Correctness / Undefined Behavior

**Commit under review:** `e57ed0375a` — "Fix interlocking beam density not applying (inverted filter axis)"
**Reviewer lens:** C++ correctness / undefined behavior
**Date:** 2026-06-01
**Scope:** `src/libslic3r/Feature/Interlocking/InterlockingGenerator.{cpp,hpp}`, `src/libslic3r/PrintConfig.cpp`, `src/slic3r/GUI/ConfigManipulation.cpp`. (`.github/` excluded by instruction.)

## Context established

- `coord_t = int64_t` (`src/libslic3r/libslic3r.h:43`). `GridPoint3 = Vec3crd = Eigen::Matrix<coord_t,3,1>` (`VoxelUtils.hpp:15`, `Point.hpp:42`). So `cell.x()/y()/z()` return `int64_t`.
- `beam_group_count` and `beam_gap` are `const int` members (`InterlockingGenerator.hpp:198-200`), sourced from `coInt` config options with `min=0, max=100` (`PrintConfig.cpp`).
- `filterCellsForAxis` has exactly two callers, both in `applyMicrostructureToOutlines` (lines 338-339), both guarded by `density_enabled = beam_group_count > 0 && beam_gap > 0` (line 335). Therefore inside `filterCellsForAxis` and its lambdas, `M >= 1` and `G >= 1` are guaranteed; `stride = M + G >= 2`.
- The restored body is byte-for-byte the same algorithm as the pre-`4e540c4a26` original (`c751da3686`); this commit is effectively a revert of the stripe rewrite plus a doc/GUI/config-tooltip cleanup.

---

## Findings

### 1. Modulo by zero in `filterSegment` — NOT a defect (gated)
**Severity:** N/A (clean)
**Location:** `InterlockingGenerator.cpp:452-454` (`stride = M + G; ... % stride`)
**Analysis:** `stride = M + G`. A `% stride` with `stride == 0` would be UB. `stride == 0` requires `M == 0 && G == 0`. But `filterCellsForAxis` is only reached when `density_enabled` (`M > 0 && G > 0`), so `stride >= 2` always. The `% stride` is also dead unless `middle_start < middle_end`, an additional guard. There is no other call path to `filterCellsForAxis`. **No division/modulo-by-zero is reachable.** Note the modulo operands here are both `int` and both non-negative (`i - middle_start >= 0` because the loop starts at `middle_start`), so even the sign-of-result subtlety the old stripe code had to patch (`if (pos_in_stride < 0) pos_in_stride += stride;`) cannot occur — that defensive branch is correctly absent.

### 2. Empty-container access `positions[0]` in `filterRow` — NOT a defect
**Severity:** N/A (clean)
**Location:** `InterlockingGenerator.cpp:471` (`segment.push_back(positions[0])`)
**Analysis:** `filterRow` is invoked only from `for (auto& [key, positions] : rows)` (lines 493-494 / 506-507). Every entry in `rows` is created by `rows[...].push_back(...)` (lines 490 / 503), so every mapped `vector` has size >= 1. `positions[0]` is therefore always valid. If `cells` is empty, `rows` is empty and the loop body (hence `filterRow`) never runs; there is no empty-vector path. (Independently, the upstream caller `generateInterlockingStructure` returns early when `has_all_meshes.empty()`, lines 189-191, so `cells` is non-empty in practice.) Likewise `filterSegment` is only ever called on `segment`, which always has >= 1 element pushed before each of its two call sites (lines 474, 480). **No out-of-bounds `operator[]`.**

### 3. Index loops in `filterSegment` stay in bounds — NOT a defect
**Severity:** N/A (clean)
**Location:** `InterlockingGenerator.cpp:441-457`
**Analysis:**
- Left end `for (i=0; i<M; i++) positions[i]` — only reached when `count > 2*M` (the `count <= 2*M` short-circuit at 435 returns first), so `M < count`, i.e. `i < M < count`. In bounds.
- Right end `for (i=count-M; i<count; i++)` — `count - M > M > 0` here, so the start index is positive and `< count`. In bounds.
- Middle `for (i=middle_start; i<middle_end; i++)` with `middle_end = count - M - G` (exclusive) and the guard `middle_start < middle_end`. `middle_end <= count - M - G < count`. In bounds. When `count - M - G` underflows to a negative `int`, `middle_start (= M+G >= 2) < middle_end (<0)` is false, so the loop is skipped — no negative index, no UB. **All three loops are bounds-safe.**

### 4. `(int)positions.size()` truncation — NOT a defect (benign in practice)
**Severity:** Low (theoretical only)
**Location:** `InterlockingGenerator.cpp:430` (`int count = (int)positions.size();`)
**Analysis:** `positions.size()` is `size_t`; the cast to `int` truncates above `INT_MAX`. `positions` holds the cells of a single `(perp-axis, z)` row of the interlocking voxel grid. Reaching 2^31 cells in one row is physically impossible (it would require a multi-kilometre print at sub-mm cell pitch and would exhaust memory long before). No `-Werror=conversion`/`-Wsign-conversion` is enabled (only `-Wall` non-fatal and `-Werror=return-type`/availability — see `CMakeLists.txt:210,392-397,482-483`), so this does not break the build either. Pre-existing pattern, unchanged from the original. **Real defect: no. Landmine grade: Low.** A `size_t`/`coord_t` count would be more defensible but is not required.

### 5. Mixed `int` counts vs `coord_t` (int64) positions — correct by construction
**Severity:** N/A (clean)
**Location:** `InterlockingGenerator.cpp:428-461`
**Analysis:** The function deliberately separates two roles: **values** (grid coordinates) stay `coord_t` throughout (`positions` is `vector<coord_t>`, `keep` is `set<coord_t>`, `GridPoint3` is reconstructed from `coord_t`), while **indices/counts** (`count, M, G, i, stride, middle_*`) are `int`. Indices are bounded by `count` (a row length), so `int` is safe per finding #4. Crucially, the change *removes* the genuinely dangerous narrowing the stripe version had — `int pos_in_stride = ((int)(perp - min_perp)) % stride;` truncated a full `coord_t` *coordinate* (not a count) to `int`, which for large rotated grid coordinates could wrap. The restored code never casts a coordinate to `int`. This is a net correctness improvement. **Clean.**

### 6. Lambda captures `[this]` and `[&filterSegment]` — no dangling
**Severity:** N/A (clean)
**Location:** `InterlockingGenerator.cpp:428` (`[this]`), `466` (`[&filterSegment]`)
**Analysis:** `filterSegment` captures `[this]` to read the `beam_group_count`/`beam_gap` members — `this` outlives the lambda (member function scope), valid. `filterRow` captures `filterSegment` **by reference**; both are local objects in the same function body, and `filterRow` is invoked synchronously (lines 494/507) within that same scope while `filterSegment` is still alive. Neither lambda is stored, returned, or run on another thread (the surrounding code is single-threaded per region pair; TBB parallelism in this feature is at the layer/region level, not inside this function). No reference outlives its referent. **No dangling capture.**

### 7. Iterator / reference validity across `std::map` and `std::set` — clean
**Severity:** N/A (clean)
**Location:** `InterlockingGenerator.cpp:488-512`
**Analysis:** `rows` is fully populated (lines 489-490 / 502-503) before any iteration; the subsequent `for (auto& [key, positions] : rows)` does not insert into or erase from `rows` during iteration, and `positions` is mutated in place (sorted) — `std::map` node references/iterators are not invalidated by mutating a mapped value. `filterRow` takes `positions` by non-const reference and sorts it; that aliases the live map node, which is fine (the node is not relocated). `keep_set` (a `std::set`) is iterated by value (`for (coord_t x : keep_set)`), no invalidation concern. `result` (`unordered_set`) is only inserted into. **No invalidation.**

### 8. Structured bindings (C++17) — clean
**Severity:** N/A (clean)
**Location:** `InterlockingGenerator.cpp:493, 506`
**Analysis:** `for (auto& [key, positions] : rows)` binds to `std::pair<const std::pair<coord_t,coord_t>, std::vector<coord_t>>`. Project is C++17 (per CLAUDE.md / CMake), so structured bindings are available. `key.first/.second` access the `coord_t` pair members; types match the `GridPoint3(...)` reconstruction. **Clean.**

### 9. Required includes — correct; `<limits>` correctly no longer needed
**Severity:** N/A (clean)
**Location:** `InterlockingGenerator.cpp:8-10`
**Analysis:** The change adds `#include <map>` and `#include <set>`, both now directly used (`std::map`, `std::set`). `<algorithm>` (already present) covers `std::sort`. `std::pair`/`std::vector` arrive transitively (ubiquitous via `ClipperUtils.hpp`/`libslic3r.h`) and were already relied upon elsewhere. The old code's `std::numeric_limits<coord_t>::max()` was removed, so `<limits>` is no longer required; it was never explicitly `#include`d in this file (relied on transitive includes), so no stale include remains to clean up and nothing else in the file uses `numeric_limits` (verified by grep). **Includes are correct and minimal.** Minor nit: relying on transitive `<utility>`/`<vector>` is fragile in principle, but unchanged by this commit and consistent with the file's existing style — not actionable here.

### 10. Overflow in `2 * M`, `M + G`, `count - M - G` — clean
**Severity:** N/A (clean)
**Location:** `InterlockingGenerator.cpp:435, 449-452`
**Analysis:** `M, G <= 100` (config `max`). `2*M <= 200`, `M+G <= 200` — no overflow. `count - M - G` can go negative but only as a normal `int` subtraction (no UB; result used only in a `<` comparison that gates the loop, per finding #3). `i - middle_start` is non-negative within the loop. **No signed-overflow UB.**

### 11. GUI change (`ConfigManipulation.cpp`) — clean
**Severity:** N/A (clean)
**Location:** `ConfigManipulation.cpp:545-565`
**Analysis:** The removed odd→even snap deleted a block that allocated `new ConfigOptionInt(...)` and re-read the config; its removal leaves the XOR-warning logic intact and self-consistent (`beam_group`/`beam_gap` read once as `int`, compared as `(x>0) != (y>0)`). No new allocations, no ownership changes, no dangling. `m_beam_density_xor_warned` latch logic is unchanged. **No C++ issues.**

### 12. PrintConfig.cpp change — clean
**Severity:** N/A (clean)
**Location:** `PrintConfig.cpp:4246-4264`
**Analysis:** Only tooltip string literals changed; `coInt`, `min=0`, `max=100`, defaults unchanged. No type or UB surface. **Clean.**

---

## Pre-existing observations (NOT introduced by this commit, for awareness only)

- `std::hash<GridPoint3>` (lines 12-24) folds the three `coord_t` (int64) coordinates through `int` arithmetic, truncating to 32 bits before widening to `size_t`. This degrades hash distribution for the `unordered_set<GridPoint3>` that `filterCellsForAxis` returns into, but does **not** affect correctness — equality uses the full `Vec3crd ==`. Unchanged by this commit; out of scope.

---

## Verdict

From the C++ correctness / UB lens the change is **clean**. Every concern in the brief was traced to a concrete guard or invariant:
- **Modulo-by-zero:** unreachable — both callers gated on `M>0 && G>0`, so `stride >= 2`.
- **Out-of-range indexing** (`filterSegment` left/right/middle loops, `filterRow` `positions[0]`): all bounded by `count`/non-empty-row invariants.
- **Truncations** (`(int)positions.size()`): bounded by physically-impossible row sizes; no `-Werror=conversion` build risk. The change additionally **removes** the only genuinely risky narrowing (the stripe code's `(int)(perp - min_perp)` on a full coordinate).
- **Lambda captures** (`[this]`, `[&filterSegment]`): synchronous, same-scope, no escape — no dangling.
- **Includes:** `<map>`/`<set>` correctly added; `<limits>` correctly dropped (was transitive, now unused).

The only graded item is **Finding #4 (Low / theoretical)** — `(int)positions.size()` truncation — which is benign in any realistic print. No Critical/High/Medium defects.
