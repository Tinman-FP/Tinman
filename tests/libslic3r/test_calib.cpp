#include <catch2/catch_all.hpp>

#include <algorithm>
#include <sstream>
#include <string>
#include <limits>

#include "libslic3r/calib.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/GCode.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/GCode/AdaptivePAProcessor.hpp"

using namespace Slic3r;

TEST_CASE("Calibration steps stay within the requested range", "[Calib][Regression]")
{
    CHECK(calibration_step_count(20, 200, 10) == 19);
    CHECK(calibration_step_count(25, 106, 20) == 5);
    CHECK(calibration_step_count(270, 230, 5) == 9);
    CHECK(calibration_step_count(0.f, .08f, .002f) == 41);
    CHECK_THROWS(calibration_step_count(0, 1, 0));
    CHECK_THROWS(calibration_step_count(0, 1, -1));
    CHECK_THROWS(calibration_step_count(0, std::numeric_limits<double>::infinity(), 1));
    CHECK_THROWS(calibration_step_count(0, 1, 1e-20));
    for (int layer = 0; layer < 100; ++layer) {
        CHECK(calibration_value_for_layer(25, 106, 20, layer, 100) <= 106);
        CHECK(calibration_value_for_layer(270, 230, 5, layer, 100) >= 230);
    }
    CHECK(calibration_value_for_layer(25, 106, 20, 99, 100) == 105);
    CHECK(calibration_value_for_layer(20, 200, 10, 0, 1) == 20);
    CHECK(calibration_value_for_layer(20, 200, 10, -1, 0) == 20);
    CHECK(calibration_value_for_layer(20, 200, 0, 99, 100) == 200);
}

TEST_CASE("VFA speed changes align with every 25-layer block", "[Calib][Regression]")
{
    for (int bands : {2, 3, 19, 41}) {
        for (int layer = 0; layer < bands * vfa_layers_per_block; ++layer) {
            CAPTURE(bands, layer);
            CHECK(calibration_value_for_layer(20, 20 + (bands - 1) * 10, 10, layer,
                bands * vfa_layers_per_block) == 20 + (layer / vfa_layers_per_block) * 10);
        }
    }
}

TEST_CASE("VFA flow adjustment respects the nozzle and the requested speed grid", "[Calib][Regression]")
{
    for (double nozzle : {.2, .4, .6, .8, 1.0}) {
        const auto plan = plan_vfa_calibration(nozzle, nozzle * 1.125, nozzle / 2., .06, nozzle * .75,
            200, 25, 205, 20, {});
        CAPTURE(nozzle);
        REQUIRE(plan.reachable);
        CHECK(plan.layer_height == nozzle / 2.);
        CHECK(plan.end_speed == 205);
    }
    SECTION("choose the largest suitable height, regardless of input order") {
        const auto plan = plan_vfa_calibration(.4, .45, .2, .08, .3, 8, 25, 205, 20,
            {.08, .16, .12, .08, .4, -1, std::numeric_limits<double>::quiet_NaN()});
        REQUIRE(plan.reachable);
        CHECK(plan.layer_height == .08);
        CHECK(plan.end_speed == 205);
    }
    SECTION("lower speed from a nonzero-origin grid without exceeding flow") {
        const auto plan = plan_vfa_calibration(.4, .45, .2, .08, .3, 5, 25, 205, 20, {.08, .12});
        REQUIRE(plan.reachable);
        CHECK(plan.end_speed == 125);
        CHECK(plan.end_speed * Flow(.45, plan.layer_height, .4).mm3_per_mm() <= 5);
    }
    SECTION("do not claim success when start plus step is already unreachable") {
        const auto plan = plan_vfa_calibration(.4, .45, .2, .08, .3, 1, 100, 200, 20, {.08});
        CHECK_FALSE(plan.reachable);
    }
    SECTION("missing candidates must not override flow limits") {
        const auto plan = plan_vfa_calibration(.4, .45, .2, .08, .3, 8, 25, 205, 20, {});
        REQUIRE(plan.reachable);
        CHECK(plan.end_speed == 85);
    }
    SECTION("large nozzles can exceed even a high volumetric cap") {
        const auto plan = plan_vfa_calibration(1., 1.125, .5, .06, .75, 100, 25, 205, 20, {});
        REQUIRE(plan.reachable);
        CHECK(plan.end_speed == 185);
        CHECK(plan.end_speed * Flow(1.125, plan.layer_height, 1.).mm3_per_mm() <= 100);
    }
    SECTION("default height also respects printer limits") {
        const auto plan = plan_vfa_calibration(.8, .9, .4, .08, .3, 100, 25, 205, 20, {});
        CHECK(plan.layer_height == .3);
    }
    CHECK_THROWS(plan_vfa_calibration(.4, .45, .2, .3, .1, 8, 25, 205, 20, {}));
    CHECK_THROWS(plan_vfa_calibration(.4, .45, .2, .08, .3, -1, 25, 205, 20, {}));
    CHECK_THROWS(plan_vfa_calibration(.4, .1, .2, .08, .3, 8, 25, 205, 20, {}));
}

