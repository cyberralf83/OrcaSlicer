# Feature 2: Beam Density Control (DEFERRED)

## Context

Currently beams extend across the entire boundary region — every cell in an overlap zone gets a beam. On long walls this produces far more beams than needed, wasting print time. The beams near the edges of a segment are structurally most important (resist peeling/separation), while center beams add diminishing returns.

This feature adds two settings to control beam density: group size (M — how many consecutive beams) and gap size (G — how many empty cells between groups). Both ends of a segment always get a full M-beam group, then a G-cell gap. The remaining middle is filled left-to-right with repeating M+G blocks, truncating the last beam group if needed.

## Settings

### `interlocking_beam_group_count`
- **Type**: `ConfigOptionInt`
- **Default**: `0` (disabled — unlimited, current behavior)
- **Label**: "Beam group count"
- **Tooltip**: "Number of consecutive interlocking beams per group. Both ends of a segment always get a full group, with the remaining middle filled left-to-right. Set to 0 for unlimited (current behavior). Both this and beam gap must be greater than 0 to enable density control."

### `interlocking_beam_gap`
- **Type**: `ConfigOptionInt`
- **Default**: `0` (disabled — no gaps between groups)
- **Label**: "Beam gap"
- **Tooltip**: "Number of empty cells between beam groups. Set to 0 for no gaps (all beams placed). Both this and beam group count must be greater than 0 to enable density control."
- **Max**: `100` (clamped in config definition)

## Example

The pattern is built in 5 zones:

1. **Left end**: M beams (indices `0` to `M-1`)
2. **Left gap**: G empty cells
3. **Middle fill**: repeating M beams + G gaps, left-to-right. Last beam group truncated if it reaches the right gap. If the last middle fill gap merges with the right gap, the combined gap may be wider than G — this is expected.
4. **Right gap**: G empty cells
5. **Right end**: M beams (indices `count-M` to `count-1`)

When the row is too short for a middle section (`count <= 2*(M+G)`), there is no middle fill — just left end, merged gaps, right end. When the row is very short (`count <= 2*M`), the ends overlap and all cells are beams.

For rows longer than 2*M, maximum consecutive beams is capped at M. For short rows (`count <= 2*M`), ends overlap and all cells are kept — up to 2*M consecutive beams.

### M=4, G=4

| Count | Pattern (B=beam, .=gap) | Beams | Notes |
|-------|-------------------------|-------|-------|
| 20 | `BBBB....BBBB....BBBB` | 12 | 1 full middle group |
| 25 | `BBBB....BBBB....B....BBBB` | 13 | Last middle group truncated to 1 |
| 28 | `BBBB....BBBB....BBBB....BBBB` | 16 | Perfect fit, 2 middle groups |

### M=4, G=2

| Count | Pattern | Beams | Notes |
|-------|---------|-------|-------|
| 22 | `BBBB..BBBB..BBBB..BBBB` | 16 | Perfect fit |
| 20 | `BBBB..BBBB..BB..BBBB` | 14 | Last middle group truncated to 2 |
| 18 | `BBBB..BBBB....BBBB` | 12 | Middle gap merges with right gap (4 total) |

### M=2, G=4

| Count | Pattern | Beams | Notes |
|-------|---------|-------|-------|
| 20 | `BB....BB....BB....BB` | 8 | 2 middle groups, perfect fit |
| 16 | `BB....BB......BB` | 6 | 1 middle group, gap merges (6 total gap) |
| 14 | `BB....BB....BB` | 6 | 1 middle group, perfect fit |

### M=1, G=2

| Count | Pattern | Beams | Notes |
|-------|---------|-------|-------|
| 22 | `B..B..B..B..B..B..B..B` | 8 | Perfect fit |
| 20 | `B..B..B..B..B..B...B` | 7 | Middle gap merges with right gap (3 total) |

### Short rows (no room for middle)

| M | G | Count | Pattern | Notes |
|---|---|-------|---------|-------|
| 4 | 2 | 12 | `BBBB....BBBB` | Gaps merge, no middle |
| 4 | 2 | 10 | `BBBB..BBBB` | Gaps overlap |
| 4 | 2 | 8 | `BBBBBBBB` | Ends overlap, all beams |
| 4 | 0 | 20 | `BBBBBBBBBBBBBBBBBBBB` | G=0 disables feature |
| 0 | * | 20 | `BBBBBBBBBBBBBBBBBBBB` | M=0 disables feature |

