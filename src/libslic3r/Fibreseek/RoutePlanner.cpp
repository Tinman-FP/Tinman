#include "RoutePlanner.hpp"

#include "../BoundingBox.hpp"
#include "../ClipperUtils.hpp"
#include "../PrintConfig.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <limits>
#include <optional>

// Clean-room FibreSeek route planner. Section numbers refer to
// docs/fibreseek-route-planner-spec.md.

namespace Slic3r {
namespace Fibreseek {

namespace {

constexpr double kInf            = std::numeric_limits<double>::infinity();
constexpr double kArcToleranceMm = 0.01; // round-join tolerance for every offset
// Measuring a polygonised arc reads up to a few percent tight: the chord sagitta over the
// 4 mm curvature window. A radius counts as below target only when it is under 95% of it.
constexpr double kRadiusTolerance = 0.95;

Vec2d to_mm(const Point &p) { return Vec2d(double(p.x()) * SCALING_FACTOR, double(p.y()) * SCALING_FACTOR); }
Point to_scaled(const Vec2d &p) { return Point(coord_t(std::llround(scale_(p.x()))), coord_t(std::llround(scale_(p.y())))); }

Path2d to_path(const Points &points)
{
    Path2d out;
    out.reserve(points.size());
    for (const Point &p : points)
        out.push_back(to_mm(p));
    return out;
}

Polyline to_polyline(const Path2d &path)
{
    Polyline out;
    out.points.reserve(path.size());
    for (const Vec2d &p : path) {
        const Point q = to_scaled(p);
        if (out.points.empty() || out.points.back() != q)
            out.points.push_back(q);
    }
    return out;
}

void append_path(Path2d &dst, const Path2d &src)
{
    for (const Vec2d &p : src)
        if (dst.empty() || (dst.back() - p).squaredNorm() > 1e-18)
            dst.push_back(p);
}

Path2d reversed(Path2d p)
{
    std::reverse(p.begin(), p.end());
    return p;
}

double start_heading(const Path2d &p)
{
    for (size_t i = 1; i < p.size(); ++i)
        if ((p[i] - p[0]).squaredNorm() > 1e-12)
            return std::atan2(p[i].y() - p[0].y(), p[i].x() - p[0].x());
    return 0.;
}

double end_heading(const Path2d &p)
{
    for (size_t i = p.size() - 1; i-- > 0;)
        if ((p.back() - p[i]).squaredNorm() > 1e-12)
            return std::atan2(p.back().y() - p[i].y(), p.back().x() - p[i].x());
    return 0.;
}

ExPolygons inset(const ExPolygons &area, double mm)
{
    return offset_ex(area, float(-scale_(mm)), jtRound, scale_(kArcToleranceMm));
}

ExPolygons open_by(const ExPolygons &area, double radius_mm)
{
    return opening_ex(area, float(scale_(radius_mm)), jtRound, scale_(kArcToleranceMm));
}

double area_of(const ExPolygons &a)
{
    double sum = 0.;
    for (const ExPolygon &e : a)
        sum += e.area();
    return sum;
}

uint64_t splitmix64(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

// Fillets each convex corner of a polygon (a left turn, with material on the left in the
// stored orientation) with radius up to `r`. A fillet's tangent length is at most half of
// either neighbouring edge, so neighbouring fillets never overlap. A fillet that would
// come within `clearance` of another polygon of the same level is halved until it fits.
// Polygonised curves pass through unchanged: their small turns on short edges already sit
// at the curve's own radius.
Polygon fillet_polygon(const Polygon &poly, double r, double clearance, double step, const SegmentIndex &others, int own)
{
    const Path2d p = to_path(poly.points);
    const size_t n = p.size();
    if (n < 3 || r <= 0.)
        return poly;
    Path2d out;
    for (size_t i = 0; i < n; ++i) {
        const Vec2d &a = p[(i + n - 1) % n], &b = p[i], &c = p[(i + 1) % n];
        const double la = (b - a).norm(), lc = (c - b).norm();
        const double turn = (la > 1e-9 && lc > 1e-9) ?
            std::atan2((b - a).x() * (c - b).y() - (b - a).y() * (c - b).x(), (b - a).dot(c - b)) : 0.;
        Path2d arc;
        if (turn > 0.05) {
            const Vec2d u = (b - a) / la;
            for (double t = std::min({ r * std::tan(0.5 * turn), 0.5 * la, 0.5 * lc }); t > 0.02 && arc.empty(); t *= 0.5) {
                const double rho   = t / std::tan(0.5 * turn);
                const Vec2d  A     = b - t * u;
                const Vec2d  C     = A + rho * Vec2d(-u.y(), u.x());
                const double a0    = std::atan2(A.y() - C.y(), A.x() - C.x());
                const size_t steps = std::max<size_t>(1, size_t(std::ceil(rho * turn / step)));
                for (size_t k = 0; k <= steps; ++k) {
                    const double ang = a0 + turn * double(k) / double(steps);
                    const Vec2d  x   = C + rho * Vec2d(std::cos(ang), std::sin(ang));
                    if (others.distance(x, clearance, own) < clearance) {
                        arc.clear();
                        break;
                    }
                    arc.push_back(x);
                }
            }
        }
        if (arc.empty())
            out.push_back(b);
        else
            append_path(out, arc);
    }
    Polygon res;
    for (const Vec2d &x : out) {
        const Point q = to_scaled(x);
        if (res.points.empty() || res.points.back() != q)
            res.points.push_back(q);
    }
    if (res.points.size() > 1 && res.points.front() == res.points.back())
        res.points.pop_back();
    return res.points.size() >= 3 ? res : poly;
}

// Sec. 3.3: shapes one level of wall loops.
//  1. An opening by w/2 drops any strip narrower than one bead, since a loop around it would
//     overlap itself.
//  2. Convex corners are filleted locally up to the bend target. Unlike a morphological
//     opening, a fillet never deletes the strip between a hole and the edge, so the edge
//     loop keeps hugging the edge and the hole keeps its own loop.
//  3. Narrow protrusions the strand cannot follow at the target radius, such as gear teeth
//     or thin tabs, are removed by opening the outer contour alone, ignoring holes, and then
//     taking the holes out again. The largest radius (down from the target) that keeps the
//     piece's topology (one piece, same holes) wins. Holes take no part in the opening: an
//     opening of the whole piece pinches the necks between holes into cusps.
ExPolygons round_convex_corners(const ExPolygons &level, const PlannerSettings &s)
{
    const double w      = s.fibre_width_mm;
    ExPolygons   pieces = open_by(level, 0.5 * w);
    if (s.bend_radius_mm <= 0.5 * w || pieces.empty())
        return pieces;

    SegmentIndex index(std::max(2., 2. * w));
    int          id = 0;
    for (const ExPolygon &e : pieces) {
        index.add(to_path(e.contour.points), id++, true);
        for (const Polygon &h : e.holes)
            index.add(to_path(h.points), id++, true);
    }
    const double step = std::min(s.chord_mm, 0.5);
    id = 0;
    for (ExPolygon &e : pieces) {
        e.contour = fillet_polygon(e.contour, s.bend_radius_mm, w, step, index, id++);
        for (Polygon &h : e.holes)
            h = fillet_polygon(h, s.bend_radius_mm, w, step, index, id++);
    }

    ExPolygons out;
    for (ExPolygon &piece : pieces) {
        for (double f : { 1., 0.7, 0.5, 0.35, 0.25 }) {
            const double radius = s.bend_radius_mm * f;
            if (radius <= 0.5 * w)
                break;
            ExPolygons contour = open_by({ ExPolygon(piece.contour) }, radius);
            if (contour.size() != 1)
                continue;
            ExPolygons opened = open_by(diff_ex(contour, piece.holes), 0.5 * w);
            if (opened.size() == 1 && opened.front().holes.size() == piece.holes.size() && opened.front().area() >= 0.5 * piece.area()) {
                piece = std::move(opened.front());
                break;
            }
        }
        out.push_back(std::move(piece));
    }
    return out;
}

// --- Wall loops (sec. 4.1) ------------------------------------------------------------

struct Loop
{
    Path2d                    pts; // CCW, mm, first point not repeated
    std::vector<double>       cum;
    double                    length { 0. };
    std::vector<RadiusSample> radii;
    Polygon                   poly; // scaled, for containment tests
    int                       level { 0 };
    bool                      hole { false };
    size_t                    island { 0 };
    size_t                    id { 0 };
    std::vector<size_t>       parents, children;
    int                       next { -1 }; // one-to-one link to the next level
    bool                      linked_from_parent { false };
};

struct Draft
{
    RouteKind                kind { RouteKind::Infill };
    bool                     closed { false };
    std::vector<RoutePiece>  pieces;
    std::vector<size_t>      candidate_ids;
    std::vector<std::string> warnings;
    double                   transition_length { 0. };
    size_t                   island { 0 };

    double length() const
    {
        double l = 0.;
        for (const RoutePiece &p : pieces)
            l += p.length_mm;
        return l;
    }
    Vec2d start() const { return pieces.front().points.front(); }
    Vec2d end() const { return pieces.back().points.back(); }
    void  reverse()
    {
        std::reverse(pieces.begin(), pieces.end());
        for (RoutePiece &p : pieces)
            std::reverse(p.points.begin(), p.points.end());
    }
    Path2d path() const
    {
        Path2d out;
        for (const RoutePiece &p : pieces)
            append_path(out, p.points);
        return out;
    }
};

RoutePiece make_piece(PieceKind kind, size_t candidate, Path2d points, double step)
{
    RoutePiece piece;
    piece.kind          = kind;
    piece.candidate_id  = candidate;
    piece.length_mm     = path_length(points);
    piece.min_radius_mm = min_radius(radius_profile(points, step));
    piece.points        = std::move(points);
    return piece;
}

class LayerPlanner
{
public:
    LayerPlanner(const PlannerSettings &s, const LayerInput &in, LayerPlan &plan) : s(s), in(in), plan(plan) {}
    void run();

private:
    const PlannerSettings &s;
    const LayerInput      &in;
    LayerPlan             &plan;

    ExPolygons          islands;
    std::vector<Loop>   loops;
    std::vector<Draft>  wall_drafts;
    std::vector<Draft>  infill_drafts;
    size_t              next_candidate { 0 };

    void   build_loops();
    void   plan_walls();
    void   plan_infill(size_t island);
    double preferred_seam(const Loop &loop, const Vec2d &hint) const;
    double refine_seam(const Loop &loop, double s_pref) const;
    std::vector<Draft> build_spiral(const std::vector<const Loop *> &chain, double seam) const;
    void   skip(RouteKind kind, double length, const Vec2d &where);
    Route  finalize(Draft &&draft, size_t id, const ExPolygons &bead_area) const;
};

void LayerPlanner::skip(RouteKind kind, double length, const Vec2d &where)
{
    const SkipReason reason = length < s.hardware_min_length_mm ? SkipReason::BelowHardwareMinLength : SkipReason::BelowCutWindow;
    plan.diagnostics.skipped.push_back({ kind, reason, length, where });
}

void LayerPlanner::build_loops()
{
    const double w        = s.fibre_width_mm;
    const int    max_used = std::max(s.outer_loops, s.hole_loops);
    if (!s.walls || max_used <= 0)
        return;
    const int levels = max_used + std::max(0, s.max_extra_turns);
    const double step = s.sample_step_mm();

    for (size_t island = 0; island < islands.size(); ++island) {
        // E_0 = round(inset(island, d0)), E_{k+1} = round(inset(E_k, w)). Each level is cut
        // from the level before it, so loops of different levels stay at least w apart.
        ExPolygons level = round_convex_corners(inset({ islands[island] }, s.wall_offset_mm()), s);
        if (level.empty()) {
            plan.diagnostics.skipped.push_back({ RouteKind::OuterWall, SkipReason::RegionTooNarrow, 0., to_mm(islands[island].contour.points.front()) });
            continue;
        }
        const size_t first = loops.size();
        for (int k = 0; k < levels && !level.empty(); ++k) {
            auto add = [&](Polygon poly, bool hole) {
                poly.make_counter_clockwise();
                Loop loop;
                loop.pts    = to_path(poly.points);
                loop.cum    = arc_lengths(loop.pts, true);
                loop.length = loop.cum.back();
                loop.radii  = radius_profile(loop.pts, step, true);
                loop.poly   = std::move(poly);
                loop.level  = k;
                loop.hole   = hole;
                loop.island = island;
                loop.id     = next_candidate++;
                if (loop.length > 0.)
                    loops.push_back(std::move(loop));
            };
            for (const ExPolygon &e : level) {
                if (k < s.outer_loops + s.max_extra_turns && s.outer_loops > 0)
                    add(e.contour, false);
                if (k < s.hole_loops + s.max_extra_turns && s.hole_loops > 0)
                    for (const Polygon &h : e.holes)
                        add(h, true);
            }
            level = round_convex_corners(inset(level, w), s);
        }

        // Link each loop to its next-level child where the relation is one-to-one. An outer
        // loop's child lies inside it. A hole loop's child is the bigger hole loop around it.
        for (size_t i = first; i < loops.size(); ++i)
            for (size_t j = first; j < loops.size(); ++j) {
                Loop &child = loops[i], &parent = loops[j];
                if (child.level != parent.level + 1 || child.hole != parent.hole)
                    continue;
                const bool nested = child.hole ? child.poly.contains(parent.poly.points.front())
                                               : parent.poly.contains(child.poly.points.front());
                if (nested) {
                    child.parents.push_back(j);
                    parent.children.push_back(i);
                }
            }
        for (size_t i = first; i < loops.size(); ++i) {
            Loop &parent = loops[i];
            if (parent.children.size() == 1 && loops[parent.children.front()].parents.size() == 1) {
                parent.next = int(parent.children.front());
                loops[parent.next].linked_from_parent = true;
            }
        }
    }
    for (const Loop &l : loops)
        if (l.level < (l.hole ? s.hole_loops : s.outer_loops))
            ++plan.diagnostics.loop_candidates;
}

double LayerPlanner::preferred_seam(const Loop &loop, const Vec2d &hint) const
{
    size_t best = 0;
    switch (s.seam) {
    case SeamMode::Nearest: return project(loop.pts, loop.cum, hint, true).s;
    case SeamMode::Random:
        return double(splitmix64((uint64_t(in.layer_id) << 32) ^ uint64_t(loop.id) ^ uint64_t(in.fibre_layer_ordinal) << 16) >> 11) /
               double(1ULL << 53) * loop.length;
    case SeamMode::Aligned: {
        const Vec2d dir(std::cos(s.seam_angle_deg * M_PI / 180.), std::sin(s.seam_angle_deg * M_PI / 180.));
        for (size_t i = 1; i < loop.pts.size(); ++i)
            if (loop.pts[i].dot(dir) > loop.pts[best].dot(dir))
                best = i;
        break;
    }
    case SeamMode::Rear:
        for (size_t i = 1; i < loop.pts.size(); ++i)
            if (loop.pts[i].y() > loop.pts[best].y())
                best = i;
        break;
    case SeamMode::Source:
        // The planner's own default: the front-left point, which is stable and easy to find.
        for (size_t i = 1; i < loop.pts.size(); ++i)
            if (loop.pts[i].x() + loop.pts[i].y() < loop.pts[best].x() + loop.pts[best].y())
                best = i;
        break;
    }
    return loop.cum[best];
}

// Sec. 4.1: moves the seam up to 10 mm to the straightest nearby spot. The spiral step uses
// the T before the seam and the anchor uses the a after it, so both want low curvature.
double LayerPlanner::refine_seam(const Loop &loop, double s_pref) const
{
    const double window = std::min(10., loop.length / 4.);
    const double T      = std::min(s.transition_length_mm(), 0.25 * loop.length);
    double best_s = s_pref, best_score = kInf;
    for (double d = -window; d <= window + 1e-9; d += std::max(0.5, window / 10.)) {
        const double r       = min_radius_in(loop.radii, s_pref + d - T, s_pref + d + s.anchor_length_mm, loop.length);
        const double penalty = (std::isfinite(r) && r > 0.) ? std::max(0., s.bend_radius_mm / r - 1.) : (r > 0. ? 0. : 1e3);
        const double score   = 10. * penalty + 0.05 * std::abs(d);
        if (score < best_score) {
            best_score = score;
            best_s     = s_pref + d;
        }
    }
    return best_s;
}

// Sec. 3.5 and 4.1: turns a chain of nested loops into one spiral route. Each loop is laid
// for its perimeter minus T, then a smoothstep blend moves sideways onto the next loop.
// When a blend fails (a projection jump at a concave spot), the chain splits there.
std::vector<Draft> LayerPlanner::build_spiral(const std::vector<const Loop *> &chain, double seam) const
{
    std::vector<Draft> out;
    const double w    = s.fibre_width_mm;
    const double step = s.sample_step_mm();
    const RouteKind kind = chain.front()->hole ? RouteKind::HoleWall : RouteKind::OuterWall;

    auto new_draft = [&]() {
        Draft d;
        d.kind              = kind;
        d.island            = chain.front()->island;
        d.transition_length = s.transition_length_mm();
        return d;
    };

    Draft  cur = new_draft();
    double pos = seam;
    for (size_t j = 0; j < chain.size(); ++j) {
        const Loop  &L    = *chain[j];
        const double T    = std::min(s.transition_length_mm(), 0.25 * L.length);
        const bool   last = j + 1 == chain.size();
        cur.candidate_ids.push_back(L.id);
        if (chain.size() == 1) {
            // A single closed loop stops half a bead short of its start (sec. 1, end gap).
            cur.pieces.push_back(make_piece(PieceKind::Loop, L.id, walk_loop(L.pts, L.cum, pos, L.length - s.end_gap_widths * w), step));
            cur.closed = true;
            break;
        }
        cur.pieces.push_back(make_piece(PieceKind::Loop, L.id, walk_loop(L.pts, L.cum, pos, L.length - T), step));
        if (last)
            break;

        const Loop &N = *chain[j + 1];
        const size_t n = std::max<size_t>(8, size_t(std::ceil(T / (0.25 * w))));
        Path2d blend;
        bool   ok = true;
        for (size_t i = 0; i <= n; ++i) {
            const double     u  = double(i) / double(n);
            const double     b  = u * u * (3. - 2. * u);
            const Vec2d      a  = point_at(L.pts, L.cum, pos - T + u * T);
            const Projection pr = project(N.pts, N.cum, a, true);
            if (pr.distance > 3. * w)
                ok = false;
            blend.push_back(a + b * (pr.point - a));
        }
        const Projection landing = project(N.pts, N.cum, point_at(L.pts, L.cum, pos), true);
        blend.back() = landing.point;
        RoutePiece transition = make_piece(PieceKind::Transition, N.id, std::move(blend), step);
        if (!ok || transition.min_radius_mm < s.turn_floor_mm()) {
            cur.warnings.push_back("spiral_split");
            out.push_back(std::move(cur));
            cur = new_draft();
            cur.warnings.push_back("spiral_split");
            pos = landing.s;
            if (j + 2 == chain.size()) {
                // The remaining loop stands alone, so it is closed like any single loop.
                cur.candidate_ids.push_back(N.id);
                cur.pieces.push_back(make_piece(PieceKind::Loop, N.id, walk_loop(N.pts, N.cum, pos, N.length - s.end_gap_widths * w), step));
                cur.closed = true;
                break;
            }
            continue;
        }
        cur.pieces.push_back(std::move(transition));
        pos = landing.s;
    }
    if (!cur.pieces.empty())
        out.push_back(std::move(cur));
    return out;
}

void LayerPlanner::plan_walls()
{
    // Families start at a requested level with no one-to-one parent. They are built in
    // nearest-neighbour order, so "nearest" seams and travel both follow the nozzle.
    std::vector<size_t> starts;
    for (size_t i = 0; i < loops.size(); ++i) {
        const Loop &l = loops[i];
        if (!l.linked_from_parent && l.level < (l.hole ? s.hole_loops : s.outer_loops))
            starts.push_back(i);
    }
    const double L_min = s.min_route_length_mm(true);
    Vec2d        nozzle(0., 0.);
    while (!starts.empty()) {
        size_t best = 0;
        double best_d = kInf;
        for (size_t k = 0; k < starts.size(); ++k) {
            const Loop &l = loops[starts[k]];
            const double d = project(l.pts, l.cum, nozzle, true).distance;
            if (d < best_d) {
                best_d = d;
                best   = k;
            }
        }
        const Loop &first = loops[starts[best]];
        starts.erase(starts.begin() + best);

        const int requested = first.hole ? s.hole_loops : s.outer_loops;
        std::vector<const Loop *> chain { &first };
        std::deque<const Loop *>  reserve;
        for (const Loop *l = &first; l->next >= 0;) {
            l = &loops[l->next];
            (l->level < requested ? (void) chain.push_back(l) : (void) reserve.push_back(l));
            if (l->level >= requested + s.max_extra_turns - 1)
                break;
        }

        const double seam   = refine_seam(first, preferred_seam(first, nozzle));
        std::vector<Draft> drafts = build_spiral(chain, seam);
        bool extended = false;
        // Sec. 5, fallback 2: a short family gets extra turns from its reserve levels.
        while (drafts.size() == 1 && drafts.front().length() < L_min && !reserve.empty()) {
            chain.push_back(reserve.front());
            reserve.pop_front();
            extended = true;
            drafts   = build_spiral(chain, seam);
        }
        for (Draft &d : drafts) {
            if (d.length() < L_min) {
                skip(d.kind, d.length(), d.start());
                continue;
            }
            if (extended)
                d.warnings.push_back("extended_turns_for_cut_window");
            nozzle = d.end();
            wall_drafts.push_back(std::move(d));
        }
    }
}

// --- Infill ribs (sec. 4.2) -----------------------------------------------------------

struct Rib
{
    Path2d pts;
    long   row { 0 };
    double along { 0. };
    bool   used { false };
    size_t id { 0 };
};

struct Connector
{
    Path2d pts;
    double score { kInf };
    double radius { 0. };
};

bool paths_cross(const Path2d &a, const Path2d &b, size_t skip_a_front, size_t skip_a_back)
{
    for (size_t i = skip_a_front; i + 1 + skip_a_back < a.size(); ++i)
        for (size_t j = 0; j + 1 < b.size(); ++j) {
            const Vec2d &p = a[i], &q = a[i + 1], &r = b[j], &t = b[j + 1];
            const double d1 = (q - p).x() * (r - p).y() - (q - p).y() * (r - p).x();
            const double d2 = (q - p).x() * (t - p).y() - (q - p).y() * (t - p).x();
            const double d3 = (t - r).x() * (p - r).y() - (t - r).y() * (p - r).x();
            const double d4 = (t - r).x() * (q - r).y() - (t - r).y() * (q - r).x();
            if (((d1 > 0.) != (d2 > 0.)) && ((d3 > 0.) != (d4 > 0.)))
                return true;
        }
    return false;
}

// True when no point of `conn` lies within `clearance` of `rib` beside it, that is, at a
// closest point other than the junction end where the two meet.
bool stays_ahead_of(const Path2d &conn, const Path2d &rib, const Vec2d &junction, double clearance)
{
    const std::vector<double> cum = arc_lengths(rib);
    for (const Vec2d &x : resample(conn, 0.1)) {
        const Projection pr = project(rib, cum, x, false);
        if (pr.distance < clearance && (pr.point - junction).norm() > 1e-6)
            return false;
    }
    return true;
}

void LayerPlanner::plan_infill(size_t island)
{
    const double w = s.fibre_width_mm;
    const double h = s.turn_radius_mm();

    // F: where rib and connector centrelines may go. R: where ribs may go, which is F
    // without the headland h used by turns.
    ExPolygons F = inset({ islands[island] }, s.wall_offset_mm() + (s.walls ? 0. : s.infill_inset_mm));
    Polylines  wall_lines;
    for (const Draft &d : wall_drafts)
        if (d.island == island)
            wall_lines.push_back(to_polyline(d.path()));
    if (!wall_lines.empty())
        F = diff_ex(F, offset(wall_lines, float(scale_(s.rib_clearance_mm())), jtRound, scale_(kArcToleranceMm), etOpenRound));
    if (s.nozzle_contact_radius_mm > 0.5 * w)
        F = open_by(F, s.nozzle_contact_radius_mm - 0.5 * w);
    const ExPolygons R = inset(F, h);
    if (R.empty())
        return;

    // Candidate ribs: generated lines, or the caller's own paths when it supplies any.
    std::vector<Rib> ribs;
    const double     min_rib = std::max(3., 2. * w);
    auto add_rib = [&](Path2d pts, long row, double along) {
        if (path_length(pts) < min_rib)
            return;
        Rib r;
        r.pts   = std::move(pts);
        r.row   = row;
        r.along = along;
        r.id    = next_candidate++;
        ribs.push_back(std::move(r));
    };
    if (!in.infill_candidates.empty()) {
        Polylines clipped = intersection_pl(in.infill_candidates, R);
        plan.diagnostics.external_candidates += clipped.size();
        std::sort(clipped.begin(), clipped.end(), [](const Polyline &a, const Polyline &b) {
            const BoundingBox ba = a.bounding_box(), bb = b.bounding_box();
            return ba.min.y() != bb.min.y() ? ba.min.y() < bb.min.y() : ba.min.x() < bb.min.x();
        });
        long row = 0;
        for (const Polyline &pl : clipped)
            add_rib(to_path(pl.points), row++, to_mm(pl.points.front()).x());
    } else {
        const std::vector<double> angles = s.angles_deg();
        const double theta   = angles[in.fibre_layer_ordinal % angles.size()] * M_PI / 180.;
        const double spacing = s.spacing_mm();
        // Each repeat of an angle moves the grid half a spacing, so ribs at the same angle
        // on different layers sit between each other rather than on top.
        const double phase = double((in.fibre_layer_ordinal / angles.size()) % 2) * 0.5 * spacing;
        ExPolygons rotated = R;
        for (ExPolygon &e : rotated)
            e.rotate(-theta);
        const BoundingBox bb = get_extents(rotated);
        const double y0 = bb.min.y() * SCALING_FACTOR, y1 = bb.max.y() * SCALING_FACTOR;
        Polylines lines;
        for (long j = long(std::ceil((y0 - phase) / spacing)); double(j) * spacing + phase <= y1; ++j) {
            const coord_t y = coord_t(scale_(double(j) * spacing + phase));
            lines.push_back(Polyline(Point(coord_t(bb.min.x() - scale_(1.)), y), Point(coord_t(bb.max.x() + scale_(1.)), y)));
        }
        Polylines clipped = intersection_pl(lines, rotated);
        for (Polyline &pl : clipped) {
            if (pl.points.front().x() > pl.points.back().x())
                pl.reverse();
            const long   row   = std::lround((double(pl.points.front().y()) * SCALING_FACTOR - phase) / spacing);
            const double along = double(pl.points.front().x()) * SCALING_FACTOR;
            pl.rotate(theta);
            add_rib(to_path(pl.points), row, along);
        }
    }
    plan.diagnostics.rib_candidates += ribs.size();
    if (ribs.empty())
        return;

    // Obstacles: every rib (owner = rib index), every wall route and each accepted connector.
    // The boundary index holds F's edges; connectors may not cross them.
    SegmentIndex obstacles(std::max(2., 2. * w)), boundary(2.);
    for (size_t i = 0; i < ribs.size(); ++i)
        obstacles.add(ribs[i].pts, int(i));
    for (const Draft &d : wall_drafts)
        if (d.island == island)
            obstacles.add(d.path(), -1);
    for (const ExPolygon &e : F) {
        boundary.add(to_path(e.contour.points), 0, true);
        for (const Polygon &hole : e.holes)
            boundary.add(to_path(hole.points), 0, true);
    }
    int next_owner = int(ribs.size());

    const double clearance = 0.95 * w;
    const double search    = 2. * h + 4. * s.spacing_mm() + 2.;
    auto connect = [&](const Vec2d &p, double hp, const Rib &from, const Path2d &to, int to_idx, double bound) -> Connector {
        Connector    best;
        const Vec2d &q       = to.front();
        const double hq      = start_heading(to);
        const double lateral = std::abs(std::cos(hp) * (q - p).y() - std::sin(hp) * (q - p).x());
        std::vector<double> radii { h };
        const double tight = std::max(s.turn_floor_mm(), 0.5 * lateral);
        if (tight < h - 1e-6)
            radii.push_back(tight);
        for (double radius : radii) {
            const double penalty = s.tight_turn_penalty_mm * std::max(0., s.bend_radius_mm / radius - 1.);
            const std::vector<DubinsPath> paths = dubins_paths(p, hp, q, hq, radius);
            for (size_t k = 0; k < std::min<size_t>(3, paths.size()); ++k) {
                if (paths[k].length() + penalty >= std::min(bound, best.score))
                    break; // sorted by length: nothing later can win
                Path2d pts = sample_dubins(paths[k], std::min(s.chord_mm, 0.5 * w));
                bool   ok  = !paths_cross(pts, from.pts, 1, 1) && !paths_cross(pts, to, 1, 1);
                for (size_t i = 0; ok && i + 1 < pts.size(); ++i)
                    ok = !boundary.crosses(pts[i], pts[i + 1]);
                if (ok)
                    for (const Vec2d &x : resample(pts, 0.25 * w))
                        if (obstacles.distance(x, clearance, int(&from - ribs.data()), to_idx) < clearance) {
                            ok = false;
                            break;
                        }
                // The two linked ribs are left out of the clearance test above, since every
                // turn starts and ends on them. The connector must still neither cut back
                // across them nor run alongside them: near a linked rib, its closest point on
                // that rib has to be the junction end itself.
                ok = ok && stays_ahead_of(pts, from.pts, p, clearance) && stays_ahead_of(pts, to, q, clearance);
                if (!ok)
                    continue;
                const double score = paths[k].length() + penalty;
                if (score < best.score) {
                    best.pts    = std::move(pts);
                    best.score  = score;
                    best.radius = radius;
                }
                break;
            }
        }
        return best;
    };

    std::vector<size_t> order(ribs.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return ribs[a].row != ribs[b].row ? ribs[a].row < ribs[b].row : ribs[a].along < ribs[b].along;
    });

    const double L_min = s.min_route_length_mm(false);
    const double step  = s.sample_step_mm();
    for (size_t start : order) {
        if (ribs[start].used)
            continue;
        ribs[start].used = true;
        Draft d;
        d.kind   = RouteKind::Infill;
        d.island = island;
        d.pieces.push_back(make_piece(PieceKind::Rib, ribs[start].id, ribs[start].pts, step));
        d.candidate_ids.push_back(ribs[start].id);
        size_t tail_rib = start;
        // Sec. 4.2: grow the chain forwards, then reverse it and grow from the other end.
        for (int pass = 0; pass < 2; ++pass) {
            for (;;) {
                const Vec2d  p  = d.end();
                const double hp = end_heading(d.pieces.back().points);
                // Nearest entries first. A connector is never shorter than the straight line
                // to its entry, so once that distance exceeds the best score, stop.
                struct Entry { size_t rib; bool rev; double dist; };
                std::vector<Entry> entries;
                for (size_t r = 0; r < ribs.size(); ++r)
                    if (!ribs[r].used)
                        for (bool rev : { false, true }) {
                            const double dist = ((rev ? ribs[r].pts.back() : ribs[r].pts.front()) - p).norm();
                            if (dist <= search)
                                entries.push_back({ r, rev, dist });
                        }
                std::sort(entries.begin(), entries.end(), [](const Entry &l, const Entry &r) { return l.dist < r.dist; });
                Connector best;
                size_t    best_rib = 0;
                bool      best_rev = false;
                for (const Entry &e : entries) {
                    if (e.dist >= best.score)
                        break;
                    Connector c = connect(p, hp, ribs[tail_rib], e.rev ? reversed(ribs[e.rib].pts) : ribs[e.rib].pts, int(e.rib), best.score);
                    if (c.score < best.score) {
                        best     = std::move(c);
                        best_rib = e.rib;
                        best_rev = e.rev;
                    }
                }
                if (!std::isfinite(best.score))
                    break;
                obstacles.add(best.pts, next_owner++);
                d.pieces.push_back(make_piece(PieceKind::Connector, ribs[best_rib].id, std::move(best.pts), step));
                ribs[best_rib].used = true;
                d.pieces.push_back(make_piece(PieceKind::Rib, ribs[best_rib].id, best_rev ? reversed(ribs[best_rib].pts) : ribs[best_rib].pts, step));
                d.candidate_ids.push_back(ribs[best_rib].id);
                tail_rib = best_rib;
            }
            if (pass == 0) {
                d.reverse();
                tail_rib = start;
            }
        }
        if (d.length() < L_min) {
            skip(RouteKind::Infill, d.length(), d.start());
            continue;
        }
        infill_drafts.push_back(std::move(d));
    }
}

// --- Phases, warnings and tails (sec. 3.2, 4.4) ---------------------------------------

// The Normal / SlowTurn labels between the start anchor and the after-cut tail. Each
// curvature sample owns the bin between the midpoints to its neighbours, so the bins tile
// [from, to] with no gaps. Fragments are then merged: a Normal gap under 2 mm between tight
// turns joins them, and a tight spot under 1 mm is dropped. Each span becomes a writer
// segment, so the result stays short and has no zero-length slivers.
std::vector<PhaseSpan> phase_middle(const std::vector<RadiusSample> &profile, double from, double to, double tight_below)
{
    std::vector<PhaseSpan> spans;
    auto add = [&spans](Phase phase, double a, double b) {
        if (b <= a + 1e-9)
            return;
        if (!spans.empty() && spans.back().phase == phase)
            spans.back().to_mm = b;
        else
            spans.push_back({ phase, a, b });
    };
    if (to <= from + 1e-9)
        return spans;
    double cursor = from;
    for (size_t i = 0; i < profile.size(); ++i) {
        const double hi = i + 1 < profile.size() ? 0.5 * (profile[i].s + profile[i + 1].s) : to;
        const double b  = std::min(hi, to);
        if (b > cursor) {
            add(profile[i].radius < tight_below ? Phase::SlowTurn : Phase::Normal, cursor, b);
            cursor = b;
        }
    }
    add(Phase::Normal, cursor, to);

    auto relabel = [&spans](Phase phase, Phase into, double shorter_than, bool only_between) {
        for (size_t i = 0; i < spans.size(); ++i)
            if (spans[i].phase == phase && spans[i].to_mm - spans[i].from_mm < shorter_than &&
                (!only_between || (i > 0 && i + 1 < spans.size())))
                spans[i].phase = into;
        std::vector<PhaseSpan> merged;
        for (const PhaseSpan &sp : spans)
            if (!merged.empty() && merged.back().phase == sp.phase)
                merged.back().to_mm = sp.to_mm;
            else
                merged.push_back(sp);
        spans = std::move(merged);
    };
    relabel(Phase::Normal, Phase::SlowTurn, 2., true);
    relabel(Phase::SlowTurn, Phase::Normal, 1., false);
    return spans;
}


Route LayerPlanner::finalize(Draft &&d, size_t id, const ExPolygons &bead_area) const
{
    Route r;
    r.id                   = id;
    r.kind                 = d.kind;
    r.closed               = d.closed;
    r.candidate_ids        = std::move(d.candidate_ids);
    r.warnings             = std::move(d.warnings);
    r.transition_length_mm = d.kind == RouteKind::Infill ? 0. : d.transition_length;
    r.pieces               = std::move(d.pieces);

    const Path2d              path    = r.path();
    const std::vector<double> cum     = arc_lengths(path);
    const std::vector<RadiusSample> profile = radius_profile(path, s.sample_step_mm());
    r.length_mm     = cum.back();
    r.cut_at_mm     = std::max(0., r.length_mm - s.cut_distance_mm);
    r.min_radius_mm = min_radius(profile);

    const double start_end = std::min(r.length_mm, std::max(s.slow_length_mm, s.anchor_length_mm));
    const double tail_from = std::max(start_end, r.cut_at_mm);
    r.phases.push_back({ Phase::Start, 0., start_end });
    append(r.phases, phase_middle(profile, start_end, tail_from, kRadiusTolerance * s.bend_radius_mm));
    if (r.length_mm > tail_from + 1e-9)
        r.phases.push_back({ Phase::PostCutTail, tail_from, r.length_mm });

    if (r.min_radius_mm < kRadiusTolerance * s.bend_radius_mm)
        r.warnings.push_back("below_min_bend_radius");
    if (min_radius_in(profile, tail_from, r.length_mm) < kRadiusTolerance * s.bend_radius_mm)
        r.warnings.push_back("tight_turn_in_post_cut_tail");

    if (s.finish_ironing_mm > 0.) {
        if (r.closed) {
            // Continue across the end gap and over the start, pressing the joint.
            const double gap = (path.front() - path.back()).norm();
            r.tail = { path.back() };
            if (s.finish_ironing_mm <= gap)
                r.tail.push_back(path.back() + (path.front() - path.back()) * (s.finish_ironing_mm / std::max(gap, 1e-9)));
            else
                append_path(r.tail, sub_path(path, cum, 0., s.finish_ironing_mm - gap));
        } else
            r.tail = reversed(sub_path(path, cum, std::max(0., r.length_mm - s.finish_ironing_mm), r.length_mm));
        r.tail_length_mm = path_length(r.tail);
    }

    // Every bead centre must lie at least 0.45 w inside the slice, so the bead stays on the part.
    for (const Vec2d &x : resample(path, 0.5)) {
        const Point p = to_scaled(x);
        bool inside = false;
        for (const ExPolygon &e : bead_area)
            if (e.contains(p)) {
                inside = true;
                break;
            }
        if (!inside)
            ++r.void_crossings;
    }
    return r;
}

void LayerPlanner::run()
{
    std::string why;
    if (!s.valid(&why)) {
        plan.diagnostics.notes.push_back("invalid_settings: " + why);
        return;
    }
    islands = union_ex(in.slices);
    if (islands.empty())
        return;

    build_loops();
    plan_walls();
    if (s.infill)
        for (size_t i = 0; i < islands.size(); ++i)
            plan_infill(i);

    // Sec. 4.3: walls first, then infill in nearest-neighbour order. An infill chain may be
    // laid either way round.
    std::vector<Draft> ordered = std::move(wall_drafts);
    Vec2d nozzle = ordered.empty() ? Vec2d(0., 0.) : ordered.back().end();
    while (!infill_drafts.empty()) {
        size_t best = 0;
        bool   rev  = false;
        double best_d = kInf;
        for (size_t i = 0; i < infill_drafts.size(); ++i)
            for (bool r : { false, true }) {
                const double dd = ((r ? infill_drafts[i].end() : infill_drafts[i].start()) - nozzle).norm();
                if (dd < best_d) {
                    best_d = dd;
                    best   = i;
                    rev    = r;
                }
            }
        if (rev)
            infill_drafts[best].reverse();
        nozzle = infill_drafts[best].end();
        ordered.push_back(std::move(infill_drafts[best]));
        infill_drafts.erase(infill_drafts.begin() + best);
    }

    // Sec. 5, fallback 6: keep the longest routes and preserve their order.
    if (s.max_routes_per_layer > 0 && ordered.size() > size_t(s.max_routes_per_layer)) {
        std::vector<size_t> idx(ordered.size());
        for (size_t i = 0; i < idx.size(); ++i)
            idx[i] = i;
        std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return ordered[a].length() > ordered[b].length(); });
        std::vector<bool> keep(ordered.size(), false);
        for (size_t i = 0; i < size_t(s.max_routes_per_layer); ++i)
            keep[idx[i]] = true;
        std::vector<Draft> kept;
        for (size_t i = 0; i < ordered.size(); ++i) {
            if (keep[i])
                kept.push_back(std::move(ordered[i]));
            else
                plan.diagnostics.skipped.push_back({ ordered[i].kind, SkipReason::RouteCap, ordered[i].length(), ordered[i].start() });
        }
        ordered = std::move(kept);
    }

