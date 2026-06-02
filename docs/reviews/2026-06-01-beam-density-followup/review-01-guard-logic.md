# Review 01 — Guard logic correctness

**Change under review:** commit `3f701d6143` "Address beam-density review: clarify tooltips, restore bidirectional guard"
**Prior reviewed state:** `e57ed0375a`
**Original known-good impl:** `c751da3686`
**Files:**
- `src/libslic3r/Feature/Interlocking/InterlockingGenerator.cpp`
- `src/libslic3r/PrintConfig.cpp`

**Lens:** logic correctness of the restored `if (bidirectional)` guard around `filtered_type1_storage = filterCellsForAxis(cells, 1);`, plus general bug review of the change.

---

## The one logic change

`InterlockingGenerator.cpp:339-341`

```cpp
const bool density_enabled = beam_group_count > 0 && beam_gap > 0;
std::unordered_set<GridPoint3> filtered_type0_storage, filtered_type1_storage;
if (density_enabled) {
    filtered_type0_storage = filterCellsForAxis(cells, 0);
    if (bidirectional)
        filtered_type1_storage = filterCellsForAxis(cells, 1);   // <- newly re-guarded
}
const auto& filtered_type0 = density_enabled ? filtered_type0_storage : cells;
const auto& filtered_type1 = density_enabled ? filtered_type1_storage : cells;
```

Everything else in the commit is comment/tooltip text (verified against `git show 3f701d6143` — only the `if (bidirectional)` insertion is executable).

---

## Verification trace

### 1. Every read of `filtered_type1`

`grep` across the whole file confirms exactly **one** read of `filtered_type1`:

- `InterlockingGenerator.cpp:366` — `if (layer_type == 1 && !filtered_type1.count(grid_loc)) continue;`

There is **no** read of `filtered_type1` anywhere else, in particular **not** in the second region loop at the bottom of the function (`:386-410`). That loop only reads `structure_per_layer[...]` and `layer_regions[...]`; it never touches `filtered_type0`/`filtered_type1`. Confirmed by grep: the only `filtered_type*.count` sites are lines 364 and 366, both inside the first cell loop.

### 2. When `!bidirectional`: is `filtered_type1` ever consulted?

No. Two independent barriers guarantee it:

- **Outer barrier (`:360-361`):**
  ```cpp
  if (!bidirectional && layer_type == 1)
      continue;
  ```
  This `continue` executes *before* the density checks (`:364`, `:366`). So in unidirectional mode, the loop body never reaches `:366` for `layer_type == 1`. The ordering is the same as the original `c751da3686` (`:343` before `:349`) — verified.

- **Short-circuit barrier (`:366`):** even if the outer barrier were absent, `layer_type == 1 && !filtered_type1.count(...)` short-circuits on `layer_type == 1`. For `layer_type == 0`, the left operand is false, so `filtered_type1.count(...)` is never evaluated.

`layer_type` can only be `0` or `1`: `layer_type = beam_layer_idx % cell_area_per_mesh_per_layer.size()` and `generateMicrostructure()` always `resize(2)`s the outer vector (`:286`). There is no third layer type that could fall through to read `filtered_type1` unguarded.

Therefore, in unidirectional mode the empty `filtered_type1_storage` (the now-unpopulated set) is **never** `.count()`-ed, and cannot silently drop any beams. The empty-set-trap risk that motivated the e57ed0375a unconditional population does not materialize because the consumer is fully gated.

### 3. When `bidirectional`: behavior unchanged vs `e57ed0375a`?

Yes. When `bidirectional` is true, the new `if (bidirectional)` is taken, so `filtered_type1_storage = filterCellsForAxis(cells, 1)` runs exactly as it did unconditionally in `e57ed0375a`. The alias `filtered_type1` then points at the populated set, and `:366` filters identically. Bit-for-bit identical output in bidirectional mode.

### 4. When `density_enabled` is false: guard irrelevant?

