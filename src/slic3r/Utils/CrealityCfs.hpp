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
// closest colour, else the same among external slots, else `fallback`. A printer rarely reports a
// spool's colour exactly as the filament profile has it, so the closest colour, not an equal one, wins.
int default_creality_mapping_slot(const std::vector<CrealityMappingSlot>& slots, const std::string& type,
                                  const std::string& color, int fallback);

} // namespace Slic3r