    const ExPolygons bead_area = inset(islands, 0.45 * s.fibre_width_mm);
    Polylines        footprint_lines;
    for (Draft &d : ordered) {
        plan.routes.push_back(finalize(std::move(d), plan.routes.size(), bead_area));
        footprint_lines.push_back(to_polyline(plan.routes.back().path()));
    }
    if (!footprint_lines.empty())
        plan.fibre_footprint = union_ex(offset(footprint_lines, float(scale_(0.5 * s.fibre_width_mm)), jtRound,
                                               scale_(kArcToleranceMm), etOpenRound));
}

// --- Config access by key (sec. 1) ----------------------------------------------------

double float_option(const DynamicPrintConfig &c, const char *key, double fallback)
{
    const ConfigOption *o = c.option(key);
    if (o == nullptr)
        return fallback;
    switch (o->type()) {
    case coFloat:
    case coPercent: return o->getFloat();
    case coInt: return double(o->getInt());
    case coFloats:
        if (const auto *v = dynamic_cast<const ConfigOptionFloats *>(o); v && !v->values.empty())
            return v->values[v->values.size() > 1 ? 1 : 0]; // per-filament: the fibre lane is slot 2 when present
        return fallback;
    default: return fallback;
    }
}

int int_option(const DynamicPrintConfig &c, const char *key, int fallback)
{
    const ConfigOption *o = c.option(key);
    if (o == nullptr)
        return fallback;
    switch (o->type()) {
    case coInt:
    case coEnum: return o->getInt();
    case coBool: return o->getBool() ? 1 : 0;
    default: return fallback;
    }
}

std::string string_option(const DynamicPrintConfig &c, const char *key)
{
    const ConfigOption *o = c.option(key);
    if (o == nullptr || o->type() != coString)
        return {};
    return static_cast<const ConfigOptionString *>(o)->value;
}

} // namespace

