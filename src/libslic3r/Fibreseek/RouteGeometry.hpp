#pragma once

// Geometry helpers for the clean-room FibreSeek route planner.
//
// Everything here works in millimetres on unscaled doubles (Vec2d). The planner converts
// to and from Slic3r's scaled integer coordinates only at its edges (Clipper offsets in,
// Polylines out). See docs/fibreseek-route-planner-spec.md.

#include "../Point.hpp"

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Slic3r {
namespace Fibreseek {

// An ordered list of points in mm. Open paths keep both ends; closed loops do not repeat
// the first point at the end.
using Path2d = std::vector<Vec2d>;

double path_length(const Path2d &path, bool closed = false);

// Cumulative arc length at each point. For a closed loop the result has one extra entry:
// the full perimeter, at the implied closing point.
std::vector<double> arc_lengths(const Path2d &path, bool closed = false);

// Point at arc length s along an open path, clamped to its ends. `cum` comes from arc_lengths().
Vec2d point_at(const Path2d &path, const std::vector<double> &cum, double s);

// The part of an open path between arc lengths s0 <= s1, end points included.
Path2d sub_path(const Path2d &path, const std::vector<double> &cum, double s0, double s1);

// The same path with points spaced evenly at about `spacing`. Both end points are kept.
Path2d resample(const Path2d &path, double spacing);

// Walks a closed loop forward (in its point order) from arc length `start` for `length`
// mm (at most one perimeter) and returns that open path.
Path2d walk_loop(const Path2d &loop, const std::vector<double> &cum, double start, double length);

struct Projection
{
    double s { 0. };        // arc length of the closest point
    Vec2d  point { 0., 0. };
    double distance { 0. };
};
// Closest point on a path (closed when `closed`) to p.
Projection project(const Path2d &path, const std::vector<double> &cum, const Vec2d &p, bool closed);

// Radius of the circle through three points; +infinity when they are collinear.
double circumradius(const Vec2d &a, const Vec2d &b, const Vec2d &c);

struct RadiusSample
{
    double s;      // arc length of the sample
    double radius; // local radius of curvature (mm)
};
// Local curvature radius along a path. The path is resampled at `spacing`, and each sample's
// radius is the circumradius of it and the samples `half_window` mm before and after it.
// That averages curvature over about 4 mm, roughly the length over which the tow's stiffness
// acts. It also keeps polygon vertices from reading as kinks: with 0.5 mm neighbours, an
// 8 mm arc drawn as 0.8 mm chords reads as 5 mm. Closed loops wrap around.
std::vector<RadiusSample> radius_profile(const Path2d &path, double spacing, bool closed = false, double half_window = 2.);
double min_radius(const std::vector<RadiusSample> &profile);
// Smallest radius among samples with s in [from, to]. On a closed loop of perimeter
// `perimeter` the range may wrap past the end.
double min_radius_in(const std::vector<RadiusSample> &profile, double from, double to, double perimeter = 0.);

// Shortest paths between two poses with a bounded turning radius (Dubins 1957), in the
// closed forms catalogued by Shkel & Lumelsky (2001) and LaValle (2006, sec. 15.3).
enum class DubinsWord { LSL, RSR, LSR, RSL, RLR, LRL };
struct DubinsPath
{
    DubinsWord            word { DubinsWord::LSL };
    double                radius { 1. };
    std::array<double, 3> params { 0., 0., 0. }; // per piece: turn angle (rad) or straight length / radius
    Vec2d                 start { 0., 0. };
    double                heading { 0. };
    double                length() const { return (params[0] + params[1] + params[2]) * radius; }
};
// Every Dubins word that reaches (p1, h1) from (p0, h0) at `radius`, shortest first. Each
// candidate is checked by integrating it, so a word whose closed form does not land on the
// goal is dropped.
std::vector<DubinsPath> dubins_paths(const Vec2d &p0, double h0, const Vec2d &p1, double h1, double radius);
// Points along a Dubins path, with arc pieces split so no step is longer than `step`.
Path2d sample_dubins(const DubinsPath &path, double step);

// A uniform grid of line segments, for "how close is this point to other fibre" and
// "does this segment cross the region boundary" queries.
class SegmentIndex
{
public:
    explicit SegmentIndex(double cell_mm = 2.);
    void add(const Path2d &path, int owner, bool closed = false);
    // Smallest distance from p to a segment whose owner is neither `ignore_a` nor `ignore_b`.
    // Returns `limit` when nothing lies within `limit`.
    double distance(const Vec2d &p, double limit, int ignore_a = INT32_MIN, int ignore_b = INT32_MIN) const;
    // True when segment ab properly crosses any stored segment.
    bool crosses(const Vec2d &a, const Vec2d &b) const;

private:
    struct Segment { Vec2d a, b; int owner; };
    int64_t key(int64_t ix, int64_t iy) const { return (ix << 32) ^ (iy & 0xffffffff); }
    int64_t cell(double v) const;
    void    insert(const Vec2d &a, const Vec2d &b, int owner);

    double                                        m_cell;
    std::vector<Segment>                          m_segments;
    std::unordered_map<int64_t, std::vector<int>> m_grid;
};

} // namespace Fibreseek
} // namespace Slic3r
