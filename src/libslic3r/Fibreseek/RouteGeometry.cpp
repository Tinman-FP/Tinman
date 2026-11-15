#include "RouteGeometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Slic3r {
namespace Fibreseek {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

double cross2(const Vec2d &a, const Vec2d &b) { return a.x() * b.y() - a.y() * b.x(); }

double mod2pi(double x)
{
    const double two_pi = 2. * M_PI;
    x -= two_pi * std::floor(x / two_pi);
    return x >= two_pi ? 0. : x;
}

// Vertex i of a path, wrapping around for closed loops.
const Vec2d &vertex(const Path2d &path, size_t i) { return path[i % path.size()]; }

// Index of the segment containing arc length s (cum[i] <= s <= cum[i+1]).
size_t segment_at(const std::vector<double> &cum, double s)
{
    auto it = std::upper_bound(cum.begin(), cum.end(), s);
    size_t i = it == cum.begin() ? 0 : size_t(it - cum.begin()) - 1;
    return std::min(i, cum.size() >= 2 ? cum.size() - 2 : 0);
}

double point_segment_distance(const Vec2d &p, const Vec2d &a, const Vec2d &b, double *t_out = nullptr)
{
    const Vec2d  ab = b - a;
    const double l2 = ab.squaredNorm();
    double       t  = l2 > 0. ? std::clamp((p - a).dot(ab) / l2, 0., 1.) : 0.;
    if (t_out)
        *t_out = t;
    return (a + t * ab - p).norm();
}

bool segments_cross(const Vec2d &a, const Vec2d &b, const Vec2d &c, const Vec2d &d)
{
    const double d1 = cross2(b - a, c - a), d2 = cross2(b - a, d - a);
    const double d3 = cross2(d - c, a - c), d4 = cross2(d - c, b - c);
    return ((d1 > 0.) != (d2 > 0.)) && ((d3 > 0.) != (d4 > 0.));
}

} // namespace

double path_length(const Path2d &path, bool closed)
{
    double l = 0.;
    for (size_t i = 1; i < path.size(); ++i)
        l += (path[i] - path[i - 1]).norm();
    if (closed && path.size() > 2)
        l += (path.front() - path.back()).norm();
    return l;
}

std::vector<double> arc_lengths(const Path2d &path, bool closed)
{
    std::vector<double> cum;
    cum.reserve(path.size() + 1);
    double l = 0.;
    for (size_t i = 0; i < path.size(); ++i) {
        if (i > 0)
            l += (path[i] - path[i - 1]).norm();
        cum.push_back(l);
    }
    if (closed && !path.empty())
        cum.push_back(l + (path.front() - path.back()).norm());
    return cum;
}

Vec2d point_at(const Path2d &path, const std::vector<double> &cum, double s)
{
    if (path.empty())
        return Vec2d::Zero();
    if (path.size() == 1 || cum.back() <= 0.)
        return path.front();
    const bool closed = cum.size() == path.size() + 1;
    if (closed) {
        s = std::fmod(s, cum.back());
        if (s < 0.)
            s += cum.back();
    } else
        s = std::clamp(s, 0., cum.back());
    const size_t i   = segment_at(cum, s);
    const double len = cum[i + 1] - cum[i];
    const double t   = len > 0. ? (s - cum[i]) / len : 0.;
    return vertex(path, i) + t * (vertex(path, i + 1) - vertex(path, i));
}

Path2d sub_path(const Path2d &path, const std::vector<double> &cum, double s0, double s1)
{
    Path2d out;
    if (path.empty())
        return out;
    s0 = std::clamp(s0, 0., cum.back());
    s1 = std::clamp(s1, s0, cum.back());
    out.push_back(point_at(path, cum, s0));
    for (size_t i = 0; i < path.size(); ++i)
        if (cum[i] > s0 + 1e-9 && cum[i] < s1 - 1e-9)
            out.push_back(path[i]);
    out.push_back(point_at(path, cum, s1));
    return out;
}

Path2d resample(const Path2d &path, double spacing)
{
    const std::vector<double> cum = arc_lengths(path);
    if (path.size() < 2 || cum.back() <= 0. || spacing <= 0.)
        return path;
    const size_t n    = std::max<size_t>(1, size_t(std::lround(cum.back() / spacing)));
    const double step = cum.back() / double(n);
    Path2d       out;
    out.reserve(n + 1);
    size_t seg = 0;
    for (size_t k = 0; k <= n; ++k) {
        const double s = k == n ? cum.back() : double(k) * step;
        while (seg + 2 < cum.size() && cum[seg + 1] < s)
            ++seg;
        const double len = cum[seg + 1] - cum[seg];
        const double t   = len > 0. ? std::clamp((s - cum[seg]) / len, 0., 1.) : 0.;
        out.push_back(path[seg] + t * (path[seg + 1] - path[seg]));
    }
    return out;
}