// --- PlannerSettings ------------------------------------------------------------------

double PlannerSettings::min_route_length_mm(bool wall) const
{
    double l = std::max({ hardware_min_length_mm, cut_distance_mm + anchor_length_mm, user_min_length_mm });
    return wall ? std::max(l, wall_min_length_mm) : l;
}

double PlannerSettings::wall_offset_mm() const
{
    const double gap = wall_inset_override_mm > 0. ? wall_inset_override_mm : double(std::max(0, plastic_loops_outside)) * matrix_width_mm;
    return gap + 0.5 * fibre_width_mm;
}

double PlannerSettings::rib_clearance_mm() const
{
    return fibre_width_mm + double(std::max(0, plastic_loops_inside)) * matrix_width_mm + std::max(0., infill_inset_mm);
}

double PlannerSettings::transition_length_mm() const
{
    // Smoothstep lateral shift w over T has peak curvature 6 w / T^2, so T = sqrt(6 w r_t)
    // keeps the step at the bend target on straight sections.
    return std::max(2. * fibre_width_mm, std::sqrt(6. * fibre_width_mm * std::max(0., bend_radius_mm)));
}

double PlannerSettings::spacing_mm() const
{
    double sp = 0.;
    if (infill_spacing_mm > 0.)
        sp = infill_spacing_mm;
    else if (infill_density_percent > 0.)
        sp = fibre_width_mm / std::min(1., infill_density_percent / 100.);
    else if (pattern == RibPattern::Solid)
        sp = fibre_width_mm;
    else
        sp = fibre_width_mm * (mode == ReinforcementMode::Light ? 4. : mode == ReinforcementMode::Medium ? 2.5 : 1.5);
    return std::max(sp, fibre_width_mm);
}

