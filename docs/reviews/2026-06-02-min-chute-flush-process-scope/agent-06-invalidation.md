# Agent 06 — Invalidation & Reslice Review

Scope: verify the min-chute-flush filament→process rename does not break re-slice /
re-export. Focus on `Print::invalidate_state_by_config_options` (Print.cpp), the
`Print::invalidate_step` cascade, and `Plater::on_config_change` (`update_scheduled`).

## Verdict

APPROVE (invalidation/reslice dimension). The two invalidation references the proposal
renames (Print.cpp:289, Plater.cpp:16692) are in the correct branches and the resulting
tier is correct, not over- or under-invalidating. No additional hidden invalidation or
preview-cache path keys on this option. One MEDIUM nuance and one LOW note below; neither
blocks the proposal.

## Confirmed correct

1. **Print.cpp:289 is in the `psWipeTower + psSkirtBrim` block, NOT posSlice.**
   The big `else if` spanning lines 283–368 ends at lines 369–370 with
   `steps.emplace_back(psWipeTower); steps.emplace_back(psSkirtBrim);`. Line 289
   (`filament_minimal_purge_on_chute`) sits inside that block, two lines below its sibling
   `filament_minimal_purge_on_wipe_tower` (288) and in company with `prime_volume` (342),
   `flush_into_infill` (343), `flush_into_support` (344), `wipe_tower_max_purge_speed` (356).
   The posSlice block is a *separate, earlier* `else if` at 271–282 — the chute key is NOT
   in it. Proposal's claim (point 5, "already in the correct block") is accurate.

2. **`psWipeTower` is the correct minimal tier; `psGCodeExport` re-runs automatically.**
   `Print::invalidate_step` (Print.cpp:426–433) propagates to dependent steps:
   ```cpp
   bool Print::invalidate_step(PrintStep step) {
       bool invalidated = Inherited::invalidate_step(step);
       if (step != psGCodeExport)
           invalidated |= Inherited::invalidate_step(psGCodeExport);
       return invalidated;
   }
   ```
   So emplacing `psWipeTower` (line 369) both regenerates tool ordering / wipe tower AND
   forces `psGCodeExport` to re-run. The purge floor is consumed inside `append_tcr`
   (GCode.cpp:873–888), which runs during the G-code export — so re-export is exactly what
   must happen, and it does. The tier is correct. (It is technically one tier broader than
   the strict minimum — see MEDIUM-1 — but matches every sibling purge/flush key, so it is
   the right choice for consistency and is safe.)

3. **Final `else` of the function = `invalidate_all_steps()` (fail-safe, NOT silent skip).**
   Print.cpp:402–407:
   ```cpp
   } else {
       // for legacy, if we can't handle this option let's invalidate all steps
       //FIXME invalidate all steps of all objects as well?
       invalidated |= this->invalidate_all_steps();
   }
   ```
   Consequence: if the rename were to MISS Print.cpp:289 (i.e. the new key
   `minimal_chute_flush_length` is not added to any branch), the key would still match no
   branch and fall through to this `else`, which invalidates ALL print steps. That is a
   perf hit (full reslice on every edit of this option), never a stale-output/silent
   no-reslice bug. So a missed Print.cpp rename fails SAFE. The proposal renames in place,
   so this is moot, but it bounds the worst case: there is no scenario where editing the
   option produces stale G-code via this path.

4. **Plater.cpp:16692 keeps `update_scheduled = true` after rename.**
   Lines 16688–16696:
   ```cpp
   else if (boost::starts_with(opt_key, "enable_prime_tower") ||
       boost::starts_with(opt_key, "prime_tower") ||
       boost::starts_with(opt_key, "wipe_tower") ||
       opt_key == "filament_minimal_purge_on_wipe_tower" ||
       opt_key == "filament_minimal_purge_on_chute" ||   // ← renamed here
       opt_key == "single_extruder_multi_material" ||
       opt_key == "prime_volume") {
       update_scheduled = true;
   }
   ```
   Renaming the string literal to `"minimal_chute_flush_length"` keeps it in this exact
   branch, still setting `update_scheduled = true`. The branch is purely string-keyed and
   preset-agnostic, so filament→process scope is irrelevant here.

5. **`on_config_change` is fed `full_config()` — process-preset keys ARE in the diff.**
   All three call sites (Plater.cpp:9404, 9526, 9565) pass
   `wxGetApp().preset_bundle->full_config()`. `full_config()` aggregates machine + process +
   filament presets, and `diff_keys = p->config->diff(config)` (16637) diffs the merged
   config. Both `invalidate_state_by_config_options` and `on_config_change` match on the
   `opt_key` *string* only, with no preset-of-origin check. Therefore moving the key from
   filament scope to process scope changes nothing about how the diff is computed or routed.
   Proposal claim #3/#6 confirmed.

