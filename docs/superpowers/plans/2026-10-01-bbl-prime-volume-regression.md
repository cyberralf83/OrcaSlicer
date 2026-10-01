# Plan: make "Prime volume" drive the Type1 tower on single-nozzle printers

Spec: `docs/superpowers/specs/2026-10-01-bbl-prime-volume-regression-design.md`
Branch: `fix/prime-volume-bbl-single-nozzle` (from `origin/nightly-builds-with-bc` @ `bbb99e1c4d`)
Fork feature marker: `FORK(bbl-prime-volume)`

## Revision after the adversarial review (this section supersedes the steps below where they differ)

Review: `docs/reviews/2026-10-01-bbl-prime-volume-regression/adversarial-review.md` (GO-WITH-CHANGES).
User decision on B3: **restore the process-level Prime volume and re-show its row** (option a).

- B1: the estimate reads `extruder_max_nozzle_count` as `ConfigOptionVector<int>` (nullable-safe); the
  H2C test sets it with `set_deserialize_strict("…", "1,6")`.
- B2: the gate also requires a Bambu Lab printer — `is_BBL_printer()` in Print, `printer_model` prefix
  "Bambu Lab" in the estimate (same predicate as `resolve_wipe_tower_type`). Header function is
  `fork_bbl_prime_volume_applies(is_bbl, counts)`. Qidi/non-BBL Type1 keep upstream (accepted residual).