double PlannerSettings::turn_radius_mm() const
{
    return std::max({ 0.5 * spacing_mm(), std::min(bend_radius_mm, headland_cap_widths * fibre_width_mm), turn_floor_mm() });
}

double PlannerSettings::sample_step_mm() const { return std::max(0.5, 0.5 * chord_mm); }

std::vector<double> PlannerSettings::angles_deg() const
{
    if (!infill_angles_deg.empty())
        return infill_angles_deg;
    switch (pattern) {
    case RibPattern::Solid: return { 0., 90. };
    case RibPattern::Rhombic: return { 45., 135. };
    case RibPattern::Isogrid: return { 0., 60., 120. };
    case RibPattern::Anisogrid: return { 30., 150., 90. };
    case RibPattern::Tetragrid: return { 0., 45., 90., 135. };
    }
    return { 0. };
}

bool PlannerSettings::valid(std::string *why) const
{
    auto fail = [why](const char *msg) {
        if (why)
            *why = msg;
        return false;
    };
    if (!(fibre_width_mm > 0.))
        return fail("fibre width must be > 0");
    if (matrix_width_mm < 0.)
        return fail("matrix width must be >= 0");
    if (!(cut_distance_mm > 0.))
        return fail("cut distance must be > 0");
    if (hardware_min_length_mm < 0. || anchor_length_mm < 0.)
        return fail("minimum lengths must be >= 0");
    if (bend_radius_mm < 0.)
        return fail("bend radius must be >= 0");
    if (!(chord_mm > 0.))
        return fail("chord must be > 0");
    return true;
}

