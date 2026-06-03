# Silent-Failure Review — min-chute-flush enforcement & invalidation (process-scope rename)

Scope: the ACTUAL committed diff `git diff 40fa1e2292..HEAD -- src/`, which converts the
min-chute-flush option from a per-filament `ConfigOptionFloats filament_minimal_purge_on_chute`
into a global scalar `ConfigOptionFloat minimal_chute_flush_length`, read at GCode.cpp:881 as
`full_config.minimal_chute_flush_length.value`. Verified against live source on branch
`nightly-builds-with-bc`.

## Verdict

PASS — no silent failures introduced. At the default value (0) the emitted purge_volume /
flush_length / flush_count are byte-identical to stock OrcaSlicer; the read accessor returns the
real configured value; invalidation re-slices on edit (psWipeTower+psSkirtBrim); the rename is
complete across every consumer (no live reference to the old key remains, including
GCodeProcessor/preview/profiles). No new divide-by-zero/NaN is reachable, and no
`flush_count=std::max(1,…)` guard was (re-)introduced. The set_extruder path is untouched.

## Findings

None. (No CRITICAL / HIGH / MEDIUM silent-failure findings.)

## Verification detail (per task focus)

### 1. Default byte-identity at value = 0 — CONFIRMED byte-identical

Stock pre-feature form (commit `eef00f7032~1`, GCode.cpp:873):

    float purge_volume = tcr.purge_volume < EPSILON ? 0 : std::max(tcr.purge_volume, g_min_purge_volume);

Live committed form (GCode.cpp:881-889):

    const float min_chute_length   = (float) full_config.minimal_chute_flush_length.value;   // 0 at default
    const float min_chute_purge    = min_chute_length * filament_area;                        // 0
    const bool  is_real_toolchange = tcr.is_tool_change && tcr.initial_tool != tcr.new_tool;
    const bool  apply_chute_min    = is_real_toolchange && min_chute_purge > EPSILON && gcodegen.is_BBL_Printer();
    float purge_volume = (tcr.purge_volume < EPSILON)
        ? (apply_chute_min ? std::max(min_chute_purge, g_min_purge_volume) : 0.f)
        : (apply_chute_min ? std::max({tcr.purge_volume, g_min_purge_volume, min_chute_purge})
                           : std::max(tcr.purge_volume, g_min_purge_volume));

At default 0: `min_chute_purge = 0.f`; `min_chute_purge > EPSILON` is `0.f > 1e-4` = false, so
`apply_chute_min == false` unconditionally (independent of tool-change / BBL state). The ternary
collapses to:
- true branch  -> `0.f`  (identical to stock `0`)
- false branch -> `std::max(tcr.purge_volume, g_min_purge_volume)`  (byte-identical to stock)

Therefore `purge_volume`, and every value derived from it — `purge_length` (889),
`first_flush_volume`/`second_flush_volume` (934-935), `flush_length` (963), `flush_count` (970),
the `flush_length_%d` units (973-983) — are bit-for-bit unchanged from stock at default. The
comment rewrite (873-879) and the rename are non-semantic; nothing in the relocation perturbs
default output. EPSILON = 1e-4 (libslic3r.h:52). g_min_purge_volume = 100.f (GCode.cpp:92).

### 2. Read accessor returns the real value — CONFIRMED

`minimal_chute_flush_length` is declared `ConfigOptionFloat` (PrintConfig.hpp:1458), which is
`ConfigOptionFloat : ConfigOptionSingle<double>` (Config.hpp:764), which exposes a public
`T value;` member (Config.hpp:307). So `.value` reads the actual configured scalar — not a silent
default, not a fallback. Member reachability on `full_config`: `FullPrintConfig` derives from
`(PrintObjectConfig, PrintRegionConfig, PrintConfig)` (hpp:1668-1671); `PrintConfig` derives from
`(MachineEnvelopeConfig, GCodeConfig)` (hpp:1485-1487); the member lives in the GCodeConfig block
(hpp:1458). Chain resolves -> `full_config.minimal_chute_flush_length.value` compiles and reads
the live value. Correct accessor: scalar uses `.value`, not the `.get_at(i)` used by the old
`ConfigOptionFloats` form — the diff updated this correctly (GCode.cpp:881).

### 3. Invalidation re-slices on edit — CONFIRMED

Print.cpp:289 reads `minimal_chute_flush_length` inside the `else if` block that closes at
Print.cpp:368-370 with `steps.emplace_back(psWipeTower); steps.emplace_back(psSkirtBrim);`. So an
edit invalidates the wipe-tower + skirt/brim steps and forces G-code re-export — the floor cannot
go stale. Plater.cpp:16692 is renamed to `minimal_chute_flush_length` and lands in the
`update_scheduled = true` branch (16688-16696), so the GUI schedules an update. Both renames are
correct; neither silently drops the edit.

### 4. Unit/zero hazards — UNCHANGED from stock; no new divide-by-zero / NaN

- mm -> mm³ conversion uses `filament_area` from the incoming filament's diameter (GCode.cpp:880),
  same expression as stock; `purge_length = purge_volume / filament_area` (889) is the stock divisor.
  `filament_area` is non-zero for any physical diameter; this divisor is not newly reachable as zero.
