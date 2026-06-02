# Silent-Failure Review — Interlocking Beam Density Fix (commit e57ed0375a)

Scope: slicing / algorithm layer only. Focus: places where the beam-density
feature can silently do nothing, silently disable itself, or silently produce
wrong-but-plausible output with no error/warning surfaced to the user.

Reviewed files:
- `src/libslic3r/Feature/Interlocking/InterlockingGenerator.cpp`
- `src/libslic3r/Feature/Interlocking/InterlockingGenerator.hpp`
- `src/libslic3r/PrintConfig.cpp`
- `src/slic3r/GUI/ConfigManipulation.cpp`
- Call site: `src/libslic3r/PrintObjectSlice.cpp:1243`

Key context: the slicing layer DOES have a non-disruptive user-feedback channel
here. `PrintObject::active_step_add_warning(...)` is available and is used
elsewhere in the same translation unit family (e.g. `PrintObject.cpp:846`,
`PrintObjectSlice.cpp:1232`). The interlocking generator currently uses NONE of
it — every abort path is a bare `return`. So "the slicer can't warn from here"
is not a valid excuse for the silent paths below.

---

## Finding 1 — Density silently disabled at slice time when only one of group/gap is set

Severity: HIGH

Location:
- `src/libslic3r/Feature/Interlocking/InterlockingGenerator.cpp:335`
  (`const bool density_enabled = beam_group_count > 0 && beam_gap > 0;`)
- Fallback at lines 341-342 (`density_enabled ? ... : cells`)

Why it is a silent failure:
When the user sets exactly one of `beam_group_count` / `beam_gap` to a positive
value and leaves the other at 0, `density_enabled` is false, the filter is never
run, and `filtered_type0 / filtered_type1` alias the full `cells` set. The
feature silently produces a SOLID comb (every beam placed) — the exact opposite
of what a user who just typed "group count = 4" expects. There is no warning,
log, or `active_step_add_warning` from the slicer. The commit's own comment
(lines 545-550 in ConfigManipulation.cpp) explicitly acknowledges this: "Setting
only one silently produces a solid comb."

The GUI (`ConfigManipulation.cpp:554-565`) does warn — but only once
(`m_beam_density_xor_warned` latches true), only when the value is changed
interactively in the live config UI, and only in the GUI process. It does NOT
fire for: profiles imported/loaded with a one-sided value, 3MF project files,
CLI/headless slicing, or any path that reaches `generate_interlocking_structure`
without round-tripping through `update_print_fff_config`. In all those cases the
slicer is the last line of defence and it stays silent.

Suggested remedy:
In `generateInterlockingStructure` (or `generate_interlocking_structure`, which
has the `PrintObject*`), detect the XOR condition
(`(beam_group_count > 0) != (beam_gap > 0)`) and emit
`print_object->active_step_add_warning(WarningLevel::NON_CRITICAL, _u8L("Beam
density control was ignored: both 'Beam group count' and 'Beam gap' must be > 0.
The interlocking beams were left solid."), ...)`. This makes the no-op visible on
every slice path, not just the live GUI edit path.

---

## Finding 2 — Tiny interface regions silently ignore the density setting (keep-all threshold)

Severity: HIGH

Location:
- `src/libslic3r/Feature/Interlocking/InterlockingGenerator.cpp:434-438`
  (`if (count <= 2 * M) { keep all; return; }`)

Why it is a silent failure:
The new `filterSegment` keeps EVERY beam in any contiguous segment whose length
is `<= 2 * M` ("Short segment: ends overlap, keep all"). `M` (=`beam_group_count`)
is user-settable up to 100 (`PrintConfig.cpp:4254`). So any interface segment up
to `2*M` cells long is rendered solid with no thinning whatsoever, while the user
believes density control is active. With a modest `group_count = 8`, every
boundary run up to 16 cells long is silently exempt; with the max of 100, runs up
to 200 cells are exempt — which for most real parts is the entire interface. The
density setting then has zero visible effect on the output and the user gets
wrong-but-plausible geometry (a solid comb that looks like density was simply
"not aggressive enough") with no indication that the threshold swallowed it.

