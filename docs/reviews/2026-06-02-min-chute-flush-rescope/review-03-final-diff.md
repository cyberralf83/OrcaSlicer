# Review 03 — Final holistic correctness pass

**Feature:** `filament_minimal_purge_on_chute` (per-filament, mm, default 0 = off) — floors the Bambu
chute "poop" flush, scoped to `WipeTowerIntegration::append_tcr`, gated on BBL + real tool change.

**Diff under review:** `git diff eef00f7032~1 HEAD -- src/` (full final feature diff vs pre-feature base).
**Latest rescope verified:** `4673720c01`.
**Scope limit:** `.github/` ignored.
**Lens:** final holistic correctness — compile-safety, config-def correctness, whitelist/invalidation/GUI
completeness, leftover inconsistencies from prior rounds, zero-footprint-at-default guarantee.

---

## Verdict

**No issues found in final diff.** The feature compiles cleanly, is internally consistent across all
six touched files, correctly floors the chute purge for a BBL user with the option set, and is
byte-identical to upstream at the default value (0) on every code path.

---

## What was verified (evidence)

### 1. Compile-safety of the final `GCode.cpp` edits (lines 873–888) — PASS
- **`gcodegen.is_BBL_Printer()` call:** `bool GCode::is_BBL_Printer();` (GCode.hpp:257) is a non-const
  member. The enclosing function is `WipeTowerIntegration::append_tcr(GCode& gcodegen, ...) const`
  (GCode.cpp:712); `gcodegen` is a **non-const** `GCode&` parameter (not `this`), so the const-ness of
  `append_tcr` does not restrict the call. Callable.
- **`(float)` casts:** `ConfigOptionFloats::get_at` returns `const double&` (Config.hpp:624). The two
  casts (`min_chute_length`, and the inherited `filament_diameter`/`filament_max_volumetric_speed`
  patterns) are valid `double → float` narrowing matching surrounding style. `get_at` falls back to
  `values.front()` for out-of-range indices, so it is safe even if `new_filament_id` exceeds the vector
  size (never throws/UB).
- **`std::max({...})` initializer-list:** all three elements (`tcr.purge_volume` float,
  `g_min_purge_volume` `static const float`, `min_chute_purge` float) are `float`; template arg deduces
  to `float` with no ambiguity. `<algorithm>` is included (GCode.cpp:25). This is the only
  initializer-list `std::max` in the file (i.e. it is the new code, not a pre-existing pattern), but it
  is well-formed.
- **Nested ternary type unification:** outer branches both yield `float`
  (`std::max(float,float)` / `std::max({float...})` / `0.f`); no implicit-conversion or
  common-type ambiguity. `purge_volume` is `float`. Downstream `purge_length = purge_volume /
  filament_area` unchanged.
- **`apply_chute_min` bool:** `is_real_toolchange && min_chute_purge > EPSILON &&
  gcodegen.is_BBL_Printer()`. `min_chute_purge` (float) compared to `EPSILON` (`double` 1e-4,
  libslic3r.h:52) promotes to double — fine.
- **`tcr` field types:** `ToolChangeResult::initial_tool`/`new_tool` are `int` (WipeTower.hpp:100,103),
  `is_tool_change` is `bool` (WipeTower.hpp:85), `purge_volume` is `float` (WipeTower.hpp:97). The guard
  `tcr.is_tool_change && tcr.initial_tool != tcr.new_tool` is type-valid and semantically meaningful:
  both fields are populated by `construct_tcr` (WipeTower.cpp:1250–1251) and `construct_block_tcr`
  (1275–1276) for every TCR.

### 2. Config-def correctness (PrintConfig.cpp:2722–2736) and hpp/cpp agreement — PASS
- `add("filament_minimal_purge_on_chute", coFloats)` — matches hpp
  `((ConfigOptionFloats, filament_minimal_purge_on_chute))` (PrintConfig.hpp:1456). Type agreement
  confirmed.
- `label = "Minimal chute flush length"`, `sidetext = "mm"` (length, distinct from the sibling's `mm³`
  volume), `min = 0`, `mode = comAdvanced`, default `ConfigOptionFloats { 0. }`. All match the
  requested spec and are consistent with the sibling `filament_minimal_purge_on_wipe_tower` (no
  `def->category` on either — GUI page handles grouping, so the omission is consistent, not a defect).
- Placement is immediately after the wipe-tower sibling and before `filament_cooling_before_tower`;
  clean, no stray edits to neighbours.
