# Feature 1: Bidirectional Toggle & Skip Layers (IMPLEMENTED)

## Context

The interlocking beam generator alternates beams between two perpendicular directions on successive layers. This feature adds control over that behavior with two settings.

## Settings

### `interlocking_beam_bidirectional`
- **Type**: `ConfigOptionBool`
- **Default**: `true` (current behavior — alternating perpendicular beams)
- **Label**: "Bidirectional beams"
- **Tooltip**: "When enabled, interlocking beams alternate between two perpendicular directions. When disabled, beams are generated in only one direction (the interlocking orientation angle). The alternating layers will have normal walls with no interlocking."

### `interlocking_beam_skip_layers`
- **Type**: `ConfigOptionInt`
- **Default**: `0` (no gaps between cycles)
- **Min**: `0`
- **Label**: "Skip layers between cycles"
- **Tooltip**: "Number of normal layers (without interlocking) to insert between each interlocking cycle. Set to 0 for no gaps between cycles (default behavior)."

## Files Modified (7-file pattern)

| File | Change |
|------|--------|
| `src/libslic3r/PrintConfig.cpp` | Define both settings |
| `src/libslic3r/PrintConfig.hpp` | Declare `ConfigOptionBool` and `ConfigOptionInt` |
| `src/libslic3r/Feature/Interlocking/InterlockingGenerator.hpp` | Add `bidirectional` and `skip_layers` members, constructor params, `isActiveBeamLayer()` declaration |
| `src/libslic3r/Feature/Interlocking/InterlockingGenerator.cpp` | Read settings, pass to constructor, implement `isActiveBeamLayer()`, skip logic in both loops |
| `src/slic3r/GUI/Tab.cpp` | Add both UI lines |
| `src/libslic3r/Preset.cpp` | Add both to preset key list |
| `src/libslic3r/PrintObject.cpp` | Add both to invalidation |

## Implementation Details

### `isActiveBeamLayer()`
Determines whether a given beam layer index should have interlocking beams, based on the skip_layers setting. Uses a cycle of `2 + ceil(skip_layers / beam_layer_count)` groups, where the first 2 positions are active.

### Bidirectional toggle
In `applyMicrostructureToOutlines()`, layers with `layer_type == 1` (the perpendicular direction) are skipped when `bidirectional == false`. This check appears in both the cell iteration loop and the application loop.

## Status

Implemented in commit `18c1eee687`.
