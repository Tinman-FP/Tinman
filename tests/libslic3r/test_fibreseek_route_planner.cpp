// Unit tests for the clean-room FibreSeek route planner (src/libslic3r/Fibreseek/).
// The design they check is in docs/fibreseek-route-planner-spec.md.

#include <catch2/catch_all.hpp>

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Fibreseek/RouteGeometry.hpp"
#include "libslic3r/Fibreseek/RoutePlanner.hpp"
#include "libslic3r/Fibreseek/WriterAdapter.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <cmath>
#include <random>

using namespace Slic3r;
using namespace Slic3r::Fibreseek;
using Catch::Approx;

namespace {

Point pt(double x, double y) { return Point(coord_t(scale_(x)), coord_t(scale_(y))); }

Polygon circle(double cx, double cy, double r, int n = 180)
{
    Polygon p;
    for (int i = 0; i < n; ++i) {
        const double a = 2. * M_PI * double(i) / double(n);
        p.points.push_back(pt(cx + r * std::cos(a), cy + r * std::sin(a)));
    }
    return p;
}

ExPolygon plate(double w, double h, const std::vector<std::array<double, 3>> &holes = {})
{
    ExPolygon e;
    e.contour.points = { pt(-w / 2, -h / 2), pt(w / 2, -h / 2), pt(w / 2, h / 2), pt(-w / 2, h / 2) };
    for (const auto &hole : holes) {
        Polygon c = circle(hole[0], hole[1], hole[2] / 2.);
        c.make_clockwise();
        e.holes.push_back(c);
    }
    return e;
}

PlannerSettings walls_only()
{
    PlannerSettings s;
    s.infill = false;
    return s;
}

LayerPlan plan(const PlannerSettings &s, ExPolygons slices, size_t ordinal = 0)
{
    LayerInput in;
    in.layer_id            = 5;
    in.print_z             = 1.2;
    in.slices              = std::move(slices);
    in.fibre_layer_ordinal = ordinal;
    return RoutePlanner(s).plan_layer(in);
}

// Smallest distance between two different routes. Sec. 3.4 needs it to be at least w.
double min_route_separation(const LayerPlan &p)
{
    double best = 1e9;
    for (size_t i = 0; i < p.routes.size(); ++i) {
        SegmentIndex index(2.);
        index.add(p.routes[i].path(), 0);
        for (size_t j = i + 1; j < p.routes.size(); ++j)
            for (const Vec2d &x : resample(p.routes[j].path(), 0.2))
                best = std::min(best, index.distance(x, 5.));
    }
    return best;
}

// Smallest distance between parts of one route that are far apart along it. That catches a
// route that folds back onto itself (overlapping fibre).
double min_self_separation(const Route &r, double w)
{
    const Path2d pts  = resample(r.path(), 0.2);
    const size_t near = size_t(std::ceil(4. * w / 0.2));
    double best = 1e9;
    for (size_t i = 0; i < pts.size(); ++i)
        for (size_t j = i + near; j < pts.size(); ++j) {
            // A closed loop's end stops half a bead short of its own start on purpose.
            if (r.closed && i < near && j + near >= pts.size())
                continue;
            best = std::min(best, (pts[i] - pts[j]).norm());
        }
    return best;
}

double area_of_footprint(const LayerPlan &p)
{
    double a = 0.;
    for (const ExPolygon &e : p.fibre_footprint)
        a += e.area();
    return a * SCALING_FACTOR * SCALING_FACTOR;
}

} // namespace