Correct. If `density_enabled` is false, the whole `if (density_enabled)` block is skipped (neither axis is computed), and both aliases bind to `cells`:
```cpp
const auto& filtered_type1 = density_enabled ? filtered_type1_storage : cells;  // -> cells
```
The `if (bidirectional)` guard lives *inside* `if (density_enabled)`, so it has no effect when density is off. `filtered_type1` aliases `cells`, and `:366` becomes a membership test against the full cell set (every iterated cell is in `cells`, so it never drops a beam). Unchanged from both prior commits.

### 5. Could the empty `filtered_type1_storage` be read and silently drop all type-1 beams?

No. The only path that reads it requires `layer_type == 1`, and reaching `:366` with `layer_type == 1` requires `bidirectional == true` (otherwise `:361` already `continue`d). When `bidirectional == true`, `filtered_type1_storage` was populated at `:341`. So the empty-set state and the read state are mutually exclusive. `bidirectional` is a `const bool` member (`InterlockingGenerator.hpp:194`, initialized in the ctor list `:96`), so it cannot change between population (`:340`) and consumption (`:360`,`:366`) — no TOCTOU concern.

### 6. Exact match to `c751da3686` guard semantics?

Yes, exactly. `git show c751da3686` shows the identical structure:
```cpp
if (density_enabled) {
    filtered_type0_storage = filterCellsForAxis(cells, 0);
    if (bidirectional)
        filtered_type1_storage = filterCellsForAxis(cells, 1);
}
const auto& filtered_type0 = density_enabled ? filtered_type0_storage : cells;
const auto& filtered_type1 = density_enabled ? filtered_type1_storage : cells;
```
and the same consumer ordering (`!bidirectional && layer_type==1 continue` before the `layer_type==1` density check). The commit is a faithful restoration of the original known-good guard.

---

## Tooltip / comment changes (PrintConfig.cpp + comments)

- `PrintConfig.cpp:4250-4252` — `interlocking_beam_group_count` tooltip: adds "(of each filament)" and "very short boundaries may keep every beam". This matches the code: `filterSegment` keeps every position when `count <= 2*M` (`InterlockingGenerator.cpp:437-440`), i.e. short boundary segments keep all beams. Accurate.
- `PrintConfig.cpp:4263` — `interlocking_beam_gap` tooltip: adds "(of each filament)". Consistent.
- Comment edits (`:205-213`, `:331-335`, `:420`) are descriptive only and now correctly describe per-axis gap behavior and use "beams" instead of "fingers". No semantic impact.

No tooltip text affects slicing logic.

---

## Findings

**No defects found. The change is clean.**

The restored `if (bidirectional)` guard is output-neutral:
- bidirectional mode: identical to `e57ed0375a`;
- unidirectional mode: the now-empty `filtered_type1_storage` is provably never read (double-gated by the `!bidirectional && layer_type==1` `continue` and by `&&` short-circuit on `layer_type==1`);
- density-disabled: guard is inert, alias falls back to `cells`.

It exactly matches the original known-good `c751da3686` semantics, and the commit message's "No slicing-behavior change" claim is accurate. The benefit is the stated micro-optimization (skips one `filterCellsForAxis` pass in unidirectional + density-enabled mode) with no correctness cost. Tooltip and comment edits are accurate and non-functional.

### Optional (Low, non-blocking)

- **[Low] Robustness depends on consumer-loop statement order — InterlockingGenerator.cpp:360-367 — why:** the safety of the empty `filtered_type1_storage` rests on `:360-361` preceding `:366` and on `layer_type` only ever being 0/1. Both hold today and match upstream, so this is not a bug. If a future refactor reordered the `continue`s or introduced a code path that reads `filtered_type1` in unidirectional mode, it would read an empty set and silently drop all type-1 beams. The e57ed0375a comment called this a "latent empty-set trap." This is purely a maintainability note, not a defect in the current code; matching upstream `c751da3686` is the right call. **Fix (optional only):** none required; if extra defensiveness were ever wanted, an `assert(bidirectional)` before the `:366` read or a comment cross-referencing the `:361` guard would document the invariant. No change recommended for this minimal-diff fork.