PlannerSettings settings_from_config(const DynamicPrintConfig &c, double matrix_width_mm)
{
    PlannerSettings s;
    auto positive = [](double v, double fallback) { return v > 0. ? v : fallback; };
    s.fibre_width_mm         = positive(float_option(c, "fiber_line_width", s.fibre_width_mm), s.fibre_width_mm);
    s.matrix_width_mm        = matrix_width_mm;
    s.plastic_loops_outside  = int_option(c, "fiber_plastic_outer_loops_with_fiber", s.plastic_loops_outside);
    s.plastic_loops_inside   = int_option(c, "fiber_plastic_inner_loops_with_fiber", s.plastic_loops_inside);
    s.wall_inset_override_mm = float_option(c, "fiber_perimeter_inset", 0.);
    s.infill_inset_mm        = float_option(c, "fiber_infill_inset", 0.);
    s.walls                  = int_option(c, "fiber_generate_perimeters", 1) != 0;
    s.infill                 = int_option(c, "fiber_generate_infill", 1) != 0;
    s.outer_loops            = int_option(c, "fiber_outer_perimeter_loops", s.outer_loops);
    s.hole_loops             = int_option(c, "fiber_inner_perimeter_loops", s.hole_loops);
    s.cut_distance_mm        = float_option(c, "fiber_cut_distance", 0.); // the printer profile's; 0 = not set, invalid
    s.hardware_min_length_mm = float_option(c, "fiber_mechanical_min_route_length", s.hardware_min_length_mm);
    s.wall_min_length_mm     = float_option(c, "fiber_perimeter_min_route_length", s.wall_min_length_mm);
    s.user_min_length_mm     = float_option(c, "fiber_min_route_length", 0.);
    s.bend_radius_mm         = float_option(c, "fiber_min_radius", s.bend_radius_mm);
    s.chord_mm               = positive(float_option(c, "fiber_max_arc_segment_length", s.chord_mm), s.chord_mm);
    s.infill_spacing_mm      = float_option(c, "fiber_infill_spacing", 0.);
    s.infill_density_percent = float_option(c, "fiber_infill_density", 0.);
    s.nozzle_contact_radius_mm = float_option(c, "fiber_nozzle_contact_radius", 0.);
    s.seam_angle_deg         = float_option(c, "fiber_seam_angle", 0.);
    s.max_routes_per_layer   = int_option(c, "fiber_max_routes_per_layer", 0);
    s.slow_length_mm         = float_option(c, "fiber_slow_length", 0.);
    s.finish_ironing_mm      = float_option(c, "fiber_finish_ironing_distance", 0.);

    switch (FiberReinforcementMode(int_option(c, "fiber_reinforcement_mode", int(FiberReinforcementMode::Light)))) {
    case FiberReinforcementMode::Light: s.mode = ReinforcementMode::Light; break;
    case FiberReinforcementMode::Medium: s.mode = ReinforcementMode::Medium; break;
    case FiberReinforcementMode::Heavy: s.mode = ReinforcementMode::Heavy; break;
    }
    switch (FiberInfillPattern(int_option(c, "fiber_infill_pattern", int(FiberInfillPattern::Solid)))) {
    case FiberInfillPattern::Solid: s.pattern = RibPattern::Solid; break;
    case FiberInfillPattern::Rhombic: s.pattern = RibPattern::Rhombic; break;
    case FiberInfillPattern::Isogrid: s.pattern = RibPattern::Isogrid; break;
    case FiberInfillPattern::Anisogrid: s.pattern = RibPattern::Anisogrid; break;
    case FiberInfillPattern::Tetragrid: s.pattern = RibPattern::Tetragrid; break;
    }
    switch (FiberSeamPosition(int_option(c, "fiber_seam_position", int(FiberSeamPosition::Source)))) {
    case FiberSeamPosition::Source: s.seam = SeamMode::Source; break;
    case FiberSeamPosition::Nearest: s.seam = SeamMode::Nearest; break;
    case FiberSeamPosition::Aligned: s.seam = SeamMode::Aligned; break;
    case FiberSeamPosition::Rear: s.seam = SeamMode::Rear; break;
    case FiberSeamPosition::Random: s.seam = SeamMode::Random; break;
    }

    // Comma (or space / semicolon) separated degrees; anything unparsable is ignored.
    const std::string angles = string_option(c, "fiber_infill_angles");
    for (const char *p = angles.c_str(); *p;) {
        char        *end = nullptr;
        const double v   = std::strtod(p, &end);
        if (end == p) {
            ++p;
            continue;
        }
        if (std::isfinite(v))
            s.infill_angles_deg.push_back(v);
        p = end;
    }
    return s;
}

