#ifndef slic3r_FibreSeekMaterials_hpp_
#define slic3r_FibreSeekMaterials_hpp_

#include "PrintConfig.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r {

class Preset;
class PresetBundle;

// Continuous-fiber printers print material 1 as the polymer and material 2 as the
// polymer + fiber composite (filament_type "CFC"). These helpers keep that contract
// in one place for the sidebar, the filament combos and the preset bundle.

// True when the edited printer preset can print continuous fiber.
bool selected_printer_supports_continuous_fiber(const PresetBundle &bundle);
// The material's filament_type decides its role; the legacy "CFC " name prefix
// counts only for profiles without a type.
bool filament_is_cfc_slot_profile(const Preset &filament);
// On a continuous-fiber printer material 1 must be a polymer and material 2 a CFC.
// Other slots and other printers are unrestricted.
bool filament_fits_continuous_fiber_lane(const PresetBundle &bundle, size_t slot, const Preset &filament);

// The FibreSeek Seeker 3, recognized by printer_model only.
bool selected_printer_is_fibreseek_seeker3(const PresetBundle &bundle);
// The Seeker 3's fixed material-to-nozzle map (1-based nozzle_diameter indexes, as in
// filament_map): material 2 and any other CFC print on the composite nozzle, the rest
// on the plastic nozzle. Each nozzle is found by its diameter. Nothing for another
// printer, when a nozzle can't be told apart, or when a material is missing.
// selections defaults to the bundle's filament_presets.
std::optional<std::vector<int>> fibreseek_material_tool_map(const PresetBundle &bundle,
                                                            const std::vector<std::string> *selections = nullptr);
// Writes that map with Manual mode into config, so automatic mapping can never put a
// polymer on the composite nozzle or the composite on the plastic one.
bool apply_fibreseek_material_tool_map(const PresetBundle &bundle, DynamicPrintConfig &config,
                                       const std::vector<std::string> *selections = nullptr);

// The project's own map and mode while the Seeker 3 role map replaces them. Kept in
// memory by the bundle so they come back when another printer is selected; never saved.
struct FibreSeekMappingMemory
{
    std::optional<DynamicPrintConfig> ordinary; // filament_map and filament_map_mode
    std::vector<int>                  role_map; // the map that replaced them
};

// Puts project (default: the bundle's project_config) on the role map while the
// Seeker 3 is selected. Once another printer is selected, restores the saved map
// unless the map changed in the meantime (a loaded project, a user edit).
// Returns true when the role map is applied.
bool normalize_fibreseek_material_mapping(PresetBundle &bundle, DynamicPrintConfig *project = nullptr,
                                          const std::vector<std::string> *selections = nullptr);

// What the continuous-fiber choice asks for in material 2.
struct ContinuousFiberFilamentIdentity
{
    std::string matrix_type;        // filament_type of the polymer in material 1
    std::string source_material_id; // catalog ID of the fiber strand
    std::string fiber_name;
    std::string fiber_type;
};

// Finds the saved composite profile for material 2 by its material data, never by
// its name: filament_type CFC plus matching fiber_plastic_type, fiber_name,
// fiber_type and, for strand profiles, fiber_source_material_id. The profile must be
// compatible with the selected printer and process, and on the Seeker 3 with its
// composite nozzle. Keeps the current material 2 when it matches. Changes nothing;
// when no profile or several match, returns nothing and says why in reason.
std::optional<std::string> resolve_continuous_fiber_filament(const PresetBundle &bundle,
                                                             const ContinuousFiberFilamentIdentity &identity,
                                                             std::string &reason);

} // namespace Slic3r

#endif