Path2d walk_loop(const Path2d &loop, const std::vector<double> &cum, double start, double length)
{
    Path2d out;
    if (loop.size() < 2)
        return out;
    const double perimeter = cum.back();
    length = std::clamp(length, 0., perimeter);
    start  = std::fmod(start, perimeter);
    if (start < 0.)
        start += perimeter;
    out.push_back(point_at(loop, cum, start));
    const size_t n = loop.size();
    const size_t i = segment_at(cum, start);
    for (size_t k = 1; k <= n; ++k) {
        const size_t j   = i + k;
        const double pos = j <= n ? cum[j] : cum[j - n] + perimeter;
        if (pos >= start + length - 1e-9)
            break;
        if (pos > start + 1e-9)
            out.push_back(vertex(loop, j));
    }
    out.push_back(point_at(loop, cum, start + length));
    return out;
}

Projection project(const Path2d &path, const std::vector<double> &cum, const Vec2d &p, bool closed)
{
    Projection best;
    best.distance = kInf;
    const size_t segments = closed ? path.size() : (path.size() > 0 ? path.size() - 1 : 0);
    for (size_t i = 0; i < segments; ++i) {
        const Vec2d &a = vertex(path, i), &b = vertex(path, i + 1);
        double       t = 0.;
        const double d = point_segment_distance(p, a, b, &t);
        if (d < best.distance) {
            best.distance = d;
            best.point    = a + t * (b - a);
            best.s        = cum[i] + t * (cum[i + 1] - cum[i]);
        }
    }
    if (path.size() == 1) {
        best.point    = path.front();
        best.distance = (p - path.front()).norm();
        best.s        = 0.;
    }
    return best;
}

double circumradius(const Vec2d &a, const Vec2d &b, const Vec2d &c)
{
    const double twice_area = std::abs(cross2(b - a, c - a));
    const double edges      = (b - a).norm() * (c - b).norm() * (a - c).norm();
    if (twice_area <= 1e-12 * std::max(1., edges))
        return kInf;
    return edges / (2. * twice_area);
}

std::vector<RadiusSample> radius_profile(const Path2d &path, double spacing, bool closed, double half_window)
{
    std::vector<RadiusSample> out;
    if (path.size() < (closed ? 3u : 2u))
        return out;
    Path2d q;
    if (closed) {
        Path2d ring = path;
        ring.push_back(path.front());
        q = resample(ring, spacing);
        q.pop_back();
    } else
        q = resample(path, spacing);
    const size_t n = q.size();
    if (n < 3)
        return out;
    const double length = path_length(path, closed);
    const double step   = closed ? length / double(n) : length / double(n - 1);
    size_t       k      = std::max<size_t>(1, size_t(std::lround(half_window / std::max(step, 1e-9))));
    out.reserve(n);
    if (closed) {
        k = std::min(k, std::max<size_t>(1, n / 3));
        for (size_t i = 0; i < n; ++i)
            out.push_back({ double(i) * step, circumradius(q[(i + n - k) % n], q[i], q[(i + k) % n]) });
    } else {
        // Near the ends the window slides inwards rather than shrinking, because three points
        // only 0.5 mm apart would bring the polygon-vertex artefact back. A path shorter than
        // the window is measured over its whole length.
        for (size_t i = 1; i + 1 < n; ++i) {
            size_t l = i >= k ? i - k : 0;
            size_t r = std::min(n - 1, l + 2 * k);
            if (r == n - 1)
                l = r >= 2 * k ? r - 2 * k : 0;
            const size_t m = (l + r) / 2;
            if (m == l || m == r)
                continue;
            out.push_back({ double(i) * step, circumradius(q[l], q[m], q[r]) });
        }
    }
    return out;
}

double min_radius(const std::vector<RadiusSample> &profile)
{
    double r = kInf;
    for (const RadiusSample &s : profile)
        r = std::min(r, s.radius);
    return r;
}

double min_radius_in(const std::vector<RadiusSample> &profile, double from, double to, double perimeter)
{
    double r = kInf;
    for (const RadiusSample &sample : profile) {
        if (perimeter > 0.) {
            for (int k = -1; k <= 2; ++k) {
                const double s = sample.s + double(k) * perimeter;
                if (s >= from && s <= to) {
                    r = std::min(r, sample.radius);
                    break;
                }
            }
        } else if (sample.s >= from && sample.s <= to)
            r = std::min(r, sample.radius);
    }
    return r;
}

// --- Dubins paths ---------------------------------------------------------------------

