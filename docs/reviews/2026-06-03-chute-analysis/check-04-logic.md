# Logic & Reasoning Check — Chute Analysis

Adversarial review of the assistant's *reasoning* (not its arithmetic — that is
covered in `check-02-arithmetic.md`, which found all 11 numbers correct). Every
empirical claim below was re-verified against the source and the real slice
(`/tmp/orca_slice/plate_1.gcode`, P1S, 0.4 mm, 6-colour, 366 tool changes).

## Verdict

The assistant's **measurements are solid and reproduce exactly**; its **core
contradiction (E) is real**; but its **conclusion (F) over-reaches** by treating
one sliced file as proof of a behavioural diagnosis, and by presenting the
worm-vs-tail dichotomy as if those were the only two options. The single most
useful, defensible fact for the user is buried under the confusing "2.6 feet"
framing and a string of retracted guesses.

Key independently-verified facts that anchor everything:

- **The chute flush is extruded in free air over the chute** (X70–120, Y245–265,
  back-left wiper area), pulsatile (alternating `F523/F50`), then "shake to put
  down garbage" + "wipe and shake". It is **not** laid on the wipe tower.
  (`GCode.cpp` change_filament_gcode; confirmed in the slice at the per-toolchange
  `FLUSH_START`/`FLUSH_END` blocks.)
- **The prime tower is separate** (510 regions, `purge_in_prime_tower=0`,
  prime_volume=15) — it primes the next colour; it is *not* where the 100 mm³ goes.
  With `flush_into_infill=1`, diverted purge goes into object infill; the remainder
  hits the chute.
- **`g_min_purge_volume = 100.f`** (`GCode.cpp:92`) is an *unconditional* floor in
  the stock upstream code — it applies to every purge regardless of the new feature.
- This user set **`minimal_chute_flush_length = 10`** mm → 10 × A_fil (2.405) =
  **24 mm³**, which is *below* the 100 mm³ floor, so the new feature is inert here;
  the 100 mm³ the assistant measured is the pre-existing upstream floor.
- Re-measured per-tool-change chute flush feed: **median 41.58 mm flush + 2 mm
  compensate = 43.58 mm (median 100 mm³ flush-only / 104.8 mm³ incl. compensate)**,
  mean 61 mm, max 194 mm; **245/366 (67%) sit exactly on the 100 mm³ floor**
  (68.9% within 1 mm); **exactly 1 of 366 collapsed to ~0**. This matches the
  assistant's claim D (its "72% at floor" is the same finding at a looser tolerance).

## Claim-by-claim (solid / speculative / wrong)

| Claim | Status | Basis |
|---|---|---|
| **A** — "chute poop is a fat ~2 mm coily worm" (early) | **Was SPECULATIVE, and the retraction was a MISTAKE** | This was an unsupported guess *when first asserted*. But the assistant's own claim-#10 geometry (100 mm³ over ~30 mm ⇒ Ø≈2.06 mm) and the in-air pulsatile extrusion later *re-support* exactly this picture. Retracting it as "wrong" was itself an over-correction — see Flip-flops. |
| **B** — user's observation (thin ~0.4 mm, ~30 mm, hangs straight, sticks over wiper) | **DATA, not a claim** — treat as ground truth to be explained | The strand "hangs straight / sticks over the wiper" is consistent with the in-air extrusion + "shake/wipe" not always releasing it. |
| **C** — 30 mm × 0.4 mm = 3.77 mm³ = 1.57 mm feed | **SOLID** | Re-derived: 3.770 mm³, 1.567 mm feed. Arithmetic exact. |
| **D** — slice: median ~43.6 mm feed (~100 mm³), 72% at floor, 1/366 collapsed | **SOLID — reproduced** | My independent parse: median 43.58 mm, 67–69% on the 100 mm³ floor, 1/366 at ~0. Effectively identical. |
| **E** — contradiction: Orca pushes ~100 mm³ (~26×) the 3.8 mm³ implied by "30 mm × 0.4 mm" ⇒ either Ø≈2 mm OR the 30 mm strand is a residual tail | **SOLID that a discrepancy exists; the *binary* "either/or" is INCOMPLETE** | The 26.5× gap is real and correctly computed. But framing it as exactly two mutually-exclusive options is a false dichotomy (see Missed explanations). |
| **F** — "feature is mis-aimed; chute rarely starves; flooring won't fix sticking; real problem is chunky blob (→bigger) or thin tail (→stringing/wipe/cut)" | **PARTLY SOLID, PARTLY OVER-REACHING** | SOLID sub-claims: for *this file* the chute rarely starves (only 1/366 hit ~0), and raising `minimal_chute_flush_length` to 10 mm changed nothing because 100 mm³ > 24 mm³ floor — so *this user's* setting can't fix sticking. OVER-REACHING: "the feature is mis-aimed" generalises a per-file result to the feature's whole purpose; and the root-cause split is presented as a conclusion when it is still a hypothesis. |

## Flip-flops & guesses-stated-as-fact

1. **The "2 mm worm" (A): asserted → retracted → silently re-supported.** First
   stated as if observed; then declared "wrong"; yet the assistant's *own* later
   arithmetic (Ø≈2.06 mm over 30 mm for 100 mm³) and the in-air pulsatile
   extrusion both point right back at a ~2 mm fat extrudate. The honest status is
   **"plausible model, not directly observed"** — it should never have been
   asserted as fact *or* retracted as false. It flip-flopped twice.