TEST_CASE("Dubins paths reach the goal pose and are shortest", "[Fibreseek][Geometry]")
{
    SECTION("U-turn between parallel ribs two radii apart is a half circle")
    {
        const auto paths = dubins_paths({ 0., 0. }, 0., { 0., 2. }, M_PI, 1.);
        REQUIRE(!paths.empty());
        CHECK(paths.front().length() == Approx(M_PI).epsilon(1e-9));
        CHECK((sample_dubins(paths.front(), 0.1).back() - Vec2d(0., 2.)).norm() < 1e-6);
    }
    SECTION("Straight ahead is a straight line")
    {
        const auto paths = dubins_paths({ 0., 0. }, 0., { 10., 0. }, 0., 2.);
        REQUIRE(!paths.empty());
        CHECK(paths.front().length() == Approx(10.).epsilon(1e-9));
    }
    SECTION("Every pose pair has a path and every sample lands on its goal")
    {
        std::mt19937                           rng(12345);
        std::uniform_real_distribution<double> pos(-20., 20.), ang(-M_PI, M_PI), rad(0.5, 8.);
        for (int i = 0; i < 300; ++i) {
            const Vec2d  p0(pos(rng), pos(rng)), p1(pos(rng), pos(rng));
            const double h0 = ang(rng), h1 = ang(rng), r = rad(rng);
            const auto   paths = dubins_paths(p0, h0, p1, h1, r);
            REQUIRE(!paths.empty());
            for (const DubinsPath &path : paths) {
                const Path2d pts = sample_dubins(path, 0.2);
                REQUIRE((pts.back() - p1).norm() < 1e-5);
                REQUIRE(path_length(pts) <= path.length() + 1e-6);
                // A path is never shorter than the straight line between its ends.
                REQUIRE(path.length() >= (p1 - p0).norm() - 1e-9);
            }
            for (size_t k = 1; k < paths.size(); ++k)
                REQUIRE(paths[k - 1].length() <= paths[k].length());
        }
    }
}

TEST_CASE("Curvature and path helpers", "[Fibreseek][Geometry]")
{
    CHECK(circumradius({ 5., 0. }, { 0., 5. }, { -5., 0. }) == Approx(5.));
    CHECK(std::isinf(circumradius({ 0., 0. }, { 1., 0. }, { 2., 0. })));

    Path2d ring;
    for (int i = 0; i < 360; ++i)
        ring.push_back({ 10. * std::cos(i * M_PI / 180.), 10. * std::sin(i * M_PI / 180.) });
    const auto profile = radius_profile(ring, 0.5, true);
    CHECK(min_radius(profile) == Approx(10.).epsilon(0.02));

    const std::vector<double> cum = arc_lengths(ring, true);
    CHECK(cum.back() == Approx(2. * M_PI * 10.).epsilon(1e-3));
    const Path2d walked = walk_loop(ring, cum, cum.back() - 5., 20.); // wraps through the start
    CHECK(path_length(walked) == Approx(20.).epsilon(1e-6));
    CHECK((walked.front() - point_at(ring, cum, cum.back() - 5.)).norm() < 1e-9);

    SegmentIndex index(2.);
    index.add({ { 0., 0. }, { 10., 0. } }, 7);
    CHECK(index.distance({ 5., 3. }, 10.) == Approx(3.));
    CHECK(index.distance({ 5., 3. }, 10., 7) == Approx(10.)); // owner 7 ignored
    CHECK(index.crosses({ 5., -1. }, { 5., 1. }));
    CHECK_FALSE(index.crosses({ 5., 1. }, { 6., 2. }));
}

TEST_CASE("Derived settings follow the cut and bend physics", "[Fibreseek][Settings]")
{
    PlannerSettings s;
    // 58 mm cut distance + 7 mm anchor = the 65 mm standalone cut window (sec. 3.1).
    CHECK(s.min_route_length_mm(false) == Approx(65.));
    CHECK(s.min_route_length_mm(true) == Approx(65.));
    s.cut_distance_mm = 30.;
    CHECK(s.min_route_length_mm(false) == Approx(55.)); // the hardware floor wins
    s.wall_min_length_mm = 80.;
    CHECK(s.min_route_length_mm(true) == Approx(80.));

    PlannerSettings d;
    CHECK(d.wall_offset_mm() == Approx(2 * 0.45 + 0.4));
    CHECK(d.transition_length_mm() == Approx(std::sqrt(6. * 0.8 * 8.)));
    CHECK(d.spacing_mm() == Approx(0.8)); // solid pattern is fully dense
    d.pattern = RibPattern::Isogrid;
    d.mode    = ReinforcementMode::Medium;
    CHECK(d.spacing_mm() == Approx(2.0));
    d.infill_density_percent = 50.;
    CHECK(d.spacing_mm() == Approx(1.6));
    d.infill_spacing_mm = 3.;
    CHECK(d.spacing_mm() == Approx(3.));
    CHECK(d.angles_deg() == std::vector<double>{ 0., 60., 120. });
    CHECK(d.turn_radius_mm() == Approx(3.2)); // min(r_t, 4w) since s/2 is smaller
}

