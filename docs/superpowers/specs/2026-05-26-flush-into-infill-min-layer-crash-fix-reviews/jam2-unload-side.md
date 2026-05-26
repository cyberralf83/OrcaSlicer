# JAM2 — Unload-Side Jam Risk Review: `flush_into_infill_min_layer`

**Question:** Can the `flush_into_infill_min_layer` gate cause incomplete purge of the OLD filament during toolchange unload, leading to char/clog at the next load?

**Verdict: JAM RISK — UNLIKELY (no defect found on the unload path).**

The gate denies *infill-as-purge-sink* on bottom layers; it does **not** reduce the volume of new filament that the wipe tower receives, and the OLD-filament ramming is entirely driven by per-filament ramming parameters that the gate never touches. The redirected volume actually grows the wipe tower depth via `plan_toolchange`. No clamp, no discard, no override key gap.

---

## C — Critical findings

(none)

## H — High findings

(none)

## M — Medium findings

**M1. Documentation claim is verified accurate.** The user-facing tooltip says:

> "Volume that would have purged into bottom-layer infill is redirected to the wipe tower instead, so expect the wipe tower's per-layer purge volume to grow on gated layers."

This is **true** in code. `WipingExtrusions::mark_wiping_extrusions` (`src/libslic3r/GCode/ToolOrdering.cpp:1617-1753`) returns `volume_to_wipe` — the volume **still remaining** after attempted infill overrides. The gate at `is_overriddable` (line 1561-1594) returning `false` causes the inner `set_extruder_override` call (line 1688) to be skipped, so `volume_to_wipe` is **not decremented**, and the full ungated volume is returned. Both call sites consume that return value directly:

- Multi-extruder: `Print.cpp:3299-3310` — `volume_to_purge` is the return value, passed directly to `wipe_tower.plan_toolchange(..., m_config.prime_volume, volume_to_purge)`.
- Single-extruder MM (legacy WipeTower): `Print.cpp:3417-3434` — return assigned back into `volume_to_wipe`, then `+= filament_minimal_purge_on_wipe_tower`, then `wipe_tower.plan_toolchange(..., volume_to_wipe)`.

`WipeTower::plan_toolchange` (`WipeTower.cpp:2449-2508`) computes `depth += ceil(length / width) * perimeter_width` from `wipe_volume` — i.e. the tower's per-layer depth grows linearly with the requested purge volume. `WipeTower2::plan_toolchange` (`WipeTower2.cpp:2212-2250`) does the same via `get_wipe_depth(wipe_volume - first_wipe_volume, ...)`. No clamp at "max purge speed" affects volume — `m_wipe_tower_max_purge_speed` only modulates feedrate inside `toolchange_Wipe()` (`WipeTower2.cpp:1940`, `WipeTower.cpp:2019`), never the volume.

**M2. OLD-filament unload (the actual jam vector) is decoupled from the gate.** The hot-end clog at next load comes from incomplete ramming of the OLD filament before retraction into the cooling tube. `WipeTower2::toolchange_Unload` (`WipeTower2.cpp:1633-1748`) drives ramming purely from `m_filpar[m_current_tool].ramming_speed[]` (per-filament `filament_ramming_parameters`), with cooling-tube retraction lengths from `m_cooling_tube_retraction` / `m_cooling_tube_length`. **No reference to `wipe_volume` exists in `toolchange_Unload`.** The new-filament `wipe_volume` reaching the wipe tower is only consumed downstream by `toolchange_Wipe()` (`WipeTower2.cpp:1601, 1915-1997`), which wipes the *newly-loaded* filament. So under-purging the new filament could leave color contamination on the next-but-one layer, but **cannot** under-purge the OLD filament and cannot starve the unload ramming.

## L — Low findings

**L1. Per-filament override doesn't apply.** `filament_extruder_override_keys` (`src/libslic3r/PrintConfig.cpp:63-84`) lists only retraction/wipe settings (`filament_retraction_length`, `filament_z_hop`, `filament_wipe`, etc.). It does **not** include `flush_into_infill_min_layer`. The setting is per-`PrintObject` only and cannot be silently bypassed by a filament profile — confirming the gate's intent.

**L2. Wipe-tower box "violation" comment is harmless.** `WipeTower2::toolchange_Wipe` (`WipeTower2.cpp:1931-1977`) contains a comment "even if it means violating the box" but the early-break at line 1972-1973 (`if (writer.y() > cleaning_box.lu.y() - 0.5f*line_width) break;`) would only trip if the reserved box depth were smaller than the wipe needs. The box depth is computed from the same `wipe_volume` passed into `plan_toolchange` (via `required_depth`/`wipe_area` at WipeTower2.cpp:1532, 1549), so the budget matches. No silent volume loss.

**L3. `psWipeTower` invalidation is correct.** `Print.cpp:342-343` lists both `flush_into_infill` and `flush_into_infill_min_layer` in the keys that trigger `psWipeTower` re-planning, so toggling the setting can't leave a stale wipe-tower plan referencing a different gate decision.

**L4. `grab_purge_volume` floor.** `Print.cpp:3306-3307` subtracts `grab_purge_volume = grab_length * 2.4` and clamps `volume_to_purge` to `>= 0`. This *could* zero out a tiny purge requirement, but it operates after `mark_wiping_extrusions`, so the gate doesn't shift behavior — same code runs whether the gate denies or allows. Not a gate-induced risk.

## Summary

The gate at `WipingExtrusions::is_overriddable` (`ToolOrdering.cpp:1576-1591`) **only** controls whether internal-infill extrusions can be re-purposed as purge sinks. When it denies on object-bottom layers:

1. `mark_wiping_extrusions` does not decrement `volume_to_wipe` for that infill;
2. the larger residual is returned to `Print.cpp` and passed as `volume_to_purge` to `wipe_tower.plan_toolchange()`;
3. the wipe tower grows in depth proportionally to absorb the extra purge (`plan_toolchange`/`get_wipe_depth`);
4. `toolchange_Wipe` then wipes the **new** filament the full requested volume.

The **old-filament unload** path (`toolchange_Unload`) is wholly driven by per-filament ramming parameters and cooling-tube retraction settings — quantities the gate doesn't touch and that don't depend on `wipe_volume`. There is no mechanism by which `flush_into_infill_min_layer > 0` can cause the old filament to be left in the melt zone with residual material. The user's reported jams are not attributable to the unload path of this feature.

**Adjacent observation (not a gate bug, just a pre-existing quirk):** In `Print.cpp:3302` the multi-extruder branch calls `mark_wiping_extrusions(..., current_filament_id, filament_id, ...)` rather than `pre_filament_id, filament_id`. On a multi-extruder machine where the *other* nozzle is doing the toolchange, this passes the wrong "old" extruder to the soluble-check at `ToolOrdering.cpp:1622`. This predates the gate and is unrelated to jamming, but worth flagging.
