// End-to-end checks of composite-only FibreSeek slicing. The clean-room route planner
// (src/libslic3r/Fibreseek) runs at the end of the infill step, PrintObject::plan_fibreseek_routes()
// trims the plastic fills under the fibre, and GCode::extrude_fiberseek_composite_routes_for_layer()
// writes the routes. The strand values here are throwaway test data. Nothing here talks to a printer.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstddef>
#include <string>

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Fibreseek/WriterAdapter.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/libslic3r.h"
#include "test_helpers.hpp"

using namespace Slic3r;
using Catch::Matchers::ContainsSubstring;

namespace {

// One composite material (a CFC filament with test strand data) printing a 60 x 40 x 4 mm plate,
// with medium reinforcement: fibre on every second layer.
DynamicPrintConfig composite_only_config()
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        { "fiber_enabled", "1" },
        { "fiber_manufacturing_mode", "composite_only" },
        { "fiber_generate_perimeters", "1" },
        { "fiber_generate_infill", "1" },
        { "fiber_reinforcement_mode", "medium" },
        { "fiber_cut_distance", "58" },
        { "fiber_restart_length", "55" },
        { "filament_type", "CFC" },
        { "fiber_feed_factor", "0.8" },
        { "continuous_fiber_diameter", "0.25" },
        { "layer_height", "0.2" },
        { "initial_layer_print_height", "0.2" },
        { "wall_loops", "2" },
        { "skirt_loops", "0" },
        { "brim_type", "no_brim" },
        { "enable_prime_tower", "0" },
    });
    return config;
}

void init_plate(Print &print, Model &model, const DynamicPrintConfig &config)
{
    Test::init_print({ TriangleMesh(its_make_cube(60., 40., 4.)) }, print, model, config);
}

void collect_fill_polylines(const ExtrusionEntity &entity, Polylines &out)
{
    if (const auto *collection = dynamic_cast<const ExtrusionEntityCollection *>(&entity)) {
        for (const ExtrusionEntity *child : collection->entities)
            collect_fill_polylines(*child, out);
    } else if (const auto *path = dynamic_cast<const ExtrusionPath *>(&entity)) {
        out.push_back(path->polyline.to_polyline());
    } else if (const auto *multi = dynamic_cast<const ExtrusionMultiPath *>(&entity)) {
        for (const ExtrusionPath &p : multi->paths)
            out.push_back(p.polyline.to_polyline());
    } else if (const auto *loop = dynamic_cast<const ExtrusionLoop *>(&entity)) {
        for (const ExtrusionPath &p : loop->paths)
            out.push_back(p.polyline.to_polyline());
    }
}

size_t count(const std::string &text, const std::string &needle)
{
    size_t n = 0;
    for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + needle.size()))
        ++n;
    return n;
}

} // namespace

TEST_CASE("Composite-only slicing plans fibre routes and keeps plastic fill out from under them", "[Fibreseek][FibreseekSlicing]")
{
    Print print;
    Model model;
    init_plate(print, model, composite_only_config());
    print.process();

    size_t fibre_layers   = 0;
    double plastic_fill_mm = 0.;
    for (const Layer *layer : print.objects().front()->layers()) {
        for (const LayerRegion *layerm : layer->regions()) {
            const Fibreseek::WriterLayerRoutes &routes = layerm->fibreseek_routes;
            if (routes.routes.empty())
                continue;
            ++fibre_layers;
            Polylines beads;
            for (const Fibreseek::WriterRoute &route : routes.routes) {
                CHECK(route.is_release_safe());
                for (const Fibreseek::WriterRouteSegment &segment : route.planned_segments)
                    beads.push_back(segment.polyline);
            }
            // The fibre bead is 0.8 mm wide; stay 0.05 mm inside its edge so clipping round-off
            // at the boundary doesn't count.
            const ExPolygons under_fibre = union_ex(offset(beads, float(scale_(0.35))));
            Polylines fills;
            for (const ExtrusionEntity *entity : layerm->fills.entities)
                collect_fill_polylines(*entity, fills);
            for (const Polyline &fill : fills)
                plastic_fill_mm += unscaled(fill.length());
            double overlap_mm = 0.;
            for (const Polyline &piece : intersection_pl(fills, under_fibre))
                overlap_mm += unscaled(piece.length());
            CHECK(overlap_mm < 0.1);
        }
    }
    // 20 layers, the first and the last fibre-free, fibre every second layer.
    CHECK(fibre_layers >= 8);
    CHECK(plastic_fill_mm > 0.); // plastic still fills what the fibre leaves
}

TEST_CASE("Fibre G-code cuts at the profile's cut distance and prints that same value", "[Fibreseek][FibreseekSlicing]")
{
    Print print;
    Model model;
    init_plate(print, model, composite_only_config());
    const std::string gcode = Test::gcode(print);

    const size_t routes = count(gcode, "; ORCA_CODEX_FIBER_ROUTE_START");
    REQUIRE(routes > 0);
    CHECK(count(gcode, "\nM1001 L") == routes);
    CHECK(count(gcode, "\n;CUT DISTANCE 58.000\n") == routes);
    CHECK(count(gcode, "CUT DISTANCE") == routes);
    CHECK(count(gcode, "G1 F1500 U55.000 ; Extrude restart") == routes);
}

TEST_CASE("Fibre slicing stops with the setting's name when a cut or restart value is missing", "[Fibreseek][FibreseekSlicing]")
{
    DynamicPrintConfig config = composite_only_config();
    Print              print;
    Model              model;

    SECTION("no cut distance") {
        config.set_deserialize_strict("fiber_cut_distance", "0");
        init_plate(print, model, config);
        REQUIRE_THROWS_WITH(print.process(), ContainsSubstring("Fiber cut distance"));
    }
    SECTION("no restart length") {
        config.set_deserialize_strict("fiber_restart_length", "0");
        init_plate(print, model, config);
        REQUIRE_THROWS_WITH(Test::gcode(print), ContainsSubstring("Fiber restart length"));
    }
}

TEST_CASE("An old CUT DISTANCE line in the cut G-code is dropped with a warning", "[Fibreseek][FibreseekSlicing]")
{
    // Older generated Seeker 3 profiles carry ";CUT DISTANCE 54.8" in Fiber cut G-code. Those profiles
    // keep slicing: the line is dropped, the one marker per route shows Fiber cut distance, and the
    // export warns so the profile can be tidied up.
    DynamicPrintConfig config = composite_only_config();
    config.set_deserialize_strict("fiber_cut_gcode", "M2800\nM400\n;CUT DISTANCE 54.8");
    Print print;
    Model model;
    init_plate(print, model, config);
    const std::string gcode = Test::gcode(print);

    const size_t routes = count(gcode, "; ORCA_CODEX_FIBER_ROUTE_START");
    REQUIRE(routes > 0);
    CHECK(count(gcode, "\n;CUT DISTANCE 58.000\n") == routes);
    // Marker lines only: the config block at the end still quotes the profile's cut G-code.
    CHECK(count(gcode, "\n;CUT DISTANCE ") == routes);
    CHECK(count(gcode, "\nM2800\n") == routes);

    std::string warnings;
    for (const auto &warning : print.step_state_with_warnings(psGCodeExport).warnings)
        warnings += warning.message + "\n";
    CHECK_THAT(warnings, ContainsSubstring("CUT DISTANCE 54.8") && ContainsSubstring("Fiber cut distance (58 mm)"));
}
