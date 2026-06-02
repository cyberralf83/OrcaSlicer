# Silent-failure review — Config / GUI / UX layer

**Commit under review:** `e57ed0375a` — "Fix interlocking beam density not applying (inverted filter axis)"
**Reviewer focus:** Silent loss / misrepresentation of user intent in the config, GUI and UX layer.
**Files in scope:**
- `src/slic3r/GUI/ConfigManipulation.cpp` (removed odd→even snap block; kept xor warning)
- `src/libslic3r/PrintConfig.cpp` (tooltips)
- `src/libslic3r/Feature/Interlocking/InterlockingGenerator.cpp/.hpp` (algorithm — examined for GUI/behavior mismatch only)

---

## Summary of verdict

The removal of the odd→even snap block is **clean** — it dropped no needed side effect, did not suppress any clamp, and re-slicing invalidation is correctly wired independently of it. The xor one-shot warning is **robust** against the re-entrancy / `is_msg_dlg_already_exist` paths I traced and cannot be permanently silenced by another dialog.

However, the change introduces / leaves in place one genuine **silent UX failure**: when `beam_group_count` (M) is large relative to a boundary segment (`segment length <= 2*M`), the algorithm silently keeps **every** beam and the user's `beam_gap` is completely ignored with no warning, no clamp, and a tooltip that implies a gap will always be produced. Two lower-severity tooltip/UX accuracy gaps accompany it.

---

## Finding 1 — `beam_gap` is silently ignored for short boundary segments (no warning, tooltip implies otherwise)

**Severity: Medium**

**Where:**
- `src/libslic3r/Feature/Interlocking/InterlockingGenerator.cpp:434-438` (`filterSegment`: `if (count <= 2 * M) { keep all }`)
- Tooltip text: `src/libslic3r/PrintConfig.cpp:4249-4252` (group count) and `4261-4263` (gap)

**Why it is a silent failure:**
The density filter keeps `M` anchored beams at each end of every contiguous segment. When a segment is no longer than `2*M` cells, the two end zones overlap and the early-out keeps **all** beams — the gap is never applied. With the GUI cap `max = 100` (`PrintConfig.cpp:4254, 4265`), a user can set `beam_group_count = 100`, and any interlocking boundary segment up to 200 cells long will silently come out as a **solid comb** even though the user explicitly set a non-zero `beam_gap`. Realistic interlocking boundaries are frequently well under 200 cells, so this is not an exotic corner case.

The user's intent (a gap between beam groups) is silently lost:
- No dialog/warning fires — the xor check only catches the "exactly one of the two is 0" case, not "both > 0 but M too large for the geometry."
- No value is clamped or echoed back, so the GUI keeps showing the user's chosen M/G as if they were honored.
- The new group-count tooltip states "Both ends of each boundary segment always get a full group, with the middle filled left-to-right," which actively implies a structured (gapped) result. For short segments the real result is "no gap at all," contradicting the tooltip.

This is exactly the class of "value larger than the interface, silently ignored / treated as solid with no indication" the review brief calls out.

**Suggested remedy:**
- Cheapest: extend the tooltip(s) to state that on boundary segments shorter than `2 × group count` cells, all beams are kept (no gap), so very large group counts effectively disable thinning on small contact areas.
- Better: surface it at config time — if `beam_group_count` is large relative to the model's interlocking footprint it cannot be known in `ConfigManipulation`, but a softer guardrail (e.g. warn when `beam_group_count` exceeds a sane threshold, or document the cell-unit scale) would reduce the "I set a gap and nothing changed" confusion.
- Best: log a one-time debug/notification from the generator when `density_enabled` but every segment hit the `count <= 2*M` keep-all path (i.e. the gap was a no-op for the whole object), so the silent no-op becomes observable.

---

## Finding 2 — Tooltip omits the cell-unit meaning, so "Beam gap = N" silently means something different than users expect

**Severity: Low**

**Where:** `src/libslic3r/PrintConfig.cpp:4249-4252` and `4261-4263`

**Why it is a silent failure (mild):**
The commit correctly dropped the old "finger / paired beams / odd snaps to even" wording, but the new tooltips describe counts only as "interlocking beams." The algorithm comment (`InterlockingGenerator.cpp:415-416`, `.hpp:170`) is explicit that the unit is a **cell** = "one interlocking tooth of each material," i.e. each unit is a pair of A+B teeth, not a single visible beam line. A user reading the tooltip will count visible beams and set values that quietly produce a different spacing than intended. The mismatch is a documentation-vs-behavior gap rather than a code defect, but it causes the user's numeric intent to be silently rescaled.

**Suggested remedy:** Add one clause to both tooltips clarifying the unit (e.g. "counted in interlocking cells; one cell is a tooth of each material"), matching the in-code comment so the displayed number maps to the actual geometry.

---

## Finding 3 — xor one-shot warning: verified NOT silently suppressible (no defect)

**Severity: Informational / Clean**

**Where:** `src/slic3r/GUI/ConfigManipulation.cpp:545-565`, guarded by the early return at `:193`, flag declared at `ConfigManipulation.hpp:26`.

**What I checked (per the brief):**

1. **Can the warning be skipped because another dialog set `is_msg_dlg_already_exist`?**
   No. Every preceding block in `update_print_fff_config` brackets its dialog with `is_msg_dlg_already_exist = true … = false` and resets it before returning (verified `:96-99, 116-118, 139-141, 158-162, 178-180, 206-210, 220-224, 233-237, 244-248, 256-260, 271-275, 286-290, 301-305, 354, 407-422, 506-510, 520-524, 535-542`; the spiral helper self-resets at `:1106-1108`). By the time control reaches the xor block, `is_msg_dlg_already_exist` is reliably `false`. When an earlier dialog's `ShowModal()` re-enters `update()` via CallAfter, the re-entrant call early-returns at `:193` and never touches `m_beam_density_xor_warned`, so the outer call still reaches and fires the xor warning. The flag cannot be stranded `true` by another dialog.

2. **Does the flag ever fail to reset and go permanently silent?**
   No. The `else if (!xor_bad)` branch (`:563-564`) resets `m_beam_density_xor_warned = false` whenever the condition clears (including when `interlocking_beam` is toggled off, since `xor_bad` folds in `beam_interlocking_on`). The one-shot correctly re-arms.

3. **Removed `apply()` side effect:**
   The old snap block's `apply()` only wrote the rounded-up even values back and refreshed the field (`apply()` at `:19-30` calls `load_config()`). With no rounding now performed, there is nothing to write back, so dropping `apply()` is correct and drops no needed refresh. Re-slicing invalidation for both keys is wired independently in `PrintObject.cpp:1160-1162` (`posSlice`), so a value change still reliably re-slices — no stale-slice risk from this removal.

No action required; recorded to document that the suspected silent-skip paths were ruled out.

---

## Items explicitly checked and found clean

- **Stale slice persistence:** `interlocking_beam_group_count` and `interlocking_beam_gap` both map to `posSlice` invalidation (`PrintObject.cpp:1160-1162`); changing them re-slices. Clean.
- **Removed snap suppressing a clamp:** the old block only rounded odd→even; it never clamped to min/max or disabled anything. Nothing silently lost by removal. Clean.
- **GUI range vs algorithm (`min=0, max=100`):** `0` is honored as "disabled" and the xor warning covers the asymmetric-zero case. The only range value the algorithm silently neutralizes is a large `M` on a short segment — captured as Finding 1.
- **`filterRow` empty-input deref (`positions[0]` at `:471`):** only reachable when `cells` is non-empty (each entry is created by `push_back`); not called on empty rows. No crash/silent-empty path. Clean.
