# External docs review — `filament_minimal_purge_on_chute`

**Date:** 2026-06-02
**Reviewer scope:** Verify the new per-filament option is conceptually sound, does not duplicate an existing
OrcaSlicer / Bambu Studio setting, and aligns with documented unit conventions.
**Method:** Online docs (OrcaSlicer wiki, Bambu Lab wiki/forum, GitHub issues/discussions) cross-checked
against this repo's source (`GCode.cpp`, `Print.cpp`, `PrintConfig.cpp`, BBL `change_filament_gcode`).

---

## Verdict (TL;DR)

- **Nothing in the docs CONTRADICTS the core design.** The model — flush-into-infill requires the prime
  tower; the chute poop is the residual purge after infill diversion; Bambu ejects it via the
  `flush_length` placeholders inside an `M620…M621` AMS sequence — is **confirmed correct** by both the
  upstream wiki and this repo's own BBL profile G-code.
- **No existing OrcaSlicer or Bambu Studio setting controls a minimum chute/poop amount.** This is **not a
  duplicate.** The closest sibling, `filament_minimal_purge_on_wipe_tower`, targets a *different* surface
  (the wipe-tower / infill priming floor, not the ejected chute poop). See §2.
- **One real concern (Severity: MEDIUM)** — the unit is **mm of filament length**, whereas every adjacent
  purge/flush setting in Orca is **mm³ of volume**. Defensible for UX (the poop's drop-ability scales with
  length, and `flush_length` placeholders are in mm), but it is *against the local convention* and the
  sibling control sitting on the very same UI line is mm³. See §3.
- **One minor concern (Severity: LOW)** — tooltip wording / naming proximity to the mm³ sibling invites
  user confusion; mostly already mitigated by the explicit "(Note: … is a volume in mm³, not a length.)"
  parenthetical. See §3.

---

## 1. How OrcaSlicer multi-color flushing works (confirmed)

### Flush volume matrix → required purge
The per-transition purge requirement comes from `flush_volumes_matrix` (mm³), scaled by `flush_multiplier`.
Confirmed in `Print.cpp:3449-3450`:
```
volume_to_wipe = wipe_volumes[current_extruder_id][extruder_id];
volume_to_wipe *= m_config.flush_multiplier.get_at(0);
```

### Flush into infill / support / this object REQUIRES the prime tower — CONFIRMED
The OrcaSlicer wiki states flush-into-infill "will not take effect, unless the prime tower is enabled."
The repo logic agrees: the infill-diversion call (`mark_wiping_extrusions`) lives entirely inside the
`WipeTower2` planning block, gated by `m_config.purge_in_prime_tower && m_config.single_extruder_multi_material`
(`Print.cpp:3448-3460`). So our model assumption **(a) flush-into-infill requires the prime tower = TRUE.**
- OrcaSlicer wiki, Flush Options: https://www.orcaslicer.com/wiki/print_settings/multimaterial/multimaterial_settings_flush_options
- Upstream issue confirming the same + the "remaining purge falls back to a traditional poop/purge":
  https://github.com/OrcaSlicer/OrcaSlicer/issues/13164

### The chute "poop" is the RESIDUAL purge after infill diversion — CONFIRMED
`Print.cpp:3452` subtracts `filament_minimal_purge_on_wipe_tower` before diversion, runs
`mark_wiping_extrusions` to absorb as much as possible into object infill, then adds the floor back
(`:3459`). Whatever the wipe tower / chute still has to emit is the leftover. Issue #13164 describes exactly
this: "Any remaining purge volume that cannot be absorbed by available infill would then fall back to a
traditional poop/purge." So our model assumption **(b) chute poop = residual purge after infill diversion = TRUE.**