TEST_CASE("Settings and schedule are read from a print config by key", "[Fibreseek][Settings]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_key_value("fiber_line_width", new ConfigOptionFloat(1.0));
    config.set_key_value("fiber_cut_distance", new ConfigOptionFloat(60.));
    config.set_key_value("fiber_min_radius", new ConfigOptionFloat(6.));
    config.set_key_value("fiber_infill_angles", new ConfigOptionString("15, 105;-30"));
    config.set_key_value("fiber_seam_position", new ConfigOptionEnum<FiberSeamPosition>(FiberSeamPosition::Rear));
    config.set_key_value("fiber_infill_pattern", new ConfigOptionEnum<FiberInfillPattern>(FiberInfillPattern::Tetragrid));
    config.set_key_value("fiber_reinforcement_mode", new ConfigOptionEnum<FiberReinforcementMode>(FiberReinforcementMode::Heavy));
    config.set_key_value("fiber_start_layer", new ConfigOptionInt(4));

    const PlannerSettings s = settings_from_config(config, 0.5);
    CHECK(s.fibre_width_mm == Approx(1.0));
    CHECK(s.matrix_width_mm == Approx(0.5));
    CHECK(s.cut_distance_mm == Approx(60.));
    CHECK(s.bend_radius_mm == Approx(6.));
    CHECK(s.seam == SeamMode::Rear);
    CHECK(s.pattern == RibPattern::Tetragrid);
    CHECK(s.mode == ReinforcementMode::Heavy);
    CHECK(s.infill_angles_deg == std::vector<double>{ 15., 105., -30. });

    // An unset cut distance stays unset (0): the planner refuses it rather than guessing, and
    // PrintObject::plan_fibreseek_routes() stops the slice with an error naming the setting.
    const PlannerSettings unset = settings_from_config(DynamicPrintConfig::full_print_config());
    CHECK(unset.cut_distance_mm == 0.);
    CHECK_FALSE(unset.valid());

    // "4 starts fiber on layer 4 and leaves the last 4 layers without fiber"; heavy is every layer.
    const LayerSchedule sch = schedule_from_config(config, 0.2);
    CHECK_FALSE(sch.is_fibre_layer(2, 20)); // visible layer 3
    CHECK(sch.is_fibre_layer(3, 20));       // visible layer 4
    CHECK(sch.is_fibre_layer(15, 20));      // visible layer 16 = 20 - 4
    CHECK_FALSE(sch.is_fibre_layer(16, 20));
    CHECK(sch.ordinal(5) == 2);
}

