# V2 — Verify Defensive Checks (ToolOrdering + PrintConfig)

Scope: `src/libslic3r/GCode/ToolOrdering.cpp` (sp.valid guard, log demotion) and `src/libslic3r/PrintConfig.cpp` (tooltip cleanup).

## Verdict: APPROVE (all three fixes correctly motivated; no regressions found)

---

## Findings

### Critical
None.

### High
None.

### Medium
None.

### Low
- **L1 — Tooltip removal is logically correct but UX-redundant.**
  `src/slic3r/GUI/ConfigManipulation.cpp:917-921` calls `toggle_line("flush_into_infill_min_layer", … && print_sequence != ByObject)` — so in ByObject mode the UI already *hides the row*, and the user never sees the tooltip in that state. The removed line "Has no effect when By object print sequence is active with multiple extruders." was therefore (a) misleading on its terms (engine doesn't gate on print_sequence; see Summary §3) and (b) functionally unreachable to the typical user. Removal is correct; no action needed. Mentioning here only because a reviewer may ask "why bother removing dead text?" — the answer is: it's still wrong text, and clean removal beats leaving a misleading fossil that could re-surface via translation or grep.

### Summary (verifications)

1. **`sp.valid` is the right field.**
   - `src/libslic3r/Slicing.hpp:54` declares `bool valid { false; };`. Default-constructed `SlicingParameters` is invalid by design.
   - `src/libslic3r/Slicing.cpp:228` sets `params.valid = true` only at the very end of `create_from_config()` — i.e. the flag is the canonical "fully populated" sentinel. `raft_layers()` reads `base_raft_layers + interface_raft_layers`, both default-zero, so a stale/uninitialized `SlicingParameters` would silently return `raft_layers() == 0` and `this_layer->id() < 0` would be false — meaning the OLD code would *not* crash but would compute against a phantom zero raft. Failing closed (returning `false`) is strictly safer: it denies an override that was computed against unverified data, sending the volume to the wipe tower instead. Confirmed correct.
   - Reachability: `ToolOrdering.cpp:2` includes `Print.hpp`, which at line 12 includes `Slicing.hpp`. `Print.hpp:419` exposes `const SlicingParameters& slicing_parameters() const` via `PrintObject`. No new include needed; type and accessor are both in scope.

2. **`BOOST_LOG_TRIVIAL(debug)` demotion is idiomatic and release-safe.**
   - Pattern is used 9+ times across `src/libslic3r/GCode/` (e.g. `SeamPlacer.cpp:133,222,628,650,653`, `PostProcessor.cpp:171`, `GCodeProcessor.cpp:5984`). Established convention for low-severity diagnostics.
   - Default release log level is **2 = warning** (`src/libslic3r/Utils.cpp:95-106, 178`). `debug` is severity 5; `set_filter(severity >= warning)` discards it. Pre-release builds (dev/alpha/beta) are clamped to `info` (line 120-124), which still discards `debug`. So this message is invisible in shipping builds *and* nightly builds unless the user manually raises log level via `--loglevel`, which is the desired behavior for diagnostic instrumentation.
   - Message content is preserved (Layer id, raft count, print_z) — still useful when a developer/power user enables debug logging.

3. **No engine-side gating on `print_sequence == ByObject` for `flush_into_infill_min_layer`.**
   - Grepped `src/libslic3r/` for any co-mention of `flush_into_infill` and `print_sequence`: zero hits. The 17+ `PrintSequence::ByObject` references in `Print.cpp` / `GCode.cpp` / `Brim.cpp` all govern wipe-tower placement, brim layout, tool order, and layer iteration — none touch flush-into-infill eligibility.
   - The relationship is **indirect** via wipe tower availability: `Print::has_wipe_tower()` (`Print.cpp:3090`) requires `enable_prime_tower && filament_diameter.size() > 1`. Combined with the UI gate (`ConfigManipulation.cpp:921`) which already hides the option in ByObject mode, the engine never reaches `is_overriddable` for this code path in ByObject + multi-extruder scenarios. So the old tooltip claim conflated UI gating with engine semantics. The removed sentence was genuinely wrong on its face — confirmed.

4. **Tooltip remaining wording is intact and sufficient.**
   Re-read `src/libslic3r/PrintConfig.cpp:6911-6917`. Preserved constraints:
   - Counts from object layer 1 (raft excluded, stripped empties excluded).
   - `0` disables.
   - Bottom-shell cleanliness rationale.
   - Wipe-tower volume growth on gated layers.
   - Object-wide application (not per-region) — sized to max `bottom_shell_layers`.
   All four relevant invariants survive. No behavioral knowledge lost.

---

## Files inspected
- `/Volumes/MacMicroSD/Github/OrcaSlicer-nighty/src/libslic3r/Slicing.hpp` (lines 28-54)
- `/Volumes/MacMicroSD/Github/OrcaSlicer-nighty/src/libslic3r/Slicing.cpp` (line 228)
- `/Volumes/MacMicroSD/Github/OrcaSlicer-nighty/src/libslic3r/Print.hpp` (lines 12, 419)
- `/Volumes/MacMicroSD/Github/OrcaSlicer-nighty/src/libslic3r/Print.cpp` (line 3090, sequence refs)
- `/Volumes/MacMicroSD/Github/OrcaSlicer-nighty/src/libslic3r/Utils.cpp` (lines 95-130, 173-178)
- `/Volumes/MacMicroSD/Github/OrcaSlicer-nighty/src/libslic3r/GCode/ToolOrdering.cpp` (lines 1563-1603)
- `/Volumes/MacMicroSD/Github/OrcaSlicer-nighty/src/libslic3r/PrintConfig.cpp` (lines 6908-6921)
- `/Volumes/MacMicroSD/Github/OrcaSlicer-nighty/src/slic3r/GUI/ConfigManipulation.cpp` (lines 917-921)
