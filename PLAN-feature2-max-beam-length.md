# Feature 2: Beam Density Control (DEFERRED)

## Context

Currently beams extend across the entire boundary region — every cell in an overlap zone gets a beam. On long walls this produces far more beams than needed, wasting print time. The beams near the edges of a segment are structurally most important (resist peeling/separation), while center beams add diminishing returns.

This feature adds two settings to control beam density: cap the number of continuous beams from each side, and optionally space out beams in the middle.

## Settings

### `interlocking_max_continuous_beams`
- **Type**: `ConfigOptionInt`
- **Default**: `0` (disabled — unlimited, current behavior)
- **Label**: "Max continuous beams"
- **Tooltip**: "Maximum number of consecutive interlocking beams from each end of a segment. Beams are placed starting from the outside edges working inward. Set to 0 for unlimited (current behavior)."

### `interlocking_beam_gap`
- **Type**: `ConfigOptionInt`
- **Default**: `0` (no beams in middle gap)
- **Label**: "Beam gap"
- **Tooltip**: "In the middle section (between the continuous end beams), place one beam every N+1 cells. Set to 0 to leave the middle empty. Only applies when max continuous beams is active."

## Example

Wall with 20 cells, `max_continuous_beams = 4`, `beam_gap = 2`:
```
[B][B][B][B][ ][ ][B][ ][ ][B][ ][ ][B][ ][ ][ ][B][B][B][B]
 <--left-->  <-------middle: every 3rd cell------> <--right->
```
- 4 beams on left end, 4 on right end
- Middle 12 cells: one beam every 3 cells (gap=2 means skip 2, place 1)
- Total: ~12 beams instead of 20

With `beam_gap = 0`:
```
[B][B][B][B][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][ ][B][B][B][B]
 <--left-->  <---------middle: empty-------------> <--right->
```
- Total: 8 beams instead of 20

Short segments (e.g., 6 cells with max=4): all beams kept — left 4 and right 4 overlap, so every cell is included.

## Files to Modify (same 7-file pattern as Feature 1)

| File | Change |
|------|--------|
| `src/libslic3r/PrintConfig.cpp` | Define both settings |
| `src/libslic3r/PrintConfig.hpp` | Declare `ConfigOptionInt` for both |
| `src/libslic3r/Feature/Interlocking/InterlockingGenerator.hpp` | Add members + `filterCellsByDensity()` declaration |
| `src/libslic3r/Feature/Interlocking/InterlockingGenerator.cpp` | Read settings, call filter before cell iteration |
| `src/slic3r/GUI/Tab.cpp` | Add both UI lines |
| `src/libslic3r/Preset.cpp` | Add both to preset key list |
| `src/libslic3r/PrintObject.cpp` | Add both to invalidation |

## Implementation Strategy

### Where to intervene

The filtering happens **before** the cell iteration loop in `applyMicrostructureToOutlines()`, operating on the `std::unordered_set<GridPoint3>& cells` input. This is the cleanest approach — we remove cells from the set before any beam polygons are generated, so the union and application steps work unchanged.

### Reading the settings
```cpp
const int max_continuous = config.interlocking_max_continuous_beams.value;
const int beam_gap = config.interlocking_beam_gap.value;
```
A `max_continuous` value of 0 means disabled (keep all cells).

### Grouping cells into rows

Cells need to be grouped by "row" — cells that will produce beams along the same line. The beam direction alternates per interlocking layer:
- **Even interlocking layers**: beams run along Y, so cells at the same `grid_loc.y()` AND same `grid_loc.z()` form a row. The row axis is X.
- **Odd interlocking layers**: beams run along X, so cells at the same `grid_loc.x()` AND same `grid_loc.z()` form a row. The row axis is Y.

However, since each cell spans multiple interlocking layers (both even and odd), and filtering at the cell level removes the cell from ALL layers, we should group by the **dominant** axis. Since cells contribute to both even and odd layers, the simplest correct approach is:

**Group cells by both row axes independently, then filter conservatively** — a cell is kept if it would be kept in either orientation.

OR, simpler: **group by 2D position (y, z) for rows along X, AND (x, z) for rows along Y, and a cell survives if it passes the filter in at least one grouping.**

**Simplest approach**: Group cells into rows along X (by `y, z` key) and filter. Then separately group into rows along Y (by `x, z` key) and filter. Keep a cell if it survives either pass. This ensures short segments in any direction keep their beams.

