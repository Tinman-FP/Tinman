#include "FibreSeekMaterials.hpp"
#include "PresetBundle.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <boost/algorithm/string.hpp>

namespace Slic3r {

namespace {

using boost::algorithm::iequals;
using boost::algorithm::trim_copy;

std::string lower_option(const DynamicConfig &config, const char *key)
{
    const ConfigOption *option = config.option(key);
    return option ? boost::algorithm::to_lower_copy(trim_copy(option->serialize())) : std::string();
}

std::optional<double> number(const std::string &value)
{
    try {
        size_t parsed = 0;
        const double result = std::stod(value, &parsed);
        return parsed > 0 ? std::optional<double>(result) : std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

bool truthy(const std::string &value)
{
    if (value.empty() || value == "0" || value == "false" || value == "no" || value == "off" || value == "null")
        return false;
    if (value == "1" || value == "true" || value == "yes" || value == "on")
        return true;
    const auto result = number(value);
    return result && *result != 0.;
}

bool positive(const std::string &value)
{
    const auto result = number(value);
    return result && *result > 0.;
}

bool names_seeker3(const std::string &lower_text)
{
    const bool brand = lower_text.find("fibreseek") != std::string::npos ||
                       lower_text.find("fiberseek") != std::string::npos;
    return (brand && lower_text.find("seeker 3") != std::string::npos) || lower_text == "seeker 3";
}

// The 1-based index of the only nozzle with this diameter; 0 when none or several.
int unique_nozzle(const std::vector<double> &nozzles, double diameter)
{
    int found = 0;
    for (size_t i = 0; i < nozzles.size(); ++i)
        if (std::abs(nozzles[i] - diameter) < 1e-6) {
            if (found != 0)
                return 0;
            found = int(i + 1);
        }
    return found;
}

// The Seeker 3's plastic and composite nozzles; 0 for one that can't be found.
std::pair<int, int> fibreseek_role_nozzles(const DynamicPrintConfig &printer)
{
    const auto *nozzles   = printer.option<ConfigOptionFloats>("nozzle_diameter");
    const auto *plastic   = printer.option<ConfigOptionFloat>("plastic_nozzle_diameter");
    const auto *composite = printer.option<ConfigOptionFloat>("composite_nozzle_diameter");
    if (!nozzles || !plastic || !composite || !std::isfinite(plastic->value) || !std::isfinite(composite->value) ||
        plastic->value <= 0. || composite->value <= 0. || std::abs(plastic->value - composite->value) < 1e-6)
        return {0, 0};
    return {unique_nozzle(nozzles->values, plastic->value), unique_nozzle(nozzles->values, composite->value)};
}

// The single value of a per-filament string option: empty when absent, nothing
// when the option has the wrong type or its entries disagree.
std::optional<std::string> single_string(const DynamicConfig &config, const char *key)
{
    const ConfigOption *raw = config.option(key);
    if (raw == nullptr)
        return std::string();
    const auto *option = dynamic_cast<const ConfigOptionStrings *>(raw);
    if (option == nullptr)
        return std::nullopt;
    if (option->values.empty())
        return std::string();
    const std::string value = trim_copy(option->values.front());
    for (const std::string &entry : option->values)
        if (!iequals(trim_copy(entry), value))
            return std::nullopt;
    return value;
}

} // namespace

bool selected_printer_supports_continuous_fiber(const PresetBundle &bundle)
{
    const Preset &printer = bundle.printers.get_edited_preset();
    const DynamicPrintConfig &config = printer.config;
    const std::string cut_gcode = lower_option(config, "fiber_cut_gcode");
    return truthy(lower_option(config, "fiber_enabled")) ||
           (!cut_gcode.empty() && cut_gcode != "{}" && cut_gcode != "[]" && cut_gcode != "null") ||
           (positive(lower_option(config, "fiber_cut_distance")) &&
            positive(lower_option(config, "composite_nozzle_diameter"))) ||
           names_seeker3(boost::algorithm::to_lower_copy(Preset::remove_suffix_modified(printer.name))) ||
           names_seeker3(lower_option(config, "printer_model")) ||
           names_seeker3(lower_option(config, "printer_settings_id"));
}

bool filament_is_cfc_slot_profile(const Preset &filament)
{
    // Names carry status labels and product names; an explicit type is never
    // overridden by a name that happens to start with "CFC".
    if (const ConfigOption *option = filament.config.option("filament_type")) {
        const auto *types = dynamic_cast<const ConfigOptionStrings *>(option);
        if (types == nullptr)
            return false;
        bool typed = false;
        for (const std::string &value : types->values) {
            const std::string type = trim_copy(value);
            if (type.empty())
                continue;
            if (type != "CFC")
                return false;
            typed = true;
        }
        if (typed)
            return true;
    }
    const std::string name = trim_copy(get_preset_bare_name(Preset::remove_suffix_modified(filament.name)));
    return boost::algorithm::starts_with(name, "CFC ");
}

bool filament_fits_continuous_fiber_lane(const PresetBundle &bundle, size_t slot, const Preset &filament)
{
    return slot > 1 || !selected_printer_supports_continuous_fiber(bundle) ||
           filament_is_cfc_slot_profile(filament) == (slot == 1);
}

bool selected_printer_is_fibreseek_seeker3(const PresetBundle &bundle)
{
    const Preset &printer = bundle.printers.get_edited_preset();
    const auto   *model   = printer.config.option<ConfigOptionString>("printer_model");
    if (printer.printer_technology() != ptFFF || model == nullptr)
        return false;
    const std::string value = trim_copy(model->value);
    return iequals(value, "FibreSeek Seeker 3") || iequals(value, "FiberSeek Seeker 3");
}

std::optional<std::vector<int>> fibreseek_material_tool_map(const PresetBundle &bundle,
                                                            const std::vector<std::string> *selections)
{
    if (!selected_printer_is_fibreseek_seeker3(bundle))
        return std::nullopt;
    const auto [plastic, composite] = fibreseek_role_nozzles(bundle.printers.get_edited_preset().config);
    const auto &names = selections ? *selections : bundle.filament_presets;
    if (names.empty())
        return std::nullopt;
    std::vector<int> map;
    map.reserve(names.size());
    for (size_t slot = 0; slot < names.size(); ++slot) {
        const Preset *material = bundle.filaments.find_preset(names[slot], false);
        if (material == nullptr)
            return std::nullopt;
        // Material 2 is the composite lane even while it still holds the copy of
        // material 1 that a new project starts with.
        const int nozzle = slot == 1 || (slot > 1 && filament_is_cfc_slot_profile(*material)) ? composite : plastic;
        if (nozzle == 0)
            return std::nullopt;
        map.push_back(nozzle);
    }
    return map;
}

bool apply_fibreseek_material_tool_map(const PresetBundle &bundle, DynamicPrintConfig &config,
                                       const std::vector<std::string> *selections)
{
    auto map = fibreseek_material_tool_map(bundle, selections);
    if (!map)
        return false;
    config.set_key_value("filament_map", new ConfigOptionInts(std::move(*map)));
    config.set_key_value("filament_map_mode", new ConfigOptionEnum<FilamentMapMode>(fmmManual));
    return true;
}

bool normalize_fibreseek_material_mapping(PresetBundle &bundle, DynamicPrintConfig *project,
                                          const std::vector<std::string> *selections)
{
    DynamicPrintConfig     &target = project ? *project : bundle.project_config;
    FibreSeekMappingMemory &memory = bundle.fibreseek_mapping;
    if (selected_printer_is_fibreseek_seeker3(bundle)) {
        // A Seeker 3 with an incomplete setup is still the Seeker 3: keep the saved map.
        auto map = fibreseek_material_tool_map(bundle, selections);
        if (!map)
            return false;
        if (!memory.ordinary) {
            memory.ordinary.emplace();
            memory.ordinary->apply_only(target, {"filament_map", "filament_map_mode"}, true);
        }
        target.set_key_value("filament_map", new ConfigOptionInts(*map));
        target.set_key_value("filament_map_mode", new ConfigOptionEnum<FilamentMapMode>(fmmManual));
        memory.role_map = std::move(*map);
        return true;
    }
    if (!memory.ordinary)
        return false;
    const size_t count    = (selections ? *selections : bundle.filament_presets).size();
    auto         role_map = memory.role_map;
    role_map.resize(count, 1);
    const auto *map  = target.option<ConfigOptionInts>("filament_map");
    const auto *mode = target.option<ConfigOptionEnum<FilamentMapMode>>("filament_map_mode");
    if (map && mode && map->values == role_map && mode->value == fmmManual) {
        DynamicPrintConfig ordinary = std::move(*memory.ordinary);
        if (auto *ordinary_map = ordinary.option<ConfigOptionInts>("filament_map")) {
            // Fit the saved map to the current materials and nozzles.
            const int nozzles = int(bundle.get_printer_extruder_count());
            ordinary_map->values.resize(count, 1);
            for (int &nozzle : ordinary_map->values)
                if (nozzle < 1 || nozzle > nozzles)
                    nozzle = 1;
        }
        target.apply(ordinary);
    }
    memory = {};
    return false;
}

std::optional<std::string> resolve_continuous_fiber_filament(const PresetBundle &bundle,
                                                             const ContinuousFiberFilamentIdentity &identity,
                                                             std::string &reason)
{
    reason.clear();
    if (!selected_printer_supports_continuous_fiber(bundle) || identity.matrix_type.empty() ||
        identity.fiber_name.empty() || identity.fiber_type.empty()) {
        reason = "Select a continuous-fiber printer and a polymer for material 1 first.";
        return std::nullopt;
    }
    const Preset *polymer = bundle.filament_presets.empty() ? nullptr :
                                                              bundle.filaments.find_preset(bundle.filament_presets.front(), false);
    const auto polymer_type = polymer ? single_string(polymer->config, "filament_type") : std::nullopt;
    if (!polymer_type || !iequals(*polymer_type, identity.matrix_type)) {
        reason = "The fiber's matrix does not match the polymer in material 1.";
        return std::nullopt;
    }

    const PresetWithVendorProfile printer = bundle.printers.get_edited_preset_with_vendor_profile();
    const PresetWithVendorProfile process = bundle.prints.get_edited_preset_with_vendor_profile();
    std::optional<size_t> composite_nozzle;
    if (selected_printer_is_fibreseek_seeker3(bundle))
        if (const int nozzle = fibreseek_role_nozzles(printer.preset.config).second)
            composite_nozzle = size_t(nozzle - 1);

    std::vector<std::string> matches;
    for (const Preset &preset : bundle.filaments.get_presets()) {
        if (!preset.is_visible)
            continue;
        const auto type       = single_string(preset.config, "filament_type");
        const auto matrix     = single_string(preset.config, "fiber_plastic_type");
        const auto name       = single_string(preset.config, "fiber_name");
        const auto fiber_type = single_string(preset.config, "fiber_type");
        const auto source     = single_string(preset.config, "fiber_source_material_id");
        const auto kind       = single_string(preset.config, "fiber_material_kind");
        // A composite_matrix profile's source ID names its matrix + fiber recipe, a
        // continuous_fiber profile's names the strand: only the strand ID is compared.
        const bool recipe = kind && iequals(*kind, "composite_matrix");
        const bool strand = kind && (kind->empty() || iequals(*kind, "continuous_fiber"));
        // filament_type CFC also satisfies the material 2 lane rule.
        if (!type || *type != "CFC" || !matrix || !name || !fiber_type || !source || !(recipe || strand) ||
            !iequals(*matrix, identity.matrix_type) || !iequals(*name, identity.fiber_name) ||
            !iequals(*fiber_type, identity.fiber_type) ||
            (strand && !source->empty() && !iequals(*source, identity.source_material_id)))
            continue;
        // The same checks that decide which profiles the filament combos offer.
        const PresetWithVendorProfile material = bundle.filaments.get_preset_with_vendor_profile(preset);
        if (!is_compatible_with_printer(material, printer) || !is_compatible_with_print(material, process, printer) ||
            (composite_nozzle && !is_compatible_with_physical_printer_scope(preset, printer.preset, composite_nozzle)))
            continue;
        matches.push_back(preset.name);
    }

    if (bundle.filament_presets.size() > 1) {
        const Preset *current = bundle.filaments.find_preset(bundle.filament_presets[1], false);
        if (current && std::find(matches.begin(), matches.end(), current->name) != matches.end())
            return current->name;
    }
    if (matches.size() == 1)
        return matches.front();
    reason = matches.empty() ? "No saved composite profile for the selected printer matches this polymer and fiber." :
                               "Several saved composite profiles match this polymer and fiber; select one for material 2.";
    return std::nullopt;
}

} // namespace Slic3r
