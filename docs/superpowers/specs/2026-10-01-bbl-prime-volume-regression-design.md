# BBL single-nozzle printers ignore "Prime volume" — regression analysis

Date: 2026-10-01
Branch: `fix/prime-volume-bbl-single-nozzle` (from `origin/nightly-builds-with-bc` @ `bbb99e1c4d`)

## Symptom

On Bambu Lab single-nozzle printers (X1C, P1S, P2S, A1 …) the Print Settings → Prime tower →
**Prime volume** box has no effect on the prime tower. A 7-colour test plate (Ahava widget, X1C,
373 tool changes, 6 per layer) produced the same tower at `prime_volume` 5, 10 and 30. The tower
size is instead driven by the per-filament `filament_prime_volume` (30 mm³ in the Bambu PLA Basic
system preset, 45 mm³ config default), which has **no GUI field anywhere** (no
`append_single_option_line` for it in `Tab.cpp`). The user can therefore not size the tower from
the GUI at all. `prime_volume_mode = Saving` (15 mm³) is `comDevelop` and is reset to Default on
single-nozzle printers, so it is not an escape hatch either.

## Measured evidence (headless CLI, same 3mf copies, rib tower, 0.2 mm layers, 73 layers)

| Build | prime_volume 10 / fil_prime 30 | prime_volume 5 | fil_prime 10 |
|---|---|---|---|
| Fork 2.5.0-dev (this repo, pre-fix) | 49×47 mm, 38.3 cm³ | **unchanged** | 27×27 mm, 28.2 cm³ |
| Stock 2.4.2 (brew `orcaslicer`) | 27×27 mm, 26.6 cm³ | **21×20 mm**, 23.6 cm³ | unchanged |
| Stock nightly (`865e9963c3`, 2026-09-30) | refused (tower too big for test position) | refused, unchanged | 27×27 mm |

Sizes are the extent of `; FEATURE: Prime tower` extrusions (include rib protrusions). Stock
2.4.2 honours `prime_volume` and ignores `filament_prime_volume`; the fork and upstream nightly do
the reverse. So the regression is **upstream**, not fork code, and it is still present at upstream
`865e9963c3` (2026-09-30) and in the fork's latest merge `bbb99e1c4d` (upstream `828278af`).

## Root cause

`Print::wipe_tower_type()` forces `WipeTowerType::Type1` for every BBL printer
(`Print.hpp`: `return is_BBL_printer() ? WipeTowerType::Type1 : m_config.wipe_tower_type.value;`,
from `d58d9be07b`, intentional). In the Type1 branch of `Print::_make_wipe_tower()` the prime
volume handed to `WipeTower::plan_toolchange()` is:

```cpp
float wipe_volume_ec = filament_id < m_config.filament_prime_volume.values.size()
    ? m_config.filament_prime_volume.values[filament_id]
    : (float) m_config.prime_volume;           // fallback never taken: the vector is always populated
float wipe_volume_nc = ... filament_prime_volume_nc ...;
if (m_config.prime_volume_mode == PrimeVolumeMode::pvmSaving) { wipe_volume_ec = wipe_volume_nc = 15.f; }
```

`WipeTower::plan_toolchange()` uses `wipe_volume_nc` only for a nozzle change on the same extruder
(`is_same_extruder && !is_same_nozzle`, i.e. an H2C carousel slot change) and `wipe_volume_ec`
otherwise — so on every non-carousel printer every tool change uses `filament_prime_volume`.

The pre-slice footprint estimate mirrors this (`GCode/WipeTowerEstimate.cpp`,
`purge.prime_volume = saving_mode ? 15 : float_at("filament_prime_volume", id, prime_volume)`),
so arrange/validation also size the tower from the hidden value.

`PrintConfig.cpp` still documents the intended split: *"prime_volume — used by the generic (Type2)
wipe tower; also the fallback for filament_prime_volume on Type1"* and *"filament_prime_volume —
Used by the Type1 wipe tower"*. That comment describes the code, but the "fallback" is dead.

## Commit history (the same switch has been made, reverted, and re-made)