2. **"The 2 mm worm compensates the strand" idea, disproven by its own histogram.**
   At one point the reasoning tried to explain the strand as a side-effect of a
   per-change compensation; the per-tool-change histogram (67% pinned at the
   100 mm³ floor, near-zero variance) shows the volume is a *floor*, not a
   compensation term. The assistant was right to drop this — but it was stated
   confidently first.

3. **"2.6 feet" framing — numerically true, rhetorically wrong, and the user
   objected.** 100 mm³ as a hypothetical 0.4 mm thread is indeed ≈796 mm ≈2.6 ft,
   but the printer never lays a 0.4 mm thread in the chute — it dumps a ~3 cm fat
   nub. Quoting "2.6 feet" mixes the *strand* length basis (÷A_noz) with a
   *physical poop* the user can see, which is exactly the 19.14× unit-switch that
   confused them. (Same conclusion as check-02 §2.)

4. **Implicit assumption treated as measurement: "0.4 mm" strand diameter.** The
   user *estimated* 0.4 mm by eye; the assistant for a while propagated it as a
   given, computed 3.8 mm³, then used that to manufacture the contradiction.
   The contradiction is real, but its cleaner reading is "the eyeballed 0.4 mm is
   the wrong diameter for the bulk," not "the slicer is doing something impossible."

## Missed explanations (the third, fourth, … options)

The "either Ø≈2 mm OR residual tail" dichotomy in (E) omits several live
possibilities — at least one of which is almost certainly the real picture:

1. **Both at once (most likely).** 100 mm³ extruded in free air piles into a
   ~2 mm-thick, ~30 mm coil (the bulk) **with** a thin drawn-out tail/string on
   pull-away. The user sees the *tail* (thin, 0.4 mm, hangs straight) but the
   *mass* is the fat coil. "Worm" and "tail" are not mutually exclusive — they're
   the body and the wisp of the same poop. The slice supports the fat body; the
   user's eyes report the tail. No contradiction once you stop forcing one answer.

2. **Die-swell / no-substrate geometry.** Extruding a 0.4 mm nozzle trace *into
   air* (no bed to spread against) yields an extrudate well above 0.4 mm — die
   swell plus coiling easily reaches the ~2 mm the arithmetic implies. The
   assistant treated "0.4 mm" as if the in-air strand should match the nozzle
   width; it should not.

3. **Pulsatile extrusion shapes the poop.** The flush is deliberately pulsed
   (`F523` then `F50`, repeated) — this bunches material into a coil rather than a
   smooth line, reinforcing the fat-worm body and the thin pull-tail. Missed
   entirely.

4. **Not all 100 mm³ is necessarily one clean strand.** Some changes split into
   2–4 flush sub-blocks with a "move aside X3" between them (seen in the slice),
   so a single change can drop *2–3 nubs*, not one strand. The "one 30 mm strand"
   mental model is itself an approximation.

5. **This is ONE file.** Other prints (different colour matrix, `flush_into_infill`
   off, different filaments, taller layers) would shift how much is diverted to
   infill vs. chute and how often the floor binds. The behavioural claim in (F)
   cannot be generalised from a single 6-colour PLA slice.

## Is conclusion (F) over-reaching for a single file?

Yes, in two specific ways:

- **"The feature is mis-aimed."** What the data actually supports is narrower:
  *for this file and this 10 mm setting*, the feature is **inert**, because the
  pre-existing 100 mm³ floor already exceeds the 24 mm³ this setting requests, and
  the chute almost never starves (1/366). That says the user's *chosen value* is
  too low to matter — not that the feature is wrong for its intended starvation
  case (which needs a slice where `flush_into_infill` actually drives
  `tcr.purge_volume` toward 0, i.e. many changes collapsing to ~0). Here only one
  did.

- **The root-cause split is a hypothesis, not a finding.** To support (F) you'd
  need, at minimum: (a) a photo/scale weight of the actual poop (does ~0.12 g /
  change match 100 mm³ × 1.24?); (b) a slice where the floor *does* bind on many
  changes (to test whether starvation is even the user's regime); (c) a test at a
  *higher* `minimal_chute_flush_length` (e.g. 50–60 mm → 120–145 mm³) to see if a
  bigger poop releases — which directly tests the "chunky blob needs bigger"
  branch; and (d) checking whether the sticking correlates with the low-flush
  changes or is uniform (which would point at wipe/cut, not volume).

## Clearest correct summary for the user

> Every tool change in your file dumps a **median ~100 mm³** of filament straight
> into the chute — that's the 30-ish-mm "worm" you see. It looks thin because it's
> squirted **into the air** (no bed to flatten it) and pulsed, so it coils into a
> ~2 mm-thick nub with a thin tail; your eye catches the **tail** (~0.4 mm) but the
> **mass** is the fat coil. The "0.4 mm × 30 mm = tiny" math is what mismatched —
> the bulk isn't a 0.4 mm line. That ~100 mm³ is a **built-in floor in the stock
> slicer** (`g_min_purge_volume`), *not* your `minimal_chute_flush_length` setting:
> you set it to 10 mm (~24 mm³), which is **below** the 100 mm³ floor, so it
> currently does nothing. The chute is **not starving** in this print (only 1 of
> 366 changes purged near zero), so *lowering* purge won't help the sticking and
> *this* feature value can't change it. If the nub won't drop, that's a
> release/wipe/cut issue, not too-little purge — try a bigger flush
> (`minimal_chute_flush_length` ≈ 50–60 mm to push past the floor) to see if a
> heavier nub falls free, and ignore the "2.6 feet" figure entirely (it's a
> counterfactual 0.4 mm-thread length the printer never makes).
