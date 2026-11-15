// Coupon harness for the clean-room FibreSeek route planner.
//
// This test is hidden, so it only runs when asked for by tag:
//   FIBRESEEK_COUPON_DIR=<STLs from scripts/generate_fiberseek_test_coupons.py>
//   FIBRESEEK_COUPON_OUT=<output folder>
//   [FIBRESEEK_COUPON_PROFILE=medium|heavy]
//   libslic3r_tests "[fibreseek_coupons]"
//
// For each coupon it slices the STL, plans every fibre layer, and converts the plans with
// the writer adapter. It then emits G-code with the same command sequence that
// GCode::extrude_fiberseek_composite_routes_for_layer() and its start, contract and
// shutdown helpers produce, so scripts/source-helpers/audit_fiberseek_gcode_contract.py
// can check it. It also writes the audit's summary JSON and an SVG of the first two fibre
// layers. Plastic moves are left out: they don't take part in the fibre contract. Nothing
// here talks to a printer.

#include <catch2/catch_all.hpp>

#include "libslic3r/Fibreseek/RoutePlanner.hpp"
#include "libslic3r/Fibreseek/WriterAdapter.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"

#include <boost/filesystem.hpp>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

using namespace Slic3r;
using namespace Slic3r::Fibreseek;

namespace {

std::string strf(const char *f, ...)
{
    char    buf[512];
    va_list args;
    va_start(args, f);
    std::vsnprintf(buf, sizeof(buf), f, args);
    va_end(args);
    return buf;
}

// The values the writer reads from the printer and filament profile. These are the
// FibreSeeker 3 contract values from docs/fiberseek-gcode-contract.md.
struct MachineValues
{
    double cut_distance { 58. };
    double restart_length { 55. }; // fiber_restart_length in the generator's Seeker 3 printer profile
    double start_length { 0. };
    double print_feed { 30. * 60. };
    double slow_feed { 5. * 60. };
    double tail_feed { 3. * 60. };
    double ironing_feed { 3. * 60. };
    double correction_feed { 2. * 60. };
    double slow_length { 0. };
    double fibre_p { std::round(M_PI * 0.125 * 0.125 * 0.835 * 100000.) / 100000. };
    double ironing { 5. };
    int    fibre_temp { 270 }, plastic_temp { 250 }, bed { 75 }, chamber { 0 }, plastic_standby { 150 }, fibre_standby { 180 };
    int    plastic_fan { 2 }, composite_fan { 0 };
    double offset_x { 150. }, offset_y { 150. };
};

struct EmittedLayer
{
    size_t            layer_index;
    double            z;
    WriterLayerRoutes routes;
};

std::string xy(const MachineValues &m, const Point &p)
{
    return strf("X%.3f Y%.3f", double(p.x()) * SCALING_FACTOR + m.offset_x, double(p.y()) * SCALING_FACTOR + m.offset_y);
}

// One layer's fibre block, in the writer's order.
void emit_layer(std::string &g, const MachineValues &m, const EmittedLayer &layer, bool &planner_started)
{
    const double v_per_mm = m.fibre_p;
    if (!planner_started) {
        g += "; ORCA_CODEX_NATIVE_FIBER_PLANNER_START\n; Generated with Tinman clean-room FibreSeek route planner (test harness)\n";
        g += "G21\nG90\nM83 ; use relative distances for extrusion\n";
        planner_started = true;
    }
    g += "; Start change extruder\nM400\n" + strf("M104 S%d T0\n", m.fibre_temp) + "G1 F1800 E-15 ; Retract\n";
    g += strf("M104 S%d T1\n", m.plastic_standby) + strf("M106 P%d S230\n", m.plastic_fan);
    g += "MOVE_TO_BRUSH_STATION\nCLEAN_NOZZLE\nMOVE_OUT_BRUSH_STATION\nT0 R ; switch extruder type to:FIBER\n";
    g += strf("M106 P%d S64\n", m.composite_fan) + strf("M109 S%d T0\n", m.fibre_temp) + strf("M106 P%d S0\n", m.plastic_fan);
    g += "; End change extruder\n";
    g += strf("; ORCA_CODEX_FIBER_LAYER layer=%zu z=%.3f routes=%zu\n", layer.layer_index, layer.z, layer.routes.routes.size());

    for (const WriterRoute &route : layer.routes.routes) {
        if (!route.is_release_safe()) {
            g += strf("; TINMANX1_SKIPPED_UNSAFE_FIBER_ROUTE layer=%zu region=%zu route=%zu length=%.3f unsupported_void_crossings=%zu\n",
                     route.layer_id, layer.routes.region_id, route.id, route.length_mm, route.unsupported_void_crossing_count);
            continue;
        }
        double fibre = 0.;
        for (const WriterRouteSegment &s : route.planned_segments)
            if (s.emits_fiber && s.fiber_mm_per_mm > 0.)
                fibre += s.length_mm;
        std::string warnings;
        for (const std::string &w : route.warnings)
            warnings += (warnings.empty() ? "" : ",") + w;
        g += strf("; ORCA_CODEX_FIBER_ROUTE_START layer=%zu z=%.3f kind=%s fiber_length=%.3f route_length=%.3f tail_length=%.3f warnings=%s\n",
                 route.layer_id, layer.z, to_string(route.shape), fibre, route.length_mm, route.tail_length_mm, warnings.empty() ? "none" : warnings.c_str());
        g += strf("; TINMANX1_FIBER_ROUTE region=%zu route=%zu candidate_count=%zu route_total_length=%.3f coalesce_threshold=%.3f\n",
                 layer.routes.region_id, route.id, route.candidate_ids.size(), route.length_mm, route.coalesce_transition_length_mm);
        g += strf("; planned_segment_count=%zu\n;TYPE:Continuous fiber\n;WIDTH:0.800\n;HEIGHT:0.200\n", route.planned_segments.size());
        const Point start = route.planned_segments.front().polyline.points.front();
        g += "G0 " + xy(m, start) + " F12000 ; move to first FibreSeek composite route point\n";
        g += strf("G0 Z%.3f ; FibreSeek composite route Z\n", layer.z);
        g += strf("M1001 L%d\n", int(std::ceil(fibre + m.cut_distance)));
        g += strf("G1 F1500 U%.3f ; Extrude restart\n", m.restart_length);
        g += strf("G1 F600 V%.5f ; Extrude restart\nG4 P0\n", m.start_length * v_per_mm);
        g += "; SEAM Fiber at " + xy(m, start) + strf(" Z%.3f\n", layer.z);

        const double cut_at = std::max(0., fibre - m.cut_distance);
        double       walked = 0.;
        bool         cut    = false;
        Point        last   = start;
        auto move = [&](const Point &to, double len, double feed, bool feed_fibre) {
            g += "G1 " + xy(m, to) + strf(" V%.5f", len * v_per_mm);
            if (feed_fibre)
                g += strf(" U%.5f P%.5f", len, m.fibre_p);
            g += strf(" F%.0f\n", feed);
            last = to;
        };
        // The writer's marker carries Fiber cut distance itself (tinman_fiber_cut_gcode in GCode.cpp).
        auto emit_cut = [&]() { g += strf("M2800\nM400\n;CUT DISTANCE %.3f\n", m.cut_distance); };
        for (size_t si = 0; si < route.planned_segments.size(); ++si) {
            const WriterRouteSegment &seg = route.planned_segments[si];
            g += strf("; TINMANX1_FIBER_SEGMENT index=%zu phase=%s length=%.3f plastic=1 fiber=1\n", si, to_string(seg.phase), seg.length_mm);
            const Points &pts = seg.polyline.points;
            for (size_t i = 1; i < pts.size(); ++i) {
                const double len = (pts[i] - pts[i - 1]).cast<double>().norm() * SCALING_FACTOR;
                if (len <= EPSILON)
                    continue;
                if (!cut && cut_at > walked + EPSILON && cut_at < walked + len - EPSILON) {
                    const double f   = (cut_at - walked) / len;
                    const Point  mid = pts[i - 1] + ((pts[i] - pts[i - 1]).cast<double>() * f).cast<coord_t>();
                    move(mid, cut_at - walked, walked < m.slow_length ? m.slow_feed : m.print_feed, true);
                    emit_cut();
                    cut = true;
                    move(pts[i], len - (cut_at - walked), m.tail_feed, false);
                    walked += len;
                    continue;
                }
                if (!cut && cut_at <= walked + EPSILON) {
                    emit_cut();
                    cut = true;
                }
                move(pts[i], len, cut ? m.tail_feed : (walked < m.slow_length ? m.slow_feed : m.print_feed), !cut);
                walked += len;
            }
        }
        if (!cut)
            emit_cut();
        g += "; Cutting completed.\n";
        if (m.ironing > 0. && !route.tail_segments.empty()) {
            double left = m.ironing, done = 0.;
            for (const Polyline &tail : route.tail_segments) {
                if ((tail.points.front() - last).cast<double>().norm() * SCALING_FACTOR > 0.05)
                    continue;
                for (size_t i = 1; i < tail.points.size() && left > EPSILON; ++i) {
                    const double len = std::min(left, (tail.points[i] - tail.points[i - 1]).cast<double>().norm() * SCALING_FACTOR);
                    move(tail.points[i], len, m.ironing_feed, false);
                    left -= len;
                    done += len;
                }
            }
            g += strf("; TINMANX1_FIBER_FINISH_IRONING distance=%.3f requested=%.3f\n", done, m.ironing);
        }
        g += "G1 F600 V-1 ; Retract\nM1002\n" + strf("; ORCA_CODEX_FIBER_ROUTE_END layer=%zu route=%zu\n", route.layer_id, route.id);
    }
    g += "; Start change extruder\nM400\n" + strf("M104 S%d T1\n", m.plastic_temp) + "G1 F600 V-14 ; Retract\n";
    g += strf("M104 S%d T0\n", m.fibre_standby) + "T1 ; switch extruder type to:PLASTIC\n" + strf("M106 P%d S255\n", m.plastic_fan);
    g += "MOVE_TO_BRUSH_STATION\n" + strf("M109 S%d T1\n", m.plastic_temp) + "CLEAN_NOZZLE\nMOVE_OUT_BRUSH_STATION\n";
    g += strf("M106 P%d S0\n", m.composite_fan) + "; End change extruder\n";
}

std::map<std::string, size_t> command_counts(const std::string &g)
{
    std::map<std::string, size_t> counts;
    std::istringstream            in(g);
    for (std::string line; std::getline(in, line);) {
        const std::string code = line.substr(0, line.find(';'));
        std::istringstream words(code);
        std::string        first;
        if (words >> first)
            ++counts[first];
    }
    return counts;
}

// The whole file: header summary, machine contract, initial plastic tool, the fibre layers,
// then shutdown, in the writer's order.
std::string emit_gcode(const MachineValues &m, const std::vector<EmittedLayer> &layers, size_t layer_count, double &fibre_total, size_t &route_count)
{
    fibre_total = 0.;
    route_count = 0;
    size_t fibre_layers = 0;
    for (const EmittedLayer &l : layers) {
        size_t here = 0;
        for (const WriterRoute &r : l.routes.routes)
            if (r.is_release_safe()) {
                fibre_total += r.length_mm;
                ++here;
            }
        route_count += here;
        fibre_layers += here > 0;
    }
    std::string g;
    g += "; ORCA_CODEX_NATIVE_FIBER_PLANNER_MERGED\n; Generated with Tinman clean-room FibreSeek route planner (test harness)\n";
    g += "; PRINTING_MODE: Composite Only\n";
    g += strf("; continuous_fiber_route_count = %zu\n; continuous_fiber_layers = %zu\n", route_count, fibre_layers);
    g += strf("; continuous_fiber_used_mm = %.3f\n; continuous_fiber_used_g = %.4f\n", fibre_total, fibre_total * 102. / 1e6);
    g += strf("; continuous_fiber_matrix_path_mm = %.3f\n; chamber_temperature = %d\n", fibre_total, m.chamber);
    g += "; ORCA_CODEX_FIBERSEEK_MACHINE_CONTRACT_START\n" + strf("SET_PRINT_STATS_INFO TOTAL_LAYER=%zu\n", layer_count);
    g += "SET_PRESSURE_ADVANCE EXTRUDER=extruder ADVANCE=0\nSET_VELOCITY_LIMIT MINIMUM_CRUISE_RATIO=0.8\n";
    g += "SET_VELOCITY_LIMIT SQUARE_CORNER_VELOCITY=1\nSET_TOOL_CORNER_VELOCITY T=0 SCV=1\n";
    g += strf("M104 S%d T0\nM140 S%d\nM141 S%d\n", m.fibre_temp, m.bed, m.chamber) + "; ORCA_CODEX_FIBERSEEK_MACHINE_CONTRACT_END\n";
    g += strf("M190 S%d\nM191 S%d\nM104 S%d T1\n", m.bed, m.chamber, m.plastic_temp);
    g += "; ORCA_CODEX_FIBERSEEK_INITIAL_PLASTIC_TOOL\nT1 ; switch extruder type to:PLASTIC\n" + strf("M109 S%d T1\n", m.plastic_temp);
    bool started = false;
    size_t next = 0;
    for (size_t i = 0; i < layer_count; ++i) {
        g += strf(";LAYER_CHANGE\n; layer %zu: plastic moves are not part of this harness\n", i);
        if (next < layers.size() && layers[next].layer_index == i) {
            if (!layers[next].routes.routes.empty())
                emit_layer(g, m, layers[next], started);
            ++next;
        }
    }
    if (started)
        g += "; ORCA_CODEX_NATIVE_FIBER_PLANNER_END\n";
    g += "; ORCA_CODEX_FIBERSEEK_MACHINE_SHUTDOWN_START\nM400\nM104 S0 T1\nM104 S0 T0\nM106 P2 S0\nM140 S0\nM141 S0\nM400\nG90\n";
    g += "; ORCA_CODEX_FIBERSEEK_MACHINE_SHUTDOWN_END\n; EXECUTABLE_BLOCK_END\n\n";
    return g;
}

// --- SVG ------------------------------------------------------------------------------

std::string svg_path(const Points &pts, bool closed)
{
    std::string d;
    for (size_t i = 0; i < pts.size(); ++i)
        d += strf("%s%.3f %.3f ", i == 0 ? "M" : "L", double(pts[i].x()) * SCALING_FACTOR, -double(pts[i].y()) * SCALING_FACTOR);
    return closed ? d + "Z" : d;
}

std::string svg_path(const Path2d &pts)
{
    std::string d;
    for (size_t i = 0; i < pts.size(); ++i)
        d += strf("%s%.3f %.3f ", i == 0 ? "M" : "L", pts[i].x(), -pts[i].y());
    return d;
}

void write_svg(const std::string &file, const std::string &title, const ExPolygons &slices, const LayerPlan &plan, const PlannerSettings &s)
{
    BoundingBox bb = get_extents(slices);
    const double x0 = bb.min.x() * SCALING_FACTOR - 6., x1 = bb.max.x() * SCALING_FACTOR + 6.;
    const double y0 = -bb.max.y() * SCALING_FACTOR - 6., y1 = -bb.min.y() * SCALING_FACTOR + 6.;
    const double legend_h = 19.;
    std::ofstream out(file);
    out << strf("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"%.2f %.2f %.2f %.2f\" width=\"%.0f\" height=\"%.0f\" font-family=\"Helvetica, Arial, sans-serif\">\n",
               x0, y0 - legend_h, x1 - x0, y1 - y0 + legend_h, (x1 - x0) * 8., (y1 - y0 + legend_h) * 8.);
    out << strf("<rect x=\"%.2f\" y=\"%.2f\" width=\"%.2f\" height=\"%.2f\" fill=\"#ffffff\"/>\n", x0, y0 - legend_h, x1 - x0, y1 - y0 + legend_h);
    for (const ExPolygon &e : slices) {
        std::string d = svg_path(e.contour.points, true);
        for (const Polygon &h : e.holes)
            d += " " + svg_path(h.points, true);
        out << "<path d=\"" << d << "\" fill=\"#ececec\" stroke=\"#9a9a9a\" stroke-width=\"0.2\" fill-rule=\"evenodd\"/>\n";
    }
    const std::map<PieceKind, const char *> colour { { PieceKind::Loop, "#1f5fbf" }, { PieceKind::Transition, "#8e44ad" },
                                                     { PieceKind::Rib, "#2e8b57" }, { PieceKind::Connector, "#e67e22" } };
    for (const Route &r : plan.routes) {
        for (const RoutePiece &p : r.pieces)
            out << "<path d=\"" << svg_path(p.points) << "\" fill=\"none\" stroke=\"" << colour.at(p.kind)
                << strf("\" stroke-width=\"%.2f\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.9\"/>\n", s.fibre_width_mm);
        // After-cut span: a thin dashed white line over the bead.
        const Path2d path = r.path();
        const std::vector<double> cum = arc_lengths(path);
        out << "<path d=\"" << svg_path(sub_path(path, cum, r.cut_at_mm, r.length_mm))
            << "\" fill=\"none\" stroke=\"#ffffff\" stroke-width=\"0.22\" stroke-dasharray=\"0.8 0.6\"/>\n";
        const Vec2d st = r.start(), cut = point_at(path, cum, r.cut_at_mm);
        out << strf("<circle cx=\"%.3f\" cy=\"%.3f\" r=\"0.9\" fill=\"#111111\"/>\n", st.x(), -st.y());
        out << strf("<circle cx=\"%.3f\" cy=\"%.3f\" r=\"0.75\" fill=\"#d62728\" stroke=\"#ffffff\" stroke-width=\"0.2\"/>\n", cut.x(), -cut.y());
        out << strf("<text x=\"%.3f\" y=\"%.3f\" font-size=\"2.6\" fill=\"#111111\">%zu</text>\n", st.x() + 1.2, -st.y() - 1.2, r.id);
    }
    for (const SkippedCandidate &k : plan.diagnostics.skipped)
        out << strf("<text x=\"%.3f\" y=\"%.3f\" font-size=\"3\" fill=\"#d62728\" text-anchor=\"middle\">&#215;</text>\n", k.where.x(), -k.where.y() + 1.);

    size_t warned = 0;
    for (const Route &r : plan.routes)
        warned += r.has_warning("below_min_bend_radius");
    const double ly = y0 - legend_h + 4.5;
    out << strf("<text x=\"%.2f\" y=\"%.2f\" font-size=\"3.4\" font-weight=\"bold\" fill=\"#111111\">%s</text>\n", x0 + 1., ly, title.c_str());
    out << strf("<text x=\"%.2f\" y=\"%.2f\" font-size=\"2.6\" fill=\"#333333\">%zu routes, %.0f mm fibre, %zu below the %.0f mm bend target, %zu candidates skipped</text>\n",
               x0 + 1., ly + 4., plan.routes.size(), plan.total_length_mm(), warned, s.bend_radius_mm, plan.diagnostics.skipped.size());
    double lx = x0 + 1.;
    for (const auto &[kind, c] : colour) {
        out << strf("<rect x=\"%.2f\" y=\"%.2f\" width=\"4\" height=\"1.6\" fill=\"%s\"/>", lx, ly + 6.3, c);
        out << strf("<text x=\"%.2f\" y=\"%.2f\" font-size=\"2.4\" fill=\"#333333\">%s</text>\n", lx + 5., ly + 7.8, to_string(kind));
        lx += 19.;
    }
    lx = x0 + 1.;
    out << strf("<circle cx=\"%.2f\" cy=\"%.2f\" r=\"0.9\" fill=\"#111111\"/><text x=\"%.2f\" y=\"%.2f\" font-size=\"2.4\" fill=\"#333333\">route start</text>\n", lx + 1., ly + 10.6, lx + 3., ly + 11.3);
    out << strf("<circle cx=\"%.2f\" cy=\"%.2f\" r=\"0.75\" fill=\"#d62728\"/><text x=\"%.2f\" y=\"%.2f\" font-size=\"2.4\" fill=\"#333333\">cut (dashed: after-cut tail)</text>\n", lx + 20., ly + 10.6, lx + 22., ly + 11.3);
    out << strf("<text x=\"%.2f\" y=\"%.2f\" font-size=\"3\" fill=\"#d62728\">&#215;</text><text x=\"%.2f\" y=\"%.2f\" font-size=\"2.4\" fill=\"#333333\">skipped (too short)</text>\n", lx + 54., ly + 11.6, lx + 57., ly + 11.3);
    out << "</svg>\n";
}

PlannerSettings coupon_settings(const std::string &profile)
{
    PlannerSettings s;
    s.finish_ironing_mm = 5.;
    if (profile == "heavy") {
        s.mode    = ReinforcementMode::Heavy;
        s.pattern = RibPattern::Solid;
    } else {
        s.mode    = ReinforcementMode::Medium;
        s.pattern = RibPattern::Rhombic;
    }
    return s;
}

} // namespace

