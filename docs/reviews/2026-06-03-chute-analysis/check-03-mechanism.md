# Check 03 — Chute-purge CODE MECHANISM verification

Adversarial verification of another assistant's code-mechanism assumptions about
OrcaSlicer's BBL chute purge and the `minimal_chute_flush_length` feature.
All claims checked against actual source. No code was modified.

## Verdict

The mechanism assumptions are **largely correct**, but the assistant's headline
**conclusion is WRONG**. The 100 mm³ floor does NOT prevent chute starvation in
the regime that actually matters: when flush-into-infill absorbs ~all of the
purge, `tcr.purge_volume < EPSILON` and the floor is deliberately **bypassed**,
yielding a chute purge of **exactly 0** (when the feature is OFF). That is the
sticky-poop regime the `minimal_chute_flush_length` feature exists to fix — so
the feature is NOT redundant; it is the only thing that raises that 0 to a
droppable amount. The assistant missed this `< EPSILON → 0` branch.

## Per-assumption (✓/✗ + file:line)

**1. `g_min_purge_volume = 100.f` hardcoded constant, mm³, not a user setting — ✓**
`src/libslic3r/GCode.cpp:92` `static const float g_min_purge_volume = 100.f;`
File-scope `static const`, no config plumbing. Used as a literal mm³ floor at
GCode.cpp:886-888. Sibling constants `g_purge_volume_one_time = 135.f`
(GCode.cpp:93) and `g_max_flush_count = 4` (GCode.cpp:94) corroborate the mm³
interpretation (volume / 135 → flush count). ✓

**2. `append_tcr` purge_volume logic and EPSILON value — ✓ (with a sharper statement)**
`src/libslic3r/GCode.cpp:885-888`:
```cpp
float purge_volume = (tcr.purge_volume < EPSILON)
    ? (apply_chute_min ? std::max(min_chute_purge, g_min_purge_volume) : 0.f)
    : (apply_chute_min ? std::max({tcr.purge_volume, g_min_purge_volume, min_chute_purge})
                       : std::max(tcr.purge_volume, g_min_purge_volume));
```
With the feature OFF (`apply_chute_min == false`):
 - `tcr.purge_volume < EPSILON` → **0.f** (floor bypassed)
 - else → `std::max(tcr.purge_volume, g_min_purge_volume)` → **≥100 mm³**
So a real tool change with ANY non-trivial purge is floored to ≥100; only the
fully-absorbed (`< EPSILON`) case yields 0. ✓
`EPSILON = 1e-4` confirmed at `src/libslic3r/libslic3r.h:52`
(`static constexpr double EPSILON = 1e-4;`) — tiny, as claimed. ✓

**3. `flush_length` = `purge_volume / filament_area` (mm of filament) — ✓**
`GCode.cpp:880` `filament_area = (π/4)·filament_diameter²`.
`GCode.cpp:889` `float purge_length = purge_volume / filament_area;`
`GCode.cpp:963` `config.set_key_value("flush_length", new ConfigOptionFloat(purge_length));`
This `flush_length` (plus `flush_length_1..4` split at GCode.cpp:970-983, summing
to `purge_length`) is fed to the placeholder parser at GCode.cpp:985
(`placeholder_parser_process("change_filament_gcode", …)`). So `flush_length` is
mm of filament feed, derived directly from the floored purge volume. ✓

**4. With `purge_in_prime_tower=0` on BBL, the purge goes to the WASTE CHUTE, not a prime tower — ✓**
BBL `change_filament_gcode`
(`resources/profiles/BBL/machine/fdm_bbl_3dp_001_common.json`, key
`change_filament_gcode`): the macro moves the toolhead to the rear chute/wiping
zone (`G1 X70…`, `G1 Y245`, `G1 Y265`) and only THEN runs the
`; FLUSH_START … G1 E{flush_length_*}… ; FLUSH_END` extrusion moves, followed by
`M106 P1 S255` + `G1 X80/X60 … ; shake to put down garbage` and
`… ; wipe and shake`. The `G1 E` flush is therefore deposited at the chute and
shaken off — it is NOT a prime-tower extrusion. (`fdm_bbl_3dp_002_common.json`
is structurally identical for this purpose.) Confirmed the FLUSH `G1 E` moves
happen at a chute/wipe location. ✓

