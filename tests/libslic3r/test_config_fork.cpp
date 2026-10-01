// Fork-only config tests (cyberralf83/OrcaSlicer). These live in their own file, not in
// test_config.cpp, so that upstream's test_config.cpp stays byte-identical to upstream and
// scheduled upstream merges never conflict on it.
#include <catch2/catch_all.hpp>

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/GCode/SeamPlacer.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/GCode/ForkPrimeVolume.hpp"
#include "libslic3r/GCode/WipeTower.hpp"
#include "libslic3r/GCode/WipeTowerEstimate.hpp"

#include <algorithm>
#include <limits>

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

// "BBL prime volume" (FORK(bbl-prime-volume)). Upstream 407c78fb30 made the Type1 tower prime with the
// hidden per-filament filament_prime_volume; the fork restores the visible prime_volume on single-nozzle
// Bambu Lab printers. The real tower (Print::_make_wipe_tower) can only be checked by slicing; these
// cases pin the shared rule and the pre-slice estimate that must agree with it.
TEST_CASE("fork_bbl_prime_volume_applies gate", "[Config][BblPrimeVolume]") {
    const int nil = std::numeric_limits<int>::max();
    CHECK_FALSE(fork_has_multi_nozzle_extruder({}));
    CHECK_FALSE(fork_has_multi_nozzle_extruder({1}));
    CHECK_FALSE(fork_has_multi_nozzle_extruder({1, 1}));
    CHECK_FALSE(fork_has_multi_nozzle_extruder({nil}));
    CHECK(fork_has_multi_nozzle_extruder({1, 6}));

    CHECK(fork_bbl_prime_volume_applies(true, {1}));      // X1C / P1S / P2S / A1
    CHECK(fork_bbl_prime_volume_applies(true, {1, 1}));   // H2D / X2D
    CHECK_FALSE(fork_bbl_prime_volume_applies(true, {1, 6}));  // H2C carousel keeps upstream
    CHECK_FALSE(fork_bbl_prime_volume_applies(false, {1}));    // Qidi and other Type1 keep upstream
}

// Shaped like upstream's tests/libslic3r/test_wipe_tower_estimate.cpp make_config(): apply() builds
// enums as ConfigOptionEnumGeneric, as the GUI does. Rectangle wall (the default is rib) and a 5 mm
// object keep the depth equal to the stacked purge blocks.
static DynamicPrintConfig bbl_prime_volume_config(const char *printer_model)
{
    DynamicPrintConfig config;
    config.apply(FullPrintConfig::defaults());
    config.set_key_value("printer_model", new ConfigOptionString(printer_model));
    config.set_key_value("prime_tower_width", new ConfigOptionFloat(35.));
    config.set_key_value("prime_tower_infill_gap", new ConfigOptionPercent(150.));
    config.set_key_value("initial_layer_print_height", new ConfigOptionFloat(0.21));
    config.set_key_value("prime_volume", new ConfigOptionFloat(60.));
    config.set_key_value("filament_prime_volume", new ConfigOptionFloats({30., 45.}));
    config.set_key_value("filament_adhesiveness_category", new ConfigOptionInts({100, 0}));
    config.set_key_value("prime_tower_brim_width", new ConfigOptionFloat(3.));
    config.set_deserialize_strict("wipe_tower_wall_type", "rectangle");
    config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.4}));
    config.set_deserialize_strict("timelapse_type", "0");
    config.set_key_value("enable_wrapping_detection", new ConfigOptionBool(false));
    config.set_key_value("purge_in_prime_tower", new ConfigOptionBool(false));
    config.set_key_value("single_extruder_multi_material", new ConfigOptionBool(false));
    return config;
}

static double bbl_prime_volume_depth(const ConfigBase &config)
{
    return estimate_wipe_tower_footprint(config, WipeTowerType::Type1, {0, 1}, 0.21, 5.).depth;
}

static double bbl_prime_volume_blocks(float first, float second)
{
    return WipeTower::estimate_tower_blocks_depth({{first, 100}, {second, 0}}, 35.f, 0.21f, 0.4f, 1.5f);
}

TEST_CASE("Type1 estimate sizes single-nozzle Bambu Lab towers from prime_volume", "[Config][BblPrimeVolume]") {
    SECTION("single-nozzle Bambu Lab: prime_volume drives the depth, filament_prime_volume does not") {
        DynamicPrintConfig config = bbl_prime_volume_config("Bambu Lab X1 Carbon");
        CHECK_THAT(bbl_prime_volume_depth(config), Catch::Matchers::WithinAbs(bbl_prime_volume_blocks(60.f, 60.f), 1e-4));
        config.set_key_value("prime_volume", new ConfigOptionFloat(120.));
        const double at_120 = bbl_prime_volume_depth(config);
        CHECK_THAT(at_120, Catch::Matchers::WithinAbs(bbl_prime_volume_blocks(120.f, 120.f), 1e-4));
        config.set_key_value("filament_prime_volume", new ConfigOptionFloats({10., 10.}));
        CHECK_THAT(bbl_prime_volume_depth(config), Catch::Matchers::WithinAbs(at_120, 1e-9));
    }
    SECTION("Saving mode keeps upstream's fixed prime, independent of prime_volume") {
        DynamicPrintConfig config = bbl_prime_volume_config("Bambu Lab X1 Carbon");
        config.set_deserialize_strict("prime_volume_mode", "Saving");
        const double saving = bbl_prime_volume_depth(config);
        config.set_key_value("prime_volume", new ConfigOptionFloat(120.));
        CHECK_THAT(bbl_prime_volume_depth(config), Catch::Matchers::WithinAbs(saving, 1e-9));
    }
    SECTION("H2C carousel keeps upstream: per-filament volumes") {
        DynamicPrintConfig config = bbl_prime_volume_config("Bambu Lab H2C");
        // Nullable option: set it the way presets do, not as a plain ConfigOptionInts.
        config.set_deserialize_strict("extruder_max_nozzle_count", "1,6");
        CHECK_THAT(bbl_prime_volume_depth(config), Catch::Matchers::WithinAbs(bbl_prime_volume_blocks(30.f, 45.f), 1e-4));
    }
    SECTION("non-Bambu Type1 keeps upstream: per-filament volumes") {
        DynamicPrintConfig config = bbl_prime_volume_config("");
        CHECK_THAT(bbl_prime_volume_depth(config), Catch::Matchers::WithinAbs(bbl_prime_volume_blocks(30.f, 45.f), 1e-4));
    }
}

TEST_CASE("prime_volume stays in the print preset whitelist", "[Config][BblPrimeVolume]") {
    const std::vector<std::string> &po = Slic3r::Preset::print_options();
    REQUIRE(std::find(po.begin(), po.end(), std::string("prime_volume")) != po.end());
}
