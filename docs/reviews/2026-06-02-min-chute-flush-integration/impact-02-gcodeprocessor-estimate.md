# Impact review — GCodeProcessor / Preview / Statistics / Estimates

Commit: `eef00f7032` — "Add minimum chute flush length filament option"
Feature: per-filament `filament_minimal_purge_on_chute` (mm, default 0) floors the chute "poop" flush.
Clamp sites: `GCode.cpp` `WipeTowerIntegration::append_tcr` (~880) and `GCode::set_extruder` (~7843).

Lens: does the floored poop flow correctly into filament-used / flush-weight / cost / time
estimates, with NO divergence between **reported** and **emitted**?

## Verdict

No reported-vs-emitted divergence found. Every absolute estimate the user reads is derived
from the GCodeProcessor parsing the **emitted** (already-floored) G-code, so the floor
propagates uniformly into volume, weight, cost, per-filament breakdown, time, and the
"flushed filament" line. One low-severity completeness note on a matrix-derived comparison
stat, and one pre-existing-and-unchanged understatement in a G-code header comment.

---

## How the floored value reaches the estimates (the load-bearing chain)

1. Both clamp sites raise `purge_volume` / `wipe_volume` and recompute `purge_length` /
   `wipe_length` from it (GCode.cpp:883-886 and 7847-7850).
2. That length drives the `flush_length` and `flush_length_%d` placeholder keys
   (GCode.cpp:960, 974-975 / 7932, 7939-7940).
3. The Bambu `change_filament_gcode` template expands those keys into real `G1 E…` moves
   wrapped in ` FLUSH_START` / ` FLUSH_END` markers
   (`resources/profiles/BBL/machine/fdm_bbl_3dp_002_common.json`).
4. GCodeProcessor parses those markers (GCodeProcessor.cpp:99-107, 3098-3108), sets
   `m_flushing`, and at GCodeProcessor.cpp:3878-3890 integrates the **actual E-axis delta**
   of each unretract-while-flushing move into `update_flush_per_filament`.
5. `update_flush_per_filament` (GCodeProcessor.cpp:1537-1549) adds the volume to BOTH
   `flush_per_filament` AND `total_volumes_per_filament`.
6. These become `print_statistics.flush_per_filament` and
   `total_volumes_per_extruder` (GCodeProcessor.cpp:5935-5938).

So the **emitted** floored extrusion is exactly what gets counted. Confirmed downstream:

- **Main GUI weight / used filament / cost**: `DoExport::update_print_estimated_stats`
  (GCode.cpp:1916-1953) recomputes `total_used_filament` / `total_weight` / `total_cost`
  from `result.print_statistics.total_volumes_per_extruder`. This call (GCode.cpp:2187) runs
  AFTER full G-code processing and is the FINAL writer of `Print::m_print_statistics`,
  overwriting the earlier header-comment pass — so the GUI/cost figures include the floored
  flush.
- **"Flushed filament" line**: GCodeViewer.cpp:3596-3601 reads
  `m_print_statistics.flush_per_filament` directly — emitted value.
- **Per-filament used g/m (sliced-plate info, 3MF, SelectMachine AMS slots)**:
  `PlateData::parse_filament_info` (bbs_3mf.cpp:679-706) derives `info.used_g/used_m` from
  `ps.total_volumes_per_extruder` — emitted value.
- **SelectMachine send dialog weight**: SelectMachine.cpp:4299-4311 uses
  `print_statistics().total_weight` — the overwritten emitted value.
- **Per-color-change volumes** (partial times): `volumes_per_color_change`
  (GCodeProcessor.cpp:1480-1483, 5934) accumulated from emitted parse, not a matrix estimate.
- **Time estimate**: the FLUSH `G1 E…` moves are E-only moves; the time section treats them
  via `move_length = |delta_pos[E]|` (GCodeProcessor.cpp:3894-3897) and emits a TimeBlock
  (3927+). A larger floored flush → larger E delta → more time. Time accounts for the extra
  flush.

No parallel pre-computed flush estimate feeds any of these absolute figures.

---

## Findings

