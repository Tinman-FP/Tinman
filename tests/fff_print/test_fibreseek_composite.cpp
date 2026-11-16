// End-to-end checks of composite-only FibreSeek slicing. The clean-room route planner
// (src/libslic3r/Fibreseek) runs at the end of the infill step, PrintObject::plan_fibreseek_routes()
// trims the plastic fills under the fibre, and GCode::extrude_fiberseek_composite_routes_for_layer()
// writes the routes. The strand values here are throwaway test data. Nothing here talks to a printer.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Fibreseek/WriterAdapter.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/libslic3r.h"
#include "test_helpers.hpp"

using namespace Slic3r;
using Catch::Matchers::ContainsSubstring;

namespace {

// The fibre settings for a 60 x 40 x 4 mm plate with medium reinforcement: fibre on every second layer.
void set_composite_only(DynamicPrintConfig &config)
{
    config.set_deserialize_strict({
        { "fiber_enabled", "1" },
        { "fiber_manufacturing_mode", "composite_only" },
        { "fiber_generate_perimeters", "1" },
        { "fiber_generate_infill", "1" },
        { "fiber_reinforcement_mode", "medium" },
        { "fiber_cut_distance", "58" },
        { "fiber_restart_length", "55" },
        { "continuous_fiber_diameter", "0.25" },
        { "layer_height", "0.2" },
        { "initial_layer_print_height", "0.2" },
        { "wall_loops", "2" },
        { "skirt_loops", "0" },
        { "brim_type", "no_brim" },
        { "enable_prime_tower", "0" },
    });
}

// The Seeker 3 composite printer as the generated profile sets it up: extruder 1 (0.4 mm) prints
// the plastic filament, extruder 2 (0.7 mm) the CFC composite, which carries the strand data.
DynamicPrintConfig composite_only_config()
{
    DynamicPrintConfig config = Test::multifilament_config(2, {
        { "nozzle_diameter", "0.4,0.7" },
        { "printer_extruder_id", "1,2" },
        { "printer_extruder_variant", "Direct Drive Standard,Direct Drive Standard" },
        { "extruder_printable_height", "0,0" },
        { "filament_self_index", "1,2" },
        { "filament_map", "1,2" },
        { "single_extruder_multi_material", "0" },
        { "filament_type", "PCTG;CFC" },
        { "fiber_feed_factor", "0,0.8" },
    });
    Test::size_flush_to_nozzles(config);
    set_composite_only(config);
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

// The plate with FibreSeek's Seeker 3 values as Rocket's presets give them: FibreSeek's cut G-code,
// a 1.8 mm extended contact radius, and the composite's retraction (1 mm on travel, 5 mm on tool
// change, extra length on restart -1 for both, at 10 mm/s).
DynamicPrintConfig seeker3_config()
{
    DynamicPrintConfig config = composite_only_config();
    config.set_deserialize_strict({
        { "fiber_cut_gcode", "M2800\nM400\n;CUT DISTANCE 54.8" },
        { "fiber_nozzle_contact_radius_extended", "1.8" },
        { "retraction_length", "1,1" },
        { "retract_restart_extra", "-1,-1" },
        { "retract_length_toolchange", "5,5" },
        { "retract_restart_extra_toolchange", "-1,-1" },
        { "retraction_speed", "10,10" },
    });
    return config;
}

// A number following `axis` in a G-code move, if the move has that axis.
bool axis_value(const std::string &move, char axis, double &value)
{
    const size_t at = move.find(std::string(" ") + axis);
    if (at == std::string::npos)
        return false;
    value = std::stod(move.substr(at + 2));
    return true;
}

// The XY path from each "; Start to cut" to the following "; Cutting completed.".
std::vector<double> path_after_cuts(const std::string &gcode)
{
    std::vector<double> paths;
    double x = 0., y = 0.;
    bool after_cut = false;
    std::istringstream stream(gcode);
    for (std::string line; std::getline(stream, line);) {
        if (line == "; Start to cut") {
            paths.push_back(0.);
            after_cut = true;
        } else if (line == "; Cutting completed.") {
            after_cut = false;
        }
        if (line.rfind("G0 ", 0) != 0 && line.rfind("G1 ", 0) != 0)
            continue;
        const std::string move = line.substr(0, line.find(';'));
        double nx = x, ny = y;
        axis_value(move, 'X', nx);
        axis_value(move, 'Y', ny);
        if (after_cut)
            paths.back() += std::hypot(nx - x, ny - y);
        x = nx;
        y = ny;
    }
    return paths;
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

TEST_CASE("Fibre G-code writes the cut G-code verbatim and cuts the extended contact radius short of the cut distance", "[Fibreseek][FibreseekSlicing]")
{
    Print print;
    Model model;
    init_plate(print, model, seeker3_config());
    const std::string gcode = Test::gcode(print);

    const size_t routes = count(gcode, "; ORCA_CODEX_FIBER_ROUTE_START");
    REQUIRE(routes > 0);
    const size_t cuts = routes + 1; // and the priming line
    CHECK(count(gcode, "\n; Start to cut\nM2800\nM400\n;CUT DISTANCE 54.8\n") == cuts);
    // Marker lines only: the config block at the end still quotes the profile's cut G-code.
    CHECK(count(gcode, "\n;CUT DISTANCE ") == cuts);
    CHECK(count(gcode, "\nM1001 L") == cuts);
    CHECK(count(gcode, "\nG1 U55.000 ") == cuts);
    // The cut fires when 58 - 1.8 mm of path is left, as Rocket's does.
    const std::vector<double> paths = path_after_cuts(gcode);
    REQUIRE(paths.size() == cuts);
    for (const double mm : paths)
        CHECK(std::abs(mm - 56.2) < 0.05);
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

TEST_CASE("Composite-only slicing on a one-extruder printer stops with a clear error", "[Fibreseek][FibreseekSlicing]")
{
    // The writer prints plastic with T1 and fibre with T0, so one extruder can't run it.
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({ { "filament_type", "CFC" }, { "fiber_feed_factor", "0.8" } });
    set_composite_only(config);
    Print print;
    Model model;
    init_plate(print, model, config);
    REQUIRE_THROWS_WITH(print.process(), ContainsSubstring("needs a printer with two extruders"));
}

TEST_CASE("The idle tool waits at its own material's standby temperature", "[Fibreseek][FibreseekSlicing]")
{
    // 0 keeps the values the writer used before: 150 plastic, 180 composite.
    const bool set = GENERATE(true, false);
    DynamicPrintConfig config = composite_only_config();
    config.set_deserialize_strict("fiber_nozzle_temperature_standby", set ? "120,150" : "0,0");
    Print print;
    Model model;
    init_plate(print, model, config);
    const std::string gcode = Test::gcode(print);

    const size_t blocks = count(gcode, "T0 R ; switch extruder type to:FIBER");
    REQUIRE(blocks > 0);
    CHECK(count(gcode, set ? "\nM104 S120 T1\n" : "\nM104 S150 T1\n") == blocks);
    CHECK(count(gcode, set ? "\nM104 S150 T0\n" : "\nM104 S180 T0\n") == blocks);
}

TEST_CASE("Each composite tool change lifts Z 5 mm above its layer before the brush station", "[Fibreseek][FibreseekSlicing]")
{
    // A 6 mm printable height caps the lift above the 4 mm plate's upper fibre layers.
    const double printable_height = GENERATE(250., 6.);
    DynamicPrintConfig config = composite_only_config();
    config.set_deserialize_strict("printable_height", printable_height > 10. ? "250" : "6");
    Print print;
    Model model;
    init_plate(print, model, config);
    const std::string gcode = Test::gcode(print);

    // Per fibre layer: the change to fibre, the layer marker, the routes, the change back to plastic.
    static const std::regex layer_marker(R"(^; ORCA_CODEX_FIBER_LAYER layer=\d+ z=(\S+) )");
    std::vector<double> lifts, expected;
    bool in_change = false, lifted = false;
    std::istringstream stream(gcode);
    for (std::string line; std::getline(stream, line);) {
        std::smatch match;
        if (line == "; Start change extruder") {
            in_change = true;
            lifted = false;
        } else if (in_change && line.rfind("G0 Z", 0) == 0 && !lifted) {
            lifts.push_back(std::stod(line.substr(4)));
            lifted = true;
        } else if (in_change && line == "MOVE_TO_BRUSH_STATION") {
            CHECK(lifted);
            in_change = false;
        } else if (std::regex_search(line, match, layer_marker)) {
            const double z = std::min(std::stod(match[1].str()) + 5., printable_height);
            expected.insert(expected.end(), { z, z });
        }
    }
    REQUIRE(!expected.empty());
    REQUIRE(lifts.size() == expected.size());
    for (size_t i = 0; i < lifts.size(); ++i)
        CHECK(std::abs(lifts[i] - expected[i]) < 0.001);
    if (printable_height < 10.)
        CHECK(std::any_of(lifts.begin(), lifts.end(), [](double z) { return std::abs(z - 6.) < 0.001; }));
}

TEST_CASE("Each route start unretracts the composite's retraction plus extra length", "[Fibreseek][FibreseekSlicing]")
{
    Print print;
    Model model;
    init_plate(print, model, seeker3_config());
    const std::string gcode = Test::gcode(print);

    const size_t blocks = count(gcode, "T0 R ; switch extruder type to:FIBER");
    const size_t routes = count(gcode, "; ORCA_CODEX_FIBER_ROUTE_START");
    REQUIRE(blocks > 0);
    REQUIRE(routes >= blocks);
    // 5 - 1 after each tool change (the priming line takes the first one), 1 - 1 between routes, at
    // 10 mm/s. The G-code export drops an F that repeats the one before.
    CHECK(count(gcode, "\nG1 V4.00000 F600 ; Extrude restart\nG4 P0\n") == blocks);
    CHECK(count(gcode, "\nG1 V0.00000 F600 ; Extrude restart\nG4 P0\n") == routes + 1 - blocks);
    // The travel retraction after every cut, topped up to the tool-change retraction before the change to plastic.
    CHECK(count(gcode, "\nG1 V-1 F600 ; Retract\n") + count(gcode, "\nG1 V-1 ; Retract\n") == routes + 1);
    CHECK(count(gcode, "\nG1 V-4 F600 ; Retract\n") + count(gcode, "\nG1 V-4 ; Retract\n") == blocks);
    // Every one of these stays a G1 move: none is left as a bare axis word.
    CHECK(count(gcode, "\n V") + count(gcode, "\n U") + count(gcode, "\n E") == 0);
}

TEST_CASE("Fibre route moves take P from the bead geometry, the priming line keeps its own", "[Fibreseek][FibreseekSlicing]")
{
    // 0.2 mm layers, 0.8 mm fibre line width, 0.25 mm fibre and 1.75 mm composite filament. With
    // Composite plastic multiplier 0.8, as Rocket: 0.8 x (0.8 x 0.2 - pi x 0.25^2 / 4) / (pi x 1.75^2 / 4)
    // = 0.03689. With only the legacy Fiber feed factor 0.8, P stays pi x 0.25^2 / 4 x 0.8 = 0.03927.
    const bool multiplier = GENERATE(true, false);
    DynamicPrintConfig config = composite_only_config();
    if (multiplier)
        config.set_deserialize_strict("fiber_composite_plastic_multiplier", "0,0.8");
    const std::string route_p = multiplier ? "0.03689" : "0.03927";
    Print print;
    Model model;
    init_plate(print, model, config);
    const std::string gcode = Test::gcode(print);

    // V = U x P on every route move.
    static const std::regex fibre_move(R"(^G1 X\S+ Y\S+ V(\S+) U(\S+) P(\S+))");
    size_t moves = 0;
    std::istringstream stream(gcode);
    for (std::string line; std::getline(stream, line);) {
        std::smatch match;
        if (!std::regex_search(line, match, fibre_move))
            continue;
        ++moves;
        CHECK(match[3].str() == route_p);
        CHECK(std::abs(std::stod(match[1].str()) - std::stod(match[2].str()) * std::stod(route_p)) < 1e-5);
    }
    CHECK(moves > 0);
    // The priming line keeps the legacy model either way.
    const size_t prime = gcode.find("; ORCA_CODEX_FIBER_PRIME_START");
    REQUIRE(prime != std::string::npos);
    CHECK_THAT(gcode.substr(prime, gcode.find("; ORCA_CODEX_FIBER_PRIME_END") - prime), ContainsSubstring(" P0.03927 F"));
}

TEST_CASE("Fibre slicing needs a composite plastic multiplier or the legacy feed factor", "[Fibreseek][FibreseekSlicing]")
{
    DynamicPrintConfig config = composite_only_config();
    config.set_deserialize_strict({ { "fiber_feed_factor", "0,0" }, { "fiber_composite_plastic_multiplier", "0,0" } });
    Print print;
    Model model;
    init_plate(print, model, config);
    REQUIRE_THROWS_WITH(Test::gcode(print), ContainsSubstring("Composite plastic multiplier"));
}

TEST_CASE("M1001 L covers the restart and the fibre fed before the cut", "[Fibreseek][FibreseekSlicing]")
{
    // A 60 mm restart: the priming line feeds 60 + (90 - 56.2) = 93.8 mm before its cut.
    DynamicPrintConfig config = seeker3_config();
    config.set_deserialize_strict("fiber_restart_length", "60");
    Print print;
    Model model;
    init_plate(print, model, config);
    const std::string gcode = Test::gcode(print);

    CHECK(count(gcode, "\nM1001 L94\n") == 1);
    // Every route: L at least the restart plus the U fed up to its cut.
    static const std::regex fed(R"( U(\S+) P)");
    double load = 0., fed_mm = 0.;
    size_t routes = 0;
    std::istringstream stream(gcode);
    for (std::string line; std::getline(stream, line);) {
        std::smatch match;
        if (line.rfind("M1001 L", 0) == 0) {
            load = std::stod(line.substr(7));
            fed_mm = 0.;
        } else if (std::regex_search(line, match, fed)) {
            fed_mm += std::stod(match[1].str());
        } else if (line == "M1002") {
            ++routes;
            CHECK(load + 1e-6 >= std::ceil(60. + fed_mm - 1e-3));
        }
    }
    CHECK(routes > 1);
}

TEST_CASE("Composite-only G-code lays one priming line, in front of the print, before the first route", "[Fibreseek][FibreseekSlicing]")
{
    // The test part sits around the origin: a bed reaching below Y 0 has room in front of it, the
    // default bed from Y 0 only behind it. The part widens after its first layer, from 20 to 40 mm
    // deep, before the first fibre layer: the line must clear the wide part, not the first layer.
    const bool room_in_front = GENERATE(true, false);
    DynamicPrintConfig config = seeker3_config();
    if (room_in_front)
        config.set_deserialize_strict("printable_area", "-100x-100,200x-100,200x200,-100x200");
    indexed_triangle_set part = its_make_cube(60., 20., 0.2);
    its_translate(part, Vec3f(0.f, 10.f, 0.f));
    indexed_triangle_set wide = its_make_cube(60., 40., 3.8);
    its_translate(wide, Vec3f(0.f, 0.f, 0.2f));
    its_merge(part, wide);
    Print print;
    Model model;
    Test::init_print({ TriangleMesh(part) }, print, model, config);
    const std::string gcode = Test::gcode(print);

    REQUIRE(count(gcode, "; ORCA_CODEX_FIBER_PRIME_START") == 1);
    const size_t prime = gcode.find("; ORCA_CODEX_FIBER_PRIME_START");
    const size_t prime_end = gcode.find("; ORCA_CODEX_FIBER_PRIME_END");
    CHECK(prime_end < gcode.find("; ORCA_CODEX_FIBER_ROUTE_START"));
    // It lifts back to the tool-change height before travelling to the first route.
    const std::string lift_after_prime = "; ORCA_CODEX_FIBER_PRIME_END\nG0 Z";
    CHECK(gcode.compare(prime_end, lift_after_prime.size(), lift_after_prime) == 0);

    // 2 mm in front of everything printed (behind it without room there), reached before coming down.
    static const std::regex approach(R"(\nG0 X(\S+) Y(\S+) F30000\nG0 Z1\.200 F600\n)");
    std::smatch match;
    const std::string block = gcode.substr(prime, prime_end - prime);
    REQUIRE(std::regex_search(block, match, approach));
    BoundingBoxf first_layer, printed;
    for (const Point &point : print.first_layer_convex_hull().points)
        first_layer.merge(print.translate_to_print_space(point));
    const PrintObject &object = *print.objects().front();
    for (const Point &corner : { object.bounding_box().min, object.bounding_box().max })
        printed.merge(print.translate_to_print_space(Point(corner + object.instances().front().shift)));
    REQUIRE(printed.max.y() - printed.min.y() > first_layer.max.y() - first_layer.min.y() + 15.);
    CHECK(std::abs(std::stod(match[1].str()) - 106.5) < 0.001);
    const double expected_y = room_in_front ? printed.min.y() - 2. : printed.max.y() + 2.;
    CHECK(std::abs(std::stod(match[2].str()) - expected_y) < 0.001);
}

TEST_CASE("The priming line must fit on the bed polygon, not just its bounding box", "[Fibreseek][FibreseekSlicing]")
{
    // A triangular bed whose bounding box holds the line in front of and behind the part, while the
    // bed itself holds neither: the line is skipped, with a warning.
    DynamicPrintConfig config = seeker3_config();
    config.set_deserialize_strict("printable_area", "-100x-100,200x-100,-100x200");
    Print print;
    Model model;
    init_plate(print, model, config);
    const std::string gcode = Test::gcode(print);

    CHECK(count(gcode, "; ORCA_CODEX_FIBER_PRIME_START") == 0);
    CHECK(count(gcode, "; TINMANX1_FIBER_PRIME_SKIPPED") == 1);
    std::string warnings;
    for (const auto &warning : print.step_state_with_warnings(psGCodeExport).warnings)
        warnings += warning.message + "\n";
    CHECK_THAT(warnings, ContainsSubstring("fiber priming line"));
}

TEST_CASE("Each fibre route runs at the start speed for exactly Fiber slow length", "[Fibreseek][FibreseekSlicing]")
{
    // 3 mm is shorter than the planner's 7 mm anchor, so the slow zone ends inside the Start phase.
    DynamicPrintConfig config = composite_only_config();
    config.set_deserialize_strict({ { "fiber_slow_length", "3" }, { "fiber_start_speed", "5" } });
    Print print;
    Model model;
    init_plate(print, model, config);
    const std::string gcode = Test::gcode(print);

    // Fibre-feeding moves while the feed rate is 5 mm/s (F300; F is modal). U is the move's fibre.
    static const std::regex fibre_move(R"(^G1 X\S+ Y\S+ V\S+ U(\S+) P\S+)");
    static const std::regex feed_word(R"( F(\d+))");
    std::vector<double> slow_fibre_mm;
    double              feed = 0.;
    std::istringstream  stream(gcode);
    for (std::string line; std::getline(stream, line);) {
        if (line.rfind("; ORCA_CODEX_FIBER_ROUTE_START", 0) == 0)
            slow_fibre_mm.push_back(0.);
        if (line.rfind("G1 ", 0) != 0)
            continue;
        const std::string move = line.substr(0, line.find(';'));
        std::smatch       match;
        if (std::regex_search(move, match, feed_word))
            feed = std::stod(match[1].str());
        if (!slow_fibre_mm.empty() && feed == 300. && std::regex_search(move, match, fibre_move))
            slow_fibre_mm.back() += std::stod(match[1].str());
    }
    REQUIRE(!slow_fibre_mm.empty());
    for (const double mm : slow_fibre_mm)
        CHECK(std::abs(mm - 3.) < 0.01);
}

TEST_CASE("A fibre setting change redoes the infill only while composite-only fibre is on", "[Fibreseek][FibreseekSlicing]")
{
    const bool fibre_on = GENERATE(false, true);
    DynamicPrintConfig config = composite_only_config();
    config.set_deserialize_strict({ { "fiber_enabled", fibre_on ? "1" : "0" }, { "gcode_comments", "1" } }); // init_print sets gcode_comments
    Print print;
    Model model;
    init_plate(print, model, config);
    print.process();
    REQUIRE(print.objects().front()->is_step_done(posInfill));

    config.set_deserialize_strict("fiber_restart_length", "60");
    print.apply(model, config);
    CHECK(print.objects().front()->is_step_done(posInfill) == !fibre_on);
}
