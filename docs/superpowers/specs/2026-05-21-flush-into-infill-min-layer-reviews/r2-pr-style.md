# Reviewer 2 — pr-review-toolkit:code-reviewer (PR-style rigorous)

Agent ID: aec4b46302cc5cc4d

## Findings

### Critical
- **C1: Gate also blocks `flush_into_objects` perimeter overrides.** `is_overriddable` is called for perimeters when `flush_into_objects=true` (line 1678). Placing the gate before the role check means it silently gates perimeter purging too — but the option name and tooltip promise infill only. **Fix:** branch on role first; apply gate only inside the infill arm.
  File: `src/libslic3r/GCode/ToolOrdering.cpp:1559`

- **C2: Per-entity O(log N) layer lookup hot path.** `m_layer_tools->print_z` is constant across inner loops; hoist the layer lookup. **Fix:** compute object-local idx once per (object, layer_tools), pass down or cache.
  File: `src/libslic3r/GCode/ToolOrdering.cpp:1559`

### High
- **H1: `size_t` subtraction underflow when `raft_layers() > id()`.** The cast-to-int pattern masks the underflow but is fragile if anyone removes the casts. **Fix:** guard explicitly — `if (this_layer->id() < raft_layers) return false;` then unsigned arithmetic.

- **H2: clang-format / brace style mismatch.** Surrounding `is_overriddable` uses early-return style with no compound braces; new block should match. **Fix:** `clang-format -i` after edit; match early-return style.

- **H3: Tooltip / sidetext: ensure `L()` on everything, set `def->mode = comAdvanced`, mirror tooltip phrasing from `flush_into_infill`.** Design already has these but the consolidated design used `def->sidetext = L("layer")` — H3 suggests `L("layers")` for consistency with neighbours (`enforce_support_layers`, `raft_layers`).

- **H4: Insertion position in `s_Preset_print_options`.** Insert AT line 1126 (between `flush_into_infill` and `flush_into_objects`) — not at 1125 (before the trio), to keep the group cohesive.
  File: `src/libslic3r/Preset.cpp:1126`

### Medium
- **M1: `ConfigManipulation.cpp` should also hide in global-config view.** Mirror line 911's `!is_global_config` pattern — new option is per-object so it shouldn't appear at global scope. **Fix:** `toggle_line("flush_into_infill_min_layer", !is_global_config && have_prime_tower && config->opt_bool("flush_into_infill"));`
  File: `src/slic3r/GUI/ConfigManipulation.cpp:911-ish`

- **M2: `GUI_Factories.cpp:68` category contains only `coBool` siblings.** Verify the right-click context menu widget supports `coInt` inline; otherwise drop from this list and rely on Tab page entry only.

- **M3: Invalidation symmetry.** `flush_into_infill=false` + hot-toggling min_layer still invalidates psWipeTower. Acceptable for consistency.

### Low
- **L1: Insertion position alphabetical convention.** No fix needed.
- **L2: No existing profile under `resources/profiles/` references the new key.** All inherit 0 = disabled — desired.
- **L3: `scripts/run_gettext.sh`** must run to refresh `.po` files. Not blocking the C++ patch.

### Not bugs (verified)
- `WipeTower.cpp`/`WipeTower2.cpp` don't read `flush_into_infill` directly — consume marked overrides through WipingExtrusions. Single-gate approach is sound.
- No schema/migration file in `resources/profiles/` needs updating.
- `Preset::print_options()` is key-agnostic for diff/dirty logic — safe to add.
