# Min-chute-flush process-scope Implementation Plan

> **For agentic workers:** Implement task-by-task. Steps use checkbox (`- [ ]`) syntax. Spec:
> `docs/superpowers/specs/2026-06-02-min-chute-flush-process-scope-design.md`. Review notes:
> `docs/reviews/2026-06-02-min-chute-flush-process-scope/`.

**Goal:** Move the minimum chute-flush option from the filament preset (`filament_minimal_purge_on_chute`,
per-filament `ConfigOptionFloats`) to the process preset (`minimal_chute_flush_length`, global
`ConfigOptionFloat`), shown beside the `flush_into_*` options, BBL-only — with default 0 byte-identical
to upstream.

**Architecture:** Pure config-plumbing + GUI relocation. Two compiling commits: (1) backend
(config def + type + whitelist + G-code read + invalidation + remove filament-tab UI); (2) GUI (add to
Print Settings "Flush options" + BBL/prime-tower toggle in ConfigManipulation). Then a multi-agent diff
review, push, and a self-hosted-runner CI build.

**Tech Stack:** C++17, wxWidgets, OrcaSlicer config system. Build via CI (macOS ARM64 self-hosted
runner); local build is ~1h and not required for authoring.

**Note on testing:** this change is config/GUI plumbing not covered by the Catch2 unit suites; the real
gate is **compile (CI) + the smoke tests in the spec**. Do NOT add a `flush_count=std::max(1,…)` guard
(reverted regression `4673720c01`).

---

### Task 1: Backend — rename, retype, scope move, G-code read, invalidation, remove filament UI

One atomic commit. After it, the option exists as a process scalar, persists, is read by the slicer,
and re-slices on change — with no GUI yet (removed from Filament tab, not yet on Print tab).

**Files:**
- Modify: `src/libslic3r/PrintConfig.hpp:1456`
- Modify: `src/libslic3r/PrintConfig.cpp:2722-2736`
- Modify: `src/libslic3r/Preset.cpp:1127` (add) and `:1282` (remove)
- Modify: `src/libslic3r/GCode.cpp:873-880`
- Modify: `src/libslic3r/Print.cpp:289`
- Modify: `src/slic3r/GUI/Plater.cpp:16692`
- Modify: `src/slic3r/GUI/Tab.cpp:4151` and `:4360-4362`

- [ ] **Step 1.1 — `PrintConfig.hpp:1456`: rename + retype the member.**

Replace:
```cpp
    ((ConfigOptionFloats,              filament_minimal_purge_on_chute))
```
with:
```cpp
    // ORCA: process-scoped (persisted via s_Preset_print_options) despite living in this GCodeConfig
    // block — preset membership is whitelist-driven, not struct-driven. Global scalar chute-flush floor.
    ((ConfigOptionFloat,               minimal_chute_flush_length))
```

- [ ] **Step 1.2 — `PrintConfig.cpp:2722-2736`: rename, `coFloat`, add category, reword tooltip, scalar default.**

Replace the whole block (2722-2736) with:
```cpp
    def = this->add("minimal_chute_flush_length", coFloat);
    def->label = L("Minimal chute flush length");
    def->category = L("Flush options");
    def->tooltip = L("Minimum length of filament purged into the waste chute on a tool change, as a "
                     "length in millimetres of filament (not a volume). This is a single global value; "
                     "the resulting purge volume scales with each filament's diameter (about 40 mm is "
                     "100 mm³ for 1.75 mm filament). When most of the flush is redirected into the "
                     "object's infill, the leftover chute purge can become too small to fall free and "
                     "may stick to the nozzle; raising this guarantees enough filament to drop cleanly. "
                     "A built-in minimum of about 40 mm (100 mm³) already applies, so smaller values "
                     "have little effect. Set to 0 to disable (default). Only effective on printers that "
                     "eject purge through a chute via the change filament G-code (e.g. Bambu Lab), and "
                     "only when the prime tower is enabled.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.));
```

- [ ] **Step 1.3 — `Preset.cpp`: remove from filament whitelist (1282).**

Replace:
```cpp
    "filament_flow_ratio", "filament_density", "filament_adhesiveness_category", "filament_cost", "filament_minimal_purge_on_wipe_tower", "filament_minimal_purge_on_chute",
```
with:
```cpp
    "filament_flow_ratio", "filament_density", "filament_adhesiveness_category", "filament_cost", "filament_minimal_purge_on_wipe_tower",
```

