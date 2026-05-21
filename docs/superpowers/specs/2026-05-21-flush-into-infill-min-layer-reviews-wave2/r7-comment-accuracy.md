# W2-R7 — pr-review-toolkit:comment-analyzer

Agent ID: ae4a7da06130e063b

## Findings

### Critical
- **C1: Tooltip claims "no effect on the wipe tower" but volume IS redirected there.** Gate denies → residual `volume_to_wipe` flows back to wipe tower → per-layer purge volume grows on gated layers. **Fix:** rewrite tooltip — "Volume that would have purged into bottom-layer infill is redirected to the wipe tower instead — expect the wipe tower's per-layer purge volume to grow on gated layers."
  Spec line: 33

### High
- **H1: Decisions table sentinel collides with on-domain value.** "Start at layer N (inclusive)" with `0 = disabled` works only because N is 1-based. Clarify: "0 reserved as 'disabled' sentinel since 1-based layer 0 is meaningless."
  Spec line: 20
- **H2: `BOOST_LOG_TRIVIAL(debug)` wrong severity.** "Shouldn't happen" guard-trip needs `warning` (matches GCode.cpp:2057 idiom). `debug` is filtered in release builds — silent invisibility. **Fix:** use `warning`. (W2-R1 H2 confirmed by W2-R7 H2 — **2 agents.**)

### Medium
- **M1: Layer-numbering arithmetic narration incomplete.** Decisions table row doesn't explain WHY the comparison has `-1`. Append: "Layer::id() is 0-based per Layer.hpp:269; user input is 1-based, hence the `-1` adjustment."
  Spec line: 21
- **M3: "By Object" tooltip overgeneralizes.** Print.cpp:1443 disables wipe tower only when `extruders.size() > 1`. Single-extruder ByObject doesn't trigger disable. **Fix:** "No effect under By Object print sequence when multiple extruders are in use (prime tower disabled there)."
  Spec line: 35

### Low
- **L1: Out-of-scope SF2 C1 misclassified** if tooltip is corrected per C1. Becomes genuinely out-of-scope after fix.
- **L2: Spec's only "Verify" marker** is at test step 5 — flag noted, no action.

### Confirmed accurate
- Multi-region caveat phrasing ✓
- Gate arithmetic trace ✓
- bbs_3mf.cpp generic round-trip ✓
- Pre-existing psSkirtBrim asymmetry distinction ✓
- Label vs key vocabulary mismatch — low impact, no fix
