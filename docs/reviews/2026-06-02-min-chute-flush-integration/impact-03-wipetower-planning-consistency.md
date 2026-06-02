# Impact review 03 — Wipe-tower PLANNING vs EMISSION consistency

**Commit:** `eef00f7032` — "Add minimum chute flush length filament option"
**Feature:** per-filament `filament_minimal_purge_on_chute` (mm, default 0) floors the chute
"poop" flush at G-code EMISSION time.
**Lens:** does the emission-time clamp desync the wipe-tower geometry/depth/preview that was
PLANNED earlier (before the clamp)?

**Scope reviewed:** the two emission edits and their connection outward to planning —
`GCode.cpp` `WipeTowerIntegration::append_tcr` (~880) and `GCode::set_extruder` (~7843); the
planner in `Print.cpp::_make_wipe_tower` (3220-3500); `WipeTower::plan_toolchange` / `tool_change_new`
/ `construct_tcr` (Type1) and `WipeTower2::extract_wipe_volumes` / `plan_toolchange` (Type2); the
statistics path in `GCodeProcessor.cpp`. `.github/` ignored per scope.

---

## Executive verdict

**No geometry-corrupting or preview-desync issue found.** The value floored at emission
(`tcr.purge_volume`, the chute "poop") is architecturally separate from the value that drives wipe
tower depth/geometry (`prime_volume`/`wipe_volume`). Flooring the poop cannot change required tower
depth, the depth bar, or the tower preview mesh. User-facing flush/filament statistics are derived by
the GCodeProcessor from the *emitted* (floored) G-code, so they stay consistent with reality.

Two non-corrupting findings: (1) the `set_extruder` clamp also fires on the WipeTower2 (Type2) tower
path via `append_tcr2`, which is broader than the commit message's "no-wipe-tower / Type2 emitter
path" claim — but harmless and never reached for the intended BBL use case; (2) the same
`set_extruder` clamp is effectively dead code for the targeted BBL workflow. Both Low.

---

## How planning and emission are wired (the load-bearing fact)

`Print.hpp:1072` — `wipe_tower_type()` returns **`Type1` for every BBL printer**
(`is_BBL_printer() ? Type1 : m_config.wipe_tower_type.value`). The new option is shown only for BBL
printers (Tab.cpp), so the targeted workflow is **always Type1**.

**Type1 planning** (`Print.cpp` 3290-3401, `WipeTower` / `generate_new`):

- `Print.cpp:3339-3340` `plan_toolchange(z, lh, cur, filament, m_config.prime_volume, volume_to_purge)`.
  - arg5 `wipe_volume = prime_volume` → the deposit ON the tower.
  - arg6 `purge_volume = volume_to_purge` → the chute "poop" (residual flush AFTER
    `mark_wiping_extrusions` diverts purge into infill, minus `grab_length`).
- `WipeTower::plan_toolchange` (`WipeTower.cpp` 2449-2508):
  `length_to_extrude = volume_to_length(wipe_volume, …)` → `depth` (+ `nozzle_change_depth`).
  **Only `wipe_volume`/`prime_volume` and the nozzle-change feed the depth.** `purge_volume` is merely
  stashed in `ToolChange.purge_volume` and never touches `depth`.
- `tool_change_new` (`WipeTower.cpp` 2760-2912): the cleaning box (deposited geometry) is sized by
  `wipe_depth = b.required_depth` and `nozzle_change_depth`; `purge_volume = b.purge_volume` is copied
  verbatim into `construct_tcr(… purge_volume …)` → `result.purge_volume` (`WipeTower.cpp:1267`).

**Emission** (`GCode.cpp` `append_tcr`, 880-886): floors `tcr.purge_volume` and feeds the result into
the `change_filament_gcode` placeholders (`first/second_flush_volume`, `flush_length_N`). This is the
AMS chute ejection — it is **not** deposited on the tower and has no tower extrusions.