TEST_CASE("A plain plate gets one closed, rounded, cut-safe wall loop", "[Fibreseek][Planner]")
{
    const PlannerSettings s = walls_only();
    const LayerPlan       p = plan(s, { plate(100., 40.) });
    REQUIRE(p.routes.size() == 1);
    const Route &r = p.routes.front();
    CHECK(r.kind == RouteKind::OuterWall);
    CHECK(r.closed);
    CHECK(r.void_crossings == 0);
    CHECK(r.length_mm >= s.min_route_length_mm(true));
    // Convex corners are opened to the 8 mm target, so there's no radius warning.
    CHECK(r.min_radius_mm >= 0.9 * s.bend_radius_mm);
    CHECK_FALSE(r.has_warning("below_min_bend_radius"));
    // The loop sits d0 inside the edge: a 97.4 x 37.4 rectangle with 8 mm corners.
    const double expected = 2. * (97.4 + 37.4) - (8. - 2. * M_PI) * 8.;
    CHECK(r.length_mm == Approx(expected - s.end_gap_widths * s.fibre_width_mm).epsilon(0.01));
    // Phases: start anchor first, after-cut tail exactly the last cut distance.
    REQUIRE(!r.phases.empty());
    CHECK(r.phases.front().phase == Phase::Start);
    CHECK(r.phases.back().phase == Phase::PostCutTail);
    CHECK(r.phases.back().from_mm == Approx(r.length_mm - s.cut_distance_mm));
    CHECK(r.cut_at_mm == Approx(r.length_mm - s.cut_distance_mm));
    for (size_t i = 1; i < r.phases.size(); ++i)
        CHECK(r.phases[i].from_mm == Approx(r.phases[i - 1].to_mm));
    // The footprint area is about length x width.
    CHECK(area_of_footprint(p) == Approx(r.length_mm * s.fibre_width_mm).epsilon(0.03));
}

TEST_CASE("Hole loops hug holes, spiral when short, and are skipped when no spiral is long enough", "[Fibreseek][Planner]")
{
    const PlannerSettings s = walls_only();

    SECTION("A 20 mm hole gets one closed loop with no radius warning")
    {
        const LayerPlan p = plan(s, { plate(100., 60., { { { 0., 0., 20. } } }) });
        size_t holes = 0;
        for (const Route &r : p.routes)
            if (r.kind == RouteKind::HoleWall) {
                ++holes;
                CHECK(r.closed);
                CHECK(r.length_mm == Approx(M_PI * (20. + 2. * s.wall_offset_mm()) - 0.4).epsilon(0.02));
                CHECK_FALSE(r.has_warning("below_min_bend_radius"));
            }
        CHECK(holes == 1);
    }
    SECTION("A 12 mm hole is too short for one loop and becomes a spiral")
    {
        const LayerPlan p = plan(s, { plate(100., 60., { { { 0., 0., 12. } } }) });
        const Route    *hole = nullptr;
        for (const Route &r : p.routes)
            if (r.kind == RouteKind::HoleWall)
                hole = &r;
        REQUIRE(hole != nullptr);
        CHECK_FALSE(hole->closed);
        CHECK(hole->has_warning("extended_turns_for_cut_window"));
        CHECK(hole->has_warning("below_min_bend_radius")); // r is about 7.3 mm, under the 8 mm target
        CHECK(hole->length_mm >= s.min_route_length_mm(true));
        CHECK(min_self_separation(*hole, s.fibre_width_mm) >= 0.9 * s.fibre_width_mm);
        size_t transitions = 0;
        for (const RoutePiece &piece : hole->pieces)
            transitions += piece.kind == PieceKind::Transition;
        CHECK(transitions >= 1);
    }
    SECTION("A 2 mm hole cannot reach the cut window even with extra turns")
    {
        const LayerPlan p = plan(s, { plate(100., 60., { { { 0., 0., 2. } } }) });
        for (const Route &r : p.routes)
            CHECK(r.kind != RouteKind::HoleWall);
        CHECK(p.diagnostics.count(SkipReason::BelowCutWindow) + p.diagnostics.count(SkipReason::BelowHardwareMinLength) == 1);
    }
}