This is the same class of bug the commit is fixing (a setting that "had no
visible effect on slicing"), just relocated: instead of an inverted axis, it is
now a silent size threshold. The keep-all path is reached purely on segment
geometry, never logged or counted.

Suggested remedy:
At minimum, document the threshold in the user-facing tooltip (the tooltip at
`PrintConfig.cpp:4249-4252` says nothing about small regions being exempt). Better:
track whether any segment was actually thinned across the whole object; if density
is enabled but `0` segments were thinned (every segment hit the keep-all branch),
surface a NON_CRITICAL `active_step_add_warning` such as "Beam density had no
effect: all interlocking regions are shorter than the configured group size."

---

## Finding 3 — Air filtering fragments rows so density silently no-ops on the fragments

Severity: MEDIUM

Location:
- Order of operations in `generateInterlockingStructure`:
  air-cell erase at `InterlockingGenerator.cpp:199-201`, then
  `applyMicrostructureToOutlines` → `filterCellsForAxis` → `filterRow`
  segmentation at lines 466-484.

Why it is a silent failure:
When `air_filtering` is on (`boundary_avoidance > 0` or `boundary_avoidance_z >
0`, lines 51-58), cells are erased from `has_all_meshes` BEFORE density filtering
runs. `filterRow` (lines 466-484) then splits each (y,z)/(x,z) row into
contiguous segments where coordinates differ by exactly 1; every erased cell
punches a hole that starts a new segment. The result is many short segments, each
of which is far more likely to fall under the `count <= 2*M` keep-all threshold
(Finding 2) and therefore receive no thinning. The interaction is invisible:
turning up boundary avoidance can silently defeat the density setting because the
two features compose multiplicatively, and nothing reports that the effective
density changed.

This is aggravated by this commit specifically: it restored the *per-segment*
anchoring ("every disjoint boundary segment gets its own anchored ends",
lines 463-465), which means each fragment re-anchors a full `M`-beam group at both
ends — so fragmentation strictly increases the number of beams kept, pushing the
result back toward solid, again with no feedback.

Suggested remedy:
Document the air-filtering ↔ density interaction. If feasible, fold the
per-segment thinned/total counts into the same "density had little/no effect"
warning proposed in Finding 2, so the user can see when avoidance has effectively
neutralised density.

---

## Finding 4 — Feature silently does nothing for single-extruder / same-extruder geometry

Severity: MEDIUM

Location:
- `src/libslic3r/Feature/Interlocking/InterlockingGenerator.cpp:63-81`
  (region-pair loop; `if (extruder_nr_a == extruder_nr_b) continue;`)

Why it is a silent failure:
`generate_interlocking_structure` only runs `generateInterlockingStructure()` for
pairs of printing regions assigned to DIFFERENT extruders. For a single-region /
single-extruder object — or a multi-region object where all regions share one
extruder — the inner loop body is skipped for every pair (or never executes at
all when there is only one region), and the whole feature, including the
brand-new density logic, silently produces nothing. A user who enables
interlocking beams and sets group/gap on a single-material object gets exactly
zero beams and zero feedback. (Not introduced by this commit, but it is the
gating precondition for the density feature this commit ships, and it lives in a
file under review.)

Suggested remedy:
If `interlocking_beam` is enabled but no differing-extruder region pair exists,
emit one `active_step_add_warning(NON_CRITICAL, _u8L("Interlocking beams were
enabled but the object uses a single extruder/material, so no interlocking
structure was generated."))`. This converts a confusing "nothing happened" into
an actionable message.

---

## Finding 5 — Enabled-but-misconfigured early return is silent

Severity: LOW

Location:
- `src/libslic3r/Feature/Interlocking/InterlockingGenerator.cpp:34-36`

Why it is a silent failure:
The guard returns silently when `interlocking_beam` is on but
`interlocking_beam_layer_count < 1`, `interlocking_depth < 1`, or
`interlocking_beam_width < EPSILON`. The `!config.interlocking_beam` half is a
legitimate "feature off, do nothing" case; the other three are
misconfigurations of an ENABLED feature that produce no beams and no message. A
user who zeroes beam width (or a profile that ships width 0) sees the feature
silently evaporate. Severity is Low because GUI min-bounds make these hard to
reach interactively, but profile/3MF/CLI paths can still deliver such values.
(Pre-existing; not modified by this commit.)

Suggested remedy:
Split the bare `return` so that when `interlocking_beam` is true but a parameter
is out of range, a NON_CRITICAL warning is added before returning.

---

## Finding 6 — has_all_meshes.empty() early return is silent (informational)

Severity: LOW

Location:
- `src/libslic3r/Feature/Interlocking/InterlockingGenerator.cpp:189-191`

Why it is a silent failure:
When the two meshes' dilated shells never overlap (no shared boundary cells),
the function returns with no output and no message. This is usually a legitimate
"the parts don't touch, so there is nothing to interlock" case, but it is
indistinguishable to the user from a bug, and when combined with aggressive air
filtering (which erases shared cells, lines 199-201) the set can become empty
after the user expected beams. (Pre-existing.)

Suggested remedy:
Optional: a debug-level `BOOST_LOG_TRIVIAL(debug)` line noting that the meshes do
not overlap would aid diagnosis without spamming the user. Not strictly required.

---

## Clean / non-issues (verified)

- "All beams removed" (keep-none) is NOT reachable in `filterSegment`. When
  `count > 2*M`, both end loops insert `M >= 1` positions (M is guaranteed > 0 by
  the `density_enabled` gate at line 335 and by the GUI min=0/value>0 contract);
  when `count <= 2*M`, all are kept. So a row can never be silently emptied. Good.
- `filterRow` (lines 466-484) is never called with an empty `positions` vector:
  rows are populated only from existing cells, so `positions[0]` at line 471 is
  safe. No silent out-of-bounds.
- `M` cannot be negative: config `min = 0` (PrintConfig.cpp:4253/4264) and the
  enable gate requires `> 0`, so the `2*M` threshold and the stride math have no
  sign-flip / modulo-by-non-positive hazard.
- The `density_enabled ? storage : cells` aliasing (lines 341-342) correctly
  avoids the previously noted empty-set trap; populating both axes
  unconditionally is sound.

---

## Verdict

Not clean. No new crash or keep-none data-loss was found, and the axis-inversion
bug the commit targets is genuinely fixed. However the change ships / preserves
several SILENT no-op paths in the slicing layer: the slicer never warns when
density is configured but silently ignored — via the one-sided group/gap gate
(HIGH), the `count <= 2*M` keep-all threshold that can swallow the whole setting
(HIGH), the air-filtering fragmentation interaction (MEDIUM), and the
single-extruder / misconfig / non-overlap early returns (MEDIUM/LOW). All of
these have an available, non-disruptive remedy (`active_step_add_warning`) that
is already used elsewhere in the slicing layer and is currently unused here.