- B3: confirmed — upstream `edc2f8bf90` (2026-09-24 BBL profile sync) removed `purge_in_prime_tower: 0`
  (#7808), hiding the row on BBL. Added a third marked block in `ConfigManipulation.cpp` that re-shows
  `prime_volume` when the gate applies.
- B4: `build4mac_local.yml` gains a `build_ref` dispatch input: builds that branch as-is, skips the
  upstream merge/push step and the release job, own concurrency group. Default behaviour unchanged.
- S1: default `prime_volume` 45 mm³ applies on untouched BBL projects (v2.4.x behaviour) — accepted with B3(a).
- S4: one estimate block after the Type1 `if`, overriding every purge. S5: markers are
  `FORK(bbl-prime-volume)|<call-site file>` for Print.cpp, WipeTowerEstimate.cpp, ConfigManipulation.cpp.
- S6: tests tagged `[Config][BblPrimeVolume]`, upstream-shaped config, rectangle wall, 5 mm height, exact
  block depths; full `libslic3r_tests` must pass (upstream's Type1 estimate case must stay green).
- S7 acceptance: slice with `prime_volume = P` after the fix must equal, byte for byte, a pre-fix slice with
  every `filament_prime_volume = P`; H2C G-code and estimate unchanged.

## Design constraints (from the fork CLAUDE.md)

- Leave upstream statements byte-identical; override results in a marked `// FORK(...)` block
  with at least one unchanged/blank line between it and the upstream code.
- Fork-only tests go in `tests/libslic3r/test_config_fork.cpp` only.
- Put the decision logic in one pure function shared by the slicing path, the estimate and the
  tests (same pattern as `seam_point_is_embedded_enough`).
- Register the feature in `CLAUDE.md` (fork feature list) and add a marker to the
  "verify fork feature markers" list in both mac workflows.

## Step 1 — shared decision function (new fork-only header)

New file `src/libslic3r/GCode/ForkPrimeVolume.hpp` (header-only, fork-owned, so upstream can
never conflict on it):

```cpp
#pragma once
#include <algorithm>
#include <limits>
#include <vector>

namespace Slic3r {
// FORK(bbl-prime-volume): true when any extruder carries a nozzle cluster (H2C carousel).
// Nil entries of the nullable vector (INT_MAX) and non-positive values count as single-nozzle.
inline bool fork_has_multi_nozzle_extruder(const std::vector<int> &extruder_max_nozzle_count)
{
    return std::any_of(extruder_max_nozzle_count.begin(), extruder_max_nozzle_count.end(),
                       [](int v) { return v > 1 && v != std::numeric_limits<int>::max(); });
}

// FORK(bbl-prime-volume): prime volume for an extruder/filament change on the Type1 tower.
// Single-nozzle extruders use the visible process-level "Prime volume" (the v2.4.x contract,
// restored after upstream 407c78fb30 switched it to the hidden per-filament value). Multi-nozzle
// (carousel) printers keep upstream's per-filament value. Saving mode is applied by the caller
// before this and must not be overridden.
inline float fork_type1_extruder_change_prime_volume(bool multi_nozzle, float upstream_value, float prime_volume)
{
    return multi_nozzle ? upstream_value : prime_volume;
}
} // namespace Slic3r
```

## Step 2 — real tower (`src/libslic3r/Print.cpp`, Type1 branch of `_make_wipe_tower`)

Leave upstream's `wipe_volume_ec` / `wipe_volume_nc` / Saving block byte-identical. Between the
closing `}` of the Saving `if` and the `wipe_tower.plan_toolchange(...)` call, add (with a blank
line on each side):

```cpp
                // FORK(bbl-prime-volume): single-nozzle extruders prime with the visible
                // "Prime volume" box again (upstream 407c78fb30 regression); Saving keeps 15 mm³.
                if (m_config.prime_volume_mode != PrimeVolumeMode::pvmSaving)
                    wipe_volume_ec = fork_type1_extruder_change_prime_volume(
                        fork_has_multi_nozzle_extruder(m_config.extruder_max_nozzle_count.values),
                        wipe_volume_ec, (float) m_config.prime_volume);
```

`#include "GCode/ForkPrimeVolume.hpp"` added after the existing include block (own line,
separated). `wipe_volume_nc` is left alone (only used for same-extruder nozzle changes, which
cannot happen on a single-nozzle extruder).

## Step 3 — pre-slice estimate (`src/libslic3r/GCode/WipeTowerEstimate.cpp`)

The Type1 per-filament loop sets
`purge.prime_volume = saving_mode ? 15.f : float(float_at("filament_prime_volume", id, prime_volume));`.
Leave it byte-identical; after it (blank line separator) add:

```cpp
            // FORK(bbl-prime-volume): mirror Print::_make_wipe_tower so arrange/validation size
            // the same tower that will be generated.
            if (!saving_mode)
                purge.prime_volume = fork_type1_extruder_change_prime_volume(multi_nozzle, purge.prime_volume, float(prime_volume));
```

with `multi_nozzle` computed once before the loop from the config's `extruder_max_nozzle_count`
(read via `dynamic_cast<const ConfigOptionInts*>(option_of(config, "extruder_max_nozzle_count"))`;
absent option → single-nozzle). Include the fork header.

## Step 4 — tests (`tests/libslic3r/test_config_fork.cpp`)

1. `fork_has_multi_nozzle_extruder`: `{1}`→false, `{1,1}`→false, `{1,6}`→true, `{INT_MAX}`→false,
   `{}`→false.
2. `fork_type1_extruder_change_prime_volume`: single-nozzle returns prime_volume; multi-nozzle
   returns the upstream value.
3. Estimate pin: build a `DynamicPrintConfig::full_print_config()` with `printer_model` set to an
   X1C, 4 filaments, rib wall off, `filament_prime_volume` 30, and compare
   `estimate_wipe_tower_footprint(..., Type1, ...)` depth at `prime_volume` 10 vs 30. Assert depth
   grows with `prime_volume` and does NOT change when only `filament_prime_volume` changes.
   Repeat with `extruder_max_nozzle_count = {1,6}` and assert the opposite (upstream behaviour kept).
4. Registration guard: `prime_volume` is in `Preset::print_options()` (it is upstream; guards a
   silent drop).

## Step 5 — fork bookkeeping

- `CLAUDE.md`: add feature 6 "BBL prime volume" with files touched, the marker, why, and the
  upstream history (link the spec).
- `.github/workflows/build4mac.yml` and `build4mac_local.yml`: add `fork_has_multi_nozzle_extruder`
  to the fork-marker verification list (keep both identical).

## Step 6 — verification

Local build is not currently possible on this Mac (no cmake/ninja; 17 GB free, a deps+app build
needs more). Options for the user: (a) free disk + `brew install cmake ninja` and build locally
(~1–2 h first time), (b) push the branch and run `build4mac_local.yml` on it.
Acceptance once a binary exists:
1. `fork_tests`/`libslic3r_tests "[Fork]"` pass.
2. Headless slice of the Ahava X1C plate (rib tower, moved into free space): tower volume and
   footprint change with `prime_volume` 5/10/30 and are independent of `filament_prime_volume`.
   Expected ≈ stock 2.4.2 numbers (27×27 mm at 10, 21×20 mm at 5).
3. H2C sample project (or a config with `extruder_max_nozzle_count = [1,6]`): G-code identical to
   pre-fix build.
4. Estimate and generated tower agree (no new "partially outside" / conflict on a plate that
   slices fine with the generated tower).

## Rollback

Single marker `FORK(bbl-prime-volume)`; deleting the header and the two blocks restores upstream.
If upstream fixes the regression, drop the fork blocks after confirming the upstream fix covers the
estimate as well.

## Verification run (2026-10-01)

Build: `build4mac_local.yml` test build (`build_ref`), run 36823691867, commit `edeefe1aed`; release job skipped,
`nightly-mac-arm64` untouched. (First run 36823244196 failed to compile `ConfigManipulation.cpp`:
`DynamicConfig::option<ConfigOptionVector<int>>` needs `static_type()`; fixed in `edeefe1aed`.)
Headless slices of the Ahava X1C plate (7 colours, 374 changes, rib tower), `--datadir` isolated:

| prime_volume | filament_prime_volume | tower | tower vol | result |
|---|---|---|---|---|
| 5 | 30 | 20.7 × 19.6 mm | 25.1 cm³ | ok |
| 10 | 30 | 26.5 × 27.1 mm | 28.2 cm³ | ok |
| 10 | 10 | 26.5 × 27.1 mm | 28.2 cm³ | ok — motion G-code md5 identical to the row above |
| 20 | 30 | — | — | refused: "Prime Tower is partially outside the printable area" (tower grew) |
| 30 | 30 | — | — | refused: G-code path conflict with an object (tower grew) |

The 5 / 10 rows equal the pre-fix build with every `filament_prime_volume` = 5 / 10 (same size and volume).
Not verified yet: GUI row visibility on a BBL single-nozzle preset, H2C unchanged, and the
`[BblPrimeVolume]` unit tests (the mac workflow does not build tests).