// --- LayerSchedule --------------------------------------------------------------------

bool LayerSchedule::is_fibre_layer(size_t layer_index, size_t layer_count) const
{
    const long visible = long(layer_index) + 1;
    if (visible < first_visible_layer || visible > long(layer_count) - top_free_layers)
        return false;
    return (visible - first_visible_layer) % std::max(1, step) == 0;
}

size_t LayerSchedule::ordinal(size_t layer_index) const
{
    const long visible = long(layer_index) + 1;
    return visible < first_visible_layer ? 0 : size_t((visible - first_visible_layer) / std::max(1, step));
}

LayerSchedule schedule_from_config(const DynamicPrintConfig &c, double layer_height_mm)
{
    LayerSchedule sch;
    const int start = int_option(c, "fiber_start_layer", 0);
    sch.first_visible_layer = start > 0 ? start : 2;
    sch.top_free_layers     = start > 0 ? start : 1;
    const int    step  = int_option(c, "fiber_layer_step", 0);
    const double macro = float_option(c, "fiber_macro_layer_height", 0.);
    if (step > 0)
        sch.step = step;
    else if (macro > 0. && layer_height_mm > 0.)
        sch.step = std::max(1, int(std::lround(macro / layer_height_mm)));
    else
        switch (FiberReinforcementMode(int_option(c, "fiber_reinforcement_mode", 0))) {
        case FiberReinforcementMode::Light: sch.step = 3; break;
        case FiberReinforcementMode::Medium: sch.step = 2; break;
        case FiberReinforcementMode::Heavy: sch.step = 1; break;
        }
    return sch;
}

