# Impact Review 04 — ToolOrdering / Flush Matrix / Filament-Change Accounting

**Commit:** `eef00f7032` — "Add minimum chute flush length filament option"
**Lens:** Interaction of the new emission-time chute floor (`filament_minimal_purge_on_chute`)
with the purge accounting that runs BEFORE it (ToolOrdering flush stats, flush matrix,
filament-map grouping, infill diversion `mark_wiping_extrusions`, `grab_purge_volume`).
**Scope:** `.github/` ignored.

## How the pieces connect (traced)

1. **Flush matrix** is built from `flush_volumes_matrix` (× `flush_multiplier`) in
   `ToolOrdering::reorder_extruders_for_minimum_flush_volume`
   (`src/libslic3r/GCode/ToolOrdering.cpp:1227-1249`) and in
   `Print::_make_wipe_tower` (`src/libslic3r/Print.cpp:3296-3304`). The new option is
   **never** mixed into the matrix.
2. **Grouping / stats** — `calc_filament_change_info_by_toolorder`
   (`ToolOrdering.cpp:140-172`) sums `flush_matrix[ext][last][item]` (un-floored) into
   `m_stats_by_single_extruder` / `m_stats_by_multi_extruder_best` / `_curr`
   (`ToolOrdering.cpp:1338-1376`). The filament-map auto algorithm
   (`reorder_filaments_for_minimum_flush_volume`, `get_recommended_filament_maps`) likewise
   uses only the un-floored matrix.
3. **Diversion** — `Print::_make_wipe_tower` reduces the planned purge per tool change:
   `mark_wiping_extrusions` diverts flush into object infill/perimeters/support, then
   `grab_purge_volume` (`grab_length * 2.4`) is subtracted, then clamped to `>= 0`
   (`Print.cpp:3327-3337`). The residual becomes `tcr.purge_volume`.
4. **Emission (our clamp)** — `WipeTowerIntegration::append_tcr` (`GCode.cpp:~879-983`,
   Type1/BBL) and `GCode::set_extruder` (`GCode.cpp:~7842-7850`, Type2/no-wipe-tower) floor
   the residual to `max(user, g_min_purge_volume)` and feed it to `change_filament_gcode`
   via `flush_length` / `flush_length_N`. The floored purge is **emitted as real G-code**,
   so `GCodeProcessor` re-parses it and the *actual* total-filament statistics reflect the
   floor.

`filament_minimal_purge_on_chute` appears ONLY at step 4 (verified:
`grep -rn filament_minimal_purge_on_chute src/` → only GCode.cpp emission, plus
config/whitelist/invalidation/UI wiring). It does not touch the matrix, grouping, or the
wipe-tower layout/depth.

---

## Findings

### [Medium] GCode.cpp:7847 — Type2 floor silently skipped on diverted/grab-consumed real tool changes (path asymmetry)

`set_extruder` gates the floor on `wipe_volume > EPSILON`:

```cpp
if (min_chute_purge > EPSILON && wipe_volume > EPSILON) {  // 7847
    wipe_volume = std::max(wipe_volume, min_chute_purge);
```

But `wipe_volume` was just driven to **0** at `GCode.cpp:7829`
(`wipe_volume = std::max(0.f, wipe_volume - grab_purge_volume)`) whenever `grab_length`
(× 2.4) ≥ the flush volume — e.g. a small flush_volumes_matrix entry on a printer with a
non-trivial grab length. That is precisely the "most of the flush was already consumed /
diverted, residual ≈ 0" case the feature exists to rescue, yet on the Type2 path the floor
is **not** applied (`0 > EPSILON` is false), so no minimum poop is emitted.

The Type1/BBL path handles the symmetric case correctly: its `< EPSILON` branch
(`GCode.cpp:~881`) applies `max(min_chute_purge, g_min_purge_volume)` when
`is_real_toolchange`. So the two emitters disagree: append_tcr floors a real tool change
whose residual fell to ~0; set_extruder does not.

**Why it matters:** the no-wipe-tower / Type2 BBL configuration (the more common single-AMS
"chute poop" scenario the commit message describes) is exactly where the minimum can fail to
take effect, defeating the feature for that path. It is silent (no warning), config-dependent
(only when grab/diversion zeroes the residual), hence Medium not High.

**Fix:** mirror the Type1 logic — when this is a genuine tool change
(`old_extruder_id`/`old_filament_id` valid and a flush was attempted), apply the floor even
if the post-grab residual is 0. E.g. compute `min_chute_purge` and apply
`wipe_volume = std::max(wipe_volume, min_chute_purge)` for any real change (guard on the
"filament present / real change" branch that already wraps lines 7798-7834), not on
`wipe_volume > EPSILON`. Be careful to still skip same-filament / no-change cases.

---

### [Low] GCode.cpp:~882 — `>= EPSILON` branch floors finish-layer / same-tool TCRs (contradicts stated intent)

In append_tcr the non-zero branch is:

```cpp
: std::max({tcr.purge_volume, g_min_purge_volume, min_chute_purge});
```

This branch does **not** consult `is_real_toolchange` (only the `< EPSILON` branch does).
`append_tcr` is reached for sparse / finish-layer / block TCRs too
(`GCode.cpp:1555,1565`; `construct_block_tcr` sets `is_tool_change=false`,
`initial_tool==new_tool` but can still carry a non-zero `purge_volume`, e.g. the tower
interface purge at `WipeTower.cpp:2790-2796`). Such a TCR with
`purge_volume >= EPSILON` will be padded up to `min_chute_purge` despite the commit message
asserting "finish-layer / priming / same-tool results keep the original behaviour."

**Why it matters:** at most it lengthens an already-present interface/finish purge by the
user minimum on a non-tool-change segment — small, and only when the user enables the option
and an interface purge already exists. No accounting corruption (it is emitted and re-parsed).
Low.

**Fix:** if strict adherence to the documented intent is desired, gate the `min_chute_purge`
term in the `>= EPSILON` branch on `is_real_toolchange` as well:
`std::max({tcr.purge_volume, g_min_purge_volume, is_real_toolchange ? min_chute_purge : 0.f})`.

---

### [Low] ToolOrdering.cpp:140 / GCodeViewer.cpp:2910 / SelectMachine.cpp:3106 — flush-weight stats understate emitted filament when the floor pads near-zero purges

`m_stats_by_*` (filament_flush_weight) are computed purely from the un-floored matrix
(`calc_filament_change_info_by_toolorder`). When the chute floor pads a residual that the
matrix recorded as ~0 (because it was diverted into infill), the emitted G-code now contains
more chute purge than these stats account for. The stats therefore **understate** real flush
filament by `n_padded_changes × min_chute_purge` (× density).

**Why it (mostly) does NOT matter:**
- The user-visible consumers display only *deltas* between two stats computed the **same**
  way: `curr - best` ("Cost Xg … more than optimal grouping",
  `SelectMachine.cpp:3105-3114`) and the GCodeViewer grouping hints
  (`GCodeViewer.cpp:2910-2913`). The floor adds the same per-change padding to both sides, so
  the *grouping recommendation and its delta are unchanged*.
- The authoritative total-filament weight/length shown to the user comes from
  `GCodeProcessor` parsing the emitted gcode, which **does** include the floored purge —
  so the headline numbers stay consistent.

The residual inaccuracy is only in the absolute `filament_flush_weight` advisory, which is
not surfaced as an absolute figure. Hence Low / informational, not a fix-required defect.

**Fix (optional):** none required. If absolute flush stats are ever surfaced, the floor would
need to be folded into `calc_filament_change_info_by_toolorder`, which would in turn require
the per-filament min length and the diverted/grab amounts to be threaded into ToolOrdering —
a much larger change. Not worth it for the current advisory-only usage.

---

## Cleared (checked, no issue)

- **No double-count between infill diversion and the chute floor.** `mark_wiping_extrusions`
  overrides infill/perimeter extruders so the diverted material is printed as the model
  (real geometry), and the residual chute purge is a separate physical ejection. Flooring the
  chute adds genuinely-intended extra ejection; it does not re-extrude the diverted volume.
  Total filament is correctly accounted because the floored purge is emitted and re-parsed.
- **Filament-map auto algorithm is unaffected.** It runs on the un-floored matrix only; the
  minimum is a pure emission-time concern and cannot change which extruder a filament is
  grouped onto. The `Print.cpp:289` invalidation re-runs `psWipeTower` (hence ToolOrdering),
  but the recomputed maps/stats are identical w.r.t. this option.
- **Wipe-tower geometry/depth not corrupted.** Tower depth uses the planned (un-floored)
  `tcr.purge_volume` (`WipeTower.cpp:2779`); the BBL chute poop is ejected via
  `change_filament_gcode`, not deposited on the tower, so no reserved-space mismatch.
- **`flush_count = max(1, …)` guard** is consistent across both emitters
  (`GCode.cpp:969`, `:7934`) and only affects segmentation of the (already-floored) purge —
  no accounting effect.
- **Guard fields are reliable.** `is_real_toolchange = is_tool_change && initial_tool !=
  new_tool` is correctly populated by `construct_tcr` (real) vs `construct_block_tcr`
  (finish/block, `is_tool_change=false`, `initial_tool==new_tool`).

---

## Verdict

No Critical/High accounting corruption. The floor is a clean emission-time addition that the
G-code re-parse keeps consistent in the headline filament totals, and it does not perturb the
filament-map grouping decision (deltas computed symmetrically). One Medium behavioural gap:
the **Type2/no-wipe-tower path fails to enforce the minimum when grab_length/diversion zeroes
the residual** (`GCode.cpp:7847` `wipe_volume > EPSILON` guard) — the very case the feature
targets — making it asymmetric with the Type1 path. Two Low items (finish-layer flooring in
the `>= EPSILON` branch; advisory flush-weight stats understating padded purge).