- [ ] **Step 1.4 — `Preset.cpp`: add to print whitelist (after line 1127).**

Replace:
```cpp
    "flush_into_objects",
    "flush_into_support",
    "tree_support_branch_angle",
```
with:
```cpp
    "flush_into_objects",
    "flush_into_support",
    "minimal_chute_flush_length",
    "tree_support_branch_angle",
```

- [ ] **Step 1.5 — `GCode.cpp:873-880`: scalar read + comment.**

Replace:
```cpp
                // ORCA: Enforce a per-filament minimum chute flush ("poop"). The option is a filament
                // length (mm); convert it to the purge volume (mm³) the rest of this block works in.
                // When most of the tool-change flush is diverted into object infill, tcr.purge_volume can
                // fall to ~0, leaving a poop too small to drop free of the nozzle (it sticks). The floor
                // applies only on real colour changes on BBL chute printers (matching the BBL-only UI);
                // every other case keeps the original behaviour so we never emit spurious purge.
                float filament_area = float((M_PI / 4.f) * pow(full_config.filament_diameter.get_at(new_filament_id), 2));
                const float min_chute_length   = (float) full_config.filament_minimal_purge_on_chute.get_at(new_filament_id);
```
with:
```cpp
                // ORCA: Enforce a global minimum chute flush ("poop"). The option is a global filament
                // length (mm); convert it to the per-filament purge volume (mm³) the rest of this block
                // works in, using the incoming filament's cross-sectional area.
                // When most of the tool-change flush is diverted into object infill, tcr.purge_volume can
                // fall to ~0, leaving a poop too small to drop free of the nozzle (it sticks). The floor
                // applies only on real colour changes on BBL chute printers (matching the BBL-only UI);
                // every other case keeps the original behaviour so we never emit spurious purge.
                float filament_area = float((M_PI / 4.f) * pow(full_config.filament_diameter.get_at(new_filament_id), 2));
                const float min_chute_length   = (float) full_config.minimal_chute_flush_length.value;
```

- [ ] **Step 1.6 — `Print.cpp:289`: rename invalidation key.**

Replace `            || opt_key == "filament_minimal_purge_on_chute"`
with `            || opt_key == "minimal_chute_flush_length"`.

- [ ] **Step 1.7 — `Plater.cpp:16692`: rename update-scheduled key.**

Replace `            opt_key == "filament_minimal_purge_on_chute" ||`
with `            opt_key == "minimal_chute_flush_length" ||`.

- [ ] **Step 1.8 — `Tab.cpp:4151`: remove the Filament-tab option line.**

Delete the line:
```cpp
        optgroup->append_single_option_line("filament_minimal_purge_on_chute", "material_multimaterial#multimaterial-wipe-tower-parameters");
```

- [ ] **Step 1.9 — `Tab.cpp:4360-4362`: remove the Filament-tab toggle + comment.**

Replace:
```cpp
            toggle_option(el, !is_BBL_printer);

        // Orca: the chute purge minimum only applies to printers that eject purge through a chute
        // (Bambu Lab), which is the inverse of the wipe-tower minimum above.
        toggle_option("filament_minimal_purge_on_chute", is_BBL_printer);

        bool multitool_ramming = m_config->opt_bool("filament_multitool_ramming", 0);
```
with:
```cpp
            toggle_option(el, !is_BBL_printer);

        bool multitool_ramming = m_config->opt_bool("filament_multitool_ramming", 0);
```

- [ ] **Step 1.10 — Consistency self-check (local, cheap).**

Run: `grep -rn "filament_minimal_purge_on_chute" src/` → Expected: **no matches**.
Run: `grep -rn "minimal_chute_flush_length" src/` → Expected: 6 matches (hpp decl, cpp def, Preset add, GCode read, Print.cpp, Plater.cpp).

- [ ] **Step 1.11 — Commit.**

```bash
git add src/libslic3r/PrintConfig.hpp src/libslic3r/PrintConfig.cpp src/libslic3r/Preset.cpp \
        src/libslic3r/GCode.cpp src/libslic3r/Print.cpp src/slic3r/GUI/Plater.cpp src/slic3r/GUI/Tab.cpp
git commit -m "Move min chute flush to process preset (backend): minimal_chute_flush_length scalar"
```

---

### Task 2: GUI — Print Settings option line + BBL/prime-tower toggle