## Files to Modify (same 7-file pattern as Feature 1)

| File | Change |
|------|--------|
| `src/libslic3r/PrintConfig.cpp` | Define both settings |
| `src/libslic3r/PrintConfig.hpp` | Declare `ConfigOptionInt` for both |
| `src/libslic3r/Feature/Interlocking/InterlockingGenerator.hpp` | Add constructor params, members (`beam_group_count`, `beam_gap`) + `filterCellsForAxis()` declaration. Add `#include <algorithm>` for `std::min` |
| `src/libslic3r/Feature/Interlocking/InterlockingGenerator.cpp` | Read settings in `generate_interlocking_structure()`, pass to constructor, compute per-axis filtered sets, check inside inner loop. Add `#include <set>` and `#include <map>` for `filterCellsForAxis()` |
| `src/slic3r/GUI/Tab.cpp` | Add both UI lines |
| `src/libslic3r/Preset.cpp` | Add both to preset key list |
| `src/libslic3r/PrintObject.cpp` | Add both to invalidation |

## Implementation Strategy

### Where to intervene

The filtering is **per beam-layer type**, not per cell. Each cell contributes beams to both even layers (type 0, beams along Y) and odd layers (type 1, beams along X). Filtering at the cell level would require combining two axes into a single keep/discard decision, which fails for typical thin walls (see "Why per-axis" below).

Instead, we compute two filtered cell sets — one per axis — before the loop, then check membership inside the inner loop based on `layer_type`. The outer loop still iterates all cells; the beam polygon generation and union steps work unchanged.

### Reading the settings

Follow the same pattern as `bidirectional` and `skip_layers`: read from config in `generate_interlocking_structure()` and pass through the constructor as member variables.

In `generate_interlocking_structure()` (after the existing `skip_layers` read):
```cpp
const int      beam_group_count = config.interlocking_beam_group_count;
const int      beam_gap             = config.interlocking_beam_gap;
```

Constructor parameter list (add after `skip_layers`):
```cpp
const int             beam_group_count,
const int             beam_gap)
```

Member initializer list (add after `skip_layers`):
```cpp
, beam_group_count(beam_group_count)
, beam_gap(beam_gap)
```

Constructor call site (add after `skip_layers`):
```cpp
InterlockingGenerator gen(*print_object, region_a_index, region_b_index,
    beam_width, boundary_avoidance, rotation, cell_size, beam_layer_count,
    interface_dilation, air_dilation, air_filtering, bidirectional, skip_layers,
    beam_group_count, beam_gap);
```

Both `beam_group_count` and `beam_gap` must be > 0 to enable density control. Either being 0 means disabled (keep all cells).

### Grouping cells into rows

Cells need to be grouped by "row" — cells that will produce beams along the same line. The beam direction alternates per interlocking layer:
- **Even interlocking layers (type 0)**: beams run along Y, so cells at the same `grid_loc.y()` AND same `grid_loc.z()` form a row. The row axis is X.
- **Odd interlocking layers (type 1)**: beams run along X, so cells at the same `grid_loc.x()` AND same `grid_loc.z()` form a row. The row axis is Y.

Each axis is filtered independently and applied only to its matching layer type.

### Why per-axis filtering (not per-cell)

A naive approach — filter both axes and keep a cell if it survives *either* pass — fails for typical thin walls. Consider a wall 20 cells long along X but only 2 cells wide along Y:
- X-rows (grouped by y,z): 20 cells → filter prunes to ~12
- Y-rows (grouped by x,z): 2 cells each → all kept as "short segments"
- Union: every cell survives via the Y-row pass → **filter is a no-op**

Using intersection instead would be too aggressive for square overlap regions (only corners survive).

The correct approach: **apply each axis's filter only to its matching beam layer type.** Even-layer beams are thinned by the X-row filter; odd-layer beams are thinned by the Y-row filter. Each direction is handled independently and correctly.

### `filterCellsForAxis()` implementation

Takes an `axis` parameter: 0 = filter X-rows (for even beam layers), 1 = filter Y-rows (for odd beam layers).

