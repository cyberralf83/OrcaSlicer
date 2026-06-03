# Impl Review — Agent 02: Fidelity & Consistency

**Date:** 2026-06-02
**Scope:** Does the implemented diff (`git diff 40fa1e2292..HEAD -- src/`) match the approved design
(`docs/superpowers/specs/2026-06-02-min-chute-flush-process-scope-design.md`) exactly — no drift,
omission, or extra change?

## Verdict

**PASS — full fidelity.** All 8 file edits were applied exactly as specified. The key string
`minimal_chute_flush_length` is character-identical across all 8 sites (def, hpp, Preset, GCode, Print,
Plater, Tab, ConfigManipulation). Zero `filament_minimal_purge_on_chute` references remain in `src/`.
The default is `ConfigOptionFloat(0.)` (byte-identity preserved). `def->category = L("Flush options")`
was added. The tooltip matches the design text. The GCode comment was updated (block 873–876 + line
881). No out-of-scope hunk crept in — the reverted `flush_count = std::max(1, …)` regression was NOT
reintroduced; the diff touches exactly the 8 expected files and nothing else.

## Per-edit checklist

| # | File | Applied? | Matches spec? | Notes |
|---|------|----------|---------------|-------|
| 1 | `PrintConfig.hpp:1456→1458` | YES | YES | `((ConfigOptionFloats, filament_minimal_purge_on_chute))` → `((ConfigOptionFloat, minimal_chute_flush_length))`. Stays in `GCodeConfig` block (type-only change). Two-line ORCA comment added recording whitelist-driven process scope. |
| 2 | `PrintConfig.cpp:2722` (+default ~2738) | YES | YES | `add("minimal_chute_flush_length", coFloat)`; `def->category = L("Flush options")` added; label "Minimal chute flush length", sidetext "mm", `min=0`, `mode=comAdvanced` all kept; `set_default_value(new ConfigOptionFloat(0.))`. Reworded tooltip matches design. |
| 3 | `Preset.cpp` | YES | YES | Removed `"filament_minimal_purge_on_chute"` from `s_Preset_filament_options` (line ~1283); added `"minimal_chute_flush_length"` to `s_Preset_print_options` at line 1128, immediately after `"flush_into_support"` (i.e. next to the `flush_into_*` entries, as specified). |
| 4 | `GCode.cpp:881` (+comment 873–876) | YES | YES | `const float min_chute_length = (float) full_config.minimal_chute_flush_length.value;` (scalar `.value`, no `.get_at`). `filament_area` still from new filament's `filament_diameter`. Gating (`apply_chute_min`) unchanged. Comment updated: "per-filament" → "global … per-filament purge volume … incoming filament's cross-sectional area". |
| 5 | `Print.cpp:289` | YES | YES | String literal renamed in place inside the `psWipeTower + psSkirtBrim` invalidation block. |
| 6 | `Plater.cpp:16692` | YES | YES | String literal renamed in the `on_config_change` / `update_scheduled` list. |
| 7 | `Tab.cpp` | YES | YES | Filament-tab option line (was 4151) removed; its toggle + 2-line comment (was 4360–4362) removed. New `append_single_option_line("minimal_chute_flush_length", "multimaterial_settings_flush_options")` added at line 2687 on the Print tab, after `flush_into_support` and strictly before the `optgroup = page->new_optgroup(L("Advanced")…)` reassignment (line 2688). No toggle added here. |
| 8 | `ConfigManipulation.cpp` (after 882, now 888–889) | YES | YES | Added `toggle_line("minimal_chute_flush_length", is_BBL_Printer);` and `toggle_field("minimal_chute_flush_length", is_BBL_Printer && have_prime_tower);` immediately after the `flush_into_*` toggle loop. Both `is_BBL_Printer` and `have_prime_tower` confirmed in scope. 3-line ORCA comment notes UI-gate vs G-code-emission-gate distinction, as specified. |

## Findings

No CRITICAL, HIGH, or MEDIUM findings. The implementation is a faithful, exact realization of the design.

- **[LOW] (informational, no action)** Design table cites pre-edit line numbers (e.g. hpp:1456,
  cpp:2722, Tab 2686/4151). Post-edit line numbers shifted slightly (hpp:1458, cpp default at ~2738,
  Tab insertion at 2687) due to added comment/lines. This is expected line drift from the edits
  themselves, not content drift; every literal, type, default, category, and comment matches the spec.

### Detailed verification evidence

- **Key-string identity (grep across `src/`):** all 8 occurrences are the exact byte sequence
  `minimal_chute_flush_length` —
  `Print.cpp:289`, `PrintConfig.cpp:2722`, `GCode.cpp:881`, `PrintConfig.hpp:1458`, `Preset.cpp:1128`,
  `Plater.cpp:16692`, `ConfigManipulation.cpp:888` & `:889`, `Tab.cpp:2687`. No variant spellings.
- **Old key fully removed:** `grep -rn "filament_minimal_purge_on_chute" src/` → NONE.
- **Default byte-identity:** `set_default_value(new ConfigOptionFloat(0.))` (was `ConfigOptionFloats { 0. }`).
  Scalar 0 → `apply_chute_min` false → upstream-identical G-code. Confirmed.
- **Category:** `def->category = L("Flush options");` present (review M1 satisfied).
- **Tooltip fidelity:** implemented tooltip carries every design beat — "length in millimetres of
  filament (not a volume)", "single global value", "volume scales with each filament's diameter
  (about 40 mm is 100 mm³ for 1.75 mm filament)", sticks-to-nozzle rationale, "built-in minimum of
  about 40 mm (100 mm³)", "Set to 0 to disable (default)", and the BBL+tower clause "(e.g. Bambu Lab),
  and only when the prime tower is enabled."
- **GCode comment scope:** updated across the full comment block (873–876), not just the old line 881.
  New text: "global minimum chute flush … global filament length (mm) … per-filament purge volume (mm³)
  … using the incoming filament's cross-sectional area."
- **No reverted regression:** `grep "std::max(1" GCode.cpp` returns only unrelated matches
  (bed-mesh probe distance 2993/2994, slow-speed 3267). `flush_count` at line 970 is unchanged
  upstream `std::min(g_max_flush_count, …)`; the `set_extruder` / flush_length_N path (960–975) is
  untouched. No `flush_count = std::max(1, …)` guard.
- **Diff containment:** `git diff --stat` shows exactly the 8 design files and no others; no stray hunk.

## Sources

- Design: `docs/superpowers/specs/2026-06-02-min-chute-flush-process-scope-design.md`
- Diff: `git -C /Volumes/MacMicroSD/Github/OrcaSlicer-nighty diff 40fa1e2292..HEAD -- src/`
- `src/libslic3r/PrintConfig.hpp:1453-1461`
- `src/libslic3r/PrintConfig.cpp:2722-2740`
- `src/libslic3r/Preset.cpp:1125-1128, 1283`
- `src/libslic3r/GCode.cpp:870-885, 960-975`
- `src/libslic3r/Print.cpp:286-296`
- `src/slic3r/GUI/Plater.cpp:16689-16695`
- `src/slic3r/GUI/Tab.cpp:2683-2690, 4149, 4357`
- `src/slic3r/GUI/ConfigManipulation.cpp:880-889`
