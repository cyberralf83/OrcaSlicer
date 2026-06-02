# Impact review 01 — downstream G-code flush pipeline & change_filament_gcode template

Commit: `eef00f7032` "Add minimum chute flush length filament option"
Lens: how the floored `purge_length` / `purge_volume` / `wipe_length` propagate into the
`change_filament_gcode` placeholder consumers (`first_flush_volume`, `second_flush_volume`,
`flush_length`, `flush_length_1..4`, `flush_count`, `M620.10 L[flush_length]`, FLUSH/WIPE gates).

Scope: outward tracing only (downstream consumers + the actual BBL templates). Diff-local
correctness not re-litigated. `.github/` ignored.

## What the clamp produces (the two emitters)

- WT/Type1 path — `WipeTowerIntegration::append_tcr` (GCode.cpp:883-886, 960, 969-982).
  `purge_volume = (tcr.purge_volume < EPSILON) ? (real-toolchange ? max(min_chute_purge, 100) : 0)
  : max(tcr.purge_volume, 100, min_chute_purge)`. `purge_length = purge_volume / area`.
- Type2 + no-wipe-tower path — `GCode::set_extruder` (GCode.cpp:7842-7850, 7932, 7934-7947).
  `if (min_chute_purge > EPSILON && wipe_volume > EPSILON) wipe_volume = max(wipe_volume,
  min_chute_purge); wipe_length = wipe_volume / area`.

Constants (GCode.cpp:92-94): `g_min_purge_volume=100 mm³`, `g_purge_volume_one_time=135 mm³`,
`g_max_flush_count=4`. For 1.75 mm filament `area≈2.4053 mm²`, so 100 mm³ ≈ 41.6 mm.

## Emitter coverage — every toolchange path is clamped (verified, no gap)

`tool_change()` dispatch (GCode.cpp:1500-1568):
- `WipeTowerType::Type2` → `append_tcr2` (1503/1536) → `set_extruder` (1218) → **clamped** at 7847.
- otherwise → `append_tcr` (1555/1565) → **clamped** at 883.
- `prime()` (1503) also routes Type2 priming through `append_tcr2` → `set_extruder`.
- non-wipe-tower toolchanges call `set_extruder` directly (1218-context / 5228 / 3235 / 3319) →
  clamped. `set_extruder` is only reachable when there is no wipe tower (header comment GCode.cpp:7754),
  so `append_tcr` (Type1) and `set_extruder` (Type2/none) are mutually exclusive — no double clamp.

Verdict: there is no toolchange emitter that bypasses the clamp. The commit message's
"(BBL/Type1)" vs "(Type2 / no-wipe-tower)" mapping is accurate.

## Downstream placeholder consumers — all read the floored value (no stale-value bug)

Grep of every `purge_volume/purge_length/wipe_volume/wipe_length` use (GCode.cpp:883-986, 7842-7947):
- `first_flush_volume` / `second_flush_volume` (931-932 / 7881-7882) = floored `purge_length/2`.
- `flush_length` (960 / 7932) = floored total `purge_length`.
- `flush_length_1..4` (969-982 / 7934-7947) computed from floored `purge_length` and floored-derived
  `flush_count`.
- `flush_count` (969 / 7934) `= max(1, min(4, round(floored_volume / 135)))`.

No consumer reads the pre-clamp `tcr.purge_volume` / matrix `wipe_volume` after the clamp point.
No inconsistency between a "before" and "after" value. CONFIRMED CLEAN.

`first_flush_volume` / `second_flush_volume` are **not referenced by any BBL template**
(only `resources/profiles/LH/machine/fdm_lh_mmu_common.json` consumes them). For BBL they are inert
regardless of flooring.

## Template threshold interactions (this is the point of the feature, and it behaves)

Templates examined: `fdm_bbl_3dp_001_common.json`, `fdm_bbl_3dp_002_common.json`, and the leaf
overrides for X1C (001-style, per-segment G1 flush), A1 / H2S / P2S (002-style, per-segment + WIPE
gates), H2D / X2D (002-style, firmware `M620.10 L[flush_length]` flush, G1 flush commented out).

Per-segment value fed to the template is `flush_unit = purge_length / flush_count` (uniform across
the `flush_count` active segments; remaining segments forced to 0). Gate behavior as the user raises
the chute minimum (1.75 mm filament):

| purge_vol mm³ | purge_len mm | flush_count | flush_length_N mm | gates crossed |
|---|---|---|---|---|
| 100 (default floor) | 41.6 | 1 | 41.6 | `>1` flush, `>23.7` pulsatile |
| 120 | 49.9 | 1 | 49.9 | + `>45` WIPE-on-2 (no seg 2, so no WIPE) |
| 240 | 99.8 | 2 | 49.9 | WIPE@1-2 fires |
| 360 | 149.7 | 3 | 49.9 | WIPE@1-2, @2-3 |
| 481 | 200.0 | 4 | 50.0 | WIPE@1-2/2-3/3-4 |

