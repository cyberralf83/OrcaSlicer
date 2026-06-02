# Review 04 — GUI Consistency & Correctness

**Commit:** e57ed0375a — "Fix interlocking beam density not applying (inverted filter axis)"
**Lens:** GUI consistency & correctness (READ-ONLY)
**Scope:** `src/slic3r/GUI/ConfigManipulation.cpp`, `src/slic3r/GUI/ConfigManipulation.hpp`, `src/slic3r/GUI/Tab.cpp`. The InterlockingGenerator / PrintConfig changes are referenced only where they affect GUI correctness.

## Summary verdict

**Clean from a GUI-consistency standpoint.** The removal of the odd→even snap block is complete and self-contained; the surrounding `update_print_fff_config` function still compiles and flows correctly (brace balance verified = 0 over the function body, lines 186–566). The `xor_bad` warning logic and the `m_beam_density_xor_warned` one-shot flag are correct and reset consistently. Both options remain displayed and gated in Tab.cpp. No dangling variables, no stranded code. Two Low items only (both pre-existing/cosmetic).

---

## Findings

### 1. Snap-block removal is complete — no dangling variables or stranded logic — CLEAN
**File:** `src/slic3r/GUI/ConfigManipulation.cpp:545-565`

The removed block (old snap that set even values and called `apply()`) is gone cleanly. The replacement re-declares `beam_interlocking_on`, `beam_group`, `beam_gap` once (lines 551-553) and uses them only for the `xor_bad` computation (line 554). Verified:
- No leftover references to `cell_M`, `cell_G`, `stride`, `% 2`, `beam_group + 1`, `beam_gap + 1`, or "paired" anywhere in `ConfigManipulation.cpp` (grep clean).
- The old snap block was the only place that called `apply()` for these keys; removing it does not strand any `is_msg_dlg_already_exist` toggling — the warning block correctly brackets its own `ShowModal()` with `is_msg_dlg_already_exist = true/false` (lines 559, 561).
- Brace balance over `update_print_fff_config` (186–566) computes to 0; the function is the last in its run before `apply_null_fff_config` at 568, so no over/under-close leaked into a neighbor.

No fix needed.

### 2. `xor_bad` warning logic is correct and matches the slicer gate — CLEAN
**File:** `src/slic3r/GUI/ConfigManipulation.cpp:554-565`

`bool xor_bad = beam_interlocking_on && (beam_group > 0) != (beam_gap > 0);` is a true logical XOR via comparing two `bool`s — fires exactly when interlocking is on and *exactly one* of group/gap is > 0. This precisely mirrors the slicer's density gate `const bool density_enabled = beam_group_count > 0 && beam_gap > 0;` (`InterlockingGenerator.cpp:335`): the warned state is exactly the "set one, get a silent solid comb" case. The warning text accurately describes the behavior. Non-silent (modal `MessageDialog`, `wxICON_WARNING | wxOK`).

No fix needed.

### 3. `m_beam_density_xor_warned` one-shot reset is consistent — no spurious or stuck warnings — CLEAN
**File:** `src/slic3r/GUI/ConfigManipulation.cpp:555-565`, `ConfigManipulation.hpp:26`

State machine:
- `xor_bad && !warned` → show dialog once, set `warned = true` (one-shot).
- `xor_bad && warned` → neither branch runs; no repeat dialog while the bad state persists. Correct (avoids nagging on every keystroke / every `update()` re-entry).
- `!xor_bad` → `warned = false`, re-arming for the next fresh XOR entry.

The member defaults to `false` (`.hpp:26`) and is only ever touched here, so it cannot warn spuriously at construction and cannot get permanently stuck: any transition out of the bad state (fix the values, or toggle `interlocking_beam` off, which makes `beam_interlocking_on` false → `xor_bad` false) resets it. This is genuinely "non-silent and not suppressible forever" — closing the dialog does not permanently dismiss it; re-entering a bad state after fixing it warns again.

