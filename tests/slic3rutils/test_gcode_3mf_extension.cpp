#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <catch2/catch_message.hpp>
#include "slic3r/GUI/GUI.hpp"

#include <boost/filesystem/path.hpp>

#include <string>
#include <tuple>

using namespace Slic3r::GUI;

TEST_CASE("A sliced file name ends in exactly one .gcode.3mf", "[GCode3mfExtension]")
{
    // What the save dialogs return for a typed name, and the name Tinman saves.
    const auto [chosen, expected] = GENERATE(table<std::string, std::string>({
        {"U1_stage8", "U1_stage8.gcode.3mf"},
        {"U1_stage8.gcode.3mf", "U1_stage8.gcode.3mf"},
        // NSSavePanel on macOS for a name typed with the full ending.
        {"U1_stage8.gcode.3mf.3mf", "U1_stage8.gcode.3mf"},
        {"U1_stage8.gcode.3mf.gcode.3mf", "U1_stage8.gcode.3mf"},
        // wx on Windows appends only the last part of "*.gcode.3mf".
        {"U1_stage8.3mf", "U1_stage8.gcode.3mf"},
        {"U1_stage8.gcode", "U1_stage8.gcode.3mf"},
        {"U1_stage8.GCODE.3MF", "U1_stage8.gcode.3mf"},
        // Other dots belong to the name.
        {"benchy_0.2mm", "benchy_0.2mm.gcode.3mf"},
        {"benchy.v2.gcode.3mf", "benchy.v2.gcode.3mf"},
        {"part.stl", "part.stl.gcode.3mf"},
    }));
    const boost::filesystem::path dir = boost::filesystem::path("exports") / "plate 1.3mf";
    CAPTURE(chosen);

    CHECK(with_gcode_3mf_extension(dir / chosen) == dir / expected);
    CHECK(with_gcode_3mf_extension(boost::filesystem::path(chosen)) == boost::filesystem::path(expected));
}