- Tooltip accuracy: "about 40 mm (100 mm³)" matches `g_min_purge_volume = 100.f` (GCode.cpp:92) divided
  by the 1.75 mm filament area (≈2.405 mm² → 100/2.405 ≈ 41.6 mm). Accurate; the "(e.g. Bambu Lab)" /
  chute-only wording matches the actual `is_BBL_Printer()` gate.

### 3. Whitelist / invalidation / GUI lines — COMPLETE and CORRECT
- **Preset whitelist** (`s_Preset_filament_options`, Preset.cpp:1282): key added beside the sibling.
  This is the gotcha from prior feature work (per project memory) — handled correctly.
- **Invalidation** (`Print::invalidate_state_by_config_options`, Print.cpp:289): key added beside the
  sibling, so changing it re-runs G-code export. Correct.
- **`Plater::on_config_change`** (Plater.cpp:16692): key added to the list that triggers a config
  refresh, beside the sibling. Correct.
- **Tab UI line** (Tab.cpp:4151): `append_single_option_line` adds the field to the Multimaterial
  page's "Wipe tower parameters" optgroup, beside the sibling.
- **Tab toggle** (Tab.cpp:4362): `toggle_option("filament_minimal_purge_on_chute", is_BBL_printer)` —
  enabled for BBL, the exact inverse of the wipe-tower sibling (`!is_BBL_printer`, line 4358).
  `is_BBL_printer` is in scope (defined Tab.cpp:4269–4272 via `is_bbl_vendor()`).
- **GUI gate vs execution gate agree:** the UI uses `preset_bundle->is_bbl_vendor()`;
  `Print::is_BBL_printer()` is assigned from `preset_bundle.is_bbl_vendor()`
  (BackgroundSlicingProcess.cpp:199, 683); `GCode::is_BBL_Printer()` reads that same value
  (GCode.cpp:2030, Print.hpp:1070). The option is therefore visible exactly when the execution path
  honors it — no GUI-vs-execution mismatch (the exact failure mode the rescope commit set out to
  eliminate).

### 4. Leftover inconsistencies from prior rounds — NONE
- **`set_extruder` mirror fully reverted:** the full final diff (`eef00f7032~1..HEAD`) shows **zero**
  net change in `GCode::set_extruder` (verified by grepping the diff — the only `min_chute`/`wipe_*`
  matches are in the `append_tcr` block, lines 17–23 of the diff hunk). The `set_extruder` chute clamp
  and both `flush_count = std::max(1, std::min(...))` edits are gone, leaving those paths byte-identical
  to upstream as the commit claims.
- **Comment matches code:** GCode.cpp:873–878 ("applies only on real colour changes on BBL chute
  printers ... every other case keeps the original behaviour") accurately describes the `apply_chute_min`
  guard and the two-branch ternary. No stale comment referencing the removed finish-layer/priming
  wording or the reverted `flush_count` round() rationale.
- **No half-reverted lines, no stray references:** the only occurrences of the key across `src/` are the
  six intended sites (GCode.cpp, Preset.cpp, Print.cpp, PrintConfig.cpp, PrintConfig.hpp, Plater.cpp,
  Tab.cpp ×2). No `WipeTower2.*` reference (correctly — the feature does not touch wipe-tower geometry,
  and BBL is always Type1 so Type2/WipeTower2 is unreachable for this feature; `wipe_tower_type()`
  returns Type1 whenever `is_BBL_printer()`, Print.hpp:1072).

### 5. Zero footprint at default; correct behaviour when set — PASS
- **Default (0):** `min_chute_purge = 0 * filament_area = 0`, which is **not** `> EPSILON`, so
  `apply_chute_min == false` on every TCR. The ternary then reduces to
  `tcr.purge_volume < EPSILON ? 0.f : std::max(tcr.purge_volume, g_min_purge_volume)` — byte-identical to
  the upstream original line. Existing profiles that omit the key load with default 0 (no legacy
  migration needed; `handle_legacy` is for renames, not additive keys), so no behavioural change for
  any existing user.
- **When set on a BBL real colour change:** floor is `std::max(min_chute_purge, g_min_purge_volume)`
  (purge==0 branch) or `std::max({purge, 100, min_chute_purge})` (purge>0 branch), i.e. never below the
  100 mm³ built-in floor and never below the user's requested length×area. Correct.
- **Non-BBL / same-tool / priming / finish-layer:** `apply_chute_min == false` → upstream behaviour,
  no spurious purge. Correct.

---

## Findings

None. (Severity legend unused — no Critical/High/Medium/Low items.)