TEST_CASE("A hole near the edge keeps its own loop and the edge loop keeps hugging the edge", "[Fibreseek][Planner]")
{
    // An 18 mm hole 9 mm from the edge leaves an 8.4 mm strip at loop level 0. Corner rounding
    // must not delete that strip and merge the hole into the outer loop (sec. 3.3).
    const PlannerSettings s = walls_only();
    const LayerPlan       p = plan(s, { plate(120., 80., { { { 0., 22., 18. } } }) });
    const Route *outer = nullptr, *hole = nullptr;
    for (const Route &r : p.routes)
        (r.kind == RouteKind::OuterWall ? outer : hole) = &r;
    REQUIRE(outer != nullptr);
    REQUIRE(hole != nullptr);
    CHECK(outer->closed);
    // Still a rounded rectangle, not a rectangle with a bite out of it.
    const double rounded_rect = 2. * (117.4 + 77.4) - (8. - 2. * M_PI) * 8.;
    CHECK(outer->length_mm == Approx(rounded_rect - 0.4).epsilon(0.01));
    CHECK(min_route_separation(p) >= 0.9 * s.fibre_width_mm);
}

TEST_CASE("Infill ribs are linked into cut-safe routes that keep clear of everything", "[Fibreseek][Planner]")
{
    PlannerSettings s;
    s.pattern = RibPattern::Rhombic;
    s.mode    = ReinforcementMode::Medium;
    const LayerPlan p = plan(s, { plate(120., 80., { { { -30., 0., 16. } }, { { 30., 0., 16. } } }) });
    size_t infill = 0;
    for (const Route &r : p.routes) {
        CHECK(r.length_mm >= s.min_route_length_mm(r.kind != RouteKind::Infill) - 1e-6);
        CHECK(r.void_crossings == 0);
        CHECK(min_self_separation(r, s.fibre_width_mm) >= 0.9 * s.fibre_width_mm);
        if (r.kind == RouteKind::Infill) {
            ++infill;
            for (const RoutePiece &piece : r.pieces)
                if (piece.kind == PieceKind::Connector)
                    CHECK(piece.min_radius_mm >= 0.9 * s.turn_floor_mm());
        }
    }
    CHECK(infill >= 1);
    CHECK(min_route_separation(p) >= 0.9 * s.fibre_width_mm);
    // Rib direction for ordinal 0 is the first rhombic angle, 45 degrees.
    for (const Route &r : p.routes)
        if (r.kind == RouteKind::Infill)
            for (const RoutePiece &piece : r.pieces)
                if (piece.kind == PieceKind::Rib) {
                    const Vec2d  d = piece.points.back() - piece.points.front();
                    const double a = std::fmod(std::atan2(d.y(), d.x()) + 2. * M_PI, M_PI); // direction, ignoring sense
                    CHECK(std::abs(a - M_PI / 4.) < 0.02);
                }
}

TEST_CASE("Routes never join separate islands", "[Fibreseek][Planner]")
{
    PlannerSettings s;
    s.mode    = ReinforcementMode::Heavy;
    s.pattern = RibPattern::Isogrid;
    ExPolygon left = plate(45., 45.), right = plate(45., 45.);
    left.translate(pt(-35., 0.));
    right.translate(pt(35., 0.));
    const LayerPlan p = plan(s, { left, right });
    REQUIRE(p.routes.size() >= 2);
    for (const Route &r : p.routes) {
        bool any_left = false, any_right = false;
        for (const Vec2d &x : r.path())
            (x.x() < 0. ? any_left : any_right) = true;
        CHECK(any_left != any_right);
        CHECK(r.void_crossings == 0);
    }
}

TEST_CASE("Fallbacks: invalid settings, narrow regions and the route cap", "[Fibreseek][Planner]")
{
    PlannerSettings bad;
    bad.fibre_width_mm = 0.;
    const LayerPlan none = plan(bad, { plate(100., 40.) });
    CHECK(none.routes.empty());
    REQUIRE(!none.diagnostics.notes.empty());

    const LayerPlan narrow = plan(walls_only(), { plate(100., 2.) });
    CHECK(narrow.routes.empty());
    CHECK(narrow.diagnostics.count(SkipReason::RegionTooNarrow) == 1);

    PlannerSettings capped = walls_only();
    capped.max_routes_per_layer = 1;
    const LayerPlan p = plan(capped, { plate(120., 80., { { { -30., 0., 20. } }, { { 30., 0., 20. } } }) });
    CHECK(p.routes.size() == 1);
    CHECK(p.routes.front().kind == RouteKind::OuterWall); // the longest one is kept
    CHECK(p.diagnostics.count(SkipReason::RouteCap) == 2);
}