### Bambu ejects purge as a cut poop via `change_filament_gcode` (M620/M621) — CONFIRMED
This repo's `resources/profiles/BBL/machine/fdm_bbl_3dp_002_common.json` `change_filament_gcode` is wrapped
in `M620 S[next_extruder]A … M621 S[next_extruder]A` (AMS load/unload) and extrudes the purge as
`G1 E{flush_length_1…4 …}` segments. Crucially, the wipe-toward-chute moves are gated:
`{if flush_length_1 > 45 && flush_length_2 > 1} ; WIPE …`. **This is direct evidence for the feature's
premise:** if the computed `flush_length` is small, the wipe/cut steps don't fully fire and the poop can be
too small to drop free. Placeholders `flush_length` (mm) and `flush_length_1..4` are produced in
`GCode.cpp` (`set_extruder` and `get_path_of_change_filament`) from `purge_length = purge_volume / filament_area`.
- Bambu placeholder list (flush_length_1..16 are mm-of-filament extrusion lengths):
  https://wiki.bambulab.com/en/software/bambu-studio/placeholder-list
- M620/M621 = AMS load/unload (community confirmation):
  https://forum.bambulab.com/t/m620-m621/51208
- M-code cheat sheet (M620 load / M621 unload, flush behaviour):
  https://www.42prints.com/blog/reading-gcode-bambu-orca

**Model verification result: both (a) and (b) hold. No contradiction.**

---

## 2. CRITICAL — is there an existing min-chute/poop/purge setting? (No duplicate)

Searched docs, release notes, issues, and the repo for "minimal purge", "minimum flush", "poop", "min purge
volume", and "flush into infill". Findings:

| Existing control | Unit | What it floors | Same as our feature? |
|---|---|---|---|
| `filament_minimal_purge_on_wipe_tower` | **mm³** | Material **primed into the wipe tower / before infill purging** so successive infill extrusions are reliable. Subtracted then re-added around `mark_wiping_extrusions` (`Print.cpp:3452/3459`). | **No.** Floors the *wipe-tower priming*, not the *ejected chute poop*. On a pure chute/poop printer with no real tower geometry this does not guarantee a droppable poop. |
| `flush_into_infill_min_layer` (fork-custom, this repo) | layers | Gates *which layers* may purge into infill. | No — a layer gate, not an amount. |
| `flush_multiplier` | scalar | Scales the whole matrix. | No — global scale, can't set a per-filament floor. |
| `flush_volumes_matrix` | mm³ | Per-transition purge requirement. | No — the requirement, not a minimum-after-diversion floor. |
| `g_min_purge_volume` (hard-coded 100 mm³, `GCode.cpp:92`) | mm³ | Internal hard floor applied to any non-zero purge. | Related but **not user-configurable** and only applies when `tcr.purge_volume >= EPSILON`. The new option lets the user *raise* this floor and also restores a floor on the `purge_volume ≈ 0` path. |

**Conclusion: the feature does NOT duplicate any existing setting.** It fills a genuine gap — a *user-tunable,
per-filament* minimum on the **ejected chute poop** specifically, which `g_min_purge_volume` (fixed, internal)
and `filament_minimal_purge_on_wipe_tower` (tower-priming, mm³) do not provide. Community demand for finer
control over residual poop / waste exists but no implemented setting covers it:
- https://github.com/OrcaSlicer/OrcaSlicer/issues/13164 (flush-into-object replacing tower — leftover poop)
- https://github.com/SoftFever/OrcaSlicer/issues/5052 (add purge volume variable)
- https://github.com/OrcaSlicer/OrcaSlicer/issues/12171 (reduce AMS waste)

> Naming note: because the upstream sibling is `filament_minimal_purge_on_wipe_tower`, the new
> `filament_minimal_purge_on_chute` reads as a deliberate, parallel companion — good for discoverability.
> Just be aware they measure different physical things in different units (see §3).

---

## 3. Unit convention — mm vs mm³ (the one real concern)

**Orca convention:** flush/purge amounts are expressed in **mm³ of volume.**
- `filament_minimal_purge_on_wipe_tower` → `sidetext = "mm³"` (`PrintConfig.cpp:2717`)
- `flush_volumes_matrix`, `filament_tower_interface_purge_volume`, `g_min_purge_volume`,
  `g_purge_volume_one_time` are all mm³.

