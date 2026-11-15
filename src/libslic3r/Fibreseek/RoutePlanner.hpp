#pragma once

// Clean-room continuous-fibre route planner for the FibreSeeker 3.
//
// Given one layer of one region, it decides where continuous fibre goes: wall spirals along
// the outside and around holes, then linked infill ribs. Every route is long enough to be
// cut safely and keeps clear of every other fibre path. Bend radius is treated as a quality
// target. The full design is in docs/fibreseek-route-planner-spec.md; the comments here
// point to its sections.
//
// The planner does not write G-code. Fibreseek/WriterAdapter.hpp turns a LayerPlan into the
// route layout the G-code writer reads.

#include "../ExPolygon.hpp"
#include "../Polyline.hpp"
#include "RouteGeometry.hpp"

#include <string>
#include <vector>

namespace Slic3r {

class DynamicPrintConfig;

namespace Fibreseek {

enum class SeamMode { Source, Nearest, Aligned, Rear, Random };
enum class ReinforcementMode { Light, Medium, Heavy };
enum class RibPattern { Solid, Rhombic, Isogrid, Anisogrid, Tetragrid };

// Every tunable value (spec sec. 1). The defaults match the option defaults, except the cut
// distance: settings_from_config() takes it from the printer profile with no stand-in, and the
// 58 mm here (the FibreSeeker 3 contract value) only serves settings built by hand.
struct PlannerSettings
{
    // Widths and the plastic loops around the fibre.
    double fibre_width_mm         { 0.8 };
    double matrix_width_mm        { 0.45 };
    int    plastic_loops_outside  { 2 };
    int    plastic_loops_inside   { 0 };
    double wall_inset_override_mm { 0. }; // fiber_perimeter_inset; 0 = derived
    double infill_inset_mm        { 0. };

    // Candidate families.
    bool walls       { true };
    bool infill      { true };
    int  outer_loops { 1 };
    int  hole_loops  { 1 };

    // Cut safety (sec. 3.1).
    double cut_distance_mm        { 58. };
    double hardware_min_length_mm { 55. };
    double wall_min_length_mm     { 55. };
    double user_min_length_mm     { 0. };
    double anchor_length_mm       { 7. };

    // Curvature (sec. 3.3) and sampling.
    double bend_radius_mm { 8. };
    double chord_mm       { 1. };

    // Infill ribs (sec. 4.2).
    ReinforcementMode   mode    { ReinforcementMode::Light };
    RibPattern       pattern { RibPattern::Solid };
    std::vector<double> infill_angles_deg;            // empty = pattern defaults
    double              infill_spacing_mm      { 0. }; // 0 = derived
    double              infill_density_percent { 0. }; // 0 = derived
    double              nozzle_contact_radius_mm { 0. };

    // Seams (sec. 4.1).
    SeamMode seam           { SeamMode::Source };
    double   seam_angle_deg { 0. };

    // Other settings.
    int    max_routes_per_layer { 0 };
    double slow_length_mm       { 0. };
    double finish_ironing_mm    { 0. };

    // Planner constants (spec sec. 1, "Planner constants"); candidates for new settings.
    int    max_extra_turns       { 2 };
    double headland_cap_widths   { 4. };
    double tight_turn_penalty_mm { 2. };
    double end_gap_widths        { 0.5 };

    // Derived values.
    double min_route_length_mm(bool wall) const;  // L_min / L_min_wall
    double wall_offset_mm() const;                // d0: slice edge to the first fibre centreline
    double rib_clearance_mm() const;              // c: wall fibre to rib fibre, centre to centre
    double transition_length_mm() const;          // T: spiral step length
    double spacing_mm() const;                    // s: rib spacing
    double turn_radius_mm() const;                // rho_turn, which is also the headland h
    double turn_floor_mm() const { return 0.5 * fibre_width_mm; }
    double sample_step_mm() const;                // curvature sampling step h
    std::vector<double> angles_deg() const;
    bool valid(std::string *why = nullptr) const;
};

// Reads the planner settings from a print config by option key (sec. 1). The matrix width
// comes from the caller because it depends on the plastic perimeter width.
PlannerSettings settings_from_config(const DynamicPrintConfig &config, double matrix_width_mm = 0.45);

// Which layers get fibre (sec. 4.6). Layer indices are 0-based.
struct LayerSchedule
{
    int first_visible_layer { 2 }; // 1-based
    int top_free_layers     { 1 };
    int step                { 1 };