// --- Small accessors ------------------------------------------------------------------

const char *to_string(RouteKind kind)
{
    switch (kind) {
    case RouteKind::OuterWall: return "outer_wall";
    case RouteKind::HoleWall: return "hole_wall";
    case RouteKind::Infill: return "infill";
    }
    return "unknown";
}

const char *to_string(PieceKind kind)
{
    switch (kind) {
    case PieceKind::Loop: return "loop";
    case PieceKind::Transition: return "transition";
    case PieceKind::Rib: return "rib";
    case PieceKind::Connector: return "connector";
    }
    return "unknown";
}

const char *to_string(Phase phase)
{
    switch (phase) {
    case Phase::Start: return "start";
    case Phase::Normal: return "normal";
    case Phase::SlowTurn: return "slow_turn";
    case Phase::PostCutTail: return "post_cut_tail";
    }
    return "unknown";
}

const char *to_string(SkipReason reason)
{
    switch (reason) {
    case SkipReason::RegionTooNarrow: return "region_too_narrow";
    case SkipReason::BelowHardwareMinLength: return "below_hardware_min_length";
    case SkipReason::BelowCutWindow: return "below_cut_window";
    case SkipReason::RouteCap: return "route_cap";
    case SkipReason::Degenerate: return "degenerate";
    }
    return "unknown";
}

Path2d Route::path() const
{
    Path2d out;
    for (const RoutePiece &p : pieces)
        append_path(out, p.points);
    return out;
}

bool Route::has_warning(const std::string &w) const { return std::find(warnings.begin(), warnings.end(), w) != warnings.end(); }

size_t LayerDiagnostics::count(SkipReason reason) const
{
    return size_t(std::count_if(skipped.begin(), skipped.end(), [reason](const SkippedCandidate &c) { return c.reason == reason; }));
}

double LayerPlan::total_length_mm() const
{
    double l = 0.;
    for (const Route &r : routes)
        l += r.length_mm;
    return l;
}

LayerPlan RoutePlanner::plan_layer(const LayerInput &input) const
{
    LayerPlan plan;
    plan.layer_id = input.layer_id;
    plan.print_z  = input.print_z;
    LayerPlanner(m_settings, input, plan).run();
    return plan;
}

} // namespace Fibreseek
} // namespace Slic3r