**4b. flush_into_infill REDUCES `tcr.purge_volume` and the 100 floor is applied AFTER — ✓ (critical)**
This is the load-bearing fact and it is **true**. For BBL (Type1) the chute
purge is computed in `src/libslic3r/Print.cpp` (the `!is_wipe_tower_type2`
branch, line 3273+):
 - `Print.cpp:3329-3330` start volume = flush-matrix entry × `flush_multiplier`.
 - `Print.cpp:3331-3332` `volume_to_purge = …mark_wiping_extrusions(*this, current_filament_id, filament_id, volume_to_purge);`
 - `WipingExtrusions::mark_wiping_extrusions`
   (`src/libslic3r/GCode/ToolOrdering.cpp:1632`) walks object infill/perimeter/
   support entities, subtracts each diverted `fill->total_volume()` from
   `volume_to_wipe` (ToolOrdering.cpp:1704, 1718, 1745, 1756) and **returns the
   remaining volume** (ToolOrdering.cpp:1706/1720 return 0 once fully absorbed;
   1768 returns the leftover). So flush-into-infill directly shrinks what the
   tower/chute gets, and can drive it to 0.
 - `Print.cpp:3336-3337` further subtracts `grab_purge_volume`.
 - `Print.cpp:3339-3340` the reduced `volume_to_purge` is passed to
   `wipe_tower.plan_toolchange(…, volume_to_purge)` → becomes `tcr.purge_volume`
   (carried through `WipeTower::generate_new` → `construct_tcr`/`construct_block_tcr`,
   e.g. WipeTower.cpp:1267/1290, hpp:97).
