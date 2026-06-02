# Silent Inconsistencies / Misleading Fallback Review — `filament_minimal_purge_on_chute`

Date: 2026-06-02
Reviewer lens: silent inconsistencies / misleading fallback behavior (no errors, but feature "succeeds" while doing nothing or the wrong thing)
Scope: uncommitted working-tree diff only; `.github/` ignored.

## Feature under review

New per-filament option `filament_minimal_purge_on_chute` (`coFloats`, unit **mm**, default `0` = off). Forces a minimum
Bambu "chute poop" so a flush that is mostly diverted into object infill still leaves a poop large enough to drop free of the
nozzle. Implemented as a clamp in `WipeTowerIntegration::append_tcr` (`src/libslic3r/GCode.cpp`, ~873).

Relevant constants (`src/libslic3r/GCode.cpp:92-94`):
- `g_min_purge_volume   = 100.f`  (mm³)
- `g_purge_volume_one_time = 135.f` (mm³)
- `g_max_flush_count    = 4`

For 1.75 mm filament `filament_area = π/4·1.75² ≈ 2.405 mm²`, so the hidden `g_min_purge_volume` floor of 100 mm³ ≈ **41.6 mm**
of filament, and `g_purge_volume_one_time/2 ≈ 67.5 mm³ ≈ 28 mm`.

The clamp (`GCode.cpp:880-886`):
```cpp
const float min_chute_length   = (float) full_config.filament_minimal_purge_on_chute.get_at(new_filament_id);
const float min_chute_purge    = min_chute_length * filament_area; // mm -> mm³
const bool  is_real_toolchange = tcr.is_tool_change && tcr.initial_tool != tcr.new_tool;
float purge_volume = (tcr.purge_volume < EPSILON)
    ? ((is_real_toolchange && min_chute_purge > EPSILON) ? min_chute_purge : 0.f)
    : std::max({tcr.purge_volume, g_min_purge_volume, min_chute_purge});
```

---

## Findings

### F1 — HIGH — Hidden 100 mm³ floor silently overrides any setting below ~42 mm (and only in one of the two branches)
**File:** `src/libslic3r/GCode.cpp:883-885`

**Masked behavior.** In the **non-zero branch** (`tcr.purge_volume >= EPSILON`, i.e. the flush was *not* fully diverted into
infill) the result is `std::max({tcr.purge_volume, g_min_purge_volume=100mm³, min_chute_purge})`. Any chute setting below
`100 mm³` (≈ **41.6 mm** at 1.75 mm) is silently swallowed by the pre-existing 100 mm³ floor. The user enters e.g. 20 mm,
slices, sees an *identical* poop to before, and gets **no error, no warning, no clamp note** — the setting "doesn't work" below
~42 mm with no indication of why.

Worse, the floor is applied **inconsistently** between the two branches:
- non-zero branch: floor IS applied (100 mm³ minimum) → small values are ignored.
- zero branch (`tcr.purge_volume < EPSILON`): uses `min_chute_purge` **raw, with no 100 mm³ floor** → a small value like 10 mm
  *is* honored.

