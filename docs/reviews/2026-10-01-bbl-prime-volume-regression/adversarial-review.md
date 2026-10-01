# Adversarial review: BBL prime-volume regression spec + plan

Date: 2026-10-01
Reviewed: `docs/superpowers/specs/2026-10-01-bbl-prime-volume-regression-design.md`,
`docs/superpowers/plans/2026-10-01-bbl-prime-volume-regression.md`
Tree: `fix/prime-volume-bbl-single-nozzle` @ `bbb99e1c4d`; upstream nightly `865e9963c3`; `upstream/main` `ba361d9882`.
All history commands used `--full-history`.

## Verdict: GO-WITH-CHANGES (not executable as written)

The root cause is right. Upstream `407c78fb30` (PR #14800) switched the Type1 tower from
`prime_volume` to `filament_prime_volume`, and v2.4.2 used `prime_volume`. The plan as written
still has four blockers:

1. The H2C gate in the estimate can never be true.
2. The change breaks an upstream unit test.
3. The "Prime volume" box is most likely **hidden** on stock BBL printers, so the fix as planned
   makes GUI users' towers bigger with no control.
4. The proposed CI verification builds the wrong branch and overwrites the published nightly.

Resolve B1–B4 before writing code. B3 is a design decision for the user.

---

## BLOCKERS

### B1. The estimate gate's `dynamic_cast<const ConfigOptionInts*>` always fails, so H2C's estimate stops matching its generated tower
- `extruder_max_nozzle_count` is nullable: `PrintConfig.cpp:8414-8417` (`def->nullable = true; ConfigOptionIntsNullable{1}`), `PrintConfig.hpp:1760`.
- `ConfigOptionInts` and `ConfigOptionIntsNullable` are sibling template instances (`Config.hpp:1132-1133`, both derive from `ConfigOptionVector<int>`). A `dynamic_cast` from one to the other returns `nullptr`. The `option_of()` fallback returns the def default, which is also Nullable.
- Result: in the estimate, `multi_nozzle` is always false. On H2C the estimate then sizes from `prime_volume` while `Print::_make_wipe_tower` (where `m_config.extruder_max_nozzle_count.values` works) sizes from `filament_prime_volume`. That is a new arrange/validation vs generated-tower mismatch on exactly the printer the plan promises to leave unchanged.
- Upstream has the same latent bug at `Format/bbs_3mf.cpp:8451`. It is not fork scope; mention it in the upstream report.
- **Required:** read the option as `dynamic_cast<const ConfigOptionVector<int>*>` (covers both) or as `ConfigOptionIntsNullable`. In the test, build the H2C case with `set_deserialize_strict("extruder_max_nozzle_count", "1,6")`, never `new ConfigOptionInts{1,6}`. The latter would make the buggy cast pass in the test.

### B2. The plan breaks an upstream unit test the fork may not edit
- `tests/libslic3r/test_wipe_tower_estimate.cpp`, TEST_CASE "Type1 sizes the tower from each filament's own prime volume" (around lines 97-121). `make_config()` sets `prime_volume = 100` and the case sets `filament_prime_volume = {30, 45}`. It expects 18.5 mm, 11 mm and a rib-squared value for **Type1**. The config has no `printer_model`, and `extruder_max_nozzle_count` is the default `{1}`.
- Under the planned gate (any printer whose nozzle counts are all ≤1), both filaments become 100 mm³, so all three CHECKs fail. The fork rule forbids editing upstream test files, so `libslic3r_tests` would stay red permanently and hide future regressions.
- The other estimate cases pass: they use `prime_volume == filament_prime_volume` (100/100 or 0/0). The `fff_print` wipe-tower tests default to Type2 (`PrintConfig.cpp:6810`), so they are unaffected.
- **Required:** also gate on Bambu Lab. In `Print`, use `is_BBL_printer()`, the same predicate that forces Type1 (`Print.hpp:1183`). In the estimate, use the `printer_model` prefix `"Bambu Lab"`, the same predicate as `resolve_wipe_tower_type` (`WipeTowerEstimate.cpp:28-33`). This keeps the upstream test green and matches the feature's name and the spec's title.
  - Residual: Qidi's four Type1 machine profiles (`resources/profiles/Qidi/machine/fdm_*common.json`) used `prime_volume` in v2.4.2 and keep nightly behaviour. Document this as accepted. The alternative, a permanently failing upstream test, is not acceptable.

### B3. "Prime volume" is very likely hidden on every stock BBL printer, in v2.4.2 and now
- `ConfigManipulation.cpp:1092`: `toggle_line("prime_volume", have_prime_tower && (!purge_in_primetower || !bSEMM));`
- BBL `fdm_machine_common.json:148` sets `"single_extruder_multi_material": "1"`. No BBL profile sets `purge_in_prime_tower` (grep is empty), so it is the default `true` (`PrintConfig.cpp:6812-6816`). The user cannot change it either: it is greyed out for BBL (`Tab.cpp:6204`, `bSEMM && supports_wipe_tower_2`, where `supports_wipe_tower_2` is false for BBL).
- `Tab::toggle_line` sets `toggle_visible` (`Tab.cpp:1743-1751`), so the row is hidden. v2.4.2 has the identical rule (`v2.4.2:ConfigManipulation.cpp:891`).
- Consequences:
  - The spec's framing ("the box has no effect") and its stated goal ("size the tower from the GUI") are unproven. The measurements were all headless CLI.
  - After the planned fix, a GUI X1C user gets the hidden default **45 mm³** per change instead of today's 30 mm³ (see S1), with still no control. For GUI users that is worse than today.
  - On H2C the row is hidden too, so there is no misleading dead box there.
- **Required:** first check in the real GUI (X1C system preset, prime tower on) whether the row is visible. If it is hidden, pick one:
  - (a) **Keep the planned fix and add a visibility override.** Add a marked block after line 1092 that shows `prime_volume` for BBL printers with no multi-nozzle extruder. The fork already carries 52 lines in `ConfigManipulation.cpp`.
  - (b) **Pivot: expose `filament_prime_volume` in the Filament tab** instead of rerouting the slicer. This is what Bambu Studio and `e7e6405ad3` did: one `append_single_option_line("filament_prime_volume")` after `filament_change_length` (`Tab.cpp:4393`). The key is already whitelisted in `s_Preset_filament_options` (`Preset.cpp:1445`). Add `filament_prime_volume` to the Plater refresh trigger, because `Plater.cpp:20675` only reacts to `prime_volume`.
    - Benefits: no slicing-logic divergence, B1/B2 disappear, it keeps today's 30 mm³ default, and control is per-filament.
    - Costs: an edit in high-churn `Tab.cpp` (86 non-merge upstream commits since July), and it is not the 2.4.2 process-level contract.
  - Recommendation: (b) if the user only wants GUI control. (a) only if the 2.4.2 single process-level number matters more than matching Bambu Studio.

### B4. Verification option (b), "run `build4mac_local.yml` on the branch", builds the wrong code and clobbers the release
- `build4mac_local.yml:92-95` checks out the hardcoded `ref: nightly-builds-with-bc` (and `BRANCH: nightly-builds-with-bc`, line 105). It merges upstream, pushes that branch (line 305), and the `release` job deletes and recreates `nightly-mac-arm64` (lines 656-707).
- Dispatching it with `--ref fix/...` only changes which workflow file runs. The DMG would not contain the fix, yet it would replace the user's published nightly.
- The workflow also runs no unit tests.
- **Required:** either merge into `nightly-builds-with-bc` only after review (accepting that the next release ships it), or add a build-only path. For example, a manual input to override the checkout ref with publishing skipped; that change itself needs the user's approval. Or build locally (`df` shows 38 GiB free, not the 17 GB the plan states).

## SHOULD-FIX

### S1. The default behaviour change is not stated
No BBL process profile sets `prime_volume` (only other vendors do), so it is the def default 45 (`PrintConfig.cpp:7768-7774`). Bambu filament presets set `filament_prime_volume` 30 (342 of 356 entries). After the fix, every BBL user who never touched anything gets about 50% more prime per tool change than the current nightly and than Bambu Studio, matching v2.4.2. Spell this out for the user and get an explicit OK; with B3 they also cannot see it.

### S2. Commit history: one wrong causal link and missing rows
- `e7e6405ad3` has author date 2025-01-20 but commit date **2025-09-16**. Neither it nor its revert is an ancestor of PR #10780's first parent (`git merge-base --is-ancestor e7e6405ad3 3cb6da6f61` is false; the merge is `339636b91f`). The switch and its revert landed together on 2025-10-24, so the switch **never shipped on main before July 2026**.
- Issue #8151 (created 2025-01-23 against "Dev 2.3.0 1b1288c", Custom QIDI iFast) predates the switch, so it **cannot** be the same cause. It is more likely the SEMM / purge-in-tower path in B3. Drop it from the causal chain and from the upstream report.
- Add the missing rows:
  - `237ef41b06` (2026-07-09): adds `prime_volume_mode`, Saving = 15, `filament_prime_volume_nc`; the prime still came from `prime_volume`.
  - `9b5eb76478` (2026-07-20): removes `filament_prime_volume` from the `handle_legacy` erase list. Before this, profile values were discarded on load; v2.4.2 still erases them (`v2.4.2:PrintConfig.cpp:8273`).
  - `7a378d2fc4` (2026-07-26): the unnamed "later (upstream)" row that moved the ec/nc choice into `plan_toolchange`.
  - `99627c8e93` (2026-09-03): the estimate starts reading `filament_prime_volume`.
- `407c78fb30` affects **all Type1** printers (Qidi included), not only BBL.

### S3. The invalidation claim is wrong
`filament_prime_volume` is not in the psWipeTower list, so it falls to the `else` branch, which calls `invalidate_all_steps()` (in `Print::invalidate_state_by_config_options`, after the `z_hop_types` case). That over-invalidates; nothing is missing. Remove it from the non-goals and the upstream report. For the fix itself, `prime_volume` is already in psWipeTower (`Print.cpp:384`) and triggers the GUI refresh (`Plater.cpp:20675`), which is sufficient. A printer change that alters `extruder_max_nozzle_count` also falls back to invalidating everything. Nothing more is needed.

### S4. Estimate insertion point: use one block after the loop, not two inside it
`WipeTowerEstimate.cpp` had six upstream commits in September (`8df5e5e738`, `99627c8e93`, `e1efec7d6c`, `2fdc16f9f2`, `2f2a6bc3b5`, `60ebbf7daa`). The plan inserts once between `purge.prime_volume` and `purge.category`, and again before the loop for `multi_nozzle`. Both sit inside upstream's tight block. `saving_mode` is also scoped inside `if (type1 && filaments_cnt > 1) {`.

Instead, add a single marked block after that `if`'s closing brace (before `const double min_depth`), with a blank line on each side. It should recompute Saving and the gate and set `p.prime_volume` for every element of `purges`. This is equivalent, because nothing reads `purge.prime_volume` inside the loop. The Print.cpp site is fine as planned: last touched by `7a378d2fc4` on 2026-07-26. Put the Print.cpp include after `#include <codecvt>` with a blank line before `using namespace nlohmann;` (around line 53).

### S5. The workflow marker is meaningless as planned
The marker list format is `marker|file` (`build4mac.yml:59-82`). A symbol in a fork-owned header can never be dropped by a merge. The hazard is losing the call sites. Use `FORK(bbl-prime-volume)|src/libslic3r/Print.cpp` and `FORK(bbl-prime-volume)|src/libslic3r/GCode/WipeTowerEstimate.cpp`, plus the `ConfigManipulation.cpp` line if B3(a) is chosen. Add them identically to both workflows; the two lists are currently identical.

### S6. Tests: names, constructibility, assertions
- No `[Fork]` tag and no `fork_tests` target exist. The existing fork tests use `[Config][Seam]` inside `libslic3r_tests`. Tag the new ones `[Config][BblPrimeVolume]`, and require the **whole** `libslic3r_tests` suite to pass, which would have exposed B2.
- Estimate pin:
  - Copy upstream's `make_config()` shape: `DynamicPrintConfig c; c.apply(FullPrintConfig::defaults());` gives `ConfigOptionEnumGeneric`, like the GUI and CLI.
  - Set `wipe_tower_wall_type` to `rectangle`; the default is **rib** (`PrintConfig.cpp:7871`).
  - Use `max_object_height = 5` so the stability floor is 5 mm (`WipeTower.cpp:1654`); otherwise 10 vs 30 can both clamp.
  - Assert exact values against `WipeTower::estimate_tower_blocks_depth` built from explicit purges, as upstream does.
  - Cases: BBL `printer_model` with `{1}` (uses `prime_volume`, ignores `filament_prime_volume`); BBL with `"1,6"` via `set_deserialize_strict` (upstream behaviour); non-BBL Type1 (upstream behaviour, mirrors B2).
- The two pure-function tests are near-tautological (a ternary). Keep them cheap, but they are not coverage.
- Generation cannot be tested under the fork rule (`fff_print` is off-limits), so the manual slice is the only generation check. Say so in the plan.

### S7. Acceptance expectations
The fork with `fil_prime = 10` gives 28.2 cm³, and v2.4.2 with `prime_volume = 10` gives 26.6 cm³. Other upstream tower changes since 2.4.2 (`82e91bd472` #15485, `ea280ba6f6` #15841, …) account for the difference, so "≈ 2.4.2 numbers" is only a loose check. A stronger invariant: post-fix G-code with `prime_volume = P` should be **byte-identical** to pre-fix G-code with every `filament_prime_volume = P`, on the same plate and the same build. Add a second identity check for H2C: pre-fix and post-fix G-code **and** the estimate must match.

## NITS

- N1. The nil handling differs between code paths. The plan excludes `INT_MAX`, as `Tab.cpp:5923` and `Plater.cpp:3891` do. The tower itself (`WipeTower.cpp:2033`) and `GCode.cpp:110` treat any `> 1` as multi-nozzle. No shipped profile produces nil, so this is harmless, but add a one-line comment saying the gate copies Tab's rule.
- N2. The spec says Saving is the "escape hatch" that does not exist. That is correct (`Tab.cpp:5927-5931`), but note that a CLI/3mf can still carry Saving, and the plan keeps 15 for it. Fine.
- N3. The upstream report should mention the `bbs_3mf.cpp:8451` Nullable-cast bug (B1) and the hidden-row rule (B3). Those are likelier to get upstream attention than the gate itself.

## CONFIRMED-OK

- C1. v2.4.2's Type1 tower used `m_config.prime_volume` for every Type1 printer (`v2.4.2:Print.cpp:3510`; estimate at `:3338`). `407c78fb30` is the switch: in PR #14800, merged 2026-07-22 with SoftFever "LGTM", the body says it adds `filament_prime_volume` as "missing from upstream but present in BBS". `da2934d02a` is in PR #10780 (gh `commits/<sha>/pulls`). `407c78fb30` is not in v2.4.2, and `upstream/main` up to `ba361d9882` has not fixed it.
- C2. Nothing else in generation reads `filament_prime_volume`: only `Print.cpp:4658` and `WipeTowerEstimate.cpp:146`. `WipeTower` uses only the volumes passed in: the choice at `:3115`, and the replans at `:4718`/`:4745` carry `wipe_volume` forward. Type1's `ToolOrdering` uses the flush matrix (`ToolOrdering.cpp:2784`). `wipe_volume_nc` applies only to a same-extruder, different-nozzle change, so on H2D/X2D (one nozzle per extruder) every change takes the `ec` path that the plan overrides. Filament change length and ramming are untouched.
- C3. Every estimator funnels through `estimate_wipe_tower_footprint`: `Print::wipe_tower_data` (`Print.cpp:4468`), GUI `PartPlate` → `GLCanvas3D.cpp:3006`, and the CLI (`OrcaSlicer.cpp:4406`, `:5698`). `OrcaSlicer.cpp:5675` reads `prime_volume` only for a log line.
- C4. H2D/X2D land on `prime_volume`, which is their v2.4.2 behaviour. `extruder_max_nozzle_count` is a printer-level, per-extruder option (`printer_extruder_options`, `PrintConfig.cpp:9647`), not per plate or per variant. Only H2C ships `{1,6}`.
- C5. A fork-owned header-only file has no merge surface and needs no CMake entry. The Print.cpp override site is acceptable with the planned blank-line separators.
- C6. A Saving-mode reset happens on single-nozzle printers (`Tab.cpp:5930`), as the spec says. `prime_volume_mode` is not in the psWipeTower list but falls back to invalidating all steps.

## Required plan changes (summary)

1. B1: read `extruder_max_nozzle_count` as `ConfigOptionVector<int>`/`ConfigOptionIntsNullable`; build the H2C test via `set_deserialize_strict`.
2. B2: add the Bambu Lab gate (`is_BBL_printer()` in Print, `printer_model` prefix in the estimate); document Qidi as a residual.
3. B3: verify the row's visibility in the GUI. If it is hidden, choose (a) a visibility override or (b) exposing `filament_prime_volume` in the Filament tab (recommended if GUI control is the goal). Get the user's OK on S1.
4. B4: replace verification option (b) with a path that builds this branch without publishing.
5. S2/S3: correct the spec history (#8151, the shipping timeline, the missing rows) and the invalidation claim before any upstream report.
6. S4/S5/S6/S7: one estimate block after the loop; call-site markers; test tags, config shape and the full suite; byte-identity acceptance checks.
