# R5 — feature-dev:code-explorer (consumers trace)

Agent: feature-dev:code-explorer
Note: this agent had no Write tool — orchestrator persisted findings on its behalf from the returned summary.

## Critical
None directly attributable to the fix.

## High
- **H1: Dangling-reference risk in `append_menu_check_item` lambdas (pre-existing).** The `is_check` callbacks at `GUI_Factories.cpp:1142-1148, 1159-1165, 1167-1173` capture `select_object_config` and `global_config` *by reference*. These are stack-local references inside `append_menu_items_flush_options()`. wxWidgets invokes the check-state lambda when the menu is drawn/rendered — after the function returns. The pattern only works if `PopupMenu` evaluates check-state synchronously before unwinding the caller's stack frame. This is pre-existing and not introduced by the fix, but it is fragile and worth a separate investigation.

## Medium
- **M1: The "Add settings → Flush options" picker via `create_freq_settings_popupmenu` is dead code.** It is commented out at `GUI_Factories.cpp:782`. The live picker path uses `SettingsFactory::get_bundle()` (dynamic, type-agnostic), so the new `flush_into_infill_min_layer` entry only matters to the picker today via the live path, not via the FREQ bundle. Consider removing the dead bundle reference once verified, or restoring the path if it was intentional.

## Low
- **L1: `FREQ_SETTINGS_BUNDLE_SLA` is empty in production**, so there is no SLA equivalent of the positional-index hazard today.

## Consumers map

### `FREQ_SETTINGS_BUNDLE_FFF` callsites

| Index | Key | Sites | Access | Type | After-fix status |
|---|---|---|---|---|---|
| `[0]` | `flush_into_infill` | 6 in `append_menu_items_flush_options` (lines 1122, 1124, 1126, 1130, 1132 — wrap of `getBool()`/`set_key_value(ConfigOptionBool)`) | positional + `getBool()` | `coBool` | ✓ correct |
| `[1]` | `flush_into_objects` | 6 in same function (1139, 1141, 1143, 1147, 1149) | positional + `getBool()` | `coBool` | ✓ correct (was the crash site) |
| `[2]` | `flush_into_support` | 6 in same function (1156, 1158, 1160, 1164, 1166) | positional + `getBool()` | `coBool` | ✓ correct |
| `[3]` | `flush_into_infill_min_layer` | 0 positional readers | (none) | `coInt` | ✓ safe — never indexed |

Non-positional iteration:
- `GUI_Factories.cpp:453` — `create_freq_settings_popupmenu` (range-for, currently dead code, commented out at line 782)
- `GUI_Factories.cpp:736` — `append_menu_item_settings` (range-for stale menu cleanup)

### `FREQ_SETTINGS_BUNDLE_SLA` callsites
- `GUI_Factories.cpp:453` — same dead `create_freq_settings_popupmenu` iteration
- `GUI_Factories.cpp:742` — same `append_menu_item_settings` cleanup

Empty in production → no positional hazard today.

### Other `SettingsFactory::Bundle` instances
- `GUI_ObjectList.hpp:203-204` — `m_freq_settings_fff` and `m_freq_settings_sla` instance fields guarded by `#if 0`. Dead.
- `GUI_Factories.cpp:261` — `SettingsFactory::get_bundle()` builds a fresh Bundle dynamically from a config's actual keys. Used in ObjectList, ObjectSettings, ParamsPanel. No positional indexing.
- `GUI_ObjectList.cpp:922, 932, 3802`, `GUI_ObjectSettings.cpp:102`, `ParamsPanel.cpp:711, 719` — all consume `get_bundle()` by iteration only.

### `flush_into_infill_min_layer` type treatment across all 9 files

| File | How type is used | Correct? |
|---|---|---|
| `PrintConfig.hpp:1006` | `ConfigOptionInt` member in PRINT_OBJECT_CONFIG macro | ✓ |
| `PrintConfig.cpp:6908` | Registered as `coInt`, default `0`, min `0`, max `5000` | ✓ |
| `GUI_Factories.cpp:72` | String key in bundle vector at `[3]`; never `getBool()` | ✓ |
| `Tab.cpp:2655` | `append_single_option_line` — framework reads type from `ConfigDef` | ✓ |
| `ConfigManipulation.cpp:917` | `toggle_line()` visibility only — type-agnostic | ✓ |
| `Preset.cpp:1127` | String key in serialization list | ✓ |
| `Print.cpp:343` | String key comparison in invalidation switch | ✓ |
| `PrintObject.cpp:1421` | String key comparison in invalidation switch | ✓ |
| `ToolOrdering.cpp:1576` | `object.config().flush_into_infill_min_layer.value` — typed access via `PrintObjectConfig` static field (`ConfigOptionInt`) | ✓ |

No consumer calls `getBool()` on `flush_into_infill_min_layer`.

## Crash callchain

The crash is triggered exclusively by right-click context menus — two entry points:

1. **3D canvas right-click on a full object instance (FFF printer):**
   `Plater::priv::on_right_click()` (`Plater.cpp:10630`) → `menus.object_menu()` (`GUI_Factories.cpp:1832`) → `append_menu_items_flush_options()` (`GUI_Factories.cpp:1835`)

2. **ObjectList right-click on an Object item (FFF printer):**
   `GUI_ObjectList::on_context_menu()` (`GUI_ObjectList.cpp:1624`) → `plater->object_menu()` (`Plater.cpp:18552`) → `p->menus.object_menu()` → `append_menu_items_flush_options()`

In both cases the menu is rebuilt on every invocation (not cached). No background, sidebar selection refresh, slicing pipeline, or `ObjectList::update_filament_in_config` path reaches the crash.

## Summary

9 files touch `flush_into_infill_min_layer`. 5 distinct callsites use `FREQ_SETTINGS_BUNDLE_FFF`. All 3 positional index accesses (`[0]`, `[1]`, `[2]`) are confined to one function (`append_menu_items_flush_options`, lines 1089-1179 of `GUI_Factories.cpp`). No other file or function uses positional indexing on the bundle. The fix placing `flush_into_infill_min_layer` at `[3]` is complete; no consumer reads `[3]`. The crash is right-click-only.

Most concerning trace finding outside the immediate fix: dangling-reference risk in the check-state lambdas of `append_menu_check_item` (see H1). Pre-existing, not introduced by the fix; flagged for a separate investigation.
