#pragma once

#include <optional>
#include <string>
#include <vector>

namespace Slic3r {
class Preset;
class PresetBundle;

// How saving a process or filament preset treats its per-printer scope.
//   Preserve:            keep the scope of the preset being edited (default).
//   ThisPhysicalPrinter: bind it to the selected physical printer and nozzle(s).
//   GenericTemplate:     clear the scope so it follows the ordinary compatibility rules.
enum class ProfileScopeSaveMode { Preserve, ThisPhysicalPrinter, GenericTemplate };

// What the save dialog offered. It is re-checked against the live selection when saving.
struct ProfileScopeSaveSelection {
    ProfileScopeSaveMode  mode = ProfileScopeSaveMode::Preserve;
    std::string           physical_printer_name;
    std::string           physical_id;
    std::string           machine_preset_name;
    std::vector<double>   nozzle_diameters;
    // Index into nozzle_diameters for a filament. Preset only for single-nozzle
    // printers; otherwise the user must choose the nozzle the filament was tuned on.
    std::optional<size_t> nozzle_tool_index;
};

// A name the save dialog would accept for a profile or physical printer: not empty, no
// leading or trailing space, none of <>[]:/\|?*" or line breaks, no "(modified)" suffix.
bool is_valid_profile_name(const std::string &name);

// The selected physical printer, if it has an identity that its selected machine
// preset shares and no other physical printer uses. Otherwise scoping is unavailable.
std::optional<ProfileScopeSaveSelection> current_profile_scope_save_target(const PresetBundle &bundle);

// Apply the selection to the preset about to be saved as new_name. Throws, leaving
// nothing saved, if the selection is stale or would overwrite a preset with a
// different scope. Changing the scope always requires a new name, so the original
// preset is never rewritten for another printer. copied_from is the preset being
// copied when it is not the edited one (moving or importing profiles).
void prepare_profile_scope_for_save(Preset &candidate, const PresetBundle &bundle,
                                    const std::string &new_name, const ProfileScopeSaveSelection &selection,
                                    const Preset *copied_from = nullptr);
} // namespace Slic3r