**Our new option** is **mm of filament length** (`sidetext = "mm"`, `PrintConfig.cpp` block at ~:2722).
Internally it is immediately converted to volume (`min_chute_purge = min_chute_length * filament_area`,
`GCode.cpp:883`) before being max-combined with the mm³ values — so the math is consistent.

**Assessment — Severity: MEDIUM.**
- *Defensible*: the Bambu `flush_length*` placeholders the feature ultimately drives are themselves in mm of
  filament, and "how long a strand will the printer push out" maps intuitively to whether the poop is long
  enough to drop. The tooltip already calls out the discrepancy explicitly.
- *Against convention*: it is the **only** purge-family setting in mm, and it sits on the **same UI optgroup
  line directly under** the mm³ `filament_minimal_purge_on_wipe_tower` (`Tab.cpp` Multimaterial page). Two
  adjacent "Minimal purge on …" rows with different units is a classic foot-gun.

**Recommendation (non-blocking):** Either (a) switch to mm³ for convention parity (cheap — drop the
`* filament_area` conversion and compare directly, relabel sidetext `mm³`), or (b) keep mm but make the
divergence even more visible (the parenthetical already helps; consider also surfacing the equivalent mm³ in
the tooltip, e.g. "default built-in floor ≈ 100 mm³ ≈ 40 mm at 1.75 mm"). The current "~40 mm (100 mm³)"
figure in the tooltip checks out: 40 mm × π/4 × 1.75² ≈ 96 mm³ ≈ the 100 mm³ `g_min_purge_volume`.

**Value range — Severity: LOW.** `min = 0`, default `0` (disabled) is sensible. No documented upper bound to
align with; mm values in the tens (≈ tens-to-~130 mm³) are the meaningful range given `g_purge_volume_one_time
= 135 mm³` per segment. Fine as-is.

---

## 4. Additional implementation note surfaced during review (not a docs issue)

The `flush_count = std::max(1, …)` change (`GCode.cpp:969` and `:7934`) is consistent with the BBL
`change_filament_gcode`: that template only emits FLUSH/WIPE blocks when `flush_length_n > 1` / `> 45`, so
forcing at least one segment is what actually makes a small user-floored purge produce a real, wipeable poop.
This aligns the slicer-side change with the printer-side G-code template and is conceptually sound. (Flagged
for the source-review pass, not a documentation conflict.)

---

## Sources

- OrcaSlicer wiki — Flush Options: https://www.orcaslicer.com/wiki/print_settings/multimaterial/multimaterial_settings_flush_options
- OrcaSlicer wiki — Material Multimaterial (minimal purge on wipe tower): https://github.com/OrcaSlicer/OrcaSlicer/wiki/material_multimaterial
- OrcaSlicer issue #13164 — flush-into-object replacing prime tower / leftover poop: https://github.com/OrcaSlicer/OrcaSlicer/issues/13164
- OrcaSlicer issue #12659 — flush placeholders behaviour on non-Bambu printers: https://github.com/OrcaSlicer/OrcaSlicer/issues/12659
- OrcaSlicer issue #5052 — add purge volume variable: https://github.com/SoftFever/OrcaSlicer/issues/5052
- OrcaSlicer issue #12171 — reduce AMS waste: https://github.com/OrcaSlicer/OrcaSlicer/issues/12171
- Bambu wiki — Placeholder list (flush_length_1..16): https://wiki.bambulab.com/en/software/bambu-studio/placeholder-list
- Bambu wiki — Reduce wasting during filament change: https://wiki.bambulab.com/en/software/bambu-studio/reduce-wasting-during-filament-change
- Bambu forum — M620/M621 (AMS load/unload): https://forum.bambulab.com/t/m620-m621/51208
- 42 Studio — Bambu/Orca M-code cheat sheet: https://www.42prints.com/blog/reading-gcode-bambu-orca
- Repo evidence — `src/libslic3r/Print.cpp:3448-3460`, `src/libslic3r/GCode.cpp:92-94,883-885,969`, `resources/profiles/BBL/machine/fdm_bbl_3dp_002_common.json` (`change_filament_gcode`)