**Files:**
- Modify: `src/slic3r/GUI/Tab.cpp:2686` (add option line)
- Modify: `src/slic3r/GUI/ConfigManipulation.cpp:882` (add toggles)

- [ ] **Step 2.1 — `Tab.cpp:2686`: add the option line to the "Flush options" optgroup (before line 2687).**

Replace:
```cpp
        optgroup->append_single_option_line("flush_into_support", "multimaterial_settings_flush_options#flush-into-objects-support");
        optgroup = page->new_optgroup(L("Advanced"), L"advanced");
```
with:
```cpp
        optgroup->append_single_option_line("flush_into_support", "multimaterial_settings_flush_options#flush-into-objects-support");
        optgroup->append_single_option_line("minimal_chute_flush_length", "multimaterial_settings_flush_options");
        optgroup = page->new_optgroup(L("Advanced"), L"advanced");
```

- [ ] **Step 2.2 — `ConfigManipulation.cpp:882`: add BBL + prime-tower gating after the flush_into_* loop.**

Replace:
```cpp
    for (auto el : {"flush_into_infill", "flush_into_support", "flush_into_objects"})
        toggle_field(el, have_prime_tower);

    bool have_avoid_crossing_perimeters = config->opt_bool("reduce_crossing_wall");
```
with:
```cpp
    for (auto el : {"flush_into_infill", "flush_into_support", "flush_into_objects"})
        toggle_field(el, have_prime_tower);

    // ORCA: minimum chute flush — BBL-only row. The UI gate is_BBL_Printer is the preset vendor; the
    // G-code emission gate gcodegen.is_BBL_Printer() is the runtime printer (intentionally different).
    // Greys out without a prime tower, like the flush_into_* options above, since the floor is a no-op
    // without the tower.
    toggle_line("minimal_chute_flush_length", is_BBL_Printer);
    toggle_field("minimal_chute_flush_length", is_BBL_Printer && have_prime_tower);

    bool have_avoid_crossing_perimeters = config->opt_bool("reduce_crossing_wall");
```

- [ ] **Step 2.3 — Consistency self-check.**

Run: `grep -rn "minimal_chute_flush_length" src/` → Expected: 9 matches (the 6 from Task 1 + Tab.cpp line + 2 ConfigManipulation toggles).

- [ ] **Step 2.4 — Commit.**

```bash
git add src/slic3r/GUI/Tab.cpp src/slic3r/GUI/ConfigManipulation.cpp
git commit -m "Move min chute flush to process preset (GUI): Print Settings flush options, BBL-only"
```

---

### Task 3: Multi-agent diff review (bug / error / consistency / silent-failure)

- [ ] **Step 3.1** Generate the diff (`git diff <base>..HEAD -- src/`), dispatch a fan-out of review
  agents (bug/error/consistency + silent-failure) against the **actual diff** (not the proposal).
  Save raw reports under `docs/reviews/2026-06-02-min-chute-flush-process-scope/impl-agent-NN-*.md`.
- [ ] **Step 3.2** Categorize Critical/High/Medium/Low; fix consensus issues; verify singletons before
  fixing. Re-run the consistency greps after fixes. Commit any fixes.

---

### Task 4: Push + self-hosted-runner build + iterate

- [ ] **Step 4.1** Push `nightly-builds-with-bc` to `origin` (user-authorized).
- [ ] **Step 4.2** Trigger the build: `gh workflow run build4mac_local.yml -R cyberralf83/OrcaSlicer --ref nightly-builds-with-bc`.
- [ ] **Step 4.3** Monitor the run (runner status + job logs). On compile failure, capture the error,
  dispatch a fix agent / fix directly, commit, push, re-trigger. Iterate until the build is green.
- [ ] **Step 4.4** Report the green build (DMG in the `nightly-mac-arm64` release) and the spec's manual
  smoke checks for the user to run on-device.

## Self-review (against spec)

- **Spec coverage:** all 8 file edits map to Task 1/2 steps (1.1-1.9, 2.1-2.2). ✓
- **Placeholders:** none — every code step shows complete before/after. ✓
- **Type consistency:** member `minimal_chute_flush_length` is `ConfigOptionFloat` (1.1), read as
  `.value` (1.5), default `ConfigOptionFloat(0.)` (1.2); key string identical across Preset/Print/
  Plater/Tab/ConfigManipulation. ✓
- **Out-of-scope guard:** no `flush_count=max(1,…)` guard added. ✓