TEST_CASE("Coupon harness: plan coupons, emit contract G-code and route SVGs", "[.][fibreseek_coupons]")
{
    const char *in_dir  = std::getenv("FIBRESEEK_COUPON_DIR");
    const char *out_dir = std::getenv("FIBRESEEK_COUPON_OUT");
    if (in_dir == nullptr || out_dir == nullptr) {
        WARN("Set FIBRESEEK_COUPON_DIR and FIBRESEEK_COUPON_OUT to run the coupon harness.");
        return;
    }
    const char       *profile_env = std::getenv("FIBRESEEK_COUPON_PROFILE");
    const std::string profile     = profile_env ? profile_env : "medium";
    namespace fs = boost::filesystem;
    fs::create_directories(out_dir);

    std::vector<fs::path> stls;
    for (const fs::directory_entry &e : fs::directory_iterator(in_dir))
        if (e.path().extension() == ".stl")
            stls.push_back(e.path());
    std::sort(stls.begin(), stls.end());
    REQUIRE(!stls.empty());

    const PlannerSettings s = coupon_settings(profile);
    const RoutePlanner    planner(s);
    LayerSchedule         schedule;
    schedule.step = profile == "heavy" ? 1 : 2;
    const MachineValues machine;
    std::ofstream report((fs::path(out_dir) / ("report-" + profile + ".tsv")).string());
    report << "coupon\tlayers\tfibre_layers\troutes\tfibre_mm\tbelow_bend_target\textended\tskipped_cut_window\tskipped_hw_min\tskipped_other\tvoid_crossings\n";

    for (const fs::path &stl : stls) {
        TriangleMesh mesh;
        REQUIRE(mesh.ReadSTLFile(stl.string().c_str()));
        const BoundingBoxf3 bb      = mesh.bounding_box();
        const double        layer_h = 0.2;
        const size_t        n       = size_t(std::floor((bb.max.z() - bb.min.z()) / layer_h + 1e-6));
        std::vector<float>  zs;
        for (size_t i = 0; i < n; ++i)
            zs.push_back(float(bb.min.z() + (double(i) + 0.5) * layer_h));
        const std::vector<ExPolygons> slices = slice_mesh_ex(mesh.its, zs, MeshSlicingParamsEx {});

        std::vector<EmittedLayer> emitted;
        std::vector<LayerPlan>    plans;
        size_t warned = 0, extended = 0, voids = 0;
        size_t skipped_window = 0, skipped_hw = 0, skipped_other = 0;
        for (size_t i = 0; i < n; ++i) {
            if (!schedule.is_fibre_layer(i, n) || slices[i].empty())
                continue;
            LayerInput in;
            in.layer_id            = i;
            in.print_z             = bb.min.z() + double(i + 1) * layer_h;
            in.height              = layer_h;
            in.slices              = slices[i];
            in.fibre_layer_ordinal = schedule.ordinal(i);
            LayerPlan plan = planner.plan_layer(in);
            for (const Route &r : plan.routes) {
                warned += r.has_warning("below_min_bend_radius");
                extended += r.has_warning("extended_turns_for_cut_window");
                voids += r.void_crossings;
            }
            skipped_window += plan.diagnostics.count(SkipReason::BelowCutWindow);
            skipped_hw += plan.diagnostics.count(SkipReason::BelowHardwareMinLength);
            skipped_other += plan.diagnostics.skipped.size() - plan.diagnostics.count(SkipReason::BelowCutWindow) -
                             plan.diagnostics.count(SkipReason::BelowHardwareMinLength);
            emitted.push_back({ i, in.print_z, to_writer_routes(plan, 0, s.hardware_min_length_mm) });
            if (plans.size() < 2)
                write_svg((fs::path(out_dir) / (stl.stem().string() + "-" + profile + "-layer" + std::to_string(i) + ".svg")).string(),
                          stl.stem().string().substr(0, 9) + strf(" | layer %zu (z %.1f) | %s profile", i, in.print_z, profile.c_str()), slices[i], plan, s);
            plans.push_back(std::move(plan));
        }

        double            fibre = 0.;
        size_t            routes = 0;
        const std::string gcode = emit_gcode(machine, emitted, n, fibre, routes);
        const fs::path    gpath = fs::path(out_dir) / (stl.stem().string() + "-" + profile + ".gcode");
        std::ofstream(gpath.string()) << gcode;

        // Summary JSON next to the G-code, in the layout the audit script reads.
        const auto counts = command_counts(gcode);
        auto count = [&](const char *c) { auto it = counts.find(c); return it == counts.end() ? size_t(0) : it->second; };
        size_t layers_with_routes = 0;
        for (const EmittedLayer &l : emitted)
            layers_with_routes += !l.routes.routes.empty();
        std::ofstream(gpath.string() + ".native_fiber.summary.json")
            << strf("{\n  \"routes\": {\"count\": %zu, \"layers_with_routes\": %zu, \"total_length_mm\": %.3f},\n", routes, layers_with_routes, fibre)
            << strf("  \"config\": {\"cut_distance\": %.3f},\n", machine.cut_distance)
            << strf("  \"command_counts\": {\"M1001\": %zu, \"M1002\": %zu, \"M2800\": %zu, \"T0\": %zu, \"T1\": %zu},\n", count("M1001"),
                   count("M1002"), count("M2800"), count("T0"), count("T1"))
            << strf("  \"skipped\": {\"routes_skipped_by_cut_window_min_length\": %zu, \"routes_skipped_by_hardware_min_length\": %zu}\n}\n",
                   skipped_window, skipped_hw);

        report << stl.stem().string() << '\t' << n << '\t' << layers_with_routes << '\t' << routes << '\t' << strf("%.1f", fibre) << '\t' << warned << '\t'
               << extended << '\t' << skipped_window << '\t' << skipped_hw << '\t' << skipped_other << '\t' << voids << '\n';
        CHECK(voids == 0);
        CHECK(routes > 0);
    }
}