TEST_CASE("Caller-supplied infill candidates replace generated ribs", "[Fibreseek][Planner]")
{
    PlannerSettings s;
    s.walls = false;
    LayerInput in;
    in.slices = { plate(100., 40.) };
    for (int i = -5; i <= 5; ++i)
        in.infill_candidates.push_back(Polyline(pt(-60., 3. * i), pt(60., 3. * i)));
    const LayerPlan p = RoutePlanner(s).plan_layer(in);
    CHECK(p.diagnostics.external_candidates > 0);
    REQUIRE(!p.routes.empty());
    for (const Route &r : p.routes)
        for (const RoutePiece &piece : r.pieces)
            if (piece.kind == PieceKind::Rib)
                CHECK(std::abs(piece.points.front().y() - piece.points.back().y()) < 1e-6); // still horizontal
}

TEST_CASE("Seam modes pick the requested start", "[Fibreseek][Planner]")
{
    PlannerSettings s = walls_only();
    s.seam = SeamMode::Rear;
    const LayerPlan rear = plan(s, { plate(100., 40.) });
    REQUIRE(rear.routes.size() == 1);
    CHECK(rear.routes.front().start().y() > 15.);
    s.seam           = SeamMode::Aligned;
    s.seam_angle_deg = 180.;
    const LayerPlan aligned = plan(s, { plate(100., 40.) });
    CHECK(aligned.routes.front().start().x() < -40.);
}

TEST_CASE("Finish-ironing tails start at the route end", "[Fibreseek][Planner]")
{
    PlannerSettings s = walls_only();
    s.finish_ironing_mm = 12.;
    const LayerPlan p = plan(s, { plate(100., 40.) });
    REQUIRE(p.routes.size() == 1);
    const Route &r = p.routes.front();
    REQUIRE(r.tail.size() >= 2);
    CHECK((r.tail.front() - r.end()).norm() < 1e-9);
    CHECK(r.tail_length_mm == Approx(12.).epsilon(1e-3));
    // A closed loop's tail crosses its own start.
    CHECK((r.tail.back() - r.start()).norm() < 12.);
}

// The expressions below are the ones GCode::extrude_fiberseek_composite_routes_for_layer()
// and its summary helpers run on each route. Running them on the adapter's output shows the
// adapter fits the writer, and that the writer's own cut placement lands on the planner's
// PostCutTail boundary.
namespace {

template<class LayerRoutes> struct WriterProbe
{
    size_t emitted { 0 };
    bool   ok { true };
    void   run(const LayerRoutes &diagnostic, double cut_distance_mm)
    {
        (void) diagnostic.region_id;
        for (const auto &route : diagnostic.routes) {
            if (route.ordered_segments.empty() || !route.is_release_safe()) {
                ok = false;
                continue;
            }
            double fiber_length_mm = 0.0, matrix_mm = 0.0, before_tail = 0.0;
            bool   tail_seen = false;
            for (const auto &segment : route.planned_segments) {
                if (segment.emits_fiber && segment.fiber_mm_per_mm > 0.0)
                    fiber_length_mm += segment.length_mm;
                if (segment.emits_plastic && segment.plastic_mm_per_mm > 0.0)
                    matrix_mm += segment.length_mm;
                if (segment.phase == WriterRoutePhase::PostCutTail)
                    tail_seen = true;
                if (!tail_seen)
                    before_tail += segment.length_mm;
                ok = ok && segment.polyline.points.size() >= 2;
            }
            const double cut_at_mm = std::max(0.0, fiber_length_mm - cut_distance_mm);
            ok = ok && tail_seen && std::abs(cut_at_mm - before_tail) < 1e-3;
            ok = ok && std::abs(fiber_length_mm - route.length_mm) < 1e-6 && std::abs(matrix_mm - route.matrix_positive_length_mm) < 1e-6;
            ok = ok && std::abs(fiber_length_mm - route.fiber_positive_length_mm) < 1e-6;
            ok = ok && route.candidate_ids.size() >= 1 && route.coalesce_transition_length_mm >= 0.0 && route.tail_length_mm >= 0.0;
            (void) route.layer_id;
            (void) route.id;
            (void) route.unsupported_void_crossing_count;
            (void) route.warnings.empty();
            (void) route.tail_segments;
            (void) to_string(route.shape);
            ++emitted;
        }
    }
};

} // namespace