### [Low] ToolOrdering.cpp:140-172 — matrix-derived `filament_flush_weight` does not apply the chute floor
- **Interaction**: `calc_filament_change_info_by_toolorder` computes
  `FilamentChangeStats.filament_flush_weight` straight from the raw `flush_matrix`
  (line 155: `flush_matrix[extruder_id][last_filament][item]`), never applying
  `filament_minimal_purge_on_chute`. These stats populate
  `m_stats_by_single_extruder` / `_multi_extruder_*` (ToolOrdering.cpp:1338-1375,
  Print.hpp:860-862).
- **Why it is NOT a reported-vs-emitted bug**: this stat is consumed ONLY as a relative
  delta in the "single vs multi extruder grouping" savings hint —
  GCodeViewer.cpp:2910-2913 (`delta_weight_to_single_ext`, `delta_weight_to_best`) and
  SelectMachine.cpp:3105-3106 (`saving_weight = curr - best`). It is never shown as the
  print's absolute flushed weight. The chute floor adds (roughly) the same per-tool-change
  pad to every grouping option, so it largely cancels in the difference, and the
  optimization decision is unaffected.
- **Residual imperfection**: in a pathological mix where one grouping incurs many tiny
  matrix purges that the floor would lift and another grouping incurs few large ones, the
  *displayed* "saving" could be slightly optimistic relative to what the floored G-code
  actually flushes. Effect is small and only on an advisory number.
- **Fix (optional, low priority)**: when summing per-change flush at ToolOrdering.cpp:155,
  floor each change to `max(flush_matrix[...], filament_minimal_purge_on_chute[item] *
  cross_section_area[item])` before accumulating, mirroring the emitter. Not required for
  correctness of any user-facing absolute estimate. Note this is upstream (non-fork) code;
  patching it widens the diff from upstream.

### [Low] GCode.cpp:2338-2370 — `; filament used [g]` header comment excludes chute flush (pre-existing, unchanged)
- **Interaction**: the in-G-code header lines `; filament used [mm/cm3/g]` are built from
  `extruder.used_filament()` + wipe-tower-data estimate (GCode.cpp:2339-2340). The chute
  flush is emitted as a raw placeholder string (the `change_filament_gcode` template's
  `G1 E…`), which bypasses `Extruder::extrude()` (Extruder.cpp:30) — so `used_filament()`
  never reflected chute flush, before or after this change.
- **Why it is benign**: this is a long-standing property of Bambu flush accounting, not
  introduced by this commit. The authoritative GUI/3MF/AMS figures use the processor's
  `total_volumes_per_extruder` instead (see chain above), which DOES include the floored
  flush. The floor changes the emitted flush and the processor count together; the header
  comment's omission is symmetric (it omitted flush before too).
- **Fix**: none required for this feature. Left as a note so a future reader doesn't
  mistake the header `[g]` line for a divergence introduced here.

---

## Cross-checks performed (all consistent, no issue)

- `is_real_toolchange` guard (GCode.cpp:882) and `wipe_volume > EPSILON` guard (7847) only
  gate WHETHER the floor applies; the processor counts whatever is emitted regardless, so it
  self-reconciles — no path where reported and emitted diverge.
- `m_remaining_volume` attribution (GCodeProcessor.cpp:3881-3890, seeded 5850) only decides
  which filament a flush segment is charged to (old vs new); the TOTAL counted equals total
  emitted flush E either way.
- `flush_count = std::max(1, …)` guard (GCode.cpp:969, 7934): correctly removes the latent
  `purge_length / 0 = NaN` when `round(purge_volume/135) == 0`. With floor disabled and
  `purge_volume == 0`, `flush_unit = 0/1 = 0` → zero-length flush emitted, processor counts
  0. Harmless and consistent.
- `wipe_tower_volumes_per_extruder` / `model_volumes_per_extruder` /
  `support_volumes_per_extruder` (GCodeProcessor.cpp:1505-1532, 5935-5937): the chute poop
  is correctly classified as flush (not wipe-tower/model/support), so these category lines
  are unaffected — correct behaviour.
- Tooltip "about 40 mm (100 mm³)": matches `g_min_purge_volume = 100.f` (GCode.cpp:92);
  100 / (π/4·1.75²) ≈ 41.6 mm. Accurate.
