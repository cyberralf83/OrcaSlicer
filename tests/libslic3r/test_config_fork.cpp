// Fork-only config tests (cyberralf83/OrcaSlicer). These live in their own file, not in
// test_config.cpp, so that upstream's test_config.cpp stays byte-identical to upstream and
// scheduled upstream merges never conflict on it.
#include <catch2/catch_all.hpp>

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/GCode/SeamPlacer.hpp"
#include "libslic3r/Preset.hpp"

#include <algorithm>

using namespace Slic3r;

// "Hide seam at part interface" feature (multi-material). The decision of whether a perimeter point
// is buried deeply enough to be preferred as a hidden seam is factored into
// SeamPlacerImpl::seam_point_is_embedded_enough so the slicing path and these tests share exact logic.
SCENARIO("seam_hide_at_interface config registration and defaults", "[Config][Seam]") {
    GIVEN("A default full print config") {
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        THEN("the three new seam-interface keys exist with the documented defaults") {
            REQUIRE(config.has("seam_hide_at_interface"));
            REQUIRE(config.has("seam_interface_depth"));
            REQUIRE(config.has("seam_interface_skip_bottom_layers"));
            REQUIRE(config.opt_bool("seam_hide_at_interface") == false);
            REQUIRE_THAT(config.opt_float("seam_interface_depth"), Catch::Matchers::WithinAbs(2.0, 1e-9));
            REQUIRE(config.opt_int("seam_interface_skip_bottom_layers") == 5);
        }
        // Regression guard: the three keys must be in the Print-preset whitelist, otherwise
        // TabPrintModel's intersect(Preset::print_options(), keys) silently drops the per-object
        // override and presets won't round-trip. The full_print_config() checks above pass even
        // when this list is wrong, so assert membership explicitly.
        THEN("they are whitelisted in Preset::print_options so per-object override and preset save work") {
            const std::vector<std::string>& po = Slic3r::Preset::print_options();
            REQUIRE(std::find(po.begin(), po.end(), std::string("seam_hide_at_interface")) != po.end());
            REQUIRE(std::find(po.begin(), po.end(), std::string("seam_interface_depth")) != po.end());
            REQUIRE(std::find(po.begin(), po.end(), std::string("seam_interface_skip_bottom_layers")) != po.end());
        }
    }
}

TEST_CASE("seam_point_is_embedded_enough decision logic", "[Config][Seam]") {
    using Slic3r::SeamPlacerImpl::seam_point_is_embedded_enough;

    // flow_width chosen so the 0.65*flow_width offset baked into embedded_distance is a round 0.5 mm.
    const float flow_width = 0.5f / 0.65f;

    PrintObjectConfig cfg;
    cfg.seam_hide_at_interface.value          = false;
    cfg.seam_interface_depth.value            = 2.0;
    cfg.seam_interface_skip_bottom_layers.value = 5;

    SECTION("feature off: reduces to exact upstream behavior (deeper than 0.5 mm inside)") {
        // Below the -0.5 threshold => embedded enough.
        REQUIRE(seam_point_is_embedded_enough(cfg, /*layer*/ 0, /*compute*/ true, /*dist*/ -1.0f, flow_width));
        // Shallower than -0.5 => not embedded enough.
        REQUIRE_FALSE(seam_point_is_embedded_enough(cfg, 0, true, -0.2f, flow_width));
        // Bottom-layer skip is ignored when the feature is off (layer 0 still qualifies).
        REQUIRE(seam_point_is_embedded_enough(cfg, 0, true, -2.0f, flow_width));
    }

    SECTION("single-region layers never embed, regardless of feature state") {
        REQUIRE_FALSE(seam_point_is_embedded_enough(cfg, 10, /*compute*/ false, -5.0f, flow_width));
        cfg.seam_hide_at_interface.value = true;
        REQUIRE_FALSE(seam_point_is_embedded_enough(cfg, 10, /*compute*/ false, -5.0f, flow_width));
    }

    SECTION("feature on: skip the first N layers and require the configured burial depth") {
        cfg.seam_hide_at_interface.value = true;
        // The threshold is embedded_distance < -depth + 0.65*flow_width = -2.0 + 0.5 = -1.5.
        // skip=5 means layer indices 0-4 are skipped (normal seam); layer index 5 is the FIRST non-skipped layer.
        // Layer below the skip count => never embedded, even when buried deep.
        REQUIRE_FALSE(seam_point_is_embedded_enough(cfg, 4, true, -5.0f, flow_width));
        // First non-skipped layer (index 5), buried deeper than the threshold => embedded.
        REQUIRE(seam_point_is_embedded_enough(cfg, 5, true, -2.0f, flow_width));
        // At/above skip but not buried deep enough (above the -1.5 threshold) => not embedded.
        REQUIRE_FALSE(seam_point_is_embedded_enough(cfg, 5, true, -1.0f, flow_width));
        // A point that would qualify when the feature is off (-1.0 < -0.5) must NOT qualify when on
        // unless it clears the deeper configured threshold.
        REQUIRE_FALSE(seam_point_is_embedded_enough(cfg, 10, true, -1.0f, flow_width));
        REQUIRE(seam_point_is_embedded_enough(cfg, 10, true, -1.6f, flow_width));
    }
}