| When | Commit / PR | Effect |
|---|---|---|
| 2024-12-21 | `fb19c6a904` "Fix Prime volume missing on BBL printers (#7808)" (fixes #7751) | Adds `"purge_in_prime_tower": "0"` to BBL `fdm_machine_common.json` so the Prime volume row is shown on BBL (row is hidden when SEMM && purge_in_prime_tower, `ConfigManipulation.cpp`). |
| 2025-01-20 / 2025-10-05 | `e7e6405ad3` (BBS port "instead of prime_volume by filament_prime_volume") and its revert `da2934d02a` (Noisyfox) | Both landed on main only inside PR #10780 (H2D/H2S, merged 2025-10-24), so the switch never shipped. The revert documents the maintainers' intent: Type1 uses `prime_volume`. v2.4.2 does (`v2.4.2:Print.cpp:3510`). |
| 2026-07-09 / 07-16 / 07-20 | `237ef41b06` (H2C multi-nozzle engine), `407c78fb30` dnevera (PR #14800, merged 2026-07-22, "LGTM"), `9b5eb76478` (clean-up; stopped discarding the per-filament value on load) | Re-registers `filament_prime_volume` and makes Type1 use it for **all** BBL printers. Commit message claims "Safe for non-carousel printers … behaves identically" — true for purge *tracking*, not for the prime *volume source*. No review comment discussed it. |
| 2026-07-26 | `7a378d2fc4` "Sync WipeTower from BambuStudio" | ec/nc choice moved into `WipeTower::plan_toolchange`; same value source. |
| 2026-09-03 | `99627c8e93` "Size the Footprint Estimate from the Planners" | Pre-slice estimate starts reading `filament_prime_volume` too. |
| 2026-09-23/24 | `cc883e458d` / revert `b46916a8be` / reapply `edc2f8bf90` "Sync Bambu Lab profiles with BambuStudio" | **Second regression:** drops `"purge_in_prime_tower": "0"` from BBL `fdm_machine_common.json`, undoing #7808. With the default `true`, the Prime volume row is hidden again on BBL printers in the GUI. The user's installed build predates this sync, which is why the row is visible for them today. |

`git merge-base --is-ancestor 407c78fb30 v2.4.2` → false, consistent with 2.4.2 being unaffected.
No open upstream issue for the 2026 regression was found (`gh search issues "prime volume"`).
(Issue #8151, Jan 2025, QIDI on 2.3.0-dev, predates both changes and is unrelated.)

## Scope of the fix (fork feature #6: "BBL prime volume")

Restore the v2.4.x / post-revert contract for printers **without** a multi-nozzle extruder
(every `extruder_max_nozzle_count` entry ≤ 1, i.e. everything except H2C today):

- extruder/filament-change prime = `prime_volume` (the visible box), in both the real tower
  (`Print::_make_wipe_tower`) and the pre-slice estimate (`estimate_wipe_tower_footprint`);
- `prime_volume_mode = Saving` keeps upstream's 15 mm³ override;
- multi-nozzle (carousel) printers keep upstream's per-filament behaviour unchanged, since PR #14800
  was written and print-tested for them and BBS uses the per-filament values there;
- `wipe_volume_nc` (carousel nozzle change) untouched.

Non-goals: changing H2C/H2D-carousel behaviour. (`filament_prime_volume` changes fall through to
`invalidate_all_steps()`, so invalidation is not an issue — corrected after review.)

Open decision after the adversarial review (`docs/reviews/2026-10-01-bbl-prime-volume-regression/`):
the Prime volume row is hidden on BBL with current profiles (see 2026-09-23 row), so restoring
`prime_volume` as the source also requires re-showing the row, OR the alternative is to expose
`filament_prime_volume` in the Filament tab (Bambu Studio's model) and leave the source alone.
Also: no BBL process profile sets `prime_volume`, so its default (45 mm³) applies after the fix,
vs 30 mm³ from the Bambu filament presets today (v2.4.2 behaviour, but larger default towers).

Dual-extruder single-nozzle-per-extruder printers (H2D, X2D) fall on the `prime_volume` side,
which is exactly their behaviour between the Oct-2025 revert and July 2026.

## Upstream report (to file separately, after user review)

Point at `da2934d02a` (intentional revert) vs `407c78fb30` (re-introduction), the dead fallback,
and #8151. Suggested upstream fix is the same gate.