namespace {

enum class Turn { L, S, R };

std::array<Turn, 3> pieces_of(DubinsWord w)
{
    switch (w) {
    case DubinsWord::LSL: return { Turn::L, Turn::S, Turn::L };
    case DubinsWord::RSR: return { Turn::R, Turn::S, Turn::R };
    case DubinsWord::LSR: return { Turn::L, Turn::S, Turn::R };
    case DubinsWord::RSL: return { Turn::R, Turn::S, Turn::L };
    case DubinsWord::RLR: return { Turn::R, Turn::L, Turn::R };
    case DubinsWord::LRL: return { Turn::L, Turn::R, Turn::L };
    }
    return { Turn::S, Turn::S, Turn::S };
}

// Moves a pose along one piece. For turns, `param` is the angle turned; for a straight, it
// is the length divided by the radius.
void advance(Vec2d &p, double &h, Turn turn, double param, double radius)
{
    switch (turn) {
    case Turn::S: p += radius * param * Vec2d(std::cos(h), std::sin(h)); break;
    case Turn::L: {
        const Vec2d c = p + radius * Vec2d(-std::sin(h), std::cos(h));
        h += param;
        p = c + radius * Vec2d(std::sin(h), -std::cos(h));
        break;
    }
    case Turn::R: {
        const Vec2d c = p + radius * Vec2d(std::sin(h), -std::cos(h));
        h -= param;
        p = c + radius * Vec2d(-std::sin(h), std::cos(h));
        break;
    }
    }
}

} // namespace

std::vector<DubinsPath> dubins_paths(const Vec2d &p0, double h0, const Vec2d &p1, double h1, double radius)
{
    std::vector<DubinsPath> out;
    if (radius <= 0.)
        return out;
    const Vec2d  delta = (p1 - p0) / radius;
    const double d     = delta.norm();
    const double theta = d > 1e-12 ? std::atan2(delta.y(), delta.x()) : 0.;
    const double a = mod2pi(h0 - theta), b = mod2pi(h1 - theta);
    const double sa = std::sin(a), ca = std::cos(a), sb = std::sin(b), cb = std::cos(b), cab = std::cos(a - b);

    auto push = [&](DubinsWord w, double t, double p, double q) {
        if (!std::isfinite(t) || !std::isfinite(p) || !std::isfinite(q) || p < 0.)
            return;
        out.push_back({ w, radius, { t, p, q }, p0, h0 });
    };

    // LSL
    if (double p2 = 2. + d * d - 2. * cab + 2. * d * (sa - sb); p2 >= 0.) {
        const double tmp = std::atan2(cb - ca, d + sa - sb);
        push(DubinsWord::LSL, mod2pi(-a + tmp), std::sqrt(p2), mod2pi(b - tmp));
    }
    // RSR
    if (double p2 = 2. + d * d - 2. * cab + 2. * d * (sb - sa); p2 >= 0.) {
        const double tmp = std::atan2(ca - cb, d - sa + sb);
        push(DubinsWord::RSR, mod2pi(a - tmp), std::sqrt(p2), mod2pi(-b + tmp));
    }
    // LSR
    if (double p2 = -2. + d * d + 2. * cab + 2. * d * (sa + sb); p2 >= 0.) {
        const double p   = std::sqrt(p2);
        const double tmp = std::atan2(-ca - cb, d + sa + sb) - std::atan2(-2., p);
        push(DubinsWord::LSR, mod2pi(-a + tmp), p, mod2pi(-b + tmp));
    }
    // RSL
    if (double p2 = -2. + d * d + 2. * cab - 2. * d * (sa + sb); p2 >= 0.) {
        const double p   = std::sqrt(p2);
        const double tmp = std::atan2(ca + cb, d - sa - sb) - std::atan2(2., p);
        push(DubinsWord::RSL, mod2pi(a - tmp), p, mod2pi(b - tmp));
    }
    // RLR
    if (double tmp = (6. - d * d + 2. * cab + 2. * d * (sa - sb)) / 8.; std::abs(tmp) <= 1.) {
        const double p = mod2pi(2. * M_PI - std::acos(tmp));
        const double t = mod2pi(a - std::atan2(ca - cb, d - sa + sb) + p / 2.);
        push(DubinsWord::RLR, t, p, mod2pi(a - b - t + p));
    }
    // LRL
    if (double tmp = (6. - d * d + 2. * cab + 2. * d * (sb - sa)) / 8.; std::abs(tmp) <= 1.) {
        const double p = mod2pi(2. * M_PI - std::acos(tmp));
        const double t = mod2pi(-a - std::atan2(ca - cb, d + sa - sb) + p / 2.);
        push(DubinsWord::LRL, t, p, mod2pi(b - a - t + p));
    }

    // Keep only words that actually land on the goal pose. This guards the closed forms
    // above against sign slips and numerical edge cases.
    const double tol = 1e-6 * std::max(1., radius) + 1e-6;
    out.erase(std::remove_if(out.begin(), out.end(),
                             [&](const DubinsPath &path) {
                                 Vec2d  p = path.start;
                                 double h = path.heading;
                                 const std::array<Turn, 3> turns = pieces_of(path.word);
                                 for (size_t i = 0; i < 3; ++i)
                                     advance(p, h, turns[i], path.params[i], path.radius);
                                 const double dh = std::abs(std::remainder(h - h1, 2. * M_PI));
                                 return (p - p1).norm() > tol || dh > 1e-6;
                             }),
              out.end());
    std::sort(out.begin(), out.end(), [](const DubinsPath &l, const DubinsPath &r) { return l.length() < r.length(); });
    return out;
}

