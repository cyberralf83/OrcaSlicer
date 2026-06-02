# Review 06 — Slicing Invalidation / Cache Correctness

Feature: new per-filament option `filament_minimal_purge_on_chute` (`ConfigOptionFloats`, default 0),
consumed at G-code generation time in `WipeTowerIntegration::append_tcr` (`src/libslic3r/GCode.cpp:880`)
to floor the chute "poop" purge volume.

Lens: does changing the value reliably regenerate the right output, or does any caching/invalidation
gap let the change go stale until an unrelated reslice?

Files in scope this lens:
- `src/libslic3r/Print.cpp:289` (added opt_key to the `psWipeTower`+`psSkirtBrim` invalidation branch)
- `src/slic3r/GUI/Plater.cpp:16692` (added opt_key to the `update_scheduled` branch in `on_config_change`)
- consumption: `src/libslic3r/GCode.cpp:873-885`, `967-969`

---

## Verdict

The invalidation path is **correct and sufficient**. Changing `filament_minimal_purge_on_chute`
will force regeneration of the G-code export (the only place the value is consumed). No stale-output
or manual-reslice bug exists in the invalidation/caching layer.

One genuine **completeness gap** exists that is adjacent to this lens (the value silently has no
effect on one of the two tool-change code paths), reported below as Medium. It is not a staleness
bug — it is the change never applying on that path at all — but it has the same user-visible symptom
("I set the value and nothing changed") so it belongs in this report.

---

## Traced invalidation chain (evidence)

1. `filament_minimal_purge_on_chute` is declared in `GCodeConfig`
   (`src/libslic3r/PrintConfig.hpp:1456`, inside the `PRINT_CONFIG_CLASS_DEFINE(GCodeConfig, …)`
   block opened at line 1303-1304), identically to the analog `filament_minimal_purge_on_wipe_tower`
   at line 1455. `GCodeConfig` is part of `FullPrintConfig`/`PrintConfig`, so `Print::apply` diffs it
   and the key reaches `Print::invalidate_state_by_config_options`. No separate filament allow-list
   gates invalidation.

2. In `invalidate_state_by_config_options` the key lands in the branch at
   `src/libslic3r/Print.cpp:283-368`, which does:
   ```
   steps.emplace_back(psWipeTower);
   steps.emplace_back(psSkirtBrim);
   ```
   This is the *same* branch and *same* line as the analog `filament_minimal_purge_on_wipe_tower`
   (Print.cpp:288, new key at 289) — placement is correct and consistent.

3. `Print::invalidate_step` (Print.cpp:426-433) propagates any non-`psGCodeExport` step to
   `psGCodeExport`:
   ```
   if (step != psGCodeExport)
       invalidated |= Inherited::invalidate_step(psGCodeExport);
   ```
   So invalidating `psWipeTower` *also* invalidates `psGCodeExport`. This is the load-bearing link:
   the chute option is consumed only at export time, and export is invalidated.

4. Consumption is at G-code export: `WipeTowerIntegration::append_tcr` (GCode.cpp:712) is called from
   `tool_change` / `finalize` / `prime` (GCode.cpp:1503, 1555, 1608), which run inside `do_export`
   (the `psGCodeExport` step). `append_tcr` reads `full_config.filament_minimal_purge_on_chute.get_at(...)`
   live (GCode.cpp:880) and bakes it into the `change_filament_gcode` placeholder config every export.
   There is no cached `change_filament_gcode` string keyed independently of config — it is rebuilt each
   export — so re-running `psGCodeExport` is enough to pick up the new value.

