#pragma once

#include <string>
#include <vector>

namespace Slic3r {

// One loaded slot of a Creality CFS, as the Creality Hi reports it in Moonraker's `box` printer object.
struct CrealityCfsSlot {
    int         slot_index = 0;
    std::string material_type;
    std::string color;
};

// Parse a /printer/objects/query?box response without querying printer hardware.
// Empty slots, disconnected boxes and unknown material ids are omitted.
bool parse_creality_cfs_response(const std::string& response, std::vector<CrealityCfsSlot>& slots);

// A slot the CrealityPrint send dialog offers for a project filament: a CFS slot or the external spool.
struct CrealityMappingSlot {
    std::string type;
    std::string color; // "#RRGGBB"
    bool        external = false;
};

// The slot a project filament maps to by default: among the CFS slots of its type the one with the
// closest colour, else the same among external slots if the plate prints with the filament (`used`),
// else the CFS slot in the filament's `position` among the CFS slots, else the first CFS slot.
// A printer rarely reports a spool's colour exactly as the filament profile has it, so the closest
// colour, not an equal one, wins. Mapping any filament to the external spool starts the whole job
// from it, so no other filament defaults to the external spool. -1 when there are no slots.
int default_creality_mapping_slot(const std::vector<CrealityMappingSlot>& slots, const std::string& type,
                                  const std::string& color, int position, bool used);

} // namespace Slic3r
