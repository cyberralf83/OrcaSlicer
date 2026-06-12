# Independent verification — per-color-change chute purge measurement

Adversarial re-measurement of the claim that each color change purges a median
~43.6 mm of filament feed to the chute, with a 100 mm³ floor.

Source file: `/tmp/orca_slice/plate_1.gcode` (52 MB, P1S, 0.4 mm nozzle, 6-color PLA).
Parser: independent Python (`/tmp/parse_flush.py`), line-by-line, no reuse of the
original assistant's code.

## Verdict (confirmed / refuted / partly)

**CONFIRMED — partly, with two framing corrections.**

The headline numbers reproduce essentially exactly. The method (sum `G1 E` inside
`; FLUSH_START` / `; FLUSH_END`, per `M620...A`→`M621...A` block) is sound for this
G-code: relative (M83) mode throughout, all-positive E inside the flush blocks,
retraction/de-retraction correctly outside the markers, flush physically located at
the front-left poop chute (X20 Y-3). Two things the assistant slightly overstated:

1. The "100 mm³ floor" is the **flush_length proper (41.575 mm = 100.00 mm³)**, not
   the 43.575 mm per-change figure. The per-change figure = flush_length + a fixed
   **2 mm "Compensate for filament spillage"** move (≈4.8 mm³). So the median chute
   waste per change is **104.8 mm³**, of which 100.0 mm³ is the flush floor and 4.8 mm³
   is the compensate dab. Calling the whole ~43.6 mm band "the 100 mm³ floor" conflates
   the two. (The assistant did acknowledge the +2 mm compensate, so this is a labeling
   imprecision, not a counting error.)

2. The "1 of 366 collapsed to near-0" is **not** a collapsed purge — it is the
   **initial tool load** (`M620 S5A ; switch material if AMS exist`, line 885), which
   structurally has no previous color to purge. So purge statistics really run over
   **365 color changes**, not 366. The 366 count is correct; the framing "only 1
   collapsed" reads as if one real change happened to purge nothing, which is misleading.

## My independent numbers

Tool-change blocks (`^M620 S\d+A` … `^M621 S\d+A`): **366** (matches claim).
(One trailing-comment M620 line meant a naive `…A$` grep undercounts to 365 — worth
noting as a parsing trap, but the correct count is 366.)

Per-change flush feed (signed sum of all `G1 E` between FLUSH markers, M83 relative):

| metric | my value | claim |
|---|---|---|
| n changes | 366 | 366 |
| median feed | **43.575 mm** | ~43.6 mm |
| min feed | **0.000 mm** | ~0 |
| max feed | **194.077 mm** | ~194 mm |
| % in [41,46] mm | **71.9 %** (263/366) | 72 % |
| near-0 (<1 mm) | **1** | 1 |
| total feed (all changes) | 23 060.9 mm | — |

Decomposition (this is the value-add of the re-check):

- **flush_length only** (per-change flush minus the 2 mm compensate): median **41.575 mm**,
  min 0, max 192.077.
- **241 / 366 changes (66 %)** have flush_length = **exactly 41.575 mm = 100.00 mm³** —
  the minimum-flush floor. (244 within ±0.05.)
- Compensate move = **2.0 mm on 365/366 changes** (0 on the initial load). Counted
  inside FLUSH markers, so it IS in the per-change figure.
- Flush blocks per change: 2→301, 3→36, 4→28, 0→1 (initial load). All 822 flush blocks
  are inside the 366 tool changes; zero orphan flush blocks elsewhere.

Unit conversion (filament area = π/4·1.75² = 2.40528 mm²):

- 41.575 mm → **100.00 mm³** (the true floor)
- 43.575 mm (median per-change) → **104.81 mm³**
- 41 mm → 98.6 mm³, 46 mm → 110.6 mm³ (so the [41,46] band ≈ 99–111 mm³)
- 100 mm³ ⇔ 41.58 mm feed exactly.

So "~43.6 mm ≈ 100 mm³" is **off by the 2 mm compensate**: 43.6 mm is ~105 mm³.
The floor is 100 mm³ at 41.58 mm. Both statements are individually almost-true but
the assistant glued the wrong feed number to the 100 mm³ floor.

## Method errors found