5. **Over-invalidation note (benign):** unlike the analog, `filament_minimal_purge_on_chute` is *not*
   consumed by `_make_wipe_tower` / `WipeTower2`. The analog genuinely feeds wipe-tower geometry
   (`src/libslic3r/GCode/WipeTower2.cpp:1343, 2198, 2302`), so it must invalidate `psWipeTower`. The
   chute option only needs `psGCodeExport`. Putting it in the `psWipeTower` branch therefore forces an
   unnecessary wipe-tower (and skirt/brim) regeneration on every change. This is correctness-safe
   (psWipeTower → psGCodeExport covers what's needed) but slightly wasteful. Keeping it co-located with
   the analog is a reasonable trade-off for maintainability; no action required.

6. Caching layers checked: tool ordering and `m_wipe_tower_data` (`_make_wipe_tower`, Print.cpp:2388-2415,
   3220+) compute `tcr.purge_volume` from the flush matrix / prime volume only — the chute minimum is
   layered on top at export and never stored in `tcr`. So there is no stale-cache hazard: the cached
   wipe-tower data is independent of the chute option, and the option is re-applied fresh each export.

## Plater `on_config_change` addition (Plater.cpp:16692)

Correct and consistent with the analog (16691). Note that the actual reslice is triggered by
`schedule_background_process()` at Plater.cpp:16736, which runs whenever the main frame is loaded —
independent of `update_scheduled`. The `update_scheduled` flag drives `update()` (Plater.cpp:16732)
for the 3D scene / preview refresh. So the addition is harmless and consistent, and is not the
load-bearing trigger for reslice; the background process schedule is. Sufficient for the GUI to
trigger regeneration.

---

## Findings

### [Medium] src/libslic3r/GCode.cpp:7926 — chute minimum not applied on the non-wipe-tower toolchange path

`GCode::append_tcr` (the WipeTowerIntegration path, ~line 712) was patched, but the *second*
`change_filament_gcode` builder — the in-place toolchange path in `GCode` (the block around
GCode.cpp:7780-7939, used when a real filament change is emitted without going through the wipe-tower
integration, e.g. purge-to-chute without a prime tower) — computes its purge identically but was **not**
patched:
```
wipe_volume = std::max(0.f, wipe_volume - grab_purge_volume);   // 7829  — no chute floor
...
int flush_count = std::min(g_max_flush_count, (int)std::round(wipe_volume / g_purge_volume_one_time));  // 7926 — no std::max(1, …) floor
```

Why it matters for this lens: on that path, raising `filament_minimal_purge_on_chute` has **no effect at
all** — same observable symptom as a staleness bug ("I changed the value and the poop didn't grow"). It
is not a cache problem; the value is simply never read there. Whether this path is reachable for the
target hardware (BBL chute printers) depends on whether those prints always route tool changes through
the wipe-tower integration. If they ever take the in-place path, the feature is silently inert.

Fix: mirror the `append_tcr` logic in this block — apply `min_chute_purge` to `wipe_volume` for real
tool changes and floor `flush_count` with `std::max(1, …)`. Alternatively, if this path is provably
never used for chute printers, add a comment at GCode.cpp:880 documenting that `append_tcr` is the only
chute-relevant path so a future reader doesn't assume both are covered.

Confidence this is a real gap: high. Confidence it is *reachable* for the target printers: medium —
needs a hardware/tool-ordering check outside this lens.

### [Low] src/libslic3r/Print.cpp:289 — over-invalidation (forces wipe-tower + skirt/brim regen unnecessarily)

As noted in step 5: the chute option only needs `psGCodeExport`, but the chosen branch also invalidates
`psWipeTower` and `psSkirtBrim`, triggering a full wipe-tower regeneration on every change to a value
that does not affect wipe-tower geometry. Correctness-safe but adds avoidable reslice cost. If perf on
config tweaks matters, this key could instead live in the `steps_gcode` set (which emplaces only
`psGCodeExport`, Print.cpp:250-253). Optional; co-location with the analog is defensible.

---

## What was checked and is OK

- Option registered in the correct config class (`GCodeConfig`) → reaches `invalidate_state_by_config_options`.
- Invalidation branch placement identical to the analog; `psWipeTower → psGCodeExport` propagation confirmed.
- No independent `change_filament_gcode` cache; value re-read live each export.
- `m_wipe_tower_data` / tool-ordering caches are independent of the chute option (no stale-cache hazard).
- Plater `on_config_change` addition consistent; reslice trigger (`schedule_background_process`) fires regardless.
- `Preset.cpp:1282` filament-options whitelist updated so the option round-trips through filament presets
  (verified it is present; missing it would have caused a different class of "value not saved" bug).
