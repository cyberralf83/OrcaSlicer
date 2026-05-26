# R6 — Type Design Review

Scope: type design of `SettingsFactory::Bundle`, the `FREQ_SETTINGS_BUNDLE_FFF["Flush options"]` payload, the `ConfigOption::getBool()` access pattern, the positional-index contract introduced by the fix, and the `flush_into_infill_min_layer` option type itself.

Reviewed:
- `src/slic3r/GUI/GUI_Factories.hpp:32` — `typedef std::map<std::string, std::vector<std::string>> Bundle;`
- `src/slic3r/GUI/GUI_Factories.cpp:56-73, 736-747, 1112-1180`
- `src/libslic3r/Config.hpp:158-298, 1815-1820, 2855-2920`
- `src/libslic3r/PrintConfig.hpp:1004-1006`
- `src/libslic3r/PrintConfig.cpp:6899-6922`

---

## Type: `SettingsFactory::Bundle`

### Invariants Identified

- The map's value (`std::vector<std::string>`) is a *heterogeneous tuple of opaque keys*. Three orthogonal invariants live in this one container with **zero structural support**:
  1. Each string is a valid `t_config_option_key` registered in `PrintConfig` (resolvable via `DynamicPrintConfig::option(key)`).
  2. The *order* of strings within a category vector is meaningful — specifically for `"Flush options"`, indices [0]/[1]/[2] are addressed positionally by `append_menu_items_flush_options()`.
  3. The first three entries of `"Flush options"` must all be `coBool` (because consumers call `option->getBool()` on them); subsequent entries may be any type but must not be reached by the existing positional consumers.
- Implicit: keys must produce options that respond correctly to `set_key_value(..., new ConfigOptionBool(...))` — i.e., the underlying registered type for [0]/[1]/[2] must specifically be `ConfigOptionBool`, not just "any bool-ish thing".
- Implicit: category keys are passed through `L()` and matched back via `_(category.first)` in two unrelated lambdas; renaming a category requires updating multiple sites.

### Ratings

- **Encapsulation**: 2/10
  `Bundle` is a bare `typedef` over `std::map<std::string, std::vector<std::string>>`. There is no class wrapping it, no accessor methods, no way to make any invariant private. Every consumer reaches in with `bundle["Flush options"][2]` and operates on raw strings. The very type bug that triggered the crash (`coInt` smuggled in at position [1]) was *only* possible because the container has no notion of "what kind of option lives at this slot."

- **Invariant Expression**: 1/10
  None of the three invariants above are expressible in the type. `std::vector<std::string>` says "ordered list of strings"; it cannot say "first three must be bool option keys, the remainder may be any type." The positional contract is documented only as a comment at `GUI_Factories.cpp:68-71`, which is exactly the failure mode this fix is trying to recover from. There is also no structural distinction between the two access patterns the bundle supports — *bulk enumeration* (`append_menu_items_add_object_settings` iterates without caring about position) vs. *positional addressing* (`append_menu_items_flush_options` requires fixed indices). The same flat vector serves both.

- **Invariant Usefulness**: 6/10
  The invariants themselves are real and worth enforcing — a misordered `"Flush options"` payload **already caused a runtime throw** that crashes a menu predicate (which on wxWidgets evaluates during the paint/event cycle, where exceptions are particularly bad). The fix's comment correctly identifies the contract. But "first three must be bool" is itself a *symptom* of the design: the bundle is being misused as a positional struct.

- **Invariant Enforcement**: 2/10
  Enforcement is purely social: a code comment plus reviewer vigilance. There is:
  - No `static_assert` (the bundle is initialized at static-init time from string literals; the keys are not even constexpr).
  - No runtime check at startup that `FREQ_SETTINGS_BUNDLE_FFF["Flush options"][0..2]` resolve to `ConfigOptionBool` in `PrintConfig`.
  - No defensive type check at the call sites in `append_menu_items_flush_options` — `option->getBool()` is invoked directly with no `dynamic_cast`, no `type() == coBool` check, and no fallback. The only enforcement is the throw inside `ConfigOption::getBool()` itself (`Config.hpp:274`), which is "enforcement" only in the sense that a crash is feedback.

### Strengths