TEST_CASE("PA line footprint includes line width and optional labels", "[Calib][Regression]")
{
    GCode generator;
    PrintConfig config;
    generator.apply_print_config(config);
    CalibPressureAdvanceLine lines(&generator);
    const BoundingBoxf bed(Vec2d(0, 0), Vec2d(256, 256));
    lines.draw_numbers() = false;
    const auto plain = lines.print_extents(bed);
    lines.draw_numbers() = true;
    const auto labelled = lines.print_extents(bed);
    CHECK(plain.min.x() < 78.);
    CHECK(plain.max.x() > 158.);
    CHECK(labelled.min.x() == plain.min.x());
    CHECK(labelled.max.x() > plain.max.x() + 24.);
    CHECK(labelled.min.y() == 0.);
    CHECK(labelled.max.y() == 256.);
}

TEST_CASE("PA line preview reports thickness independently of Z offset", "[Calib][Regression]")
{
    GCode generator;
    PrintConfig config;
    config.initial_layer_print_height.value = .2;
    config.z_offset.value = .1;
    generator.apply_print_config(config);
    generator.writer().set_extruders({0});
    generator.writer().set_extruder(0);
    CalibPressureAdvanceLine lines(&generator);
    lines.set_speed(100, 20);
    const auto output = lines.generate_test(0, .002, 3);
    const std::string tag = ";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Height);
    std::istringstream stream(output);
    std::string line;
    int heights = 0;
    while (std::getline(stream, line)) {
        if (line.rfind(tag, 0) == 0) {
            CHECK(std::stod(line.substr(tag.size())) == Catch::Approx(.2));
            ++heights;
        }
    }
    CHECK(heights == 2);
}

TEST_CASE("Adaptive PA accepts complete numeric measurements and rejects malformed values", "[Calib][Regression]")
{
    for (const auto* valid : {"", "\n", "0.02,10,1000", " 0.02, 10, 1000\r\n0.03,20,2000\n", "2e-2,1e1,1e3", "0.1,0.05,0.01"}) {
        CAPTURE(valid);
        CHECK(AdaptivePAProcessor::validate_adaptive_pa_model(valid).empty());
    }
    for (const auto* invalid : {"0.02.3,10,1000", "0.02,10.2.3,1000", "0.02,10,1000x", "nan,10,1000", "0.02,inf,1000",
         "0.02,10,1e999", "-0.02,10,1000", "2,10,1000", "0.02,0,1000", "0.02,10,0", "0.02,10", "0.02,10,", "0.02,10,1000,3"}) {
        CAPTURE(invalid);
        CHECK_FALSE(AdaptivePAProcessor::validate_adaptive_pa_model(invalid).empty());
    }
    CHECK_THAT(AdaptivePAProcessor::validate_adaptive_pa_model("0.02,10,1000\n0.03,20,2000oops"),
               Catch::Matchers::ContainsSubstring("Line 2"));
}