So the *same* small setting (e.g. 15 mm) produces **two opposite outcomes** depending on a runtime quantity the user cannot
see (how much of this particular tool change's flush happened to be redirected into infill). The feature is non-deterministic
from the user's vantage point.

**Why it matters.** This is a classic silent-success: the operation completes, G-code is emitted, but the knob the user turned
had no effect (or an inconsistent effect). The most common debugging path — "I set it low and nothing changed, so the feature
is broken" — leads to wasted time and bug reports, when the real story is an undocumented internal floor.

**Fix.**
1. Surface the floor in the UI: set `def->min = ` (or document in the tooltip) the effective minimum below which the global
   100 mm³ floor dominates, i.e. ~42 mm at 1.75 mm. Because the floor is volumetric and the option is a length, the exact mm
   value is diameter-dependent; at minimum the tooltip must state: "Values below roughly 40 mm have no effect because a built-in
   100 mm³ minimum purge already applies." Today the tooltip is silent on this.
2. Make the two branches consistent: either apply the `g_min_purge_volume` floor in both branches, or in neither when an
   explicit chute minimum is set. The current asymmetry (floor in one branch, raw in the other) is the actual silent
   inconsistency and should be removed.

---

### F2 — HIGH — Units trap: new option is **mm**, adjacent sibling is **mm³**, both labelled "Minimal purge on …"
**File:** `src/libslic3r/PrintConfig.cpp:2722-2733` (new) vs `:2711-2719` (`filament_minimal_purge_on_wipe_tower`, mm³);
GUI placement `src/slic3r/GUI/Tab.cpp:4150-4151`

**Masked behavior.** The two options sit on consecutive lines in the same "Wipe tower parameters" optgroup:
- `filament_minimal_purge_on_wipe_tower` — label "Minimal purge on wipe tower", sidetext **`mm³`**.
- `filament_minimal_purge_on_chute` — label "Minimal purge on chute", sidetext **`mm`**.

A user who has internalized the wipe-tower value as a *volume* will very plausibly type a volume-scale number (e.g. 100, 200)
into the chute field, not realizing it is a **length**. 100 "mm" of filament = ~240 mm³, more than double what they intended;
200 → ~480 mm³. The slice still succeeds, the poop is just silently far larger (or, if they think in mm and the sibling was the
odd one out, far smaller) than expected. Nothing flags the mismatch. The only consumer is `GCode.cpp:880`, so there is no
secondary validation that could catch an out-of-scale value.

The new tooltip says "Minimum **length** of filament" which is correct, but the sidetext `mm` and the near-identical label make
conflation easy, especially since the *implementation* immediately converts mm→mm³ (`min_chute_purge = min_chute_length *
filament_area`) — i.e. internally it really is a volume, the mm framing is a UI choice that diverges from its sibling for no
surfaced reason.

**Why it matters.** Silent misconfiguration: wrong-by-a-factor-of-~2.4 input that produces no error and a plausible-looking
(but wrong) result. Hard to notice without measuring the physical poop.

**Fix.** Strongly prefer making the unit **match the sibling (mm³)** so the two adjacent "Minimal purge" knobs are directly
comparable and the internal representation matches the UI; the conversion in GCode.cpp then disappears. If mm is intentional
(matches how users think about poop length), the label should be disambiguated, e.g. "Minimal chute poop **length**", and the
tooltip should explicitly contrast it with the wipe-tower option's mm³ unit to defuse the conflation.

---

### F3 — HIGH — Clamp is brand-agnostic in code, but routing makes it silently inert for the only printers whose GUI hides it; can also be silently active via inheritance
**File:** clamp `src/libslic3r/GCode.cpp:880-885`; routing `src/libslic3r/Print.hpp:1072`, `src/libslic3r/GCode.cpp:1516,1541-1568`; GUI gate `src/slic3r/GUI/Tab.cpp:4360-4362`

**Masked behavior.** `WipeTowerIntegration::tool_change` dispatches by wipe-tower type:
```cpp
WipeTowerType wipe_tower_type() const { return is_BBL_printer() ? WipeTowerType::Type1 : m_config.wipe_tower_type.value; } // Print.hpp:1072
```
- BBL → **Type1** → `append_tcr` (the **modified** function) → clamp runs. ✔ matches GUI (`toggle_option(..., is_BBL_printer)`).
- Non-BBL → `m_config.wipe_tower_type.value`, **default Type2** (`PrintConfig.cpp:5995`) → `append_tcr2`
  (`GCode.cpp:1130`, **unmodified**, never reads `filament_minimal_purge_on_chute`) → clamp does **not** run.

Two silent inconsistencies fall out of this:

1. **Non-BBL with `wipe_tower_type = type1`.** The enum is user/profile-settable. A non-BBL printer set (or inheriting) Type1
   *will* reach `append_tcr` and the brand-agnostic clamp *will* fire — yet the GUI hid the option for non-BBL
   (`toggle_option("filament_minimal_purge_on_chute", is_BBL_printer)`). The value can still be non-zero via profile
   inheritance / 3MF import / a previously-BBL preset carried over, in which case it **silently takes effect on a printer where
   the option was never shown**. The GUI gate (`is_BBL_printer`) and the slicer gate (`wipe_tower_type==Type1`) are *different
   predicates*; they only coincide for the default config. Hidden ≠ disabled — the value is still serialized and still consumed.

2. **Non-BBL Type2 (the default non-BBL case).** Here the option is correctly hidden in the GUI, but if a value is present (same
   import/inheritance routes), it is **silently ignored** — `append_tcr2` has no clamp. A user migrating a Bambu filament
   profile onto, say, a Prusa-style multi-tool printer keeps the saved value, sees it do nothing, and has no feedback that the
   chute concept doesn't apply to their wipe-tower-only machine.

The new tooltip *does* say "Only effective on printers that eject purge through a chute … not on wipe-tower-only setups," which
partially mitigates (2) — but the tooltip is invisible when the option itself is hidden, so it never reaches the affected user.

**Why it matters.** The feature's effective scope is governed by `wipe_tower_type` (slicer) while its visibility is governed by
`is_BBL_printer` (GUI). These can disagree. The result is a value that is sometimes shown-but-inert, sometimes hidden-but-active,
with no diagnostic in any case.

**Fix.**
- Gate the clamp on the same predicate the GUI uses, or better, guard explicitly: only apply the chute floor when
  `is_BBL_printer` / Type1, and when a non-zero value is present on a printer where it cannot apply, emit a one-time
  `BOOST_LOG_TRIVIAL(warning)` (or a slicing notification) rather than silently ignoring it.
- Consider validating in `Print::validate()` that `filament_minimal_purge_on_chute != 0` is only set for configurations whose
  `wipe_tower_type` resolves to Type1, surfacing a warning otherwise.

---

### F4 — MEDIUM — `is_real_toolchange` guard can silently suppress a poop the user expects on same-filament / interface / priming results
**File:** `src/libslic3r/GCode.cpp:882-884`; semantics `src/libslic3r/GCode/WipeTower.cpp:1250-1251,1262,1275-1276`,
merge `:2684-2685`

**Masked behavior.** The zero-branch only pads when `is_real_toolchange = tcr.is_tool_change && tcr.initial_tool != tcr.new_tool`.
Cases where this is intentionally false but a poop may still be expected:
- **`construct_block_tcr` results** (`WipeTower.cpp:1275-1276`) set `initial_tool == new_tool` deliberately → `is_real_toolchange`
  is false → if `tcr.purge_volume < EPSILON`, the chute minimum is **not** applied even though this block can correspond to a
  real filament action. This is silent: no poop, no note.
- **Finish-layer / sparse tcr** (`construct_tcr(..., is_finish=true, is_tool_change=false, ...)`, `WipeTower.cpp:2445,3210`):
  `is_tool_change` is false, so correctly skipped — but the *only* signal distinguishing "no poop wanted" from "poop wanted" is
  these flags, and `merge_tcr` (`:2684-2685`) rewrites `initial_tool/new_tool` from the merged endpoints. After a merge, a
  combined tcr's `initial_tool != new_tool` test reflects the *outer* pair, which may not match the segment that actually carries
  the chute purge. The guard can therefore misclassify merged tcrs in either direction.
- **Interface / `is_contact` changes** (`GCode.cpp:914-917`): when `enable_tower_interface_features && tcr.is_contact`, temps are
  overridden but the chute floor is governed solely by `is_real_toolchange`, ignoring `is_contact`. A contact/interface pass with
  `tcr.purge_volume < EPSILON` gets no chute padding even if the user expected one.

**Why it matters.** The user sees the feature work for "normal" tool changes but silently do nothing for the block/interface/
finish-merge cases, with no way to tell which path a given change took. This is a partial silent no-op.

**Fix.** Decide and document the intended set of cases. If block/interface results should also be floored, broaden the predicate
(e.g. include `tcr.is_tool_change` regardless of equal tools for block tcrs, or test on the segment's own tools before merge). Add
a debug log line recording, per tool change, whether the chute floor was applied and why, so the behavior is observable.

---

### F5 — MEDIUM — Zero-branch honors tiny raw values, defeating the feature's own stated purpose ("succeeds" but useless)
**File:** `src/libslic3r/GCode.cpp:883-884` and `flush_count` at `:969`

**Masked behavior.** In the zero-branch a small `min_chute_purge` (e.g. 5–20 mm³, from a chute length of ~2–8 mm) is used raw.
`flush_count = std::max(1, std::min(g_max_flush_count, round(purge_volume / 135)))` then yields **1**, and `flush_unit =
purge_length`. So the code emits a single, *very short* flush — exactly the "too small to drop free, sticks to the nozzle" poop
the feature exists to prevent. The slice succeeds, a poop is emitted, but it does not achieve the feature's goal, and the user has
no indication that their value is below a physically useful threshold.

This compounds F1: below ~42 mm the value is either ignored (non-zero branch) or honored-but-useless (zero branch).

**Why it matters.** "Succeeds while doing nothing useful" — the worst kind of silent behavior, because the UI, the absence of
errors, and the presence of *a* poop all suggest the setting is working.

**Fix.** Establish a meaningful lower bound for "drops free" (the author clearly already knows ~42 mm / 100 mm³ matters) and
enforce/communicate it: either raise the effective floor for the chute feature to that threshold in both branches, or clamp the
UI `min` to it and document that smaller values are not physically effective.

---

## Severity summary

| ID | Severity | File:line | One-line masked inconsistency |
|----|----------|-----------|-------------------------------|
| F1 | HIGH | GCode.cpp:883-885 | 100 mm³ floor silently overrides settings below ~42 mm in the non-zero branch; floor inconsistently absent in the zero branch. |
| F2 | HIGH | PrintConfig.cpp:2722-2733 | New option is mm, adjacent sibling is mm³, both "Minimal purge" — easy ~2.4× silent misconfiguration. |
| F3 | HIGH | Print.hpp:1072 / GCode.cpp:1516 / Tab.cpp:4360 | GUI gate (is_BBL) ≠ slicer gate (wipe_tower_type): value can be hidden-but-active (non-BBL Type1) or shown-scope-but-ignored (Type2 append_tcr2). |
| F4 | MEDIUM | GCode.cpp:882-884 | `is_real_toolchange` guard silently skips block/interface/merged-tcr cases that may need a poop. |
| F5 | MEDIUM | GCode.cpp:883-884,969 | Zero-branch honors sub-threshold values, emitting a poop too small to drop free — succeeds but useless. |

No `logError`/`logForDebugging`/Sentry-style instrumentation exists anywhere on this path, so none of the above produce any
diagnostic; every one is a pure silent inconsistency.