- The map-of-vectors shape *does* fit the **enumeration** use case in `create_freq_settings_popupmenu` (lines 450-485) and `append_menu_items_add_object_settings` (line 736) cleanly — those sites only care that each category has a list of keys to feed into the settings UI.
- The fix correctly identified the minimum-invasive change (move the offending entry to index [3], document the contract) without breaking the broader bundle iteration.
- The accompanying comment is unusually explicit for this codebase: it names the indices, names the constraint (`ConfigOptionBool`), and tells future contributors where new options must go.

### Concerns

- **C1 (Critical)** — The positional contract is a textbook *primitive-obsession* anti-pattern. Three semantically distinct settings (`flush_into_infill`, `flush_into_objects`, `flush_into_support`) plus a fourth unrelated knob (`flush_into_infill_min_layer`) are jammed into the same `std::vector<std::string>` despite being addressed by hard-coded indices in one consumer and bulk-iterated in another. This is exactly the case where a `struct FlushOptionKeys { std::string into_infill; std::string into_objects; std::string into_support; std::vector<std::string> extras; }` would make the bug impossible.
- **C2 (High)** — `Bundle` has two distinct lifecycles colliding: it is a *menu enumeration source* (cared about by `create_freq_settings_popupmenu` and `append_menu_items_add_object_settings`) **and** a *positional registry* (cared about by `append_menu_items_flush_options`). A type that serves two callers with two different invariant sets, and exposes both through the same `operator[]`, is a type that cannot be used safely.
- **H1** — `getBool()` is a throwing accessor with no non-throwing sibling on the `ConfigOption` base class. Templated `DynamicConfig::opt<T>()` (`Config.hpp:2861`) returns `nullptr` on type mismatch via `dynamic_cast`; that *is* the non-throwing variant, but the call site doesn't use it. The menu predicates at lines 1134, 1151, 1168 each call `option->getBool()` with no guard — if anyone ever swaps the type registration of `flush_into_infill` from `coBool` to `coBoolNullable` (or similar), the same crash recurs. Defensive: `if (auto* b = dynamic_cast<const ConfigOptionBool*>(option)) return b->value; return false;`
- **H2** — The contract comment uses the phrase "first three entries" but the indices are referenced *individually* across three separate `append_menu_check_item` blocks. Future maintainers reading just one of those blocks (say, the support one at line 1156) will see `[2]` with no reminder that 0/1 also exist and that the structure is positional. Consider centralizing the three keys in named constants and grepping them out of the bundle once.
- **M1** — `std::map<std::string, std::vector<std::string>>` is initialized at static-init time, so any `static_assert` on its contents is impossible. The closest enforcement available is a startup check: at first call to `MenuFactory::init`, walk `FREQ_SETTINGS_BUNDLE_FFF["Flush options"]` and `assert(global_config.def()->get(key)->type == coBool)` for `key` at indices 0..2. One-time cost, catches the entire bug class.
- **M2** — The category key `L("Flush options")` is the *only* string that links the data table at line 72 to the consumer at line 1115 (where `flush_options_menu` is built). The consumer uses `_L("Flush into objects' infill")` etc. — different strings from the data table — so the category lookup at line 466 (`if (category_name == _(category.first))`) and the positional use at line 1122 are completely uncorrelated paths into the same data. Renaming `"Flush options"` would break nothing visible at compile time.

### Recommended Improvements

Ordered by cost/benefit. The first one alone would have prevented the original bug.

1. **Defensive accessor at call sites (lowest cost, highest immediate value).** Replace each `option->getBool()` at `GUI_Factories.cpp:1134, 1151, 1168` with:
   ```cpp
   const auto* b = dynamic_cast<const ConfigOptionBool*>(option);
   return b ? b->value : false;
   ```
   And similarly guard `set_key_value(..., new ConfigOptionBool(!option->getBool()))` — if the registered type isn't `coBool`, refuse to flip rather than crash. Zero impact on correctness when types are right; turns the crash into a no-op when they're wrong. Two lines per site, no API change.

2. **Named constants for the positional indices.** Inside `append_menu_items_flush_options`, capture the three keys once at the top:
   ```cpp
   const auto& flush_opts = FREQ_SETTINGS_BUNDLE_FFF["Flush options"];
   const std::string& key_infill   = flush_opts[0];
   const std::string& key_objects  = flush_opts[1];
   const std::string& key_support  = flush_opts[2];
   ```
   The literal `[0]/[1]/[2]` then disappears from the per-menu lambdas, the positional dependency is centralized in one place, and a regression test (or even a `assert(flush_opts.size() >= 3)`) becomes trivial to add. ~5 lines.