### `filterCellsByDensity()` implementation

```cpp
std::unordered_set<GridPoint3> InterlockingGenerator::filterCellsByDensity(
    const std::unordered_set<GridPoint3>& cells,
    int max_continuous,
    int beam_gap) const
{
    if (max_continuous <= 0)
        return cells; // disabled

    // Helper: filter a sorted row of positions, return the indices to keep
    auto filterRow = [&](std::vector<coord_t>& positions) -> std::set<coord_t> {
        std::set<coord_t> keep;
        std::sort(positions.begin(), positions.end());
        int count = (int)positions.size();

        if (count <= max_continuous * 2) {
            // Short segment — keep all
            for (auto& p : positions) keep.insert(p);
            return keep;
        }

        // Keep max_continuous from left end
        for (int i = 0; i < max_continuous && i < count; i++)
            keep.insert(positions[i]);

        // Keep max_continuous from right end
        for (int i = count - max_continuous; i < count; i++)
            keep.insert(positions[i]);

        // Fill middle with spaced beams if beam_gap > 0
        if (beam_gap > 0) {
            int middle_start = max_continuous;
            int middle_end = count - max_continuous;
            int spacing = beam_gap + 1; // gap=2 means place every 3rd
            for (int i = middle_start; i < middle_end; i += spacing)
                keep.insert(positions[i]);
        }

        return keep;
    };

    // Pass 1: group by (y, z) — rows along X axis
    std::map<std::pair<coord_t, coord_t>, std::vector<coord_t>> rows_x;
    for (const auto& cell : cells)
        rows_x[{cell.y(), cell.z()}].push_back(cell.x());

    std::unordered_set<GridPoint3> keep_x;
    for (auto& [key, positions] : rows_x) {
        auto keep_set = filterRow(positions);
        for (coord_t x : keep_set)
            keep_x.insert(GridPoint3(x, key.first, key.second));
    }

    // Pass 2: group by (x, z) — rows along Y axis
    std::map<std::pair<coord_t, coord_t>, std::vector<coord_t>> rows_y;
    for (const auto& cell : cells)
        rows_y[{cell.x(), cell.z()}].push_back(cell.y());

    std::unordered_set<GridPoint3> keep_y;
    for (auto& [key, positions] : rows_y) {
        auto keep_set = filterRow(positions);
        for (coord_t y : keep_set)
            keep_y.insert(GridPoint3(key.first, y, key.second));
    }

    // Keep cell if it survives either pass
    std::unordered_set<GridPoint3> result;
    for (const auto& cell : cells) {
        if (keep_x.count(cell) || keep_y.count(cell))
            result.insert(cell);
    }

    return result;
}
```

### Call site

In `applyMicrostructureToOutlines()`, before the cell iteration loop:

```cpp
// Filter cells by density settings
const std::unordered_set<GridPoint3> filtered_cells =
    filterCellsByDensity(cells, max_continuous, beam_gap);

// Use filtered_cells instead of cells in the iteration loop below
for (const GridPoint3& grid_loc : filtered_cells) {
    ...
}
```

### Key implementation notes

- Filtering at the **cell level** (before beam polygon generation) is the simplest approach — no polygon measurement or clipping needed
- The dual-pass (X rows + Y rows) ensures cells important in either beam direction are preserved
- Short segments (total cells <= 2 * max_continuous) are left completely untouched
- Works correctly with Feature 1 (bidirectional toggle) — if only one direction is active, only that direction's rows matter but the cell filtering still applies conservatively
- Zero overhead when disabled (max_continuous = 0)

## Original Instructions

Full details including CuraEngine reference code are in:
`/Users/michael/Downloads/orcaslicer-beam-interlocking-instructions.md`

## Verification

1. Build: `cmake --build build/arm64 --config RelWithDebInfo --target all --`
2. Test with a two-material model (two cubes, different extruders)
3. In layer preview, verify:
   - **Short segments**: All beams present (under the max, unaffected)
   - **Long segments, gap=0**: Beams only on the ends, middle empty
   - **Long segments, gap>0**: Beams on ends + spaced beams in middle
   - **max_continuous=0**: Same as current behavior (all beams, unlimited)
   - **Combined with Feature 1 (bidirectional OFF)**: Both features work correctly together
   - **Edge case**: max_continuous=1 (just one beam on each end)
