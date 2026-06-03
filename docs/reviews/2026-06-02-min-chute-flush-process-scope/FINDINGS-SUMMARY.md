# Findings summary — 10-agent review of the process-scope move

**Date:** 2026-06-02. **Proposal reviewed:** `PROPOSAL.md` (move `filament_minimal_purge_on_chute`
→ `minimal_chute_flush_length`, filament-scope → process-scope, `ConfigOptionFloats` → `ConfigOptionFloat`).

**Agents (raw reports in this folder):**
R1 config/type (`agent-01`), R2 preset persistence (`agent-02`), R3 GUI/Tab (`agent-03`),
R4 GCode enforcement (`agent-04`), R5 rename completeness (`agent-05`), R6 invalidation (`agent-06`),
R7 merge-safety (`agent-07`), R8 holistic (`agent-08`), S1 silent-fail persistence/GUI (`agent-09`),
S2 silent-fail enforcement (`agent-10`).

**Overall:** No agent found a defect in the *core design* (rename, type change, whitelist move,
default byte-identity). All blocking findings are in the **GUI toggle wiring**. The corrected plan is
in `PROPOSAL.md` (revision 2).

---

## CRITICAL

### C1 — `is_BBL_printer` is out of scope in `TabPrint::toggle_options()` → won't compile
**Agents: 7** — R7 (CRITICAL), R8 (HIGH), R1/R2/R3 (MEDIUM), R5/S2 (LOW).
The proposal's change #7 said to add `toggle_line("minimal_chute_flush_length", is_BBL_printer)` in
`TabPrint::toggle_options()` "where is_BBL_printer is already computed (~2835)". It is **not** — that
local is brace-scoped inside `if (m_preset_bundle){…}` (Tab.cpp:2833-2836) and dies at 2836. The
function then delegates to `m_config_manipulation.toggle_print_fff_options()` (2838) and itself calls
`toggle_line` zero times. The literal code would fail to compile.
**Resolution (verified):** put the gate in `ConfigManipulation::toggle_print_fff_options`
(ConfigManipulation.cpp), right after the `flush_into_*` block (line 882), where `is_BBL_Printer`
(used bare at 842/845) and `have_prime_tower` (854) are both in scope. See C2.

---

## HIGH

*(C1 was rated HIGH by R8 and CRITICAL by R7; tracked above as CRITICAL.)*

---

## MEDIUM

### M1 — Missing `def->category = L("Flush options")`
**Agents: 2** — R1, R7. All four `flush_into_*` siblings set
`def->category = L("Flush options")` (PrintConfig.cpp:7015/7024/7039/7047); the proposed def omitted
it, which would leave the option uncategorized in search/override grouping. **Fix: add it.**

### M2 — Gate on `have_prime_tower`, not just BBL (sibling consistency) — VERIFIED
**Agents: 1** — R3 (singleton; verified before fixing). The `flush_into_*` siblings are greyed via
`toggle_field(el, have_prime_tower)` (ConfigManipulation.cpp:881-882). The chute floor is likewise a
no-op without a prime tower (the floor is enforced in `append_tcr`, the wipe-tower path). So the row
should grey out when the tower is off, matching its neighbors. **Verified against live code → fix.**
Combined with C1: `toggle_line(key, is_BBL_Printer)` (BBL-only visibility) +
`toggle_field(key, is_BBL_Printer && have_prime_tower)` (grey when no tower).

### M3 — Tooltip/comment "global" is semantically incomplete
**Agents: 3+** — R8 (MEDIUM), R1/R4 (comment, LOW), R2 (tooltip reword required, LOW). The value is a
global filament **length** (mm), but the enforced purge is a per-filament **volume** (length ×
filament_area of the incoming filament), and the "~40 mm / 100 mm³" floor is itself diameter-specific.
The reword must say "global length, per-filament volume", drop the now-false "adjacent 'Minimal purge
on wipe tower'" clause, and keep "this is a length in mm, not a volume". The `GCode.cpp:873-874`
comment (not just line 880) must change "per-filament" → "global length / per-filament volume". **Fix.**

### M4 — Classification comment for the in-`GCodeConfig` declaration
**Agents: 1** — R7. Keeping the declaration in the `GCodeConfig` macro block is *correct* (whitelists,
not structs, govern persistence — confirmed by R1/R2/S1/S2), but it sits among `filament_*` keys while
being process-scoped. **Fix (cheap): add a one-line comment** at the decl noting it is process-scoped
via `s_Preset_print_options`.

---

## LOW

- **L1 — `GCode.cpp:873-874` comment** also stale (folded into M3). R1, R4, R8.
- **L2 — Insertion precision:** the new `append_single_option_line` must go strictly **before**
  Tab.cpp:2687 (where `optgroup` is reassigned to the "Advanced" group) or it lands in the wrong group.
  R3. Implementation note.
- **L3 — Non-BBL discoverability** undocumented (row hidden on non-BBL by design). R8. Covered by the
  reworded tooltip ("only effective on BBL…").
- **L4 — UI vs emission gate** use different predicates by design: UI `is_BBL_Printer` (vendor) vs
  G-code `gcodegen.is_BBL_Printer()` (runtime). R7. Add a one-line clarifying comment.
- **L5 — ~60 stale `docs/reviews/**` prose references** to the old key after rename. R5. Harmless
  (non-build); leave as historical notes.
- **L6 — Pre-existing `flush_count==0` / `filament_area==0` div-by-zero** (GCode.cpp:970/7929). R4, S2.
  **Out of scope — do NOT fix.** Adding a `flush_count=std::max(1,…)` guard is exactly the regression
  that was reverted in commit 4673720c01. The ≥100 mm³ floor already makes flush_count≥1 when the
  feature fires.

---

## PROCESS NOTES

- **P1 — Land atomically + smoke-test persistence.** S1, R2. The whitelist move only silently drops the
  value if applied *partially* (filament-removal without the print-list add). Apply all edits together;
  after build, verify a save → restart → reload round-trip preserves a non-zero value.

---

## NON-ISSUES (proven safe — do not spend effort here)

- **"Wrong C++ struct → silent default 0":** FALSE. `FullPrintConfig ⊃ PrintConfig ⊃ GCodeConfig`, and
  persistence is whitelist-driven. Keep the decl in `GCodeConfig`. (R1, R2, R4, S1, S2)
- **"Default 0 not byte-identical":** FALSE — proven identical to upstream by expression-collapse and
  raw-file diff. (R4, S2)
- **"Missed `Print.cpp` rename → silent stale G-code":** FALSE — the catch-all `else` calls
  `invalidate_all_steps()`, so a miss fails *safe* (over-invalidation), never stale. (R6, S2)
- **"Need `handle_legacy`":** FALSE — feature never compiled/shipped; zero on-disk references. (R5, R2)
- **"`set_extruder` regression reintroduced":** NOT present — `set_extruder` is byte-identical to
  upstream; no `flush_count=max(1,…)`. (R4, S2)
- **"Stale BBL visibility on printer change":** FALSE — printer change re-runs
  `load_current_preset → update → toggle_options`, recomputing the gate. (R3, S1)