3. **Startup self-check.** In `MenuFactory::init` (or a dedicated `assert_freq_settings_invariants()` helper called once), walk the first three entries and verify their registered `ConfigOptionDef::type == coBool`. If not, log and disable the flush options menu instead of throwing inside a paint cycle. Catches the entire "wrong type at positional index" class at *boot*, not at first user interaction.

4. **Promote the three boolean flush keys to a struct (medium cost, eliminates the root cause).** Introduce a small fixed-shape type for positional bundles whose layout matters:
   ```cpp
   struct FlushOptionKeys {
       std::string into_infill;
       std::string into_objects;
       std::string into_support;
       std::vector<std::string> extras;        // free-form for new options
       std::vector<std::string> all() const;   // flatten for bundle iteration
   };
   ```
   Then make the `"Flush options"` entry in the bundle a *projection* of this struct (via `.all()`) so both consumers stay happy. The positional consumer reaches in by name (`.into_infill`), not by index. Compile-time enforcement of "three named bool keys plus optional extras." This is the right shape long-term but is the only suggestion that touches the public bundle type.

5. **(Optional / aspirational) Make `Bundle`'s value type a tagged union or `std::vector<std::variant<std::string, FlushOptionKeys>>` so that positional bundles are structurally distinguishable from enumeration bundles.** This is overkill for the current change set — flagged here only because it's the destination if the project ever grows a *second* category with the same positional pattern. Don't do this yet.

### Side observation: `flush_into_infill_min_layer` type design

Declared at `PrintConfig.hpp:1006` and `PrintConfig.cpp:6908` as `ConfigOptionInt` with `min=0`, `max=5000`, default `0`. The semantic is "disable when 0, otherwise apply from this object-local layer onward."

- **Encapsulation**: 6/10 — `ConfigOptionInt` clamps via the def's `min`/`max` at the UI/parse level but the underlying `int value` is publicly settable. A bad caller can `set_key_value(..., new ConfigOptionInt(-5))` and the runtime won't object until the consumer interprets the value.
- **Invariant expression**: 4/10 — "0 means disabled, >0 means layer threshold" is a *sentinel-value* invariant. It's idiomatic in this codebase but it conflates two different states (enabled-with-threshold-1 vs. disabled) into a single integer. A `std::optional<int>` or a tagged enum `{ Disabled, FromLayer(uint16_t) }` would express this without overloading `0`.
- **Invariant usefulness**: 7/10 — The 0-means-disabled convention matches sibling options (`flush_into_infill` is itself a master toggle that gates this knob), so the sentinel is at least *consistent* with neighbors. The min/max clamps prevent obviously bad values.
- **Invariant enforcement**: 6/10 — `min`/`max` are enforced in the GUI spinner widget and in the def, but `set_key_value` bypasses them. Consumers reading the value must defensively clamp or assume the def has done its job.

Recommendation: leave as `coInt`. `coIntOrPercent` would imply "could be a percentage of something" which doesn't apply here (it's an absolute layer count). A tagged-enum migration would be a much larger surface change than this bug warrants. The pragmatic improvement is to add a one-line getter on the relevant region/object config that returns `std::optional<int>` (`return value > 0 ? std::optional{value} : std::nullopt;`) for consumers that want the "disabled" state to be type-distinct.

---

## Summary (counts)

- **Critical**: 2 (C1 primitive-obsession in `Bundle`; C2 conflicting use-cases for the same type)
- **High**: 2 (H1 throwing accessor with no defensive guard at call sites; H2 distributed positional dependencies)
- **Medium**: 2 (M1 no startup invariant check; M2 category-key string fragility)
- **Low / aspirational**: 1 (variant-typed bundle)
- **Side notes**: 1 (sentinel-0 in `flush_into_infill_min_layer`)

**Top type-design issue**: `SettingsFactory::Bundle` is a single weakly-typed `map<string, vector<string>>` doing two incompatible jobs — bulk enumeration and positional addressing — with no structural way to express the positional contract. The fix correctly identified the symptom but left the type unchanged; the next addition to `"Flush options"` (or to any other bundle that grows a positional consumer) will re-create the same bug class. The cheapest durable mitigation is **R1 (defensive `dynamic_cast` at the three menu-predicate call sites)**, which costs ~6 lines and converts the entire class from "crash" to "graceful no-op" without touching the bundle type or the broader codebase.