```cpp
std::unordered_set<GridPoint3> InterlockingGenerator::filterCellsForAxis(
    const std::unordered_set<GridPoint3>& cells, int axis) const
{
    // Helper: apply 5-zone pattern to a single contiguous segment of positions.
    // Algorithm: M beams at left end, G gap, middle fill (M beams + G gap repeating L→R,
    // last group truncated), G gap, M beams at right end.
    // For segments longer than 2*M, max consecutive beams is capped at M.
    auto filterSegment = [this](const std::vector<coord_t>& positions) -> std::set<coord_t> {
        std::set<coord_t> keep;
        int count = (int)positions.size();
        int M = beam_group_count;
        int G = beam_gap;

        // Short segment: ends overlap, keep all
        if (count <= 2 * M) {
            for (auto& p : positions) keep.insert(p);
            return keep;
        }

        // Left end: M beams
        for (int i = 0; i < M; i++)
            keep.insert(positions[i]);

        // Right end: M beams
        for (int i = count - M; i < count; i++)
            keep.insert(positions[i]);

        // Middle fill zone: from (M + G) to (count - M - G - 1)
        int middle_start = M + G;
        int middle_end = count - M - G;  // exclusive
        if (middle_start < middle_end) {
            int stride = M + G;
            for (int i = middle_start; i < middle_end; i++) {
                int pos_in_stride = (i - middle_start) % stride;
                if (pos_in_stride < M)
                    keep.insert(positions[i]);
            }
        }

        return keep;
    };

    // Helper: split a sorted row of positions into contiguous segments
    // (where consecutive grid coordinates differ by exactly 1), then apply
    // the 5-zone pattern independently to each segment.
    // This ensures both sides of any gap get anchored beam ends.
    auto filterRow = [&filterSegment](std::vector<coord_t>& positions) -> std::set<coord_t> {
        std::set<coord_t> keep;
        std::sort(positions.begin(), positions.end());

        // Split into contiguous segments
        std::vector<coord_t> segment;
        segment.push_back(positions[0]);
        for (size_t i = 1; i < positions.size(); i++) {
            if (positions[i] != positions[i - 1] + 1) {
                // Gap detected — filter the current segment and start a new one
                auto seg_keep = filterSegment(segment);
                keep.insert(seg_keep.begin(), seg_keep.end());
                segment.clear();
            }
            segment.push_back(positions[i]);
        }
        // Filter the last segment
        auto seg_keep = filterSegment(segment);
        keep.insert(seg_keep.begin(), seg_keep.end());

        return keep;
    };

    if (axis == 0) {
        // Filter X-rows: group by (y, z), vary along x — for even beam layers (type 0)
        std::map<std::pair<coord_t, coord_t>, std::vector<coord_t>> rows;
        for (const auto& cell : cells)
            rows[{cell.y(), cell.z()}].push_back(cell.x());

        std::unordered_set<GridPoint3> result;
        for (auto& [key, positions] : rows) {
            auto keep_set = filterRow(positions);
            for (coord_t x : keep_set)
                result.insert(GridPoint3(x, key.first, key.second));
        }
        return result;
    } else {
        // Filter Y-rows: group by (x, z), vary along y — for odd beam layers (type 1)
        std::map<std::pair<coord_t, coord_t>, std::vector<coord_t>> rows;
        for (const auto& cell : cells)
            rows[{cell.x(), cell.z()}].push_back(cell.y());

        std::unordered_set<GridPoint3> result;
        for (auto& [key, positions] : rows) {
            auto keep_set = filterRow(positions);
            for (coord_t y : keep_set)
                result.insert(GridPoint3(key.first, y, key.second));
        }
        return result;
    }
}
```

### Call site

In `applyMicrostructureToOutlines()`, compute both filtered sets before the loop, then check inside the inner loop:

```cpp
// Compute filtered cell sets per beam direction (only when enabled — avoids copying)
const bool density_enabled = beam_group_count > 0 && beam_gap > 0;
std::unordered_set<GridPoint3> filtered_type0_storage, filtered_type1_storage;
if (density_enabled) {
    filtered_type0_storage = filterCellsForAxis(cells, 0); // X-rows, for even beam layers
    if (bidirectional)
        filtered_type1_storage = filterCellsForAxis(cells, 1); // Y-rows, only needed in bidirectional mode
}
const auto& filtered_type0 = density_enabled ? filtered_type0_storage : cells;
const auto& filtered_type1 = density_enabled ? filtered_type1_storage : cells;

// Outer loop still iterates ALL cells — filtering is per layer type inside
for (const GridPoint3& grid_loc : cells) {
    Vec3crd bottom_corner = vu.toLowerCorner(grid_loc);
    for (size_t mesh_idx = 0; mesh_idx < 2; mesh_idx++) {
        for (size_t layer_nr = bottom_corner.z();
             layer_nr < bottom_corner.z() + cell_size.z() && layer_nr < max_layer_count;
             layer_nr += beam_layer_count) {

            size_t beam_layer_idx = static_cast<size_t>(layer_nr / beam_layer_count);
            if (!isActiveBeamLayer(beam_layer_idx))
                continue;

            size_t layer_type = beam_layer_idx % cell_area_per_mesh_per_layer.size();

            // Skip perpendicular layers in unidirectional mode
            if (!bidirectional && layer_type == 1)
                continue;

            // Density filter: check against the filtered set for this layer type
            if (layer_type == 0 && !filtered_type0.count(grid_loc))
                continue;
            if (layer_type == 1 && !filtered_type1.count(grid_loc))
                continue;

            ExPolygons areas_here = cell_area_per_mesh_per_layer[layer_type][mesh_idx];
            // ... rest unchanged (translate, append to structure_per_layer)
        }
    }
}
```

### Key implementation notes

- **5-zone pattern**: left end (M beams) → left gap (G) → middle fill (M+G repeating L→R, last group truncated) → right gap (G) → right end (M beams). For segments longer than 2*M, max consecutive is capped at M. For short segments, all cells are kept (up to 2*M consecutive). Gap merging between middle fill and right gap is expected and harmless.
- **Row splitting at gaps**: Before applying the 5-zone pattern, each row is split into contiguous segments (consecutive grid coordinates differ by exactly 1). Each segment gets its own anchored ends. This ensures that gaps created by air filtering or irregular geometry don't leave inner edges unanchored.
- Filtering is **per beam-layer type**, not per cell — each axis is filtered independently and applied only to its matching layer type
- No polygon measurement or clipping needed — filtering operates on grid coordinates before beam polygons are generated
- Short segments (`count <= 2*M`): ends overlap, all cells kept
- Medium segments (`count <= 2*(M+G)`): no middle fill, just ends + merged gaps
- Works correctly with Feature 1 (bidirectional toggle) — when `bidirectional == false`, `layer_type == 1` is skipped entirely, so `filtered_type1` is computed but never checked (negligible cost)
- **Zero overhead when disabled** — both `beam_group_count` and `beam_gap` must be > 0. When disabled, `filterCellsForAxis` is not called; the call site uses `const` references to the original `cells` set directly (no copy)
- Settings follow the same constructor-member pattern as `bidirectional` and `skip_layers` (read in `generate_interlocking_structure()`, passed to constructor, stored as `const` members)
- The outer loop iterates all original `cells` — this is intentional. A cell may be filtered out for type 0 beams but kept for type 1 beams (or vice versa)
- **Required includes**: Add `#include <set>` and `#include <map>` to `InterlockingGenerator.cpp` for `filterCellsForAxis()`

## Original Instructions

Full details including CuraEngine reference code are in:
`/Users/michael/Downloads/orcaslicer-beam-interlocking-instructions.md`

## Verification

1. Build: `cmake --build build/arm64 --config RelWithDebInfo --target all --`
2. Test with a two-material model (two cubes, different extruders)
3. In layer preview, verify:
   - **Short segments** (row <= 2*M): All beams present, ends overlap
   - **Long segments, M=4 G=4 count=20**: `BBBB....BBBB....BBBB` — both ends anchored, 1 middle group
   - **Long segments, M=4 G=2 count=20**: `BBBB..BBBB..BB..BBBB` — last middle group truncated to 2, max consecutive = 4
   - **Gap merging, M=4 G=2 count=18**: `BBBB..BBBB....BBBB` — middle gap merges with right gap (4 total), no middle beams lost
   - **M=0 or G=0**: Same as current behavior (all beams, disabled)
   - **Combined with Feature 1 (bidirectional OFF)**: Both features work correctly together
   - **Thin wall along one axis**: Beams thinned in the long direction, fully preserved in the short direction
   - **Square overlap region**: Both directions filtered equally
   - **M=1 G=2**: Single beams with 2-cell gaps, both ends anchored
