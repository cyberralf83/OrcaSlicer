# Review 05 — Min-chute-flush clamp logic (GCode.cpp)

Scope: core enforcement-logic correctness of the per-filament minimum chute "poop" feature.
Files in scope: `src/libslic3r/GCode.cpp` (clamp ~L873, flush_count guard ~L969).
Out of scope per instructions: anything under `.github/`.

## What the change does

In `WipeTowerIntegration::append_tcr` (the BBL `change_filament_gcode` placeholder builder), the local
`purge_volume` used to be:

```cpp
float purge_volume = tcr.purge_volume < EPSILON ? 0 : std::max(tcr.purge_volume, g_min_purge_volume);
```

It is now:

```cpp
float filament_area = float((M_PI / 4.f) * pow(full_config.filament_diameter.get_at(new_filament_id), 2));
const float min_chute_length   = (float) full_config.filament_minimal_purge_on_chute.get_at(new_filament_id);
const float min_chute_purge    = min_chute_length * filament_area;          // mm -> mm³
const bool  is_real_toolchange = tcr.is_tool_change && tcr.initial_tool != tcr.new_tool;
float purge_volume = (tcr.purge_volume < EPSILON)
    ? ((is_real_toolchange && min_chute_purge > EPSILON) ? min_chute_purge : 0.f)
    : std::max({tcr.purge_volume, g_min_purge_volume, min_chute_purge});
float purge_length = purge_volume / filament_area;
```

and later:

```cpp
int flush_count = std::max(1, std::min(g_max_flush_count, (int) std::round(purge_volume / g_purge_volume_one_time)));
float flush_unit = purge_length / flush_count;
```

Constants (GCode.cpp L92–94): `g_min_purge_volume = 100.f`, `g_purge_volume_one_time = 135.f`,
`g_max_flush_count = 4`. `EPSILON = 1e-4` (libslic3r.h L52).

## Verified-correct behaviour

- **Case semantics.** Three cases are handled correctly:
  - Fully-absorbed real toolchange (`tcr.purge_volume < EPSILON`, `is_real_toolchange`, user min set):
    emits exactly `min_chute_purge`. Correct — this is the feature's purpose.
  - Non-toolchange / finish-layer / priming / same-tool with absorbed purge: stays `0.f`. Correct — no
    spurious poop.
  - Normal nonzero purge: `std::max({tcr.purge_volume, g_min_purge_volume, min_chute_purge})` floors to the
    larger of the existing 100 mm³ floor and the user minimum. Correct and back-compatible (when the option
    is the default 0, `min_chute_purge = 0` and the expression collapses to the original
    `max(purge, 100)`).

- **Unit conversion is dimensionally correct.** `filament_area` is `mm²` (π/4·d²), `min_chute_length` is
  `mm`, product is `mm³`. `filament_area` is computed on the line immediately above its first use
  (`min_chute_purge`), so there is no use-before-init; the prior code computed `filament_area` after the
  `purge_volume` line, and the diff correctly moves the computation up. The `sidetext`/tooltip in
  PrintConfig.cpp consistently document the option as a length in mm.

- **No leak into infill / wipe-tower deposit.** The flush-into-infill / wipe-tower-deposit split is decided
  much earlier, at slice time in `Print.cpp` (`mark_wiping_extrusions` → `volume_to_purge` →
  `plan_toolchange`, Print.cpp ~L3327–3340), and is baked into `tcr.purge_volume` before G-code export. The
  local `purge_volume`/`purge_length` here are consumed *only* by the `change_filament_gcode` placeholders
  (`first_flush_volume`, `second_flush_volume`, `flush_length`, `flush_length_1..4`). Confirmed by grepping
  every use of `purge_volume`/`purge_length` inside the function body (L883–982): none feed wipe-tower
  geometry, extrusion planning, or `e` advance. Flooring here therefore affects only the chute poop. Concern
  in the brief is satisfied.

- **`is_real_toolchange` gate is the right gate.** `construct_tcr` sets `is_tool_change` from its parameter;
  finish-layer TCRs are built with `is_tool_change=false` (WipeTower.cpp L2445, L3210) and priming/finish
  results do not satisfy `initial_tool != new_tool` in the absorbed-purge path. The added
  `initial_tool != new_tool` term additionally guards against a same-tool TCR (e.g. a re-prime on the same
  filament) being padded. Correct.

- **`flush_count = max(1, …)` guard — correct and necessary, and fixes a latent div-by-zero.** With the new
  floor, a small toolchange purge (e.g. user min producing 30–60 mm³) gives `round(purge_volume/135) = 0`,
  so without the guard `flush_count` would be 0 and `flush_unit = purge_length / 0` → `inf`. The
  `for (flush_idx < flush_count)` loop would not run, so the `inf` would not be *written*, but the guard is
  the robust fix and is required for the feature to emit any segment at all below ~67 mm³.

- **No 0-length harm when purge_volume == 0.** In the stay-at-0 case, `purge_length == 0`, the new code
  writes `flush_length_1 = flush_unit = 0/1 = 0` and the trailing loop zeroes `flush_length_2..4`. The old
  code (`flush_count = 0`) zeroed all four directly. Net result is identical (all four = 0): no behavioural
  change, no harmful nonzero 0-length segment introduced.

