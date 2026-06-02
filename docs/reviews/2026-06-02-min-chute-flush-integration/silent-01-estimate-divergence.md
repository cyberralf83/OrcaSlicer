# Silent estimate-vs-emission divergence audit — `filament_minimal_purge_on_chute`

Commit: `eef00f7032` ("Add minimum chute flush length filament option")
Focus: silent divergence between what is REPORTED to the user and what is ACTUALLY emitted,
caused by flooring the chute purge at G-code EMISSION (after the matrix / ToolOrdering / WipeTower
estimates are already computed). Scope: estimate/accounting integration only.

## TL;DR / verdict

The headline post-slice numbers are SAFE. Both floor sites emit the extra purge as real
`G1 E…` moves inside the BBL `change_filament_gcode`, which wraps them in `; FLUSH_START` /
`; FLUSH_END`. `GCodeProcessor` re-parses those moves, so `flush_per_filament`,
`total_volumes_per_extruder`, `total_weight`, `total_cost`, `total_used_filament`, the per-filament
"Flushed" legend column, and the GCodeProcessor time estimate all reflect the FLOORED value. The
preview is self-consistent with the emitted G-code on those numbers.

The real divergence is confined to the **pre-slice / matrix-based `FilamentChangeStats` comparison
estimate** (ToolOrdering `calc_filament_change_info_by_toolorder`). That estimator computes flush
weight straight from the flush matrix and NEVER applies the chute floor. It feeds only the
single-vs-multi-extruder *recommendation* deltas (GCodeViewer color-arrangement panel, SelectMachine
"Cost Ng filament … more than optimal grouping"). Because both sides of those deltas get the same
per-filament floor, the deltas barely move — so this is Low. There is no Critical/High silent
divergence on any absolute weight/cost/time number the user reads as "what the print will consume."

---

## Finding 1 — ToolOrdering flush-weight estimate ignores the chute floor (absolute number wrong, but only shown as a delta)

- Severity: **Low**
- File:line:
  - `src/libslic3r/GCode/ToolOrdering.cpp:140-172` (`calc_filament_change_info_by_toolorder`,
    flush weight derived solely from `flush_matrix[...][...]`)
  - consumed at `src/slic3r/GUI/GCodeViewer.cpp:2908-2918` and
    `src/slic3r/GUI/SelectMachine.cpp:3101-3114`
- Masked divergence: `FilamentChangeStats.filament_flush_weight` is computed from the raw flush
  matrix volume per tool change. When `filament_minimal_purge_on_chute` floors a real tool change
  whose matrix volume was below the floor (the headline use case — purge diverted into infill so
  `tcr.purge_volume → ~0`), the actual emitted poop is larger than the matrix value. This estimator
  reports the smaller, un-floored weight. So the absolute "flushing weight" in the grouping
  recommendation is understated relative to what the machine ejects.
- Why nobody notices: the value is only ever presented as a difference between two scenarios that
  both use the identical per-filament floor (`stats_by_single_extruder` − `stats_by_multi_extruder_curr`,
  and `curr` − `best`). The floor is a per-filament constant, so it adds an (approximately) equal
  term to both operands and largely cancels in the delta. The user sees "save Xg by regrouping," not
  an absolute flush total here, so the error is masked twice: once because it cancels, once because
  the authoritative absolute flush number comes from the GCodeProcessor path (Finding 4) which IS
  floored. The two panels therefore disagree internally (matrix estimate un-floored, legend floored),
  but only an attentive user comparing the recommendation panel against the filament legend would spot it.
- Fix: lowest-effort and consistent with intent is to leave this estimator alone (it is explicitly a
  *grouping comparison*, not a consumption report) and document that it is matrix-only. If exactness
  is wanted, apply the same `max(matrix_volume, min_chute_length * filament_area)` floor inside the
  per-change loop at `ToolOrdering.cpp:155` before accumulating `flush_volume_per_filament`, guarded
  by the same "real tool change" condition used in `append_tcr`. Note this estimator has no notion of
  `tcr.is_tool_change`/`g_min_purge_volume`, so a faithful floor would have to mirror the EPSILON and
  `g_min_purge_volume` logic to avoid re-introducing a different divergence.

## Finding 2 — `is_real_toolchange` floor predicate differs between the two emit sites (no current user-visible miscount, but a latent divergence)

- Severity: **Low**
- File:line: `src/libslic3r/GCode.cpp:882-885` (Type1 / wipe-tower path) vs
  `src/libslic3r/GCode.cpp:7847-7850` (Type2 / no-wipe-tower path)
- Masked divergence: the two paths gate the floor on different conditions.
  - Type1 floors when `tcr.is_tool_change && tcr.initial_tool != tcr.new_tool` (covers the
    `tcr.purge_volume < EPSILON` case via the ternary, padding even a zero-matrix tool change).
  - Type2 floors only when `wipe_volume > EPSILON`. After `wipe_volume = max(0, wipe_volume −
    grab_purge_volume)` at line 7829, a genuine tool change whose matrix flush is fully consumed by
    `grab_purge_volume` (or diverted) collapses to `wipe_volume == 0` and is therefore NOT floored on
    the Type2 path, even though the equivalent Type1 case IS floored.
  Result: for the same model+filaments, the floor "kicks in" on one printer topology and silently
  does nothing on the other. The user enabling the option expecting a guaranteed minimum poop gets it
  on wipe-tower/Type1 BBL flow but not necessarily on the Type2/no-wipe-tower flow.
