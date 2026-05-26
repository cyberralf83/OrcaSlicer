# JAM1 — LOAD-side jam risk for `flush_into_infill_min_layer`

**Question:** Can a user setting `flush_into_infill_min_layer > 0` produce G-code where the *new* filament (after a toolchange) is under-primed and jams in the hot-end?

**Verdict: JAM RISK NO** (for the load side — i.e. priming of the new filament after a toolchange). The redirection of purge volume from infill to the wipe tower is volume-conservative or *increases* tower-side priming. The new filament gets at least as much purge as the unguarded baseline, sometimes more. No path was found in which the gate would under-prime the new filament.

The user's reported physical jam, if real, is **not** caused by the new filament being insufficiently flushed at toolchange. Look elsewhere (e.g. unload/ramming volumes, retract, AMS feeder load, or unrelated mechanical causes — JAM2 should cover those).

---

## Critical — none.

## High — none.

## Medium — none.

## Low — none.

## Investigation notes

### 1. Volume conservation through the gate

In `Print.cpp` (psWipeTower step) the per-toolchange purge budget is computed **before** any per-layer override decision and is independent of the gate:

- Type-2 wipe tower (non-BBL) path, `Print.cpp:3417-3429`:
  ```
  volume_to_wipe = wipe_volumes[old][new] * flush_multiplier;
  volume_to_wipe -= filament_minimal_purge_on_wipe_tower[new];
  volume_to_wipe  = mark_wiping_extrusions(...);          // decrement only
  volume_to_wipe += filament_minimal_purge_on_wipe_tower[new];
  wipe_tower.plan_toolchange(..., volume_to_wipe);
  ```
- BBL path, `Print.cpp:3297-3310`:
  ```
  volume_to_purge = multi_extruder_flush[nozzle][pre][new] * flush_multiplier;
  volume_to_purge = mark_wiping_extrusions(...);          // decrement only
  volume_to_purge = max(0, volume_to_purge - grab_purge_volume);
  wipe_tower.plan_toolchange(..., prime_volume, volume_to_purge);
  ```

`mark_wiping_extrusions` walks every infill ECC, asks `is_overriddable(...)` (`ToolOrdering.cpp:1677`), and **only decrements** `volume_to_wipe` when it claims the ECC. When the gate denies (sub-min layer), it does not claim, does not decrement, and the residual flows verbatim to `plan_toolchange`. The wipe-tower planner is therefore informed of the gate's denial, simply by receiving a larger remainder. **The "spec sf2 H3" planning-vs-execution mismatch is structurally absent** because there is no separate planning pass for the volume — `mark_wiping_extrusions` *is* the planner, and it consults the same `is_overriddable`.

### 2. `lt.extruders` membership is correct on sub-min layers

The genuine wave-2 concern was `collect_extruders` (ToolOrdering.cpp:649-727). The new filament's membership in `lt.extruders` decides whether the wipe-tower toolchange G-code is even emitted at that layer.

With the gate inside `is_overriddable`:
- Planning-pass call at line 709: `is_overriddable_and_mark(*fill, ...)` → returns `false` for sub-min layers → `something_nonoverriddable = true` (line 710).
- Line 714: `if (something_nonoverriddable || !m_print_config_ptr)` is `true`.
- Line 719: `layer_tools.extruders.emplace_back(region.config().sparse_infill_filament);`

So the infill region's **original** filament is added to `lt.extruders` on sub-min layers. Downstream:
- `fill_wipe_tower_partitions` (line 800) counts partitions from `lt.extruders.size()` → reserves tower space.
- `has_wipe_tower` is set wherever a toolchange is needed (line 824, 875).
- `plan_toolchange` is called with the correct `(old_extruder, new_extruder)` pair (Print.cpp:3290-3311 / 3414-3434).

**Result**: the toolchange that primes the new filament *is* emitted, and its budget is `wipe_volumes[old][new] * flush_multiplier` (BBL) or that minus/plus the `filament_minimal_purge_on_wipe_tower` band (type-2) — **the exact same total budget the unguarded run would have used**. The gate only redirects *where* that purge happens, not how much there is.

### 3. New filament's physical wipe extrusion on BBL is fixed-size

In `WipeTower::plan_toolchange` (`WipeTower.cpp:2449-2507`), the tower block depth is sized from `wipe_volume` (= `prime_volume` for BBL, from `Print.cpp:3310`), not from `purge_volume`. The actual wipe extrusion in `tool_change_new` is driven by `wipe_length` derived from `wipe_volume` (line 2882: `toolchange_wipe_new(writer, cleaning_box, wipe_length, ...)`). `purge_volume` is recorded but does not drive the extrusion.

So even in the extreme case (gate denies all infill purge on layer N), the BBL wipe tower still extrudes its full `prime_volume` block on the tower for the new filament. The new filament cannot be under-primed at toolchange.

### 4. Type-2 wipe tower scales tower size *up* on gated layers