**Conclusion:** the planner sizes the tower from `prime_volume`; emission floors the orthogonal chute
poop. The clamp cannot move tower depth or geometry. Confirmed for the only path BBL uses.

---

## Findings

### [Low] GCode.cpp:7843-7850 — `set_extruder` chute clamp also fires on the Type2 wipe-tower path, not just the "no-wipe-tower" path
**Interaction:** The commit message says the `set_extruder` mirror covers the
"no-wipe-tower / Type2 emitter path." In fact `set_extruder` has three live callers:
`GCode.cpp:5228` (no-wipe-tower toolchange), `GCode.cpp:3235/3319` (initial extruder setup), and
`GCode.cpp:1218` inside `WipeTowerIntegration::append_tcr2` — the **WipeTower2 (Type2) tower** path.
So the clamp pads the `change_filament_gcode` chute flush even when a Type2 prime tower is active.
**Why it is not a desync:** Type2 tower geometry/depth is planned by `WipeTower2` from
`extract_wipe_volumes` + `filament_minimal_purge_on_wipe_tower` (`Print.cpp` 3402-3472), which is a
completely different quantity from the `wipe_volume` (flush-matrix residual) that `set_extruder`
floors for the change-filament chute. Padding the chute poop does not alter the WipeTower2 depth, the
`z_and_depth_pairs` depth bar, or the tower mesh — same separation as Type1. The initial-setup callers
(3235/3319) start with `m_writer.filament()==nullptr`, so `wipe_volume==0` and the
`wipe_volume > EPSILON` guard skips them.
**Practical reach:** Type2 only occurs on **non-BBL** printers, where the option is hidden in the UI
and defaults to 0 (`min_chute_purge==0` → guard false → no-op). So this only matters if a non-BBL user
hand-edits the value into a profile.
**Fix:** None required for correctness. Recommend correcting the comment at `GCode.cpp:7843` to
"non-Type1 emitter path (no-wipe-tower toolchange and Type2 prime-tower change_filament_gcode)" so a
future reader does not assume Type2 towers are untouched. Optionally gate the clamp behind
`is_BBL_printer()` to match the option's UI visibility and remove the non-BBL footgun.

### [Low] GCode.cpp:7843-7850 — `set_extruder` clamp is dead code for the intended BBL workflow
**Interaction:** BBL printers are forced to `Type1` (`Print.hpp:1072`), and Type1 tool changes emit
through `append_tcr` (which has its own clamp at 880-886), not through `set_extruder`. BBL never uses
`append_tcr2` (Type2) and, with a wipe tower, never reaches the no-wipe-tower `set_extruder` path at
5228. The option is hidden for non-BBL printers.
**Why it matters (minor):** the mirrored `set_extruder` clamp therefore has no effect for any
user who can actually see/set the option through the UI. It is harmless redundancy that protects only
non-BBL profiles that set the key manually.
**Fix:** None required. If keeping it, the comment should note it is a safety mirror for manually
configured non-BBL profiles; if minimizing fork surface (per CLAUDE.md "keep the diff minimal"),
consider dropping the `set_extruder` half — the BBL chute behavior is fully delivered by `append_tcr`.

---

## Items explicitly checked and found CONSISTENT (no finding)

- **Tower depth / required_depth.** Driven solely by `wipe_volume`(=`prime_volume`) +
  `nozzle_change_depth` in `WipeTower::plan_toolchange` (2488-2502) and `plan_tower_new` (3705-3793).
  `purge_volume` never participates. Flooring the poop → no change to depth. (Type1.)
- **Depth bar / `z_and_depth_pairs` / tower preview mesh.** Built from `get_depth()` /
  `construct_mesh()` in `Print.cpp` 3368-3400 (Type1) and 3477-3482 (Type2), both fed by the
  depth computed above. Independent of `tcr.purge_volume`. No preview inconsistency.
- **Tower extrusions used for the 3D preview** (`tcr.extrusions`). Generated at planning time from the
  cleaning box; the chute poop produces no `Extrusion` entries (it is emitted as raw
  `change_filament_gcode`, off the tower). Flooring the poop adds zero preview geometry — correct,
  the chute is physically off-object.