The 100 mm³ floor is applied **later and separately**, in `append_tcr`
(GCode.cpp:885-888), on this already-reduced `tcr.purge_volume`. The infill
purge is NOT re-added. **Therefore the chute cannot drop below 100 mm³ EXCEPT
in the one case the code special-cases: when infill absorbed essentially all of
it (`tcr.purge_volume < EPSILON`), where the floor is intentionally bypassed and
the chute gets 0.** ✓ (assumption holds; see Flaws for why this defeats the
assistant's conclusion)

**5. `minimal_chute_flush_length` ONLY raises the floor; no-op for tool changes already ≥100 mm³ unless set above ~41.6 mm — ✓**
Config def `src/libslic3r/PrintConfig.cpp:2722-2735`: `coFloat`, label "Minimal
chute flush length", sidetext "mm", default 0 — a global filament **length in
mm**, not a volume. Conversion `min_chute_purge = min_chute_length *
filament_area` at GCode.cpp:881-882. It enters only inside `std::max(...)`
(GCode.cpp:886-887), so for a tool change already at/above 100 mm³ it changes
nothing until `min_chute_purge > 100 mm³`, i.e. `min_chute_length > 100/area`.
For 1.75 mm filament `area ≈ 2.405 mm²` → threshold ≈ **41.6 mm** (the tooltip
itself says "about 40 mm is 100 mm³"). ✓
Gate: `apply_chute_min = is_real_toolchange && min_chute_purge > EPSILON &&
gcodegen.is_BBL_Printer()` (GCode.cpp:883-884), with `is_real_toolchange =
tcr.is_tool_change && tcr.initial_tool != tcr.new_tool` — BBL-only, real
colour-change-only, opt-in (default 0 ⇒ off). ✓

**6. `Print::wipe_tower_type()` returns Type1 for any BBL printer; X1C/P1S use `append_tcr` — ✓**
`src/libslic3r/Print.hpp:1072`:
`WipeTowerType wipe_tower_type() const { return is_BBL_printer() ? WipeTowerType::Type1 : m_config.wipe_tower_type.value; }`
Routing in `WipeTowerIntegration::tool_change`
(`src/libslic3r/GCode.cpp:1517` Type2 → `append_tcr2`; `else`/Type1 branch →
`append_tcr` at GCode.cpp:1556/1566). `prime()` likewise gates Type2 →
`append_tcr2` (GCode.cpp:1501-1504). So any BBL printer is Type1 and runs the
patched `append_tcr`. The generation side matches: BBL takes the
`!is_wipe_tower_type2` branch (Print.cpp:3273) → `wipe_tower.generate_new`
(Print.cpp:3367). ✓

## Flaws found

**FLAW 1 (decisive): the assistant missed the `tcr.purge_volume < EPSILON → 0`
regime — there IS a regime where the chute gets < 100 mm³ (in fact 0).**
The assistant's conclusion ("the chute almost never starves because the 100 mm³
floor already prevents it; the feature only raises the floor and is therefore
redundant/ineffective for prints already at the floor") is code-incorrect.
The floor at GCode.cpp:885-888 is **not** an unconditional `max(x, 100)`. The
`< EPSILON` ternary branch deliberately routes the fully-absorbed case to `0.f`
(feature off) — it does NOT floor it to 100. And per 4b, flush-into-infill can
genuinely drive `tcr.purge_volume` to (near) 0. So the precise behavior with the
feature OFF is: chute purge ∈ {0} ∪ [100, ∞) — a binary "100-or-nothing", never
1–99. The starvation case is real and is exactly `purge_volume == 0`, which the
100 floor by construction does not catch. This is the precise regime
`minimal_chute_flush_length` targets: when ON, that same `< EPSILON` branch
yields `std::max(min_chute_purge, g_min_purge_volume) ≥ 100` instead of 0
(GCode.cpp:886). So the feature is the ONLY mechanism that lifts a fully-absorbed
chute from 0 to a droppable purge — the opposite of redundant.

**FLAW 2 (nuance the assistant glossed): "redundant for prints already at the
floor" conflates two different populations.**
 - Tool changes whose post-infill purge is ≥ ~0 but ≥ EPSILON: already floored
   to ≥100, so `minimal_chute_flush_length ≤ ~41.6 mm` is indeed a no-op there
   (assumption 5 — the assistant is right about THIS subset).
 - Tool changes whose post-infill purge is `< EPSILON` (fully absorbed): NOT at
   the floor — they are at 0. The feature is decisive here.
The assistant generalized the first subset's redundancy to all prints, which is
the error. "Redundant for tool changes already ≥100 mm³" is true; "redundant
overall / chute never starves" is false.

**FLAW 3 (minor, scope): the feature can still help even above EPSILON if set
high.** Because it also enters the non-absorbed `max({...})` branch
(GCode.cpp:887), setting `min_chute_length` above ~41.6 mm raises the floor for
ALL real BBL tool changes, not only the absorbed ones. So it is not strictly
"only meaningful at purge≈0"; it is "no-op below ~41.6 mm AND above EPSILON,
effective at purge≈0 for any value >EPSILON, effective everywhere above ~41.6 mm."

## Sources

- `src/libslic3r/GCode.cpp:92-94` — `g_min_purge_volume`, `g_purge_volume_one_time`, `g_max_flush_count`
- `src/libslic3r/GCode.cpp:880-889` — filament_area, min_chute_purge, apply_chute_min gate, purge_volume floor ternary, purge_length
- `src/libslic3r/GCode.cpp:963, 970-983, 985` — flush_length / flush_length_1..4 / placeholder parser call
- `src/libslic3r/GCode.cpp:1501-1504, 1517, 1542-1568` — Type2→append_tcr2, Type1(else)→append_tcr routing
- `src/libslic3r/libslic3r.h:52` — `EPSILON = 1e-4`
- `src/libslic3r/Print.hpp:1072` — `wipe_tower_type()` Type1 for BBL
- `src/libslic3r/Print.cpp:3273, 3327-3340, 3367` — BBL chute purge build: mark_wiping_extrusions reduction → grab subtraction → plan_toolchange → generate_new
- `src/libslic3r/GCode/ToolOrdering.cpp:1632-1768` — mark_wiping_extrusions subtracts diverted infill/perimeter/support volume, returns remainder
- `src/libslic3r/PrintConfig.cpp:2722-2735` — minimal_chute_flush_length (coFloat, mm, default 0)
- `resources/profiles/BBL/machine/fdm_bbl_3dp_001_common.json` (`change_filament_gcode`) — flush G1 E moves executed at the rear chute zone (Y245-265), "shake to put down garbage" / "wipe and shake"