In WipeTower2 the planner sizes the toolchange depth from `volume_to_wipe` (the remainder). Gate denial → larger remainder → larger tower block → **more** new-filament extrusion on the tower. The change is monotonic in the safer direction.

### 5. Toolchange-skip not possible

`has_wipe_tower` is set by `lt.extruders.size() > 1` (effectively, via `wipe_tower_partitions` at line 800-806) and downstream rules. It is **not** keyed on `something_overridable`. The flag `something_overridable` only short-circuits *claiming infill* (ToolOrdering.cpp:1622 in `mark_wiping_extrusions`, line 1764 in `ensure_perimeters_infills_order`); when both are false the entire purge volume still flows to `plan_toolchange`. There is no path where the gate causes the toolchange G-code itself to be skipped or shortened.

### 6. BBL/AMS specifics

The BBL path uses the legacy `WipeTower` class (Print.cpp:3243-3325, gated by `wipe_tower_type() != Type2`). It is fed the same `mark_wiping_extrusions` output as the type-2 path. No AMS-specific code path consults the gate or `is_overriddable`. The `is_BBL_printer()` branches in `ToolOrdering.cpp:1130, 1283, 1331` are about filament map / extruder ordering, not about toolchange volume. The gate is invisible to the AMS firmware: the firmware sees a normal `Mxxx` toolchange + a fixed `prime_volume` worth of extrusion on the wipe tower, identical to the baseline (un-gated) case.

### 7. Spec wave-2 review correctness

The design doc's wave-2 reasoning (line 22, sf2 H3) about needing the gate inside `is_overriddable` rather than outside is **borne out by the code**. Outer-gate-only placement would have:
- planning pass at `collect_extruders:709` returns `true` (no gate),
- `something_nonoverriddable` stays `false`,
- `sparse_infill_filament` NOT added to `lt.extruders`,
- wipe-tower planner sees no toolchange needed for that filament on that layer,
- execution-pass gate then denies override,
- G-code falls back to the original filament, **but the wipe-tower never queued a toolchange to it** → would print bottom-shell infill with the *previous* filament, mis-coloured at minimum, possibly with no prime extrusion at all → potential under-prime jam.

The current placement (gate **inside** `is_overriddable`, so both planning and execution see the same answer) closes that gap. This is exactly what was shipped in commit 17ec45d3fd. No remaining mismatch.

### 8. Edge cases probed (all benign)

| Case | Effect on load-side priming |
|---|---|
| `min_layer = 0` | Gate skipped. Baseline behaviour. |
| `min_layer = 1` with raft | First object layer eligible; layers above raft normal. No effect on toolchange budget. |
| `min_layer = 1000` (well above object) | All infill purge redirected to tower. Tower grows; new filament fully primed (more than baseline, in fact). |
| `flush_into_infill = false` (parent off) | Gate unreachable. Baseline. |
| `flush_into_objects = true` | Perimeter purging still active; gate only affects `erInternalInfill`. Toolchange budget unchanged. |
| `By object` print sequence + multi-extruder | Wipe tower disabled outright (Print.cpp:1443-1447). Gate moot. |
| Per-layer extruder switch K, `min_layer > K` | Toolchange at K still happens; just no infill purge to claim — remainder lands on tower. New filament primed normally. |
| Spiral vase | No infill exists. Gate never reached. Baseline. |
| Soluble or support filament | Early-return at `is_overriddable:1563` and `mark_wiping_extrusions:1622-1627` regardless of gate. Independent code path. |

### 9. Where the user's jam likely originates (out of scope for JAM1)

Since the load-side analysis rules out under-priming as the cause, candidates for JAM2 / further investigation:
- **Unload / ramming volume** on the old filament — unrelated to the gate.
- **`filament_minimal_purge_on_wipe_tower`** misconfigured per-filament — pre-existing, unrelated to the gate.
- **AMS feeder mechanics** (path resistance, buffer / hub jam) — pre-existing.
- **Hot-end clog from material residue** accumulating *because the tower grew* (volume conservation means more material lands on the tower; an over-sized tower on a small build plate could theoretically interact with print head accelerations, but this would manifest as tower delamination, not nozzle clog).
- Coincidence: the user enabled the new option *and* hit an unrelated mechanical jam, attributing one to the other.

The gate's net effect on the new-filament load path is **strictly non-decreasing** in priming volume. It cannot cause a load-side jam.

## Summary

`flush_into_infill_min_layer > 0` **cannot** under-prime the new filament during toolchange load. The gate redirects purge volume from infill into the wipe tower; total per-toolchange purge budget is preserved (type-2) or actually delivered in a fixed `prime_volume` block (BBL) plus the conserved remainder. `lt.extruders` membership and the `plan_toolchange` calls are correct under the inside-`is_overriddable` placement that shipped. No code path was identified where the gate causes the toolchange G-code to be skipped, shortened, or reduced in purge volume below the baseline.

**JAM RISK: NO** for LOAD side. If a real jam is occurring, the cause lies outside this feature.