TEST_CASE("Writer adapter output fits the writer's route contract", "[Fibreseek][Writer]")
{
    PlannerSettings s;
    s.mode              = ReinforcementMode::Medium;
    s.pattern           = RibPattern::Isogrid;
    s.finish_ironing_mm = 5.;
    const LayerPlan         p  = plan(s, { plate(120., 80., { { { -30., 0., 16. } }, { { 30., 0., 12. } } }) });
    const WriterLayerRoutes wr = to_writer_routes(p, 3, s.hardware_min_length_mm);
    REQUIRE(wr.routes.size() == p.routes.size());
    CHECK(wr.region_id == 3);
    WriterProbe<WriterLayerRoutes> probe;
    probe.run(wr, s.cut_distance_mm);
    CHECK(probe.ok);
    CHECK(probe.emitted == p.routes.size());
    for (size_t i = 0; i < wr.routes.size(); ++i) {
        CHECK(wr.routes[i].layer_id == p.layer_id);
        CHECK(wr.routes[i].ordered_segments.size() == p.routes[i].pieces.size());
        CHECK(wr.routes[i].shape == writer_shape(p.routes[i]));
        if (!wr.routes[i].tail_segments.empty()) {
            // The writer irons only a tail that starts within 0.05 mm of the nozzle.
            const Point end = wr.routes[i].planned_segments.back().polyline.points.back();
            CHECK((end - wr.routes[i].tail_segments.front().points.front()).cast<double>().norm() * SCALING_FACTOR < 0.05);
        }
    }
}

TEST_CASE("Plastic is trimmed only under the routes the writer lays", "[Fibreseek][Writer]")
{
    // Three straight routes. The writer skips one that leaves the part and one shorter than the
    // hardware minimum, so the trim footprint must leave their plastic alone.
    const auto line_route = [](double y, double length_mm) {
        WriterRouteSegment seg;
        seg.polyline.points = { pt(0., y), pt(length_mm, y) };
        seg.length_mm       = length_mm;
        WriterRoute route;
        route.planned_segments.push_back(seg);
        route.length_mm              = length_mm;
        route.hardware_min_length_mm = 68.;
        return route;
    };
    WriterLayerRoutes routes;
    routes.routes.push_back(line_route(0., 100.));
    routes.routes.push_back(line_route(10., 100.));
    routes.routes.back().unsupported_void_crossing_count = 1;
    routes.routes.push_back(line_route(20., 50.));
    REQUIRE(routes.routes[0].is_release_safe());

    const ExPolygons footprint = laid_fibre_footprint(routes, 0.8);
    const auto covered = [&footprint](double x, double y) {
        for (const ExPolygon &e : footprint)
            if (e.contains(pt(x, y)))
                return true;
        return false;
    };
    CHECK(covered(50., 0.));
    CHECK(covered(50., 0.35));      // inside the 0.8 mm bead
    CHECK_FALSE(covered(50., 0.45)); // outside it
    CHECK_FALSE(covered(50., 10.));  // leaves the part: skipped
    CHECK_FALSE(covered(25., 20.));  // too short: skipped
    CHECK(laid_fibre_footprint(WriterLayerRoutes{}, 0.8).empty());
}
