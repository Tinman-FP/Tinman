#include "WriterAdapter.hpp"

#include <algorithm>
#include <cmath>

namespace Slic3r {
namespace Fibreseek {

namespace {

Polyline to_polyline(const Path2d &path)
{
    Polyline out;
    for (const Vec2d &p : path) {
        const Point q(coord_t(std::llround(scale_(p.x()))), coord_t(std::llround(scale_(p.y()))));
        if (out.points.empty() || out.points.back() != q)
            out.points.push_back(q);
    }
    return out;
}

double polyline_length_mm(const Polyline &pl) { return pl.length() * SCALING_FACTOR; }

} // namespace

WriterRouteShape writer_shape(const Route &route)
{
    if (route.pieces.empty() || route.length_mm <= 0.)
        return WriterRouteShape::NoXYMotion;
    if (route.closed)
        return WriterRouteShape::ClosedLoop;
    if (route.kind != RouteKind::Infill)
        return WriterRouteShape::NearClosedLoop; // a multi-turn wall spiral
    return route.pieces.size() == 1 ? WriterRouteShape::LineOrTail : WriterRouteShape::OpenPath;
}

WriterRoutePhase writer_phase(Phase phase)
{
    switch (phase) {
    case Phase::Start: return WriterRoutePhase::Start;
    case Phase::Normal: return WriterRoutePhase::Normal;
    case Phase::SlowTurn: return WriterRoutePhase::SlowTurn;
    case Phase::PostCutTail: return WriterRoutePhase::PostCutTail;
    }
    return WriterRoutePhase::Unknown;
}

WriterLayerRoutes to_writer_routes(const LayerPlan &plan, size_t region_id, double hardware_min_length_mm)
{
    WriterLayerRoutes out;
    out.region_id = region_id;
    for (const Route &route : plan.routes) {
        WriterRoute wr;
        wr.id                            = route.id;
        wr.layer_id                      = plan.layer_id;
        wr.shape                         = writer_shape(route);
        wr.candidate_ids                 = route.candidate_ids;
        wr.warnings                      = route.warnings;
        wr.coalesce_transition_length_mm = route.transition_length_mm;
        wr.unsupported_void_crossing_count = route.void_crossings;
        wr.hardware_min_length_mm        = hardware_min_length_mm;
        for (const RoutePiece &piece : route.pieces)
            wr.ordered_segments.push_back(to_polyline(piece.points));

        // Split the whole path at phase boundaries. The writer sums segment lengths to get
        // the fibre length and places the cut at (length - cut distance), so each length is
        // the polyline's own scaled length. That keeps the writer's cut point on the
        // planner's PostCutTail boundary.
        const Path2d              path = route.path();
        const std::vector<double> cum  = arc_lengths(path);
        for (const PhaseSpan &span : route.phases) {
            if (span.to_mm <= span.from_mm + 1e-9)
                continue;
            WriterRouteSegment seg;
            seg.polyline = to_polyline(sub_path(path, cum, span.from_mm, span.to_mm));
            if (seg.polyline.points.size() < 2)
                continue;
            seg.length_mm = polyline_length_mm(seg.polyline);
            seg.phase     = writer_phase(span.phase);
            wr.planned_segments.push_back(std::move(seg));
        }
        for (const WriterRouteSegment &seg : wr.planned_segments)
            wr.length_mm += seg.length_mm;
        wr.fiber_positive_length_mm  = wr.length_mm;
        wr.matrix_positive_length_mm = wr.length_mm;
        if (route.tail.size() >= 2) {
            wr.tail_segments.push_back(to_polyline(route.tail));
            wr.tail_length_mm = polyline_length_mm(wr.tail_segments.back());
        }
        out.routes.push_back(std::move(wr));
    }
    return out;
}

const char *to_string(WriterRouteShape shape)
{
    switch (shape) {
    case WriterRouteShape::NoXYMotion: return "no_xy_motion";
    case WriterRouteShape::OpenPath: return "open_path";
    case WriterRouteShape::ShortOpenPath: return "short_open_path";
    case WriterRouteShape::NearClosedLoop: return "near_closed_loop";
    case WriterRouteShape::ClosedLoop: return "closed_loop";
    case WriterRouteShape::LineOrTail: return "line_or_tail";
    case WriterRouteShape::Unknown: break;
    }
    return "unknown";
}

const char *to_string(WriterRoutePhase phase)
{
    switch (phase) {
    case WriterRoutePhase::Start: return "start";
    case WriterRoutePhase::Normal: return "normal";
    case WriterRoutePhase::SlowTurn: return "slow_turn";
    case WriterRoutePhase::TensionLead: return "tension_lead";
    case WriterRoutePhase::TensionRelease: return "tension_release";
    case WriterRoutePhase::PostCutTail: return "post_cut_tail";
    case WriterRoutePhase::Unknown: break;
    }
    return "unknown";
}

} // namespace Fibreseek
} // namespace Slic3r
