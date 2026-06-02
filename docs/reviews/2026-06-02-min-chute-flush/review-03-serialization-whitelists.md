# Review 03 — Serialization / Preset-Persistence Completeness

**Feature:** new per-filament option `filament_minimal_purge_on_chute` (`ConfigOptionFloats`, mm, default `0`).
**Lens:** catch silent data-loss on save / load / inherit / 3MF-export caused by a key missing from an enumerated filament-option list or whitelist.
**Scope:** `src/` + `resources/` (`.github/` excluded).
**Method:** enumerated every list/whitelist/map that explicitly references filament option keys, using the analog `filament_minimal_purge_on_wipe_tower` as the reference key, then checked whether the chute key is present and/or required in each.

## Verdict

**No issues found in serialization whitelists.** The diff already registers the key in the two lists that actually matter for persistence (`PrintConfig` macro class + `s_Preset_filament_options`), and the key correctly follows the analog's pattern everywhere else: it is intentionally absent from the variant/override/g-code sets, all of which would be *wrong* to add it to. Profile JSONs need no change because the def default is `0` (disabled).

---

## Key architecture facts that make this clean

1. **Filament presets serialize by config keys, not a hard-coded key list.** The 3MF embedded filament preset is written with `config.save_to_json(...)` (`Format/bbs_3mf.cpp:7746`) over the preset's `DynamicPrintConfig`, whose key set comes from `Preset::filament_options()` → `s_Preset_filament_options`. On load, `Preset::normalize_fdm` iterates `Preset::filament_options()` to resize/normalize the vector (`Preset.cpp:466`). Because the diff added the key to `s_Preset_filament_options` (`Preset.cpp:1282`), both the save and the load-side normalization cover it automatically.
2. **The C++ class member is registered** (`PrintConfig.hpp:1456` and `PrintConfig.cpp:2722` `add(...)` with default `ConfigOptionFloats{0.}`), so the option exists in `FullPrintConfig::defaults()` and `config.option(key)` is non-null in every normalization loop.
3. **The analog is NOT in any variant/override/g-code set** — and neither should the chute key be. Adding it to `filament_options_with_variant` or `filament_extruder_override_keys` would change persistence semantics (per-extruder-variant expansion / nullable-nil handling) and diverge from the analog. Correctly absent.

---

## Enumerated locations

| # | Location (file:line) | What it is | Analog present? | Chute present? | Needs chute? | Severity |
|---|----------------------|-----------|:---------------:|:--------------:|:------------:|:--------:|
| 1 | `src/libslic3r/PrintConfig.hpp:1456` | `PRINT_CONFIG_CLASS_DEFINE` member list (the typed config field) | yes (1455) | **yes** | yes | OK |
| 2 | `src/libslic3r/PrintConfig.cpp:2722` | `add("...", coFloats)` option definition + default | yes (2711) | **yes** | yes | OK |
| 3 | `src/libslic3r/Preset.cpp:1282` (`s_Preset_filament_options`, via `Preset::filament_options()` @1447) | **the** filament-preset key whitelist; drives preset save/load, 3MF embedded-preset save, load normalization, EditGCodeDialog, normalize_fdm | yes (1282) | **yes** | yes — **critical path** | OK |
| 4 | `src/libslic3r/Print.cpp:289` (`invalidate_state_by_config_options`) | re-slice invalidation trigger list | yes (288) | **yes** | yes (else edits don't re-slice) | OK |
| 5 | `src/slic3r/GUI/Plater.cpp:16692` (`on_config_change`) | GUI live-update / wipe-tower refresh trigger | yes (16691) | **yes** | yes | OK |
| 6 | `src/slic3r/GUI/Tab.cpp:4151` (`TabFilament::build`) | adds the option line to the Multimaterial page (UI visibility, not persistence) | yes (4150) | **yes** | n/a (UI) | OK |
| 7 | `src/slic3r/GUI/Tab.cpp:4360` (`toggle_options`) | enable/disable toggle (BBL inverse of wipe-tower) | yes (4355) | **yes** | n/a (UI) | OK |
| 8 | `src/libslic3r/PrintConfig.cpp:8367` `filament_options_with_variant` | per-extruder-variant expansion set; consumed by PresetBundle, PrintApply, OrcaSlicer.cpp CLI merge, ParameterUtils, Preset.cpp nil/resize | **no** | no | **no** — analog absent; chute is a plain per-filament `Floats` handled by the generic branch | OK (correct omission) |
| 9 | `src/libslic3r/PrintConfig.cpp:63` `filament_extruder_override_keys` | retract/wipe per-extruder override + nullable-nil set | **no** | no | **no** — analog absent; would change nil semantics | OK (correct omission) |
| 10 | `src/OrcaSlicer.cpp:545` / `src/libslic3r/PresetBundle.cpp:1981` `gcodes_key_set` | g-code-text keys only (start/end/change-filament/layer-change gcode) | n/a (analog absent) | no | **no** — not a g-code key | OK (correct omission) |
| 11 | `src/slic3r/GUI/Plater.cpp:6312-6316` `diff_filament_keys` | one-off special-case appending only `filament_adhesiveness_category` | n/a | no | **no** — unrelated special case | OK (correct omission) |
| 12 | `resources/profiles_template/**` + `resources/profiles/**` filament JSONs (516 analog occurrences) | per-vendor default values; analog written as `"15"` because its def default is 15 | yes | no | **no** — chute def default is `0` (= disabled); omitting yields the intended default | OK (no change needed) |

---

## Generic (non-enumerated) paths verified — no per-key list, no gap

- **3MF write:** `Format/bbs_3mf.cpp:7746` `config.save_to_json` — dumps all preset keys, no whitelist.
- **3MF read:** `Format/bbs_3mf.cpp:2644/2678` `config.load_from_json` — generic; downstream `normalize_fdm` resizes via `Preset::filament_options()` (covered by #3).
- **CLI multi-filament merge:** `OrcaSlicer.cpp:3176/3232` — branches on `filament_options_with_variant`; the chute key (not in that set) falls to the generic `set_at` vector branch (line 3241). Correct.
- **Preset nil/resize:** `Preset.cpp:243-275` `replace_nil_and_resize` — iterates `config.keys()` and dispatches on the variant/override sets; chute key not in any, so it is left as a normal vector option. Correct.
- **PresetBundle default-preset nil init:** `PresetBundle.cpp:348-354` — only nils `filament_extruder_override_keys` that are nullable; chute is neither. Correct.
- **AMF / other Format files:** no per-filament-key enumeration found.

---

## Notes / non-blocking observations

- The key is a `ConfigOptionFloats` (vector, one value per filament) exactly like the analog, so every generic vector-resize / per-filament path treats it identically. No scalar/vector mismatch risk.
- No `.po`/i18n persistence concern — the label/tooltip/sidetext are new translatable strings, not persisted data.
- The only consumer of the value at slice time is `GCode.cpp:get_path_of_change_filament` (in this same diff); that is a *read* path, not a persistence list, and is out of scope for this serialization lens.
