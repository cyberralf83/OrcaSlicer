# Min-chute-flush review — consolidated findings (2026-06-02)

10 agents reviewed the uncommitted `filament_minimal_purge_on_chute` feature (8 review + 2 silent-failure). Raw per-agent output in this folder (`review-01..08`, `silent-01..02`). Severity = Critical / High / Medium / Low. Count = how many agents raised it.

## Cross-agent conflict to resolve first
**Which code path emits the BBL chute poop — `append_tcr` (Type1, PATCHED) or `append_tcr2`/`set_extruder` (Type2, NOT patched)?**
- Agents 7, 8, 10: `Print::wipe_tower_type()` (Print.hpp:1072) force-routes **BBL → Type1 → append_tcr**, so the patched path is the one BBL uses → feature works.
- Agent 9 (CRITICAL): default `WipeTowerType::Type2` → `append_tcr2`, which has no purge math → option inert out of the box.
- Resolution: **verify** wipe_tower_type() routing for single- vs multi-nozzle BBL. If BBL→Type1 confirmed, Agent 9's CRITICAL is downgraded for the user's case; the real issue becomes the unpatched second emitter for Type2/multi-toolhead/no-tower.

## Consolidated issues

| # | Severity | Count | Issue | Location | Disposition |
|---|----------|-------|-------|----------|-------------|
| 1 | High | 5 (A5,A6,A8,A9,A10) | Second emitter `set_extruder`/`append_tcr2` not patched → option inert on Type2 / multi-toolhead / no-tower; also latent `flush_count` div-by-zero there | GCode.cpp ~7800-7926 | VERIFY reachability for BBL, then FIX |
| 2 | High | 4 (A2,A5,A8,A10) | mm vs mm³ unit trap — sibling `filament_minimal_purge_on_wipe_tower` is mm³, this is mm, adjacent in GUI → silent ~2.4× misconfig | PrintConfig.cpp:2722-2733 | FIX (clarify label/tooltip) |
| 3 | High/Med | 3 (A1,A5,A10) | 100 mm³ floor applied in nonzero branch but not zero branch → path-dependent effective minimum; sub-~40 mm settings silently shadowed | GCode.cpp:883-885 | FIX (consistent floor + document) |
| 4 | Medium | 1 (A10) | `is_real_toolchange` gate may skip legit changes (construct_block_tcr equal-tool, merged tcr reclassification) | GCode.cpp:882 | VERIFY before fixing |
| 5 | Low | 1 (A8) | No upper bound on value | PrintConfig.cpp | Optional |
| 6 | Low | 1 (A4) | Option sits in "Wipe tower parameters" optgroup | Tab.cpp:4151 | Optional |
| 7 | Low | 1 (A2) | Tooltip embeds "poop" slang | PrintConfig.cpp:2726 | Optional |
| 8 | Low | 1 (A7) | No unit test added | tests/ | Optional |
| 9 | Low | 1 (A6) | Over-invalidation (psWipeTower/psSkirtBrim) — safe | Print.cpp:289 | Won't fix |

## Confirmed clean (high confidence, multiple agents)
- **Compiles / type-correct** (A1): `std::max` init-list valid, casts fine, `filament_area` reorder has no duplicate/use-before-decl, `full_config` reaches the key.
- **Config registration** (A2): coFloats matches hpp↔cpp; `s_Preset_filament_options` drives `normalize()` resize so `get_at(new_filament_id)` is correct.
- **Serialization** (A3): no whitelist gaps; default 0 means profile JSONs need no change.
- **GUI** (A4): `toggle_option` *disables* (greys) rather than hides; BBL-only gate is the correct inverse of the wipe-tower option; consistent with analog.
- **Parity** (A7): all 6 mandatory wiring sites present; all WipeTower2 absences correct-by-design.
- **Backward compat** (A8): default 0 → byte-identical G-code; `new_filament_id==-1` safe; divide-by-zero not reachable (diameter validated ≥1).

## Plan
1. **Verify** the path-routing conflict (#1) and the `is_real_toolchange` concern (#4) — read-only agent.
2. **Fix** the multi-agent issues (#1 second emitter, #2 units, #3 floor consistency) + confirmed-real single-agent issues — one fix agent (sequential edits, no parallel file conflicts).

## Resolution (applied)
- **Conflict #1 — REFUTED for BBL.** `Print::wipe_tower_type()` (Print.hpp:1072) forces every BBL printer (incl. multi-nozzle H2D) to Type1 → `append_tcr` (patched). Agent 9's "inert out of the box" only holds for non-BBL Type2. Feature works for the user's case.
- **#4 (is_real_toolchange) — VERIFIED SAFE, no fix.** Genuine color changes always carry purge_volume>0 so never reach the gated `<EPSILON` branch; `merge_tcr` sums purge; `construct_block_tcr` sets is_tool_change=false.
- **#1 FIXED** — `GCode::set_extruder` (the Type2 / no-tower emitter) now applies the same chute clamp (`m_config.filament_minimal_purge_on_chute`) and gained the `flush_count = std::max(1, …)` divide-by-zero guard (GCode.cpp ~7843, ~7934).
- **#3 FIXED** — `append_tcr` fully-absorbed branch now floors to `std::max(min_chute_purge, g_min_purge_volume)`, so the effective minimum is `max(user, 100 mm³)` in both branches (GCode.cpp:884).
- **#2 FIXED** — tooltip now states the unit is mm of filament (vs the sibling's mm³) and notes the built-in ~40 mm (100 mm³) floor (PrintConfig.cpp:2724-2732).
- **LOW items** (#5 upper bound, #6 optgroup placement, #7 "poop" wording, #8 no unit test, #9 over-invalidation) — left as-is; cosmetic / safe / out of minimal-diff scope.

Status: fixes applied to working tree, **not yet compiled** (no local build tree; build is the CI job).

## External verification (Wave 5 — docs + upstream source; external-01..03)
- **Concept correct, NOT a duplicate** (external-01): docs confirm flush-into-infill requires the prime tower and the chute poop is the residual purge; the small-`flush_length` → no-wipe/no-drop behaviour (BBL `{if flush_length_1 > 45}` WIPE gate) confirms the feature's premise. No existing OrcaSlicer/Bambu setting controls a minimum chute/poop amount — genuine gap filled.
- **Registration parity: no gaps** (external-02): all 5 mandatory touchpoints match upstream `SoftFever/OrcaSlicer@main`; cereal is macro-auto-generated, handle_legacy/3MF/search are generic, `filament_extruder_override_keys`/`filament_options_with_variant` correctly exclude it (sibling does too), and `L()` strings are auto-extracted by `scripts/run_gettext.sh` — no manual .po edits.
- **No mechanism/constant drift** (external-03): the three constants, `append_tcr`/`append_tcr2`/`set_extruder` structure, `Print::wipe_tower_type()` BBL→Type1, and the FLUSH_START/END tags all match upstream verbatim; patch applies cleanly on the next nightly; no conflicting recent commits. Our `max(1, flush_count)` also removes a latent upstream divide-by-zero.
- **Residual (MEDIUM, by design):** mm-vs-mm³ unit difference vs the adjacent "Minimal purge on wipe tower" (mm³). Mitigated by distinct sidetext ("mm" vs "mm³") + explicit tooltip note; **mm was the user's explicit choice**. No code change made.

Overall verdict: feature is set up correctly and merge-safe.