TEST_CASE("Invalid enabled Adaptive PA blocks print validation without a warning sink", "[Calib][Regression]")
{
    Model model;
    auto* object = model.add_object("cube", "", make_cube(20, 20, 20));
    object->add_instance();
    object->ensure_on_bed();
    auto config = DynamicPrintConfig::full_print_config();
    config.set_key_value("enable_pressure_advance", new ConfigOptionBools{true});
    config.set_key_value("adaptive_pressure_advance", new ConfigOptionBools{true});
    config.set_key_value("adaptive_pressure_advance_model", new ConfigOptionStrings{"0.02,10,1000oops"});
    Print print;
    print.auto_assign_extruders(object);
    print.apply(model, config);
    CHECK_THAT(print.validate().string, Catch::Matchers::ContainsSubstring("Invalid Adaptive Pressure Advance"));
    StringObjectException warning;
    CHECK_THAT(print.validate(&warning).string, Catch::Matchers::ContainsSubstring("Invalid Adaptive Pressure Advance"));
}

namespace {

// The width-resolution getters are protected; expose them so the resolution can be asserted directly.
struct PaPatternProbe : public CalibPressureAdvancePattern
{
    using CalibPressureAdvancePattern::CalibPressureAdvancePattern;
    using CalibPressureAdvancePattern::line_width;
    using CalibPressureAdvancePattern::line_width_first_layer;
};

} // namespace

TEST_CASE("Zero calibration line width resolves to a positive default", "[Calib][Regression]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        {"line_width", "0"},
        {"initial_layer_line_width", "0"},
    });

    Model model;
    model.add_object("cube", "", make_cube(20, 20, 20))->add_instance();

    Calib_Params params;
    params.mode = CalibMode::Calib_PA_Pattern;

    PaPatternProbe pattern(params, config, /* is_bbl_machine */ true, *model.objects.front(), Vec3d(0, 0, 0));

    REQUIRE(pattern.line_width() > 0.);
    REQUIRE(pattern.line_width_first_layer() > 0.);
}

namespace {

struct EndState { double final_e; double max_e; };

EndState simulate_absolute_e(const std::string &gcode)
{
    double final_e = 0.;
    double max_e   = 0.;

    std::istringstream lines(gcode);
    std::string        line;
    while (std::getline(lines, line)) {
        std::istringstream words(line);
        std::string        op;
        if (!(words >> op))
            continue;
        if (op != "G1" && op != "G0" && op != "G92")
            continue;

        std::string word;
        while (words >> word) {
            if (word.size() >= 2 && word[0] == 'E') {
                final_e = std::stod(word.substr(1));
                max_e   = std::max(max_e, final_e);
                break;
            }
        }
    }

    return {final_e, max_e};
}

} // namespace

TEST_CASE("PA pattern resets the extruder after the final layer in absolute E mode", "[Calib][Regression]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        {"use_relative_e_distances", "0"},
        {"line_width", "0.45"},
        {"initial_layer_line_width", "0.45"},
    });

    Model model;
    model.add_object("cube", "", make_cube(20, 20, 20))->add_instance();

    Calib_Params params;
    params.mode  = CalibMode::Calib_PA_Pattern;
    params.start = 0.;
    params.end   = 0.08;
    params.step  = 0.002;

    CalibPressureAdvancePattern pattern(params, config, /* is_bbl_machine */ false, *model.objects.front(), Vec3d(0, 0, 0));
    const CustomGCode::Info     info = pattern.generate_custom_gcodes(config, /* is_bbl_machine */ false, *model.objects.front(),
                                                                      Vec3d(0, 0, 0));

    std::string gcode;
    for (const CustomGCode::Item &item : info.gcodes)
        gcode += item.extra;

    const EndState state = simulate_absolute_e(gcode);

    REQUIRE(state.max_e > 1.);
    REQUIRE_THAT(state.final_e, Catch::Matchers::WithinAbs(0., 1e-9));
}
