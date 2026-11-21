#include "FibreSeekMaterials.hpp"
#include "Config.hpp"
#include "FilamentSelection.hpp"
#include "Preset.hpp"
#include "PresetBundle.hpp"
#include "PrintConfig.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <nlohmann/json.hpp>

namespace Slic3r {

namespace {

using boost::algorithm::iequals;
using boost::algorithm::trim_copy;

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
    // The lanes, the fibre choice and the role map describe the Seeker 3 only;
    // fibre options on another printer do not give it these rules.
    return selected_printer_is_fibreseek_seeker3(bundle);
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
    for (const Preset &stored : bundle.filaments.get_presets()) {
        // Slicing reads the edited copy of the preset open in the editor
        // (find_preset, full_fff_config), so validate that copy too.
        const Preset *effective = stored.is_visible ? bundle.filaments.find_preset(stored.name, false) : nullptr;
        if (effective == nullptr)
            continue;
        const Preset &preset = *effective;
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

double fiber_linear_density_g_per_km(double value)
{
    if (!std::isfinite(value) || value <= 0.)
        return 0.;
    // g/km is the unit throughout (profiles, project and both writers). The
    // filament setting was once labelled g/m; a value below 1 g/km is not a
    // printable strand, so it is read as g/m.
    return value < 1. ? value * 1000. : value;
}

namespace {
// The first value of a per-filament float option at `slot`, or 0 when unset.
double positive_at(const DynamicConfig &config, const char *key, size_t slot)
{
    const auto *values = dynamic_cast<const ConfigOptionFloats *>(config.option(key));
    if (!values || slot >= values->values.size())
        return 0.;
    const double value = values->values[slot];
    return std::isfinite(value) && value > 0. ? value : 0.;
}
} // namespace

void copy_continuous_fiber_metadata(const Preset &composite, DynamicPrintConfig &project)
{
    const auto text = [&composite](const char *key) { return single_string(composite.config, key).value_or(std::string()); };
    // The identity always follows the composite, so no other product's name stays behind.
    project.set_key_value("continuous_fiber_name", new ConfigOptionString(text("fiber_name")));
    project.set_key_value("continuous_fiber_type", new ConfigOptionString(text("fiber_type")));
    project.set_key_value("continuous_fiber_material_kind", new ConfigOptionString(text("fiber_material_kind")));
    project.set_key_value("continuous_fiber_source_material_id", new ConfigOptionString(text("fiber_source_material_id")));
    // Strand data only when the profile sets it: an explicit project value is never
    // replaced by an unset one.
    if (const double diameter = positive_at(composite.config, "fiber_diameter", 0))
        project.set_key_value("continuous_fiber_diameter", new ConfigOptionFloat(diameter));
    if (const double density = fiber_linear_density_g_per_km(positive_at(composite.config, "fiber_linear_density", 0)))
        project.set_key_value("continuous_fiber_linear_density", new ConfigOptionFloat(density));
}

std::optional<size_t> continuous_fiber_composite_slot(const ConfigBase &config)
{
    const auto *types = dynamic_cast<const ConfigOptionStrings *>(config.option("filament_type"));
    if (!types)
        return std::nullopt;
    for (size_t slot = 0; slot < types->values.size(); ++slot)
        if (trim_copy(types->values[slot]) == "CFC")
            return slot;
    return std::nullopt;
}

double continuous_fiber_composite_value(const DynamicConfig &config, const char *key)
{
    const auto slot = continuous_fiber_composite_slot(config);
    return slot ? positive_at(config, key, *slot) : 0.;
}

bool fiber_reinforcement_payload_is_set(const std::string &payload)
{
    const nlohmann::json parsed = nlohmann::json::parse(payload, nullptr, false);
    if (parsed.is_discarded())
        return !trim_copy(payload).empty();
    return !parsed.is_null() && !((parsed.is_object() || parsed.is_array()) && parsed.empty());
}

bool continuous_fiber_requested(const ConfigBase &config)
{
    const auto on = [&config](const char *key) {
        const auto *option = config.option<ConfigOptionBool>(key);
        return option != nullptr && option->value;
    };
    const auto *payload = config.option<ConfigOptionString>("fiber_reinforcement_payload");
    return on("fiber_generate_perimeters") || on("fiber_generate_infill") ||
           (payload != nullptr && fiber_reinforcement_payload_is_set(payload->value));
}

int continuous_fiber_part_cooling_fan_index(const PrintConfig &config, size_t filament_id)
{
    if (!config.fiber_enabled.value)
        return -1;
    const bool composite = continuous_fiber_composite_slot(config) == filament_id;
    const bool has_fan   = composite ? config.fiber_composite_extruder_has_fan.value : config.fiber_plastic_extruder_has_fan.value;
    const int  index     = composite ? config.fiber_composite_extruder_fan_index.value : config.fiber_plastic_extruder_fan_index.value;
    return has_fan ? std::max(0, index) : -1;
}

void apply_continuous_fiber_strand_data(DynamicPrintConfig &config)
{
    // The writers read the strand data from continuous_fiber_*; take it from the
    // effective composite in this configuration when that profile sets it, so any
    // CFC profile works, not only the panel's catalogue products.
    if (const double diameter = continuous_fiber_composite_value(config, "fiber_diameter"))
        config.set_key_value("continuous_fiber_diameter", new ConfigOptionFloat(diameter));
    if (const double density = fiber_linear_density_g_per_km(continuous_fiber_composite_value(config, "fiber_linear_density")))
        config.set_key_value("continuous_fiber_linear_density", new ConfigOptionFloat(density));
}

std::vector<std::string> missing_continuous_fiber_settings(const Preset &composite)
{
    std::vector<std::string> missing;
    if (!positive_at(composite.config, "fiber_diameter", 0))
        missing.emplace_back("Fiber diameter");
    if (!positive_at(composite.config, "fiber_linear_density", 0))
        missing.emplace_back("Fiber linear density");
    // Either model of the composite plastic: the multiplier, or the legacy feed factor.
    if (!positive_at(composite.config, "fiber_composite_plastic_multiplier", 0) && !positive_at(composite.config, "fiber_feed_factor", 0))
        missing.emplace_back("Composite plastic multiplier");
    return missing;
}

ContinuousFiberSelectionSnapshot::ContinuousFiberSelectionSnapshot(const PresetBundle &bundle)
    : filament_presets(bundle.filament_presets)
    , project_config(bundle.project_config)
    , ams_multi_color_filment(bundle.ams_multi_color_filment)
    , fibreseek_mapping(bundle.fibreseek_mapping)
{}

void ContinuousFiberSelectionSnapshot::restore(PresetBundle &bundle) const
{
    bundle.filament_presets        = filament_presets;
    bundle.project_config          = project_config;
    bundle.ams_multi_color_filment = ams_multi_color_filment;
    bundle.fibreseek_mapping       = fibreseek_mapping;
}

bool commit_continuous_fiber_choice(PresetBundle &bundle, const ContinuousFiberFilamentIdentity &identity, std::string &reason)
{
    // Resolution validates the effective composite (the edited copy when open in
    // the editor); slot selection checks that same copy.
    const auto name = resolve_continuous_fiber_filament(bundle, identity, reason);
    if (!name)
        return false;
    const Preset *composite = bundle.filaments.find_preset(*name, false);
    if (composite == nullptr || !filament_fits_continuous_fiber_lane(bundle, 1, *composite)) {
        reason = "The composite profile is no longer available for material 2.";
        return false;
    }
    // Material 2, the palette, the role map and the project's fibre data change
    // together or not at all. Adding the second slot touches all of them.
    const ContinuousFiberSelectionSnapshot before(bundle);
    if (bundle.filament_presets.size() < 2)
        bundle.set_num_filaments(2);
    if (!apply_filament_slot_selection(bundle, {1, *name, bundle.printers.get_edited_preset().name, std::nullopt}, reason)) {
        before.restore(bundle);
        return false;
    }
    copy_continuous_fiber_metadata(*composite, bundle.project_config);
    return true;
}

} // namespace Slic3r