- **`get_at(new_filament_id)` is bounds-safe.** `ConfigOptionVector::get_at` (Config.hpp L624) clamps out-of-
  range indices to `front()`, identical to every sibling `filament_*.get_at(new_filament_id)` call in this
  block. Default value `{0.}` means a short/empty vector falls back to 0 (disabled). No OOB risk.

## Findings

### [Medium] src/libslic3r/GCode.cpp:885 — 100 mm³ floor silently overrides user minimums below ~42 mm
In the nonzero-purge branch, `std::max({tcr.purge_volume, g_min_purge_volume, min_chute_purge})` always
includes the hard-coded `g_min_purge_volume = 100 mm³`. For a 1.75 mm filament, `filament_area ≈ 2.405 mm²`,
so any user `filament_minimal_purge_on_chute` below ~41.6 mm is silently raised to the 100 mm³ floor when
the underlying toolchange already had *some* purge (`tcr.purge_volume ≥ EPSILON`). The same user value of,
say, 20 mm *is* honoured when the toolchange purge was fully absorbed (the `< EPSILON` branch applies
`min_chute_purge` directly with no 100 mm³ floor).

Why it matters: the option's effective minimum is path-dependent — a user who sets 20 mm sees 20 mm of poop
on fully-absorbed changes but ~42 mm (100 mm³) on partially-absorbed changes. This is an inconsistency
between the two branches and is invisible to the user; it is a correctness/clarity concern rather than a
crash. It is also not documented in the tooltip.

Fix (choose one):
- Document the 100 mm³ behaviour in the tooltip ("values below ~40 mm may be raised to the slicer's
  internal minimum on tool changes that already purge"), or
- Make the two branches consistent, e.g. compute a single
  `float floor = std::max(min_chute_purge, is_real_toolchange ? g_min_purge_volume : 0.f);` and apply it
  uniformly, so the user minimum is treated identically regardless of whether `tcr.purge_volume` was
  fully or partially absorbed. Prefer the documentation route if matching upstream's 100 mm³ floor is
  intentional, since changing the floor alters back-compat behaviour for existing prints.

### [Low] src/libslic3r/GCode.cpp:7926 — parallel WipeTower2 flush_count site not updated (inconsistency + latent div-by-zero remains there)
The WipeTower2 path (`GCodeProcessor`/`process_layer`, L7926) has the original
`int flush_count = std::min(g_max_flush_count, (int)std::round(wipe_volume / g_purge_volume_one_time));`
with no `std::max(1, …)` and `float flush_unit = wipe_length / flush_count;` immediately after (L7927). When
`wipe_volume < 67.5 mm³`, `flush_count == 0` and `flush_unit` is a div-by-zero (the value is unused because
the loop body never runs, so it is harmless today, but it is a latent trap). More importantly: the new
minimum-chute feature is *not* applied on this code path at all, so on a printer/configuration that routes
through WipeTower2 the `filament_minimal_purge_on_chute` option will silently do nothing.

Why it matters: feature coverage gap + pre-existing latent div-by-zero. Whether WipeTower2 is reachable for
BBL chute printers determines severity; for BBL the touched `append_tcr` path is the active one, so this is
Low. Flag so the author confirms the option is meant to be a no-op on the WipeTower2 path, and consider
mirroring the `std::max(1, …)` guard there regardless to remove the latent div-by-zero.

### [Low] src/libslic3r/GCode.cpp:885 / WipeTower.cpp:2705 — merge_tcr sums purge volumes; the minimum is applied once per merged TCR, not per sub-change (acceptable, but worth noting)
`merge_tcr` accumulates `out.purge_volume += second.purge_volume` and sets the merged
`initial_tool = first.initial_tool`, `new_tool = second.new_tool`, `is_tool_change = (first||second)`. So a
layer that merges a finish-layer TCR with a real toolchange yields a single TCR carrying the summed purge
and a real-toolchange gate. The clamp then runs once on the summed volume — the user minimum is enforced
once for the merged change, not double-applied, and not applied per original sub-change. This is the
intended "one poop per emitted change_filament_gcode" behaviour and is correct, but note that if two
*distinct* real toolchanges were ever merged into one TCR (chain a→b→c), only one minimum poop would be
guaranteed for the pair. Given the BBL flow emits one `change_filament_gcode` per merged TCR this matches
the physical reality (one chute ejection), so no change is required; documented here only to close the
"double-application / merge interaction" question from the brief: no double application occurs.

## Verdict

Core enforcement logic is correct: units are dimensionally sound and computed in order, the three-case
split is right, the `is_real_toolchange` gate properly excludes finish/priming/same-tool, the floor does not
leak into infill/wipe-tower assignment (decided earlier in Print.cpp), and the `std::max(1, …)` guard is
both necessary and fixes a latent div-by-zero on the touched path. One Medium clarity/consistency issue (the
hidden 100 mm³ floor making the user minimum path-dependent) and two Low items (un-updated parallel
WipeTower2 site; merge semantics note) are worth addressing or documenting. No Critical or High defects.