- **`tcr.purge_volume` consumers.** Grepped `*.purge_volume` across GCode.cpp, GCodeProcessor.cpp,
  GCodeViewer.cpp, Plater.cpp. The only consumer of `ToolChangeResult::purge_volume` outside WipeTower
  internals is the clamp at `append_tcr` (880-886). Nothing else reads it for sizing/preview, so a
  planner-vs-emission value divergence has no second observer.
- **Flush / filament statistics.** `flush_per_filament`, `total_used_filament`,
  `wipe_tower_volumes_per_extruder` are all computed by `GCodeProcessor` parsing the **emitted**
  G-code (`GCodeProcessor.cpp` 1537-1548, 3134, 3878-3890, 5935-5940). They reflect the floored poop,
  so the sidebar numbers match what the printer actually ejects. No stale planning estimate is shown.
- **Time estimate.** Also derived from the emitted chute moves; the extra floored purge time is
  counted. No desync.
- **`prime_volume` accounting.** Untouched by the feature; the tower deposit is unchanged. The chute
  poop and the tower prime are separate material budgets, so flooring one does not rob the other.
- **`volume_to_purge` / infill diversion.** `mark_wiping_extrusions` (Print.cpp 3331-3332) runs at
  planning time and bakes diverted purge into infill extrusions. The chute floor at emission adds
  *fresh* chute material on top; it does not retroactively reduce the already-assigned infill purge.
  Intended behavior, no conflict.
- **`grab_length` accounting.** `volume_to_purge -= grab_purge_volume` (Print.cpp 3337) and
  `wipe_volume -= grab_purge_volume` (GCode.cpp 7829) both run BEFORE the new floor. The floor is the
  outermost `std::max`, so it correctly establishes a hard minimum chute length even after grab-length
  reduction has driven the residual toward zero — which is the whole point of the feature.
- **`filament_minimal_purge_on_wipe_tower` (Type2) non-interference.** The chute option does not appear
  in `WipeTower2::extract_wipe_volumes` or the Type2 planning at Print.cpp 3447-3464. The two options
  are read in disjoint code paths and never combined. Confirmed the new option does NOT belong there
  and does not interfere.
- **`is_real_toolchange` guard matches planning.** `tcr.is_tool_change && tcr.initial_tool !=
  tcr.new_tool` (GCode.cpp 882). `construct_tcr` sets `is_tool_change=true` only for `tool_change_new`
  (where a non-zero `purge_volume` can exist) and `initial_tool`/`new_tool` from the actual tool pair
  (`WipeTower.cpp` 1250-1262). finish-layer/priming TCRs have `is_tool_change=false`. So the floor
  fires exactly when the planner could have produced a chute poop, and never spuriously — no emission
  of purge the planner did not anticipate.
- **`flush_count = std::max(1, …)` change** (GCode.cpp 969, 7934). Internally self-consistent:
  `flush_unit = purge_length / flush_count`, the loop emits `flush_count` segments summing to exactly
  `purge_length`, remaining `flush_length_N` zeroed (972-982). Emitted poop total == floored
  `purge_length`. The `max(1,…)` only affects how a (now-flooded) small volume is *segmented*, not the
  total, and prevents a 0-segment / divide-by-zero when `round(purge_volume/135) == 0`. No planning
  impact.

---

## Summary table

| Sev | Location | Issue |
|-----|----------|-------|
| Low | GCode.cpp:7843 | `set_extruder` clamp also affects Type2 tower path (via append_tcr2), broader than comment claims; harmless, never hit for BBL |
| Low | GCode.cpp:7843 | `set_extruder` clamp is dead code for the intended BBL workflow (BBL = Type1 → append_tcr) |

No Critical/High/Medium planning-consistency issues. Tower geometry, depth bar, preview, and all
user-facing statistics remain consistent between planning and the floored emission.