Path2d sample_dubins(const DubinsPath &path, double step)
{
    Path2d out { path.start };
    Vec2d  p = path.start;
    double h = path.heading;
    const std::array<Turn, 3> turns = pieces_of(path.word);
    for (size_t i = 0; i < 3; ++i) {
        const double param = path.params[i];
        if (param <= 1e-12)
            continue;
        if (turns[i] == Turn::S) {
            advance(p, h, Turn::S, param, path.radius);
            out.push_back(p);
            continue;
        }
        const size_t n = std::max<size_t>(1, size_t(std::ceil(param * path.radius / std::max(step, 1e-3))));
        for (size_t k = 0; k < n; ++k) {
            advance(p, h, turns[i], param / double(n), path.radius);
            out.push_back(p);
        }
    }
    return out;
}

// --- SegmentIndex ---------------------------------------------------------------------

SegmentIndex::SegmentIndex(double cell_mm) : m_cell(std::max(cell_mm, 0.1)) {}

int64_t SegmentIndex::cell(double v) const { return int64_t(std::floor(v / m_cell)); }

void SegmentIndex::insert(const Vec2d &a, const Vec2d &b, int owner)
{
    const int idx = int(m_segments.size());
    m_segments.push_back({ a, b, owner });
    for (int64_t ix = cell(std::min(a.x(), b.x())); ix <= cell(std::max(a.x(), b.x())); ++ix)
        for (int64_t iy = cell(std::min(a.y(), b.y())); iy <= cell(std::max(a.y(), b.y())); ++iy)
            m_grid[key(ix, iy)].push_back(idx);
}

void SegmentIndex::add(const Path2d &path, int owner, bool closed)
{
    const size_t segments = closed ? path.size() : (path.empty() ? 0 : path.size() - 1);
    for (size_t i = 0; i < segments; ++i) {
        const Vec2d &a = vertex(path, i), &b = vertex(path, i + 1);
        // Split long segments so each one touches at most a 2x2 block of cells.
        const size_t n = std::max<size_t>(1, size_t(std::ceil((b - a).norm() / m_cell)));
        for (size_t k = 0; k < n; ++k)
            insert(a + (b - a) * (double(k) / double(n)), a + (b - a) * (double(k + 1) / double(n)), owner);
    }
}

double SegmentIndex::distance(const Vec2d &p, double limit, int ignore_a, int ignore_b) const
{
    double best = limit;
    for (int64_t ix = cell(p.x() - limit); ix <= cell(p.x() + limit); ++ix)
        for (int64_t iy = cell(p.y() - limit); iy <= cell(p.y() + limit); ++iy) {
            auto it = m_grid.find(key(ix, iy));
            if (it == m_grid.end())
                continue;
            for (int idx : it->second) {
                const Segment &s = m_segments[idx];
                if (s.owner == ignore_a || s.owner == ignore_b)
                    continue;
                best = std::min(best, point_segment_distance(p, s.a, s.b));
            }
        }
    return best;
}

bool SegmentIndex::crosses(const Vec2d &a, const Vec2d &b) const
{
    const size_t n = std::max<size_t>(1, size_t(std::ceil((b - a).norm() / m_cell)));
    for (size_t k = 0; k < n; ++k) {
        const Vec2d pa = a + (b - a) * (double(k) / double(n));
        const Vec2d pb = a + (b - a) * (double(k + 1) / double(n));
        for (int64_t ix = cell(std::min(pa.x(), pb.x())); ix <= cell(std::max(pa.x(), pb.x())); ++ix)
            for (int64_t iy = cell(std::min(pa.y(), pb.y())); iy <= cell(std::max(pa.y(), pb.y())); ++iy) {
                auto it = m_grid.find(key(ix, iy));
                if (it == m_grid.end())
                    continue;
                for (int idx : it->second)
                    if (segments_cross(a, b, m_segments[idx].a, m_segments[idx].b))
                        return true;
            }
    }
    return false;
}

} // namespace Fibreseek
} // namespace Slic3r