    bool   is_fibre_layer(size_t layer_index, size_t layer_count) const;
    size_t ordinal(size_t layer_index) const;
};
LayerSchedule schedule_from_config(const DynamicPrintConfig &config, double layer_height_mm);

enum class RouteKind { OuterWall, HoleWall, Infill };
enum class PieceKind { Loop, Transition, Rib, Connector };
enum class Phase { Start, Normal, SlowTurn, PostCutTail };
enum class SkipReason { RegionTooNarrow, BelowHardwareMinLength, BelowCutWindow, RouteCap, Degenerate };

const char *to_string(RouteKind kind);
const char *to_string(PieceKind kind);
const char *to_string(Phase phase);
const char *to_string(SkipReason reason);

// One geometric piece of a route. Each piece ends where the next begins.
struct RoutePiece
{
    PieceKind kind { PieceKind::Loop };
    size_t    candidate_id { 0 };
    Path2d    points;
    double    length_mm { 0. };
    double    min_radius_mm { 0. };
};

struct PhaseSpan
{
    Phase  phase { Phase::Normal };
    double from_mm { 0. };
    double to_mm { 0. };
};

// One continuous strand: one load, one cut, one finish (spec sec. 2).
struct Route
{
    size_t                   id { 0 };
    RouteKind                kind { RouteKind::Infill };
    bool                     closed { false };      // a single closed loop
    std::vector<RoutePiece>  pieces;
    double                   length_mm { 0. };      // all of it carries fibre
    double                   cut_at_mm { 0. };      // length - cut distance
    double                   min_radius_mm { 0. };
    double                   transition_length_mm { 0. };
    std::vector<PhaseSpan>   phases;
    Path2d                   tail;                  // after-cut ironing path from the route end
    double                   tail_length_mm { 0. };
    std::vector<size_t>      candidate_ids;
    std::vector<std::string> warnings;
    size_t                   void_crossings { 0 };

    Path2d path() const;
    Vec2d  start() const { return pieces.front().points.front(); }
    Vec2d  end() const { return pieces.back().points.back(); }
    bool   has_warning(const std::string &w) const;
};

struct SkippedCandidate
{
    RouteKind  kind { RouteKind::Infill };
    SkipReason reason { SkipReason::Degenerate };
    double     length_mm { 0. };
    Vec2d      where { 0., 0. };
};

struct LayerDiagnostics
{
    size_t                        loop_candidates { 0 };
    size_t                        rib_candidates { 0 };
    size_t                        external_candidates { 0 };
    std::vector<SkippedCandidate> skipped;
    std::vector<std::string>      notes;

    size_t count(SkipReason reason) const;
};

struct LayerInput
{
    size_t     layer_id { 0 };
    double     print_z { 0. };
    double     height { 0.2 };
    ExPolygons slices;               // scaled
    Polylines  infill_candidates;    // scaled; optional, replaces generated ribs
    size_t     fibre_layer_ordinal { 0 };
};

struct LayerPlan
{
    size_t             layer_id { 0 };
    double             print_z { 0. };
    std::vector<Route> routes;          // in emission order
    ExPolygons         fibre_footprint; // scaled
    LayerDiagnostics   diagnostics;

    double total_length_mm() const;
};

class RoutePlanner
{
public:
    explicit RoutePlanner(PlannerSettings settings) : m_settings(std::move(settings)) {}
    const PlannerSettings &settings() const { return m_settings; }
    LayerPlan              plan_layer(const LayerInput &input) const;

private:
    PlannerSettings m_settings;
};

} // namespace Fibreseek
} // namespace Slic3r