- `flush_count = std::min(g_max_flush_count, (int)std::round(purge_volume / g_purge_volume_one_time))`
  (GCode.cpp:970) is byte-identical to stock (`eef00f7032~1`:956). NO `std::max(1, …)` guard was
  added — the previously-reverted regression is confirmed absent. `flush_unit = purge_length /
  flush_count` (971) therefore retains the exact stock divide-by-zero exposure: `flush_count == 0`
  iff `purge_volume` rounds to 0, i.e. `purge_volume < 67.5`. That requires `apply_chute_min ==
  false` AND `0 < tcr.purge_volume < 67.5` — a pre-existing stock path the floor does not create.
- The chute floor cannot newly trigger that divide: whenever `apply_chute_min == true`,
  `purge_volume >= g_min_purge_volume = 100.f`, and `round(100/135) = 1`, so `flush_count >= 1`.
  The floor strictly raises purge_volume, which can only raise (never lower) flush_count. No new
  NaN/zero path.
- set_extruder path (GCode.cpp:~7929) uses `wipe_volume`/`wipe_length` and contains no chute-min
  logic and no reference to `minimal_chute_flush_length`. It is untouched, matching the
  "revert set_extruder changes" intent (commit 4673720c01).

### 5. GCodeProcessor / preview do not read the old key — CONFIRMED

A repository-wide grep (excluding compiled .po/.mo) finds the old name
`filament_minimal_purge_on_chute` in ZERO live source/profile files. GCode/GCodeProcessor.cpp,
libvgcode/, and resources/profiles/ have no reference to either the old or the new key. The flush
data reaches the change_filament_gcode placeholder parser via `flush_length` / `flush_length_%d`
config keys (set at GCode.cpp:963,976,982) exactly as before; the option key name itself is never
consumed downstream by the processor or preview. No stale-key silent no-op.

## Non-issues (considered and dismissed)

- "Global scalar loses per-filament granularity" — a deliberate design change (process-scope), not
  a silent failure: it compiles, runs, and the single value is faithfully applied per change via
  the incoming filament's area at GCode.cpp:880-882. Out of scope for silent-failure review.
- UI vs G-code BBL gate divergence (`is_BBL_Printer` preset-vendor in ConfigManipulation.cpp:888 vs
  `gcodegen.is_BBL_Printer()` runtime in GCode.cpp:884) is documented as intentional
  (ConfigManipulation.cpp:884-887 comment). At default 0 it is moot (apply_chute_min false either
  way). Not a silent failure.
- Preset whitelist: `minimal_chute_flush_length` is added to `s_Preset_print_options`
  (Preset.cpp:1128) and removed from `s_Preset_filament_options`, so the process preset persists
  the value despite the member living in the GCodeConfig struct block (hpp:1456-1458 comment notes
  membership is whitelist-driven). Persistence path is intact — no silent drop on save/load.
- `tcr.purge_volume < EPSILON` uses EPSILON=1e-4 as a mm³ threshold (same as stock); not changed.

## Sources

- src/libslic3r/GCode.cpp:92-94 (g_min_purge_volume=100, g_purge_volume_one_time=135, g_max_flush_count=4)
- src/libslic3r/GCode.cpp:873-889 (live append_tcr floor expression)
- src/libslic3r/GCode.cpp:963,970-983 (flush_length / flush_count / flush_length_%d emission)
- src/libslic3r/GCode.cpp:~7927-7942 (set_extruder path — untouched, no chute logic)
- src/libslic3r/Print.cpp:283-370 (invalidation else-if block -> psWipeTower+psSkirtBrim; key at :289)
- src/libslic3r/Preset.cpp:1128 (added to s_Preset_print_options); diff removes from filament_options
- src/libslic3r/PrintConfig.cpp:2722 (coFloat def, default ConfigOptionFloat(0.))
- src/libslic3r/PrintConfig.hpp:1458 (ConfigOptionFloat member in GCodeConfig block);
  :1485-1487 PrintConfig<-GCodeConfig; :1668-1671 FullPrintConfig<-PrintConfig
- src/libslic3r/Config.hpp:305-309 (ConfigOptionSingle public T value); :764 ConfigOptionFloat
- src/libslic3r/libslic3r.h:52 (EPSILON = 1e-4)
- src/slic3r/GUI/Plater.cpp:16688-16696 (renamed key -> update_scheduled = true)
- Stock pre-feature baseline: `git show eef00f7032~1:src/libslic3r/GCode.cpp` lines 873, 956
- Base-of-diff baseline: `git show 40fa1e2292:src/libslic3r/{GCode.cpp,PrintConfig.cpp}` (old key default 0)
- Repo-wide grep: no live reference to `filament_minimal_purge_on_chute` in src/ or resources/profiles/

## One-line verdict

PASS — the process-scope rename is a no-op at default (byte-identical purge/flush output vs stock), the `.value` read is real, edits invalidate psWipeTower+psSkirtBrim, the rename is complete with no stale-key reader (incl. GCodeProcessor/preview), and no new divide-by-zero/NaN or `std::max(1,…)` guard regression was introduced.