6. **No duplicate / hidden invalidation path.** A repo-wide grep for both the old and new
   key names returns exactly the 8 references in the 7 proposed files — nothing else.
   `PrintObject::invalidate_state_by_config_options` (PrintObject.cpp:1064) does NOT
   reference the chute key (it handles brim/speed/support object-level keys), so there is no
   object-step path that must also be renamed. There is no `GCodeProcessor` /
   preview-cache / vgcode key on this option. The two invalidation sites in the proposal are
   the complete set.

## Findings

- **[MEDIUM] Print.cpp:289 — tier is one step broader than the strict minimum (perf, not
  correctness).** A purge-volume floor only affects G-code emission inside `append_tcr`; in
  principle `psGCodeExport` alone would suffice (it would not need to re-run wipe-tower tool
  ordering, since changing only the min floor does not change tool *order*). The proposal
  keeps it in the `psWipeTower + psSkirtBrim` block, so editing the value triggers a wipe
  tower regen + skirt/brim regen + G-code re-export rather than just a re-export. This is
  intentional and matches every sibling (`prime_volume`, `flush_into_infill`,
  `wipe_tower_max_purge_speed` all use the same tier), so it is the *consistent* and *safe*
  choice — over-invalidation never produces wrong output, only slightly slower reslices.
  **Fix: none required.** If a stricter tier were ever wanted, the key could move to the
  `steps_gcode` set (→ `psGCodeExport` only, Print.cpp:250–253), but this would diverge from
  the sibling purge keys and is NOT recommended. Keep as proposed.

- **[LOW] Plater.cpp:16735–16739 — `update_scheduled` is only a 3D-scene refresh gate; the
  reslice is scheduled unconditionally.** After the `update_scheduled` block:
  ```cpp
  if (update_scheduled)
      update();                              // 3D scene / sidebar refresh
  if (p->main_frame->is_loaded()) {
      this->p->schedule_background_process(); // ← always
      update_title_dirty_status();
      p->schedule_auto_reslice_if_needed();   // ← always
  }
  ```
  `schedule_background_process()` + `schedule_auto_reslice_if_needed()` run regardless of
  `update_scheduled`. So even if the Plater.cpp:16692 rename were MISSED, the background
  reslice (and thus the corrected G-code/preview) would still fire via the unconditional
  path; only the immediate `update()` scene refresh would be skipped. The Plater rename is
  therefore a polish/consistency item, not a correctness gate for re-export. (The actual
  re-slice gating lives in `Print::apply` → `invalidate_state_by_config_options`, i.e.
  Print.cpp — that is the load-bearing site.) No action needed; just clarifying that the
  proposal slightly overstates Plater.cpp:16692's role ("correctly trigger re-slice/
  re-export on edit", claim #6 — true, but the reslice would happen anyway).

## Sources

- `src/libslic3r/Print.cpp:94` — `Print::invalidate_state_by_config_options` signature
- `src/libslic3r/Print.cpp:271–282` — posSlice block (NOT where the key lives)
- `src/libslic3r/Print.cpp:283–370` — `psWipeTower + psSkirtBrim` block; key at line 289
- `src/libslic3r/Print.cpp:402–407` — final `else` → `invalidate_all_steps()` (fail-safe)
- `src/libslic3r/Print.cpp:410–419` — steps dispatch loop
- `src/libslic3r/Print.cpp:426–433` — `Print::invalidate_step` → cascades to `psGCodeExport`
- `src/libslic3r/PrintObject.cpp:1064–1093` — object-level handler (does NOT key on chute)
- `src/libslic3r/GCode.cpp:873–888` — `append_tcr` purge-floor consumer (export-time)
- `src/slic3r/GUI/Plater.cpp:16632–16637` — `on_config_change`, `diff_keys` from full_config
- `src/slic3r/GUI/Plater.cpp:16688–16696` — `update_scheduled` branch; key at 16692
- `src/slic3r/GUI/Plater.cpp:16735–16739` — unconditional `schedule_background_process` /
  `schedule_auto_reslice_if_needed`
- `src/slic3r/GUI/Plater.cpp:9404, 9526, 9565` — `on_config_change(full_config())` call sites
- repo-wide grep: exactly 8 references to old/new key across the 7 proposed files