- Why nobody notices: there is no warning when the floor is skipped; the feature simply has no effect
  in that branch, and the emitted G-code (and the GCodeProcessor stats that follow it) are
  self-consistent, so no number looks "wrong" — the poop is just absent. The divergence is between
  *user expectation* and *behavior*, and between the two code paths, not between two displayed numbers.
- Fix: make the Type2 predicate match the Type1 intent — pad genuine tool changes even when
  `wipe_volume` has collapsed to 0, e.g. compute an `is_real_toolchange` flag from the actual
  old/new filament ids on the Type2 path and apply `wipe_volume = max(min_chute_purge,
  g_min_purge_volume)` in the zero case, mirroring lines 883-885. Document explicitly if the
  asymmetry is intentional (e.g. Type2 has no chute) so reviewers do not read it as a bug.

## Finding 3 — `flush_count = max(1, …)` guard now forces a flush segment whose length can be < the real per-segment minimum (cosmetic estimate-vs-emit, self-consistent)

- Severity: **Low**
- File:line: `src/libslic3r/GCode.cpp:969` and `src/libslic3r/GCode.cpp:7934`
- Masked divergence: previously `flush_count` could round to 0 for tiny purge volumes, yielding no
  `flush_length_N` (and the BBL template's `{if flush_length_1 > 1}` guard suppressing the FLUSH
  block). Now at least one segment is always produced. For a floored small poop this is the intended
  behavior and is the whole point of the feature. The only divergence is internal: `flush_unit =
  purge_length / flush_count` with `flush_count` clamped up to 1 can produce a single segment shorter
  than `g_purge_volume_one_time` would imply — but since the template emits exactly `flush_length_1`,
  the emitted volume equals `purge_length`, and GCodeProcessor parses exactly that. So emission and
  accounting still agree.
- Why nobody notices: there is no separate "estimated flush" number that uses `flush_count *
  g_purge_volume_one_time`; everything downstream uses `purge_length`/`flush_unit` directly or
  re-parses the emitted move. No displayed number diverges.
- Fix: none required for correctness. The commit message already calls out the divide-by-zero fix.
  Recommend a one-line comment confirming that `flush_unit` is derived from `purge_length` (not from
  `g_purge_volume_one_time`) so a future reader does not "fix" it into a real divergence.

## Finding 4 — Authoritative post-slice path is correct (documented as a non-finding to prevent a false fix)

- Severity: **Informational / not a defect**
- File:line:
  - emit (Type1): `src/libslic3r/GCode.cpp:960,975` (`flush_length`, `flush_length_N`)
  - emit (Type2): `src/libslic3r/GCode.cpp:7932,7940`
  - BBL template wraps these in `; FLUSH_START` / `; FLUSH_END` (e.g.
    `resources/profiles/BBL/machine/Bambu Lab A1 0.4 nozzle.json` `change_filament_gcode`)
  - parse/accumulate: `GCodeProcessor.cpp:3099-3106` (`m_flushing` toggle),
    `GCodeProcessor.cpp:3878-3891` (Unretract-while-flushing → `update_flush_per_filament` + time),
    `GCodeProcessor.cpp:5938` (`flush_per_filament` published)
  - consume: `total_volumes_per_extruder` → `GCode.cpp:1930-1950` (`total_weight`/`total_cost`/
    `total_used_filament`), legend `GCodeViewer.cpp:3596-3608`, all-plates `GCodeViewer.cpp:2650-2654`.
- Masked divergence: NONE. Because the floored purge is emitted as real extrusion inside FLUSH tags,
  the GCodeProcessor reconstructs the exact floored volume and time. Every absolute number the user
  reads as "this print will use X g / cost Y / take Z" is computed from that reconstruction, so it
  matches the printer.
- Action: do NOT add a second flooring step in `total_volumes_per_extruder` / Print statistics — that
  would DOUBLE-count the poop and create a real over-estimate. The floor must remain emission-only.

---

## What was checked and ruled out

- `ToolOrdering::calc_filament_change_info_by_toolorder` — matrix only, no GCodeProcessor link →
  Finding 1 (delta-only, Low).
- `WipeTower` `purge_volume` (`WipeTower.cpp:1267,1290,2504`) — feeds `tcr.purge_volume`, which is
  the INPUT to the floor; the floored output is what gets emitted and re-parsed. No separate
  wipe-tower-volume estimate bypasses the floor for the chute poop (the poop goes through
  `change_filament_gcode`/FLUSH tags, not `wipe_tower_volumes_per_extruder`).
- Headline weight/cost/time (`GCode.cpp:1920-1950`) — sourced from GCodeProcessor result → correct.
- Per-filament Flushed/WipeTower/Support legend (`GCodeViewer.cpp:3585-3608`) — sourced from
  `m_print_statistics` (GCodeProcessor) → correct.
- SelectMachine grouping cost (`SelectMachine.cpp:3105-3114`) — delta of matrix stats → Finding 1.
- Time estimate of the extra forced flush — GCodeProcessor times the actual emitted FLUSH G1 E moves
  (`GCodeProcessor.cpp:3893+` after the flush attribution), so the forced poop IS in the time
  estimate. Not a divergence.
