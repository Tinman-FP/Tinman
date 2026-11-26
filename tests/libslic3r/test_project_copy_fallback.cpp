#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "libslic3r/Config.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Semver.hpp"

using namespace Slic3r;

namespace {

// A library preset, as saved by the user.
const Preset &library_preset(PresetCollection &presets, const std::string &name)
{
    presets.load_preset({}, name, presets.default_preset().config, false);
    return *presets.find_preset(name, false);
}

// What opening a project does when its preset of that name differs from the library's: it loads a
// project-embedded copy named after the project file.
std::string project_copy(PresetCollection &presets, const std::string &source, const std::string &project_file,
                         const std::string &key, ConfigOption *value, PresetCollection::LoadAndSelect select)
{
    DynamicPrintConfig config = presets.find_preset(source, false)->config;
    config.set_key_value(key, value);
    const Preset *copy = presets.load_external_preset(project_file, project_file, source, config, {}, select, Semver(2, 5, 0)).first;
    REQUIRE(copy != nullptr);
    REQUIRE(copy->is_project_embedded);
    return copy->name;
}

// A project-embedded copy as a saved project restores it, selected.
std::string selected_embedded_copy(PresetCollection &presets, Preset::Type type, const std::string &name)
{
    Preset copy(type, name, false);
    copy.config = presets.default_preset().config;
    copy.config.set_key_value(type == Preset::TYPE_PRINTER ? "printer_settings_id" : "print_settings_id", new ConfigOptionString(name));
    copy.is_project_embedded = true;
    copy.is_external         = true;
    std::vector<Preset *>      inputs{&copy};
    PresetsConfigSubstitutions substitutions;
    presets.load_project_embedded_presets(inputs, Preset::get_type_string(type), substitutions, ForwardCompatibilitySubstitutionRule::Disable);
    REQUIRE(presets.select_preset_by_name(name, true));
    return presets.get_selected_preset_name();
}

} // namespace

TEST_CASE("Clearing a project's filament copies gives each slot back the filament it was copied from", "[Preset][ProjectCopy]")
{
    PresetBundle bundle;
    const std::string tuned = library_preset(bundle.filaments, "TUNED - PCTG - r8").name;
    const std::string copy  = project_copy(bundle.filaments, tuned, "OLSON_A20 (2).3mf", "fan_max_speed", new ConfigOptionFloats{10.},
                                           PresetCollection::LoadAndSelect::Never);
    CHECK(copy == "TUNED - PCTG - r8(OLSON_A20 (2).3mf)");
    CHECK(bundle.filaments.project_copy_source(copy) == tuned);

    const std::string other = bundle.filaments.default_preset().name;
    bundle.filament_presets = {copy, other, copy};
    bundle.reset_project_embedded_presets();
    CHECK(bundle.filaments.find_preset(copy, false) == nullptr);
    CHECK(bundle.filament_presets == std::vector<std::string>{tuned, other, tuned});
}

TEST_CASE("A copy's source is found only for project copies of presets still in the library", "[Preset][ProjectCopy]")
{
    PresetBundle bundle;
    PresetCollection &filaments = bundle.filaments;
    const std::string tuned     = library_preset(filaments, "PLA (matte) r2").name;
    std::vector<Preset> inputs;
    for (const char *name : {"PLA (matte) r2-1(job.3mf)", "PLA (matte) r2(plate_1.gcode)", "Gone PLA(job.3mf)", "PLA (matte) r2(notes)"}) {
        Preset preset(Preset::TYPE_FILAMENT, name, false);
        preset.config = filaments.default_preset().config;
        preset.config.set_key_value("filament_settings_id", new ConfigOptionStrings{name});
        preset.is_project_embedded = true;
        preset.is_external         = true;
        inputs.push_back(preset);
    }
    std::vector<Preset *> pointers;
    for (Preset &preset : inputs)
        pointers.push_back(&preset);
    PresetsConfigSubstitutions substitutions;
    filaments.load_project_embedded_presets(pointers, Preset::get_type_string(Preset::TYPE_FILAMENT), substitutions, ForwardCompatibilitySubstitutionRule::Disable);

    // A numbered copy and a copy from a G-code.
    CHECK(filaments.project_copy_source("PLA (matte) r2-1(job.3mf)") == tuned);
    CHECK(filaments.project_copy_source("PLA (matte) r2(plate_1.gcode)") == tuned);
    // The source is gone, or the parentheses don't name a project file.
    CHECK(filaments.project_copy_source("Gone PLA(job.3mf)").empty());
    CHECK(filaments.project_copy_source("PLA (matte) r2(notes)").empty());
    // A library preset is no copy, whatever its name.
    CHECK(filaments.project_copy_source(tuned).empty());
    CHECK(filaments.project_copy_source("Not loaded(job.3mf)").empty());
}

TEST_CASE("A slot whose copy has no usable source keeps today's fallback", "[Preset][ProjectCopy]")
{
    PresetBundle bundle;
    const std::string tuned = library_preset(bundle.filaments, "TUNED - ABS - r6").name;
    const std::string copy  = project_copy(bundle.filaments, tuned, "job.3mf", "fan_max_speed", new ConfigOptionFloats{10.},
                                           PresetCollection::LoadAndSelect::Never);
    const std::string other = library_preset(bundle.filaments, "Printer default filament").name;
    bundle.printers.get_selected_preset().config.option<ConfigOptionStrings>("default_filament_profile", true)->values = {other};
    // The source no longer fits the printer: the slot takes the printer's default filament, as before.
    bundle.filaments.find_preset(tuned, false, true)->is_compatible = false;
    bundle.filament_presets = {copy};
    bundle.reset_project_embedded_presets();
    CHECK(bundle.filament_presets == std::vector<std::string>{other});
}

TEST_CASE("Clearing a selected process or printer copy selects the preset it was copied from", "[Preset][ProjectCopy]")
{
    PresetBundle bundle;
    bundle.filament_presets = {bundle.filaments.default_preset().name};

    const std::string process = library_preset(bundle.prints, "PRODUCTION - 0.20mm - r1").name;
    REQUIRE(selected_embedded_copy(bundle.prints, Preset::TYPE_PRINT, process + "(job.3mf)") == process + "(job.3mf)");
    bundle.reset_project_embedded_presets();
    CHECK(bundle.prints.get_selected_preset_name() == process);

    const std::string printer = library_preset(bundle.printers, "My printer 0.4").name;
    REQUIRE(selected_embedded_copy(bundle.printers, Preset::TYPE_PRINTER, printer + "(job.3mf)") == printer + "(job.3mf)");
    bundle.reset_project_embedded_presets();
    CHECK(bundle.printers.get_selected_preset_name() == printer);
}