1. **Compensate move conflated with the floor.** Summing all E inside FLUSH markers
   correctly includes the `G1 E2 ;Compensate…` move (it sits in its own
   FLUSH_START/END block at the end of the change). That 2 mm is real chute spillage-
   prep, so including it in "chute purge" is defensible — but it must NOT be equated
   with the 100 mm³ flush floor. 100 mm³ = flush_length = 41.575 mm; the per-change
   chute total = ~104.8 mm³.

2. **"1 collapsed to near-0" mischaracterised.** That entry is the initial AMS load
   (line 885), not a degenerate color change. Purge stats should be quoted over 365
   changes; the lone 0 inflates the apparent spread of real purges.

3. **Parsing trap (did not affect the claim, but flag it).** The first real M620 line
   carries a trailing comment (`M620 S5A   ; switch material if AMS exist`). A
   `grep '…A$'` style match silently drops it and reports 365. The correct, robust
   match is `^M620 S\d+A` (token, not end-anchored). The 366 count is right.

## Things checked and found CORRECT (no error)

- **Relative-mode summing.** M83 is asserted at the top of every flush region; the
  block uses pure relative E deltas, so summing the raw E numbers is correct (not
  absolute positions). No M82 inside flush. Verified the modes set per change = {M83}.
- **Retraction / de-retraction excluded.** The `G1 E-2 F1800` / `G1 E2 F300` wipe pairs
  and the final `G1 E-2` sit OUTSIDE FLUSH markers and are correctly not summed. Their
  net signed sum outside flush is **+16 mm per change** (= the +18 mm long-retraction-
  when-cut de-retract `G1 E18` minus the three net −2/−2/+2/+2/−2 wipe moves), all
  legitimately excluded. None of these should be subtracted from the flush figure — the
  flush already starts from `G92 E0` after the de-retract, so no double counting.
- **The `G1 E18` de-retract is NOT purge.** It re-feeds the 18 mm the firmware pulled
  back during the filament cut (`M620.11 … E-18`). Correctly outside the flush and
  correctly not counted as new chute waste.
- **Location = chute.** Immediately before FLUSH_START the toolhead travels to
  `X20 Y50` → `Y-3` (front-left poop/cut chute on a P1S). `flush_into_objects = 0`, so
  no flush diverts into the model. `purge_in_prime_tower = 0`, so despite
  `enable_prime_tower = 1` the purge is NOT going to the tower — it goes to the chute.
  The per-change figure is genuinely chute waste (already net of flush_into_infill=1 /
  flush_into_support=1, which is the desired "what actually reaches the chute" number).
- **No E moves missed.** All flush E moves are pure `G1 E…` (no `G1 X… E…` extrude-
  while-travel), no indentation that would defeat the `^G1 E` anchor, and the pulsatile
  sub-move coefficients sum to 1.0 (`0.02·4 + 0.23·4 = 1.0`), so the captured sum equals
  the full flush_length with no rounding loss. Reconstructed flush_length_1 = 41.575 mm
  exactly from the template.

## Bottom line for the caller

Numbers are solid: 366 changes, median 43.575 mm, min 0, max 194.077, 71.9 % in
[41,46], 1 near-0 — all reproduced. Two corrections: (a) median per-change chute waste
is **~104.8 mm³**, and the **100 mm³ floor is the flush_length (41.58 mm)**, with a
separate fixed +2 mm/+4.8 mm³ compensate move on top — don't equate "43.6 mm" with
"100 mm³"; (b) the single 0 is the initial AMS load, so real-purge stats are over 365
changes.

## Sources

- `/tmp/orca_slice/plate_1.gcode` — header config: lines 103 (enable_prime_tower=1),
  194–198 (flush_into_infill/objects/support, flush_multiplier), 406–407
  (prime_tower_width/prime_volume), 430 (purge_in_prime_tower=0); change_filament_gcode
  template at line 62.
- Example tool change: lines 3325–3450 (S4A); flush location travel lines 3346–3347
  (X20 Y50 → Y-3); de-retract `G1 E18` line 3358; flush blocks 3365–3382, 3392-ish;
  compensate `G1 E2 ;Compensate…`; wipe pairs `G1 E-2`/`G1 E2` lines 3383–3384.
- Initial load (the lone 0): lines 885–896 (`M620 S5A ; switch material if AMS exist`).
- Parser: `/tmp/parse_flush.py`, floor check `/tmp/verify_floor.py`.
- Filament area π/4·1.75² = 2.40528 mm²; 41.575 mm → 100.00 mm³, 43.575 mm → 104.81 mm³.