- The `>23.7` gate (pulsatile vs plain extrude, `flush_length_1`): the default 100 mm³ floor already
  lands at 41.6 mm > 23.7, and any chute minimum that does anything keeps it above 23.7, so the
  feature only ever keeps/strengthens pulsatile flushing — no surprising switch to plain extrude.
- The `>45 && flush_length_2>1` WIPE gate (002/A1/H2S): these are the standard purge-shaking moves
  whose whole purpose is to knock the poop free. Raising the chute minimum so these fire is exactly
  the intended effect, and it follows the identical threshold logic the slicer already used for large
  matrix-driven purges. No misbehavior.
- The H2D/X2D firmware path consumes the **total** floored `flush_length` directly in
  `M620.10 ... L[flush_length]` and in `SYNC T{ceil(flush_length / 125) * 5}`. Both scale benignly
  with the floor (longer firmware flush + slightly longer sync wait). `flush_length` is derived from
  the same floored `purge_volume` as `flush_length_1..4`, so total and per-segment stay consistent.

Sum-consistency: `flush_count * flush_unit == purge_length == flush_length` (uniform split), so the
sum of the per-segment moves equals the advertised total and the firmware `L[flush_length]`. No drift.

## Findings

### [Low / informational] flush_count `max(1,…)` guard — real latent div-by-zero existed, but its result was previously unused
GCode.cpp:7934 (and mirrored 969).
Interaction: pre-commit (`eef00f7032~1`) the Type2/`set_extruder` path computed
`flush_count = min(4, round(wipe_volume/135))` with **no** `g_min_purge_volume` floor on
`wipe_volume`. For `EPSILON < wipe_volume < 67.5 mm³` (matrix purge minus grab_length can land here),
`round(...) == 0` → `flush_count == 0` → `flush_unit = wipe_length / 0` (inf/nan). That inf was
written to `flush_unit` but the segment loop `for(; flush_idx < flush_count; …)` never executed, so
no `flush_length_N` ever received the inf (all stayed 0 from the zero-fill loop). The corrupt value
was therefore computed but discarded. The WT/`append_tcr` path was never exposed (its nonzero branch
always floored to ≥100 → `round(100/135)=1`).
Why it matters now: the `max(1,…)` guard's actual user-visible effect is not "fixing a NaN in the
output" but **emitting one flush segment where the old code emitted none** for small sub-67 mm³
flushes. That is a (small) behavior change for users with small-purge flush matrices even when the
new chute option is OFF — a fixed sub-68 mm³ flush that previously produced zero G1 flush moves now
produces one `flush_length_1 = wipe_length` move. This is almost certainly desirable (a tiny flush
that was being silently dropped now actually flushes), and matches the stated intent, but the commit
message frames it purely as a div-by-zero fix; the real downstream change is "always ≥1 flush
segment." Recommend documenting that the guard slightly changes default-config output for small
flushes, so a future upstream merge conflict here is understood.
Fix: none required — behavior is correct and an improvement. Optional: note in the commit/feature doc
that `flush_count = max(1, …)` now guarantees a flush segment for any nonzero flush, independent of
the chute option.

### [Low / informational] H2D/X2D firmware flush is not counted by GCodeProcessor (pre-existing, not a regression)
Templates: H2D/X2D do their flush via `M620.10 L[flush_length]` with the `;VG1` G1 flush moves
commented out. `GCodeProcessor` accounts flush from real `FLUSH_START/FLUSH_END` G1 E moves
(GCodeProcessor.cpp:3883-3888), so on H2D the firmware-side flush volume is invisible to filament-use
statistics. The chute minimum increases `L[flush_length]` (more real wasted filament) but the
reported flush-per-filament total will **not** rise for H2D/X2D, so the slicer's filament estimate
will under-report the extra chute purge on these printers. This mis-accounting predates this commit
(it is inherent to the firmware-flush template design), and the change does not make it worse in kind
— only in magnitude (larger uncounted flush). Flagged for awareness only; the 001-style printers
(X1C/P1S/A1/H2S/P2S) flush via real G1 moves and **are** counted correctly, so their estimates track
the floor.
Fix: out of scope for this change. Would require firmware-flush volume accounting in GCodeProcessor,
an upstream concern.

## Net verdict

The floored values propagate correctly and consistently into every downstream consumer and into
every BBL template variant (per-segment G1 flush, per-segment + WIPE gates, and firmware
`M620.10 L[flush_length]`). Threshold crossings (`>1`, `>23.7`, `>45`) move in the intended
direction — more purge → pulsatile + WIPE shaking — with no surprising macro misbehavior. No
stale-pre-clamp value is consumed anywhere. The only notable downstream effects are two
informational, pre-existing-in-kind items: the `max(1,…)` guard now always emits ≥1 flush segment
(a small default-config behavior improvement, under-described by the commit message), and H2D/X2D
firmware flush remains uncounted by filament statistics (pre-existing template design, magnified by a
higher floor). No correctness defect introduced in the flush pipeline.
