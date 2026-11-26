#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <vector>

#include "slic3r/GUI/PrinterWebViewCamera.hpp"

using namespace Slic3r::GUI;

// Printer hosts and cameras use the documentation range 192.0.2.0/24: .1x are printers, .2x cameras.

TEST_CASE("A typed camera address becomes an RTSP address", "[PrinterWebViewCamera]")
{
    CHECK(normalize_prusa_camera_url("  192.0.2.20 ") == "rtsp://192.0.2.20/live");
    CHECK(normalize_prusa_camera_url("rtsp://192.0.2.20:8554/cam") == "rtsp://192.0.2.20:8554/cam");
    CHECK(normalize_prusa_camera_url("RTSPS://192.0.2.20") == "RTSPS://192.0.2.20/live");
    CHECK(normalize_prusa_camera_url("   ").empty());
}

TEST_CASE("With no cameras saved, discovery takes a single camera and leaves several to the user", "[PrinterWebViewCamera]")
{
    const std::map<std::string, std::string> none;

    const PrusaCameraDiscovery nothing = choose_discovered_prusa_camera({}, none, "192.0.2.10");
    CHECK(nothing.url.empty());
    CHECK(nothing.available == 0);
    CHECK(nothing.other_printers == 0);

    const PrusaCameraDiscovery one = choose_discovered_prusa_camera({"192.0.2.20"}, none, "192.0.2.10");
    CHECK(one.url == "rtsp://192.0.2.20/live");
    CHECK(one.available == 1);

    const PrusaCameraDiscovery two = choose_discovered_prusa_camera({"192.0.2.20", "192.0.2.21"}, none, "192.0.2.10");
    CHECK(two.url.empty());
    CHECK(two.available == 2);
}

TEST_CASE("A printer without a camera doesn't take one saved for another printer", "[PrinterWebViewCamera]")
{
    // The only camera on the network is the first printer's: the second printer gets none.
    const std::map<std::string, std::string> saved{{"192.0.2.10", "rtsp://192.0.2.20/live"}};
    const PrusaCameraDiscovery               found = choose_discovered_prusa_camera({"192.0.2.20"}, saved, "192.0.2.11");
    CHECK(found.url.empty());
    CHECK(found.available == 0);
    CHECK(found.other_printers == 1);

    // Once a second camera answers, it is the one left for the second printer.
    const PrusaCameraDiscovery second = choose_discovered_prusa_camera({"192.0.2.20", "192.0.2.21"}, saved, "192.0.2.11");
    CHECK(second.url == "rtsp://192.0.2.21/live");
    CHECK(second.available == 1);
    CHECK(second.other_printers == 1);
}

TEST_CASE("A camera saved for this printer and another stays this printer's", "[PrinterWebViewCamera]")
{
    // Two printers saved the same camera (as older builds could). Searching again from either one
    // keeps it, so the printer the camera belongs to never loses it.
    const std::map<std::string, std::string> saved{{"192.0.2.10", "rtsp://192.0.2.20/live"},
                                                   {"192.0.2.11", "rtsp://192.0.2.20/live"}};
    for (const char* printer : {"192.0.2.10", "192.0.2.11"}) {
        const PrusaCameraDiscovery found = choose_discovered_prusa_camera({"192.0.2.20"}, saved, printer);
        CHECK(found.url == "rtsp://192.0.2.20/live");
        CHECK(found.other_printers == 0);
    }
}

TEST_CASE("A saved camera is matched by its host, and a blank one claims nothing", "[PrinterWebViewCamera]")
{
    // A typed address may carry a port, a sign-in or another path, and any case of scheme.
    const std::map<std::string, std::string> saved{{"192.0.2.10", "RTSP://viewer@192.0.2.20:554/stream"},
                                                   {"192.0.2.12", ""}};
    const PrusaCameraDiscovery               found = choose_discovered_prusa_camera({"192.0.2.20", "192.0.2.21"}, saved, "192.0.2.11");
    CHECK(found.url == "rtsp://192.0.2.21/live");
    CHECK(found.available == 1);
    CHECK(found.other_printers == 1);
}
