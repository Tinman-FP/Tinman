#include "FilamentSelection.hpp"
#include "PresetBundle.hpp"

#include <algorithm>

namespace Slic3r {

bool apply_filament_slot_selection(PresetBundle &bundle, const FilamentSlotSelection &selection,
                                   std::string &reason)
{
    reason.clear();
    if (selection.printer_name != bundle.printers.get_edited_preset().name) {
        reason = "The printer changed while the filament was being selected.";
        return false;
    }
    if (selection.index >= bundle.filament_presets.size()) {
        reason = "The selected filament slot is no longer available.";
        return false;
    }
    const Preset *preset = bundle.filaments.find_preset(
        Preset::remove_suffix_modified(selection.preset_name), false, true);
    if (!preset || !preset->is_visible) {
        reason = "The selected filament preset is no longer available.";
        return false;
    }

    auto slots = bundle.filament_presets;
    slots[selection.index] = preset->name;
    DynamicPrintConfig project = bundle.project_config;
    if (selection.color) {
        for (const char *key : {"filament_colour", "filament_colour_type", "filament_multi_colour"}) {
            auto *option = project.option<ConfigOptionStrings>(key, true);
            if (!option) {
                reason = "The project's filament color data is invalid.";
                return false;
            }
            if (option->values.size() < slots.size())
                option->values.resize(slots.size());
        }
        project.option<ConfigOptionStrings>("filament_colour")->values[selection.index] = selection.color->primary;
        project.option<ConfigOptionStrings>("filament_colour_type")->values[selection.index] = selection.color->type;
        project.option<ConfigOptionStrings>("filament_multi_colour")->values[selection.index] = selection.color->multi;
    }
    bundle.filament_presets.swap(slots);
    bundle.project_config = std::move(project);
    return true;
}

bool profile_scope_filament_map_valid(const DynamicPrintConfig &config, const std::vector<int> &maps,
                                      bool resolved, std::string &reason)
{
    reason.clear();
    Preset printer(Preset::TYPE_PRINTER, "");
    Preset process(Preset::TYPE_PRINT, "");
    for (const char *key : {"tinman_physical_printer_id", "nozzle_diameter", "printer_technology"})
        if (const ConfigOption *value = config.option(key))
            printer.config.set_key_value(key, value->clone());
    for (const char *key : {"tinman_process_scope_physical_id", "tinman_process_scope_nozzle_diameters"})
        if (const ConfigOption *value = config.option(key))
            process.config.set_key_value(key, value->clone());
    if (!is_compatible_with_physical_printer_scope(process, printer)) {
        reason = "The process profile belongs to another physical printer or nozzle setup.";
        return false;
    }

    const ConfigOption *id_raw     = config.option("tinman_filament_scope_physical_id");
    const ConfigOption *nozzle_raw = config.option("tinman_filament_scope_nozzle_diameter");
    const ConfigOption *tool_raw   = config.option("tinman_filament_scope_tool");
    const auto *ids     = dynamic_cast<const ConfigOptionStrings *>(id_raw);
    const auto *nozzles = dynamic_cast<const ConfigOptionFloats *>(nozzle_raw);
    const auto *tools   = dynamic_cast<const ConfigOptionInts *>(tool_raw);
    if ((id_raw && !ids) || (nozzle_raw && !nozzles) || (tool_raw && !tools)) {
        reason = "The filament scope settings have the wrong type.";
        return false;
    }
    // Nothing scoped, including configs written before scoping existed: leave
    // slicing exactly as it was, whatever the lengths of these vectors.
    const bool scoped =
        (ids && std::any_of(ids->values.begin(), ids->values.end(), [](const std::string &id) { return !id.empty(); })) ||
        (nozzles && std::any_of(nozzles->values.begin(), nozzles->values.end(), [](double d) { return d != 0.; })) ||
        (tools && std::any_of(tools->values.begin(), tools->values.end(), [](int tool) { return tool != -1; }));
    if (!scoped)
        return true;
    if (!ids || !nozzles || !tools) {
        reason = "The filament scope settings are incomplete.";
        return false;
    }
    // Count material slots from the vectors the slicer itself enumerates them by.
    size_t count = 0;
    for (const char *key : {"filament_settings_id", "filament_colour"})
        if (const auto *values = config.option<ConfigOptionStrings>(key))
            count = std::max(count, values->values.size());
    if (const auto *diameters = config.option<ConfigOptionFloats>("filament_diameter"))
        count = std::max(count, diameters->values.size());
    if (count == 0)
        count = ids->values.size();
    if (count == 0 || ids->values.size() != count || nozzles->values.size() != count || tools->values.size() != count) {
        reason = "The filament scope settings do not match the number of filaments.";
        return false;
    }
    for (size_t slot = 0; slot < count; ++slot) {
        Preset material(Preset::TYPE_FILAMENT, "");
        material.config.set_key_value("tinman_filament_scope_physical_id", new ConfigOptionStrings{ids->values[slot]});
        material.config.set_key_value("tinman_filament_scope_nozzle_diameter", new ConfigOptionFloats{nozzles->values[slot]});
        material.config.set_key_value("tinman_filament_scope_tool", new ConfigOptionInts{tools->values[slot]});
        // Unscoped slots pass the first check; scoped ones must also use their qualified nozzle once mapped.
        if (!is_compatible_with_physical_printer_scope(material, printer) ||
            (resolved && ids->values[slot] != "" &&
             (slot >= maps.size() || maps[slot] <= 0 ||
              !is_compatible_with_physical_printer_scope(material, printer, size_t(maps[slot] - 1))))) {
            reason = "Filament " + std::to_string(slot + 1) + " is not tuned for this physical printer and nozzle.";
            return false;
        }
    }
    return true;
}

bool add_profile_scope_tool_exclusions(const DynamicPrintConfig &config,
                                       std::vector<std::set<int>> &exclusions, std::string &reason)
{
    if (!profile_scope_filament_map_valid(config, {}, false, reason))
        return false;
    const auto *ids   = config.option<ConfigOptionStrings>("tinman_filament_scope_physical_id");
    const auto *tools = config.option<ConfigOptionInts>("tinman_filament_scope_tool");
    if (!ids || !tools)
        return true;
    for (size_t slot = 0; slot < ids->values.size() && slot < tools->values.size(); ++slot) {
        if (ids->values[slot].empty())
            continue;
        const int allowed = tools->values[slot];
        if (allowed < 0 || size_t(allowed) >= exclusions.size()) {
            reason = "The nozzle a filament is tuned on is not available for automatic mapping.";
            return false;
        }
        for (size_t tool = 0; tool < exclusions.size(); ++tool)
            if (int(tool) != allowed)
                exclusions[tool].insert(int(slot));
    }
    return true;
}

} // namespace Slic3r
