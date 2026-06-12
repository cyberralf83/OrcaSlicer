# Arithmetic & Unit-Conversion Check — Chute Analysis

Independent adversarial re-derivation of every numeric claim. All numbers
verified with Python (`math.pi`, full precision). Base constants:

```
A_fil = π/4 · 1.75²  = 2.405281875…  mm²
A_noz = π/4 · 0.40²  = 0.125663706…  mm²
A_fil / A_noz        = 19.140625      (exact: (1.75/0.4)² = 4.375²)
A_noz / A_fil        = 0.052244898…
```

## Verdict

**All 11 claims are arithmetically correct.** Every asserted number matches
my independent re-derivation to within stated rounding. No math errors, no
unit-conversion errors, no wrong constants.

The one genuine problem is **presentational, not numerical**: several claims
silently switch between two different physical quantities — "mm of 1.75 mm
filament fed into the extruder" vs. "mm of extruded 0.4 mm strand laid down" —
which differ by a factor of 19.14×. The "~2.6 feet" figure is numerically
defensible but is the clearest example of this confusing framing (details in
the last section).

## Per-claim table

| # | Claim (asserted) | Correct value | Match |
|---|------------------|---------------|-------|
| 1 | A_fil = 2.405 mm²; A_noz = 0.1257 mm² | 2.40528 → 2.405; 0.125664 → 0.1257 | ✓ |
| 2 | 100 mm³ → 41.6 mm filament fed (100/A_fil) | 41.575 ≈ 41.6 | ✓ |
| 3 | 100 mm³ as 0.4 strand → 796 mm (100/A_noz); ~80 cm / ~2.6 ft | 795.77 ≈ 796; 79.6 cm ≈ 80 cm; 2.611 ft ≈ 2.6 ft | ✓ |
| 4 | 43.6 mm feed = 105 mm³ (43.6·A_fil) | 104.87 ≈ 105 | ✓ |
| 5 | 30 mm of 0.4 strand = 3.77 mm³ = 1.57 mm feed | 3.7699 → 3.77; 1.5673 → 1.57 | ✓ |
| 6 | 1 mm feed → 19.1 mm of 0.4 strand (A_fil/A_noz) | 19.1406 ≈ 19.1 | ✓ |
| 7 | strand→feed ×0.05224; strand→volume ×0.12566 mm² | 0.0522449 → 0.05224; 0.1256637 → 0.12566 | ✓ |
| 8 | 2.5× 30 mm = 75 mm strand = 3.9 mm feed = 9.4 mm³ | 75 mm; 3.918 → 3.9; 9.4248 → 9.4 | ✓ |
| 9 | 366 × 100 mm³ ≈ 37 cm³ ≈ ~45 g PLA (ρ=1.24) | 36 600 mm³ = 36.6 cm³; ×1.24 = 45.38 g | ✓ |
| 10 | 100 mm³ over ~30 mm ⇒ implied Ø ~2.0 mm (not 0.4) | A=3.333 mm², Ø=2.060 mm ≈ 2.0 | ✓ |
| 11 | g_min_purge_volume = 100 mm³ ⇒ ~41.6 mm feed ⇒ ~796 mm of 0.4 strand | 41.575; 795.77 — same as #2/#3 | ✓ |

### Notes on individual roundings (all benign)

- **#2 (41.6):** true value 41.575; rounding 41.575 → 41.6 is the conventional
  round-half-up to 3 sig figs. Fine. (A pedant could write 41.58, but 41.6 is
  not wrong.)
- **#3 (796 / 80 cm / 2.6 ft):** 795.77 → 796 ✓; 79.58 cm → "~80 cm" ✓;
  795.77 mm ÷ 304.8 mm/ft = 2.611 ft → "~2.6 ft" ✓. All three honest.
- **#7:** the "0.12566 mm²" factor is literally A_noz, and 0.05224 is exactly
  A_noz/A_fil; 30 × 0.05224 = 1.567 reproduces #5's 1.57. Internally consistent.
- **#8:** the 2.5× scaling is exact and linear: 2.5 × 1.567 = 3.918 (→3.9),
  2.5 × 3.77 = 9.42 (→9.4). Consistent with #5.
- **#9:** 36.6 cm³ → "~37 cm³" is a slightly generous round-up but within
  "~". Mass 45.38 g → "~45 g" ✓. The density 1.24 g/cm³ is a standard PLA value.
- **#10:** d = √(4·(100/30)/π) = 2.060 mm. "~2.0 mm" is a fair round of 2.06;
  the qualitative point (the strand is ~2 mm fat, nowhere near a 0.4 mm
  nozzle-width line) is correct. The poop is a thick blob/extrudate, not a
  drawn 0.4 mm trace — see framing note below.

## Misleading framings

The arithmetic is clean; the *labeling* of which quantity each number
describes is where a reader gets lost.

1. **Two incompatible "lengths" are used without flagging the switch.**
   - "mm of filament **fed**" = length of 1.75 mm stock consumed
     (volume ÷ A_fil). 100 mm³ → 41.6 mm. (#2, #11)
   - "mm of **strand**" = length of a 0.4 mm-diameter extrudate
     (volume ÷ A_noz). 100 mm³ → 796 mm. (#3, #11)

   These describe the *same* 100 mm³ of plastic but differ by 19.14×
   (= A_fil/A_noz). Claim #11 presents both back-to-back ("~41.6 mm feed ⇒
   ~796 mm of 0.4 strand") — correct, but a reader who misses that "feed" and
   "strand" are different things will think the plastic somehow grew 19×.

2. **The "~2.6 feet" claim — numerically defensible, presentationally
   misleading.** 100 mm³ rendered as a hypothetical 0.4 mm-thick thread is
   indeed ≈ 796 mm ≈ 2.61 ft, so the figure is *not wrong*. But it is
   misleading because:
   - A purge/poop is **not** laid down as a 0.4 mm line. Claim #10 itself shows
     the real blob is ~2.0 mm in effective diameter over ~30 mm — i.e. a short
     fat extrudate, not a 2.6-foot hair. So "2.6 feet" describes a
     counterfactual geometry the printer never produces.
   - It invites the reader to picture 2.6 feet of waste per change, when the
     physical poop is a ~3 cm nub. The honest mental model is the **volume**
     (100 mm³) or the **mass** (~0.12 g per change at 1.24 g/cm³), not a
     0.4 mm-strand length.
   - The 19× gap between "41.6 mm fed" and "796 mm strand" for the *same*
     plastic is exactly what confused the user. Both numbers are right; the
     "2.6 feet" framing is the one to drop (or to caption explicitly as "if you
     stretched it into a 0.4 mm noodle").

   **Assessment:** numerically defensible, presentationally misleading. Prefer
   reporting volume (mm³) and mass (g); if a length is wanted, "≈41.6 mm of
   1.75 mm filament consumed" is the physically meaningful one.

3. **Minor:** #9 rounds 36.6 → "~37 cm³" upward while #3 rounds 79.58 → "~80 cm"
   upward and #2 rounds 41.575 → 41.6 upward — a consistent round-up bias, all
   tiny and inside the "~". Not an error, just note the numbers skew slightly high.