One benign note: the early-return guard at line 193 (`if (is_msg_dlg_already_exist) return;`) means the re-entrant `update()` triggered by the dialog's own `ShowModal()`/KillFocus dance is skipped, so the flag set at line 562 is not clobbered by a nested call. Correct.

No fix needed.

### 4. Tab.cpp display & toggle gating intact — CLEAN
**File:** `src/slic3r/GUI/Tab.cpp:2697-2698`; toggle at `ConfigManipulation.cpp:991-992`

This commit did not modify `Tab.cpp` (confirmed via `--stat`). Both options are still appended:
- `interlocking_beam_group_count` (Tab.cpp:2697)
- `interlocking_beam_gap` (Tab.cpp:2698)

Runtime visibility is gated in `toggle_print_fff_options`: `toggle_line("interlocking_beam_group_count", use_beam_interlocking)` and `toggle_line("interlocking_beam_gap", use_beam_interlocking)` (lines 991-992), where `use_beam_interlocking = config->opt_bool("interlocking_beam")` (line 981). Both options correctly appear only when beam interlocking is enabled, consistent with the sibling interlocking options. No change in this behavior.

No fix needed.

### 5. Spin-control step/min/max now consistent with "odd allowed" — CLEAN (improvement)
**File:** `src/libslic3r/PrintConfig.cpp:4253-4254, 4264-4265`

Both options are `coInt`, `min = 0`, `max = 100`, no custom step (default 1). Previously the GUI snapped odd values to even, which was an inconsistency: the spin step was 1 but the field would auto-jump odd→even on commit. Now that any positive integer is meaningful (counts are plain cell units), step=1 with no snap is internally consistent — the displayed value always equals the stored/used value. The updated tooltips (lines 4249-4252, 4261-4263) no longer mention pairing/snapping, matching the new behavior. This is a net UX-consistency improvement, not a regression.

No fix needed.

### 6. (Low) Changed English tooltip/warning strings will be untranslated until next gettext run
**File:** `src/libslic3r/PrintConfig.cpp:4249-4252, 4261-4263`; `ConfigManipulation.cpp:556-557`

The tooltip strings and the warning text were reworded. The old "snapped up to the next even" msgid is not present in any current `localization/i18n/` .po (grep clean), and the new strings are new msgids, so non-English UIs will show the English source until `scripts/run_gettext.sh` regenerates catalogs. This is expected for any string change and is a no-op for this fork's purpose; flagged only for completeness. No action required for correctness.

### 7. (Low) Warning is per-`ConfigManipulation`-instance, not per-document
**File:** `src/slic3r/GUI/ConfigManipulation.cpp:555-565`

`m_beam_density_xor_warned` lives on the `ConfigManipulation` instance. Print Settings tab, object settings, and the object table each own their own `ConfigManipulation` (callers at Tab.cpp:2900, GUI_ObjectSettings.cpp:406, GUI_ObjectTableSettings.cpp:406), so the same logical bad state could warn once per surface. This is pre-existing design for this whole file (every warning in `update_print_fff_config` shares this instance-scoping) and is mild, arguably desirable (the user sees the warning in whatever panel they edited). Not introduced by this commit; no fix recommended.

---

## Compile / flow confirmation
- `update_print_fff_config` brace balance over lines 186–566 = 0 (verified with awk char scan).
- `is_msg_dlg_already_exist` correctly bracketed around the only modal in the new block (lines 559/561); no `apply()` call needed since the XOR block is warn-only and does not mutate config.
- No references to removed locals remain (grep clean for `cell_M`/`cell_G`/`stride`/`% 2`/`+ 1` in this file).
- Slicer-side gate (`InterlockingGenerator.cpp:335`) matches the warning's stated condition.

**Overall: clean. No Critical/High/Medium issues. Two Low cosmetic/scoping notes, neither requiring a change.**
