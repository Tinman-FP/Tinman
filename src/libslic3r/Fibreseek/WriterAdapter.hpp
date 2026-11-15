#pragma once

// Turns a Fibreseek::LayerPlan into the route layout the G-code writer reads in
// GCode::extrude_fiberseek_composite_routes_for_layer() and its summary helpers.
//
// The field and enumerator names below match what that writer code reads, so moving the
// writer onto this planner is a type swap rather than a rewrite. The mapping is in
// docs/fibreseek-route-planner-spec.md sec. 7. This header deliberately does not include
// the old planner's header: these types are defined here from the writer's usage alone.

#include "../ExPolygon.hpp"
#include "../Polyline.hpp"
#include "RoutePlanner.hpp"

#include <string>
#include <vector>

namespace Slic3r {
namespace Fibreseek {

enum class WriterRouteShape { Unknown, NoXYMotion, OpenPath, ShortOpenPath, NearClosedLoop, ClosedLoop, LineOrTail };
enum class WriterRoutePhase { Unknown, Start, Normal, SlowTurn, TensionLead, TensionRelease, PostCutTail };

struct WriterRouteSegment
{
    Polyline         polyline;
    double           length_mm { 0. };
    bool             emits_fiber { true };
    double           fiber_mm_per_mm { 1. };
    bool             emits_plastic { true };
    double           plastic_mm_per_mm { 1. };
    WriterRoutePhase phase { WriterRoutePhase::Normal };
};

struct WriterRoute
{
    size_t                          id { 0 };
    size_t                          layer_id { 0 };
    WriterRouteShape                shape { WriterRouteShape::Unknown };
    std::vector<Polyline>           ordered_segments;  // one per planner piece
    std::vector<WriterRouteSegment> planned_segments;  // pieces split at phase boundaries
    std::vector<Polyline>           tail_segments;     // after-cut ironing, from the route end
    std::vector<size_t>             candidate_ids;
    std::vector<std::string>        warnings;
    double                          length_mm { 0. };
    double                          tail_length_mm { 0. };
    double                          fiber_positive_length_mm { 0. };
    double                          matrix_positive_length_mm { 0. };
    double                          coalesce_transition_length_mm { 0. };
    size_t                          unsupported_void_crossing_count { 0 };
    double                          hardware_min_length_mm { 0. };

    // Safe to emit: the bead never leaves the part and the route is cut-safe.
    bool is_release_safe() const
    {
        return unsupported_void_crossing_count == 0 && !planned_segments.empty() && length_mm + 1e-6 >= hardware_min_length_mm;
    }
};

struct WriterLayerRoutes
{
    size_t                   region_id { 0 };
    std::vector<WriterRoute> routes;
};

WriterRouteShape   writer_shape(const Route &route);
WriterRoutePhase   writer_phase(Phase phase);
WriterLayerRoutes  to_writer_routes(const LayerPlan &plan, size_t region_id, double hardware_min_length_mm);

// The bead (half a fibre width each side) along the release-safe routes only, the ones the writer
// lays. Plastic is trimmed under this rather than LayerPlan::fibre_footprint, which also covers
// routes the writer skips, so a skipped route keeps its plastic.
ExPolygons laid_fibre_footprint(const WriterLayerRoutes &routes, double fibre_width_mm);

const char *to_string(WriterRouteShape shape);
const char *to_string(WriterRoutePhase phase);

} // namespace Fibreseek
} // namespace Slic3r
