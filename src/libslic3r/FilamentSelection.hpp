#ifndef slic3r_FilamentSelection_hpp_
#define slic3r_FilamentSelection_hpp_

#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace Slic3r {

class PresetBundle;
class DynamicPrintConfig;

struct FilamentSelectionColor {
    std::string primary;
    std::string type;
    std::string multi;
};

struct FilamentSlotSelection {
    size_t index;
    std::string preset_name;
    std::string printer_name;
    std::optional<FilamentSelectionColor> color;
};

// Call only after confirmation, on the thread that owns the bundle. Stages all
// allocations before committing the slot and palette; never exports selections
// or modifies the active filament editor, tool mapping, or hardware settings.
bool apply_filament_slot_selection(PresetBundle &bundle, const FilamentSlotSelection &selection,
                                   std::string &reason);

// Per-printer scope checks on a full (packed) print config, where the filament scope
// keys hold one value per material slot. maps is filament_map (1-based nozzle
// indexes). With resolved = false (automatic mapping not yet computed) only the
// scopes are checked against the printer; with resolved = true every scoped
// material must also be mapped to its qualified nozzle.
bool profile_scope_filament_map_valid(const DynamicPrintConfig &config, const std::vector<int> &maps,
                                      bool resolved, std::string &reason);
// For automatic mapping: forbid every nozzle except its qualified one for each
// scoped material, by adding the material slot to exclusions[nozzle].
bool add_profile_scope_tool_exclusions(const DynamicPrintConfig &config,
                                       std::vector<std::set<int>> &exclusions, std::string &reason);

} // namespace Slic3r

#endif
