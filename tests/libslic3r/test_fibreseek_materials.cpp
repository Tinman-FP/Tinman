#include <catch2/catch_all.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <boost/filesystem.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>

#include "libslic3r/Config.hpp"
#include "libslic3r/FibreSeekMaterials.hpp"
#include "libslic3r/FilamentSelection.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"

using namespace Slic3r;

// All printers and materials here are built in the test; no profile pack is loaded.
namespace {
const std::string unit = "80c00526-29f8-47c8-813a-4d36de799571";

void add_material(PresetBundle &bundle, const std::string &name, const std::string &type = {})
{
    auto config = bundle.filaments.default_preset().config;
    if (!type.empty())
        config.set_key_value("filament_type", new ConfigOptionStrings{type});
    bundle.filaments.load_preset("", name, config, false);
}

void make_seeker3(DynamicPrintConfig &machine)
{
    machine.set_key_value("printer_model", new ConfigOptionString("FibreSeek Seeker 3"));
    machine.set_key_value("nozzle_diameter", new ConfigOptionFloats{0.4, 0.7});
    machine.set_key_value("plastic_nozzle_diameter", new ConfigOptionFloat(0.4));
    machine.set_key_value("composite_nozzle_diameter", new ConfigOptionFloat(0.7));
    // Two separate nozzles, as in the FS3 machine profiles. Since 2.5.1 a new project
    // gets one material slot per tool only when this is off (min_filament_count()).
    machine.set_key_value("single_extruder_multi_material", new ConfigOptionBool(false));
}

std::vector<int> project_map(const PresetBundle &bundle)
{
    return bundle.project_config.option<ConfigOptionInts>("filament_map")->values;
}

FilamentMapMode project_mode(const PresetBundle &bundle)
{
    return bundle.project_config.option<ConfigOptionEnum<FilamentMapMode>>("filament_map_mode")->value;
}

// A Seeker 3 tied to one physical printer, with a PCTG polymer in material 1 and,
// as in a new project, a copy of it in material 2.
struct ResolverFixture
{
    PresetBundle bundle;
    const ContinuousFiberFilamentIdentity identity{"PCTG", "x-ccf-025", "X-CCF Carbon fiber 0.25 mm", "carbon_fiber"};
    ResolverFixture()
    {
        auto &machine = bundle.printers.get_edited_preset().config;
        make_seeker3(machine);
        machine.set_key_value("tinman_physical_printer_id", new ConfigOptionString(unit));
        add_material(bundle, "Polymer", "PCTG");
        bundle.filaments.select_preset_by_name("Polymer", true);
        bundle.filament_presets = {"Polymer", "Polymer"};
        bundle.update_multi_material_filament_presets();
    }
    // A composite tuned on this printer's composite nozzle (index 1).
    Preset &add_composite(const std::string &name, const ContinuousFiberFilamentIdentity *fiber = nullptr)
    {
        const ContinuousFiberFilamentIdentity &identity = fiber ? *fiber : this->identity;
        auto config = bundle.filaments.default_preset().config;
        config.set_key_value("filament_type", new ConfigOptionStrings{"CFC"});
        config.set_key_value("fiber_plastic_type", new ConfigOptionStrings{identity.matrix_type});
        config.set_key_value("fiber_source_material_id", new ConfigOptionStrings{identity.source_material_id});
        config.set_key_value("fiber_material_kind", new ConfigOptionStrings{"continuous_fiber"});
        config.set_key_value("fiber_name", new ConfigOptionStrings{identity.fiber_name});
        config.set_key_value("fiber_type", new ConfigOptionStrings{identity.fiber_type});
        config.set_key_value("tinman_filament_scope_physical_id", new ConfigOptionStrings{unit});
        config.set_key_value("tinman_filament_scope_nozzle_diameter", new ConfigOptionFloats{0.7});
        config.set_key_value("tinman_filament_scope_tool", new ConfigOptionInts{1});
        return bundle.filaments.load_preset("", name, config, false);
    }
};
} // namespace

TEST_CASE("Only the Seeker 3 gets the continuous-fiber interface", "[TinMan][Preset][FibreSeek]")
{
    PresetBundle bundle;
    auto &printer = bundle.printers.get_edited_preset();
    auto &config  = printer.config;
    add_material(bundle, "CFC X-CCF", "CFC");
    const Preset *cfc = bundle.filaments.find_preset("CFC X-CCF", false, true);
    REQUIRE(cfc != nullptr);
    REQUIRE_FALSE(selected_printer_supports_continuous_fiber(bundle));
    SECTION("the Seeker 3, by its printer model") {
        const std::string model = GENERATE("FibreSeek Seeker 3", "FiberSeek Seeker 3", " fibreseek seeker 3 ");
        config.set_key_value("printer_model", new ConfigOptionString(model));
        CHECK(selected_printer_supports_continuous_fiber(bundle));
        CHECK_FALSE(filament_fits_continuous_fiber_lane(bundle, 0, *cfc));
    }
    SECTION("not another printer with fiber options or a Seeker-like name") {
        const unsigned signal = GENERATE(0u, 1u, 2u, 3u, 4u, 5u);
        CAPTURE(signal);
        config.set_key_value("printer_model", new ConfigOptionString("Other fiber printer"));
        switch (signal) {
        case 0: config.set_key_value("fiber_enabled", new ConfigOptionBool(true)); break;
        case 1: config.set_key_value("fiber_cut_gcode", new ConfigOptionString("M9000")); break;
        case 2:
            config.set_key_value("fiber_cut_distance", new ConfigOptionFloat(5));
            config.set_key_value("composite_nozzle_diameter", new ConfigOptionFloat(0.7));
            break;
        case 3: printer.name = "FibreSeek Seeker 3"; break;
        case 4: config.set_key_value("printer_settings_id", new ConfigOptionString("FibreSeek Seeker 3")); break;
        case 5: config.set_key_value("printer_model", new ConfigOptionString("FibreSeek Seeker 3 lookalike")); break;
        }
        CHECK_FALSE(selected_printer_supports_continuous_fiber(bundle));
        // No lane rule: a composite may go anywhere, as on any ordinary printer.
        CHECK(filament_fits_continuous_fiber_lane(bundle, 0, *cfc));
    }
}

TEST_CASE("Continuous-fiber lanes reject swapped materials without changing anything", "[TinMan][Preset][FibreSeek]")
{
    PresetBundle bundle;
    add_material(bundle, "Polymer");
    add_material(bundle, "CFC X-CCF", "CFC");
    make_seeker3(bundle.printers.get_edited_preset().config);
    bundle.filament_presets = {"Polymer", "CFC X-CCF"};
    const auto slot = GENERATE(size_t(0), size_t(1));
    const std::string wrong = slot == 0 ? "CFC X-CCF" : "Polymer";
    const Preset *material = bundle.filaments.find_preset(wrong, false, true);
    REQUIRE(material != nullptr);
    CHECK_FALSE(filament_fits_continuous_fiber_lane(bundle, slot, *material));
    const auto slots   = bundle.filament_presets;
    const auto project = bundle.project_config;
    std::string reason;
    CHECK_FALSE(apply_filament_slot_selection(bundle,
        {slot, wrong, bundle.printers.get_edited_preset().name, FilamentSelectionColor{"#ABCDEF", "0", "#ABCDEF"}}, reason));
    CHECK_FALSE(reason.empty());
    CHECK(bundle.filament_presets == slots);
    CHECK(bundle.project_config == project);
}

TEST_CASE("Continuous-fiber lanes accept their own materials and leave later slots free", "[TinMan][Preset][FibreSeek]")
{
    PresetBundle bundle;
    add_material(bundle, "Polymer");
    add_material(bundle, "CFC X-CCF", "CFC");
    make_seeker3(bundle.printers.get_edited_preset().config);
    bundle.filament_presets = {"Polymer", "CFC X-CCF", "Polymer"};
    const auto slot = GENERATE(size_t(0), size_t(1), size_t(2));
    const std::string name = slot == 0 ? "Polymer" : "CFC X-CCF";
    std::string reason;
    REQUIRE(apply_filament_slot_selection(bundle, {slot, name, bundle.printers.get_edited_preset().name, std::nullopt}, reason));
    CHECK(reason.empty());
    CHECK(bundle.filament_presets[slot] == name);
}

TEST_CASE("Material roles come from filament_type, not status-leading names", "[TinMan][Preset][FibreSeek]")
{
    PresetBundle bundle;
    const std::string cfc_name     = "CANDIDATE | T0 0.7mm | Generic PCTG + X-CCF | r01";
    const std::string polymer_name = "CFC is a product name, not this material role";
    add_material(bundle, cfc_name, "CFC");
    add_material(bundle, polymer_name, "PCTG");
    make_seeker3(bundle.printers.get_edited_preset().config);
    bundle.filament_presets = {polymer_name, cfc_name};
    const auto &materials = static_cast<const PresetBundle &>(bundle).filaments;
    const Preset *cfc     = materials.find_preset(cfc_name, false, true);
    const Preset *polymer = materials.find_preset(polymer_name, false, true);
    REQUIRE(cfc);
    REQUIRE(polymer);
    CHECK(filament_is_cfc_slot_profile(*cfc));
    CHECK_FALSE(filament_is_cfc_slot_profile(*polymer));
    CHECK(filament_fits_continuous_fiber_lane(bundle, 1, *cfc));
    CHECK_FALSE(filament_fits_continuous_fiber_lane(bundle, 0, *cfc));
    CHECK(filament_fits_continuous_fiber_lane(bundle, 0, *polymer));
    CHECK_FALSE(filament_fits_continuous_fiber_lane(bundle, 1, *polymer));
    std::string reason;
    CHECK(apply_filament_slot_selection(bundle, {1, cfc_name, bundle.printers.get_edited_preset().name, std::nullopt}, reason));
    CHECK_FALSE(apply_filament_slot_selection(bundle, {1, polymer_name, bundle.printers.get_edited_preset().name, std::nullopt}, reason));
}

TEST_CASE("Only an absent or empty filament_type falls back to the CFC name prefix", "[TinMan][Preset][FibreSeek]")
{
    Preset material(Preset::TYPE_FILAMENT, "CFC Legacy reinforcement", false);
    const unsigned state = GENERATE(0u, 1u, 2u, 3u, 4u);
    CAPTURE(state);
    switch (state) {
    case 0: material.config.erase("filament_type"); break;
    case 1: material.config.set_key_value("filament_type", new ConfigOptionStrings{}); break;
    case 2: material.config.set_key_value("filament_type", new ConfigOptionStrings{""}); break;
    case 3: material.config.set_key_value("filament_type", new ConfigOptionString("CFC")); break;
    case 4: material.config.set_key_value("filament_type", new ConfigOptionStrings{"CFC", "PCTG"}); break;
    }
    CHECK(filament_is_cfc_slot_profile(material) == (state <= 2));
}

TEST_CASE("Seeker 3 material changes keep the role map instead of automatic mapping", "[TinMan][Preset][FibreSeek][FibreSeekRoleMap]")
{
    PresetBundle bundle;
    auto &machine = bundle.printers.get_edited_preset().config;
    make_seeker3(machine);
    add_material(bundle, "CANDIDATE | PCTG", "PCTG");
    add_material(bundle, "CANDIDATE | PCTG + X-CCF", "CFC");
    add_material(bundle, "CANDIDATE | PCTG + X-CGF", "CFC");
    bundle.filament_presets = {"CANDIDATE | PCTG", "CANDIDATE | PCTG + X-CCF"};
    bundle.project_config.set_key_value("filament_map", new ConfigOptionInts{1, 1});
    bundle.project_config.set_key_value("filament_map_mode", new ConfigOptionEnum<FilamentMapMode>(fmmAutoForFlush));
    const auto check_roles = [&] {
        CHECK((project_map(bundle) == std::vector<int>{1, 2}));
        CHECK(project_mode(bundle) == fmmManual);
    };
    SECTION("slot reconciliation") {
        bundle.update_multi_material_filament_presets();
        check_roles();
        const auto effective = bundle.full_config(false);
        CHECK((effective.option<ConfigOptionInts>("filament_map")->values == std::vector<int>{1, 2}));
        CHECK(effective.option<ConfigOptionEnum<FilamentMapMode>>("filament_map_mode")->value == fmmManual);
    }
    SECTION("preset setter") {
        bundle.set_filament_preset(1, "CANDIDATE | PCTG + X-CGF");
        check_roles();
    }
    SECTION("filament combo commit") {
        std::string reason;
        REQUIRE(apply_filament_slot_selection(bundle,
            {1, "CANDIDATE | PCTG + X-CGF", bundle.printers.get_edited_preset().name, std::nullopt}, reason));
        check_roles();
    }
    SECTION("a third CFC material uses the composite nozzle too") {
        bundle.filament_presets.push_back("CANDIDATE | PCTG + X-CGF");
        bundle.update_multi_material_filament_presets();
        CHECK((project_map(bundle) == std::vector<int>{1, 2, 2}));
    }
    SECTION("an imported plate or project config gets the same map") {
        auto imported = bundle.project_config;
        imported.set_key_value("filament_map", new ConfigOptionInts{2, 1});
        imported.set_key_value("filament_map_mode", new ConfigOptionEnum<FilamentMapMode>(fmmAutoForMatch));
        REQUIRE(apply_fibreseek_material_tool_map(bundle, imported));
        CHECK((imported.option<ConfigOptionInts>("filament_map")->values == std::vector<int>{1, 2}));
        CHECK(imported.option<ConfigOptionEnum<FilamentMapMode>>("filament_map_mode")->value == fmmManual);
        CHECK(project_mode(bundle) == fmmAutoForFlush);
    }
    SECTION("roles follow the nozzle diameters, not the nozzle order") {
        machine.set_key_value("nozzle_diameter", new ConfigOptionFloats{0.7, 0.4});
        bundle.update_multi_material_filament_presets();
        CHECK((project_map(bundle) == std::vector<int>{2, 1}));
        CHECK(project_mode(bundle) == fmmManual);
    }
}

TEST_CASE("The role map is never guessed for other or unclear hardware", "[TinMan][Preset][FibreSeek][FibreSeekRoleMap]")
{
    PresetBundle bundle;
    auto &machine = bundle.printers.get_edited_preset().config;
    make_seeker3(machine);
    add_material(bundle, "PCTG", "PCTG");
    add_material(bundle, "Composite", "CFC");
    bundle.filament_presets = {"PCTG", "Composite"};
    const auto before = bundle.project_config;
    SECTION("an IDEX printer with fiber settings is not the Seeker 3") {
        machine.set_key_value("printer_model", new ConfigOptionString("Ordinary IDEX"));
        machine.set_key_value("fiber_enabled", new ConfigOptionBool(true));
        CHECK_FALSE(apply_fibreseek_material_tool_map(bundle, bundle.project_config));
        CHECK(bundle.project_config == before);
    }
    SECTION("the preset name alone does not identify the printer") {
        bundle.printers.get_edited_preset().name = "FibreSeek Seeker 3";
        machine.set_key_value("printer_model", new ConfigOptionString(""));
        CHECK_FALSE(apply_fibreseek_material_tool_map(bundle, bundle.project_config));
        CHECK(bundle.project_config == before);
    }
    SECTION("two nozzles with the plastic diameter") {
        machine.set_key_value("nozzle_diameter", new ConfigOptionFloats{0.4, 0.4, 0.7});
        CHECK_FALSE(apply_fibreseek_material_tool_map(bundle, bundle.project_config));
        CHECK(bundle.project_config == before);
    }
    SECTION("a missing material is not replaced") {
        bundle.filament_presets[1] = "Unavailable";
        CHECK_FALSE(apply_fibreseek_material_tool_map(bundle, bundle.project_config));
        CHECK(bundle.project_config == before);
    }
    SECTION("a single polymer on the single nozzle") {
        machine.set_key_value("nozzle_diameter", new ConfigOptionFloats{0.4});
        bundle.filament_presets = {"PCTG"};
        REQUIRE(apply_fibreseek_material_tool_map(bundle, bundle.project_config));
        CHECK((project_map(bundle) == std::vector<int>{1}));
        CHECK(project_mode(bundle) == fmmManual);
    }
}

TEST_CASE("A new Seeker 3 project can put a composite into material 2", "[TinMan][Preset][FibreSeek][FibreSeekRoleMap]")
{
    PresetBundle bundle;
    auto &machine = bundle.printers.get_edited_preset().config;
    make_seeker3(machine);
    machine.set_key_value("tinman_physical_printer_id", new ConfigOptionString(unit));
    add_material(bundle, "PCTG", "PCTG");
    auto composite = bundle.filaments.default_preset().config;
    composite.set_key_value("filament_type", new ConfigOptionStrings{"CFC"});
    composite.set_key_value("tinman_filament_scope_physical_id", new ConfigOptionStrings{unit});
    composite.set_key_value("tinman_filament_scope_nozzle_diameter", new ConfigOptionFloats{0.7});
    composite.set_key_value("tinman_filament_scope_tool", new ConfigOptionInts{1});
    bundle.filaments.load_preset("", "CANDIDATE | scoped PCTG + X-CCF", composite, false);
    bundle.filaments.select_preset_by_name("PCTG", true);
    bundle.filament_presets = {"PCTG"};
    bundle.update_multi_material_filament_presets();
    // Material 2 starts as a copy of the polymer but is already routed to the composite nozzle.
    REQUIRE(bundle.filament_presets.size() == 2);
    CHECK(bundle.filament_presets[1] == "PCTG");
    CHECK((project_map(bundle) == std::vector<int>{1, 2}));
    const Preset *candidate = static_cast<const PresetBundle &>(bundle).filaments.find_preset("CANDIDATE | scoped PCTG + X-CCF", false);
    REQUIRE(candidate);
    const Preset &printer = bundle.printers.get_edited_preset();
    CHECK(filament_fits_continuous_fiber_lane(bundle, 1, *candidate));
    CHECK(is_compatible_with_physical_printer_scope(*candidate, printer, size_t(project_map(bundle)[1] - 1)));
    CHECK_FALSE(is_compatible_with_physical_printer_scope(*candidate, printer, size_t(project_map(bundle)[0] - 1)));
    std::string reason;
    REQUIRE(apply_filament_slot_selection(bundle, {1, candidate->name, printer.name, std::nullopt}, reason));
    CHECK(reason.empty());
    CHECK(bundle.filament_presets[1] == "CANDIDATE | scoped PCTG + X-CCF");
    CHECK((project_map(bundle) == std::vector<int>{1, 2}));
}

TEST_CASE("Leaving the Seeker 3 restores the project's own mapping", "[TinMan][Preset][FibreSeek][FibreSeekRoleMap]")
{
    PresetBundle bundle;
    auto &machine = bundle.printers.get_edited_preset().config;
    make_seeker3(machine);
    machine.set_key_value("printer_model", new ConfigOptionString("Ordinary IDEX"));
    add_material(bundle, "PCTG", "PCTG");
    add_material(bundle, "Composite", "CFC");
    bundle.filament_presets = {"PCTG", "Composite"};
    const auto previous_mode = GENERATE(fmmAutoForFlush, fmmAutoForMatch, fmmManual);
    bundle.project_config.set_key_value("filament_map", new ConfigOptionInts{2, 1});
    bundle.project_config.set_key_value("filament_map_mode", new ConfigOptionEnum<FilamentMapMode>(previous_mode));
    machine.set_key_value("printer_model", new ConfigOptionString("FibreSeek Seeker 3"));
    bundle.update_multi_material_filament_presets();
    REQUIRE((project_map(bundle) == std::vector<int>{1, 2}));
    REQUIRE(project_mode(bundle) == fmmManual);
    SECTION("back and forth keeps the exact map and mode") {
        for (int i = 0; i < 2; ++i) {
            machine.set_key_value("printer_model", new ConfigOptionString("Ordinary IDEX"));
            bundle.update_multi_material_filament_presets();
            CHECK((project_map(bundle) == std::vector<int>{2, 1}));
            CHECK(project_mode(bundle) == previous_mode);
            machine.set_key_value("printer_model", new ConfigOptionString("FibreSeek Seeker 3"));
            bundle.update_multi_material_filament_presets();
        }
    }
    SECTION("the restored map fits the new printer's nozzles") {
        machine.set_key_value("printer_model", new ConfigOptionString("Ordinary single nozzle"));
        machine.set_key_value("nozzle_diameter", new ConfigOptionFloats{0.4});
        bundle.update_multi_material_filament_presets();
        CHECK((project_map(bundle) == std::vector<int>{1, 1}));
        CHECK(project_mode(bundle) == previous_mode);
    }
    SECTION("a map changed in the meantime is kept") {
        machine.set_key_value("printer_model", new ConfigOptionString("Ordinary IDEX"));
        bundle.project_config.set_key_value("filament_map", new ConfigOptionInts{2, 2});
        bundle.project_config.set_key_value("filament_map_mode", new ConfigOptionEnum<FilamentMapMode>(fmmAutoForMatch));
        bundle.update_multi_material_filament_presets();
        CHECK((project_map(bundle) == std::vector<int>{2, 2}));
        CHECK(project_mode(bundle) == fmmAutoForMatch);
    }
    SECTION("an incomplete Seeker 3 setup is not a printer change") {
        machine.set_key_value("nozzle_diameter", new ConfigOptionFloats{0.4, 0.4});
        CHECK_FALSE(normalize_fibreseek_material_mapping(bundle));
        CHECK((project_map(bundle) == std::vector<int>{1, 2}));
        machine.set_key_value("nozzle_diameter", new ConfigOptionFloats{0.4, 0.7});
        machine.set_key_value("printer_model", new ConfigOptionString("Ordinary IDEX"));
        bundle.update_multi_material_filament_presets();
        CHECK((project_map(bundle) == std::vector<int>{2, 1}));
        CHECK(project_mode(bundle) == previous_mode);
    }
    SECTION("a copied bundle restores the same way") {
        PresetBundle copy(bundle);
        copy.printers.get_edited_preset().config.set_key_value("printer_model", new ConfigOptionString("Ordinary IDEX"));
        copy.update_multi_material_filament_presets();
        CHECK((project_map(copy) == std::vector<int>{2, 1}));
        CHECK(project_mode(copy) == previous_mode);
    }
}

TEST_CASE("A loaded Seeker 3 project keeps its map and forgets the one from before", "[TinMan][Preset][FibreSeek][FibreSeekImport]")
{
    PresetBundle source;
    make_seeker3(source.printers.get_edited_preset().config);
    add_material(source, "PCTG", "PCTG");
    add_material(source, "Composite", "CFC");
    source.filaments.select_preset_by_name("PCTG", true);
    source.filament_presets = {"PCTG", "Composite"};
    source.update_multi_material_filament_presets();
    DynamicPrintConfig saved = source.full_config_secure();
    REQUIRE((saved.option<ConfigOptionInts>("filament_map")->values == std::vector<int>{1, 2}));
    REQUIRE(saved.option<ConfigOptionEnum<FilamentMapMode>>("filament_map_mode")->value == fmmManual);

    // This bundle had its own mapping before the Seeker 3 was selected.
    PresetBundle imported;
    auto &machine = imported.printers.get_edited_preset().config;
    machine.set_key_value("nozzle_diameter", new ConfigOptionFloats{0.4, 0.7});
    add_material(imported, "PCTG", "PCTG");
    imported.filament_presets = {"PCTG", "PCTG"};
    imported.project_config.set_key_value("filament_map", new ConfigOptionInts{2, 1});
    imported.project_config.set_key_value("filament_map_mode", new ConfigOptionEnum<FilamentMapMode>(fmmAutoForMatch));
    make_seeker3(machine);
    imported.update_multi_material_filament_presets();
    REQUIRE(imported.fibreseek_mapping.ordinary);

    REQUIRE_NOTHROW(imported.load_config_model("fibreseek_project.3mf", std::move(saved)));
    CHECK(selected_printer_is_fibreseek_seeker3(imported));
    const auto effective = imported.full_config_secure();
    CHECK((effective.option<ConfigOptionInts>("filament_map")->values == std::vector<int>{1, 2}));
    CHECK(effective.option<ConfigOptionEnum<FilamentMapMode>>("filament_map_mode")->value == fmmManual);
    CHECK((effective.option<ConfigOptionStrings>("filament_type")->values == std::vector<std::string>{"PCTG", "CFC"}));

    imported.printers.get_edited_preset().config.set_key_value("printer_model", new ConfigOptionString("Ordinary IDEX"));
    imported.update_multi_material_filament_presets();
    CHECK((project_map(imported) == std::vector<int>{1, 2}));
    CHECK(project_mode(imported) == fmmManual);
}

TEST_CASE("The fiber choice finds the composite by material data without changing anything", "[TinMan][Preset][FibreSeek][CfcResolver]")
{
    ResolverFixture fixture;
    const std::string name = "CANDIDATE | Unit A | 0.7mm | PCTG + X-CCF | r01";
    auto &candidate = fixture.add_composite(name);
    const bool recipe = GENERATE(false, true);
    if (recipe) {
        // A recipe profile's source ID names the recipe, so it is not compared.
        candidate.config.set_key_value("fiber_material_kind", new ConfigOptionStrings{"composite_matrix"});
        candidate.config.set_key_value("fiber_source_material_id", new ConfigOptionStrings{"pctg-x-ccf-recipe"});
    }
    auto &bundle = fixture.bundle;
    const auto editor  = bundle.filaments.get_edited_preset().config;
    const auto machine = bundle.printers.get_edited_preset().config;
    const auto project = bundle.project_config;
    const auto slots   = bundle.filament_presets;
    std::string reason;
    const auto result = resolve_continuous_fiber_filament(bundle, fixture.identity, reason);
    REQUIRE(result);
    CHECK(*result == name);
    CHECK(reason.empty());
    CHECK(bundle.project_config == project);
    CHECK(bundle.filament_presets == slots);
    REQUIRE(apply_filament_slot_selection(bundle, {1, *result, bundle.printers.get_edited_preset().name, std::nullopt}, reason));
    CHECK((project_map(bundle) == std::vector<int>{1, 2}));
    CHECK(project_mode(bundle) == fmmManual);
    CHECK(bundle.filaments.get_edited_preset().config == editor);
    CHECK(bundle.printers.get_edited_preset().config == machine);
}

TEST_CASE("The fiber choice keeps the current composite and refuses to guess between others", "[TinMan][Preset][FibreSeek][CfcResolver]")
{
    ResolverFixture fixture;
    fixture.add_composite("Candidate A");
    fixture.add_composite("Candidate B");
    auto &bundle = fixture.bundle;
    std::string reason;
    SECTION("two matches and neither selected") {
        const auto project = bundle.project_config;
        const auto slots   = bundle.filament_presets;
        const auto machine = bundle.printers.get_edited_preset().config;
        CHECK_FALSE(resolve_continuous_fiber_filament(bundle, fixture.identity, reason));
        CHECK(reason.find("Several") != std::string::npos);
        CHECK(bundle.project_config == project);
        CHECK(bundle.filament_presets == slots);
        CHECK(bundle.printers.get_edited_preset().config == machine);
    }
    SECTION("the selected match wins") {
        bundle.set_filament_preset(1, "Candidate B");
        const auto result = resolve_continuous_fiber_filament(bundle, fixture.identity, reason);
        REQUIRE(result);
        CHECK(*result == "Candidate B");
        CHECK(reason.empty());
    }
}

TEST_CASE("The fiber choice never picks a foreign, incompatible or mislabeled composite", "[TinMan][Preset][FibreSeek][CfcResolver]")
{
    ResolverFixture fixture;
    auto &candidate = fixture.add_composite("CFC PCTG + X-CCF misleading display name");
    const auto assert_rejected = [&] {
        const auto project = fixture.bundle.project_config;
        const auto machine = fixture.bundle.printers.get_edited_preset().config;
        const auto slots   = fixture.bundle.filament_presets;
        std::string reason;
        CHECK_FALSE(resolve_continuous_fiber_filament(fixture.bundle, fixture.identity, reason));
        CHECK_FALSE(reason.empty());
        CHECK(fixture.bundle.project_config == project);
        CHECK(fixture.bundle.printers.get_edited_preset().config == machine);
        CHECK(fixture.bundle.filament_presets == slots);
    };
    SECTION("tuned on another physical printer") {
        candidate.config.set_key_value("tinman_filament_scope_physical_id", new ConfigOptionStrings{"09296b70-8bf6-4a97-afae-3db99345f57b"});
        assert_rejected();
    }
    SECTION("tuned on the plastic nozzle") {
        candidate.config.set_key_value("tinman_filament_scope_nozzle_diameter", new ConfigOptionFloats{0.4});
        candidate.config.set_key_value("tinman_filament_scope_tool", new ConfigOptionInts{0});
        assert_rejected();
    }
    SECTION("incompatible with the process") {
        candidate.config.set_key_value("compatible_prints", new ConfigOptionStrings{"Another process"});
        assert_rejected();
    }
    SECTION("incompatible with the printer") {
        candidate.config.set_key_value("compatible_printers", new ConfigOptionStrings{"Another printer"});
        assert_rejected();
    }
    SECTION("an ordinary filament_type") {
        candidate.config.set_key_value("filament_type", new ConfigOptionStrings{"PCTG"});
        assert_rejected();
    }
    SECTION("another matrix") {
        candidate.config.set_key_value("fiber_plastic_type", new ConfigOptionStrings{"PETG"});
        assert_rejected();
    }
    SECTION("another polymer in material 1") {
        // Material 1 is the polymer open in the editor.
        fixture.bundle.filaments.get_edited_preset().config.set_key_value("filament_type", new ConfigOptionStrings{"PETG"});
        assert_rejected();
    }
    SECTION("another strand with the same fiber type") {
        candidate.config.set_key_value("fiber_source_material_id", new ConfigOptionStrings{"another-carbon-product"});
        assert_rejected();
    }
    SECTION("another fiber name with the same source ID") {
        candidate.config.set_key_value("fiber_name", new ConfigOptionStrings{"Another carbon product"});
        assert_rejected();
    }
    SECTION("malformed source metadata") {
        candidate.config.set_key_value("fiber_source_material_id", new ConfigOptionInt(7));
        assert_rejected();
    }
    SECTION("an unrelated material kind") {
        candidate.config.set_key_value("fiber_material_kind", new ConfigOptionStrings{"plastic"});
        assert_rejected();
    }
    SECTION("a hidden profile") {
        candidate.is_visible = false;
        assert_rejected();
    }
    SECTION("a printer without continuous fiber") {
        auto &machine = fixture.bundle.printers.get_edited_preset().config;
        machine.set_key_value("printer_model", new ConfigOptionString("Ordinary printer"));
        machine.set_key_value("composite_nozzle_diameter", new ConfigOptionFloat(0.4));
        REQUIRE_FALSE(selected_printer_supports_continuous_fiber(fixture.bundle));
        assert_rejected();
    }
}

TEST_CASE("Ordinary multi-material and IDEX printers get no lane rule or role map", "[TinMan][Preset][FibreSeek]")
{
    PresetBundle bundle;
    add_material(bundle, "Polymer");
    add_material(bundle, "CFC Named material", "CFC");
    const unsigned tools = GENERATE(1u, 2u, 4u);
    bundle.printers.get_edited_preset().config.set_key_value("nozzle_diameter", new ConfigOptionFloats(std::vector<double>(tools, 0.4)));
    bundle.filament_presets.assign(8, "Polymer");
    const auto slot = GENERATE(size_t(0), size_t(1), size_t(7));
    REQUIRE_FALSE(selected_printer_supports_continuous_fiber(bundle));
    const auto map  = project_map(bundle);
    const auto mode = project_mode(bundle);
    std::string reason;
    REQUIRE(apply_filament_slot_selection(bundle, {slot, "CFC Named material", bundle.printers.get_edited_preset().name, std::nullopt}, reason));
    CHECK(bundle.filament_presets.size() == 8);
    CHECK(project_map(bundle) == map);
    CHECK(project_mode(bundle) == mode);
    CHECK_FALSE(fibreseek_material_tool_map(bundle));
}

TEST_CASE("A fiber choice commits material 2 and the project's fiber data together", "[TinMan][Preset][FibreSeek][CfcCommit]")
{
    ResolverFixture fixture;
    auto &bundle = fixture.bundle;
    auto &carbon = fixture.add_composite("CFC PCTG + X-CCF");
    const bool has_strand_data = GENERATE(true, false);
    if (has_strand_data) {
        // Synthetic strand data, as a user would enter it from the fibre's datasheet.
        carbon.config.set_key_value("fiber_diameter", new ConfigOptionFloats{0.31});
        carbon.config.set_key_value("fiber_linear_density", new ConfigOptionFloats{88.});
    }
    std::string reason;
    REQUIRE(commit_continuous_fiber_choice(bundle, fixture.identity, reason));
    CHECK(bundle.filament_presets[1] == "CFC PCTG + X-CCF");
    CHECK((project_map(bundle) == std::vector<int>{1, 2}));
    const auto &project = bundle.project_config;
    CHECK(project.opt_string("continuous_fiber_name") == fixture.identity.fiber_name);
    CHECK(project.opt_string("continuous_fiber_type") == fixture.identity.fiber_type);
    CHECK(project.opt_string("continuous_fiber_source_material_id") == fixture.identity.source_material_id);
    // Tinman supplies no strand data of its own: without the profile's values they stay unset.
    const auto value = [&project](const char *key) {
        const auto *option = project.option<ConfigOptionFloat>(key);
        return option ? option->value : 0.;
    };
    CHECK(value("continuous_fiber_diameter") == Catch::Approx(has_strand_data ? 0.31 : 0.));
    CHECK(value("continuous_fiber_linear_density") == Catch::Approx(has_strand_data ? 88. : 0.));
}

TEST_CASE("A failed fiber choice leaves a project's composite and fiber data as they were", "[TinMan][Preset][FibreSeek][CfcCommit]")
{
    ResolverFixture fixture;
    auto &bundle = fixture.bundle;
    // The loaded project prints a glass composite in material 2.
    const ContinuousFiberFilamentIdentity glass{"PCTG", "cgf-035", "CGF Glass fiber 0.35 mm", "glass_fiber"};
    auto &composite = fixture.add_composite("CFC PCTG + CGF", &glass);
    composite.config.set_key_value("fiber_diameter", new ConfigOptionFloats{0.33});
    composite.config.set_key_value("fiber_linear_density", new ConfigOptionFloats{150.});
    std::string reason;
    REQUIRE(commit_continuous_fiber_choice(bundle, glass, reason));
    const auto slots   = bundle.filament_presets;
    const auto project = bundle.project_config;

    // The fiber remembered from another project is carbon, and no carbon composite exists.
    CHECK_FALSE(commit_continuous_fiber_choice(bundle, fixture.identity, reason));
    CHECK_FALSE(reason.empty());
    CHECK(bundle.filament_presets == slots);
    CHECK(bundle.project_config == project);

    // Saved and reloaded, the project still describes the glass composite it prints.
    const boost::filesystem::path file = boost::filesystem::temp_directory_path() /
                                         boost::filesystem::unique_path("tinman-fibre-project-%%%%%%%%.json");
    bundle.project_config.save_to_json(file.string(), "project", "project", "2.4.2");
    DynamicPrintConfig reloaded;
    std::map<std::string, std::string> key_values;
    reloaded.load_from_json(file.string(), ForwardCompatibilitySubstitutionRule::Disable, key_values, reason);
    boost::system::error_code ec;
    boost::filesystem::remove(file, ec);
    CHECK(reloaded.opt_string("continuous_fiber_name") == glass.fiber_name);
    CHECK(reloaded.opt_float("continuous_fiber_diameter") == Catch::Approx(0.33));
    CHECK(reloaded.opt_float("continuous_fiber_linear_density") == Catch::Approx(150.));
}

TEST_CASE("The fiber choice checks a composite's unsaved edits, as slicing does", "[TinMan][Preset][FibreSeek][CfcResolver]")
{
    ResolverFixture fixture;
    auto &bundle = fixture.bundle;
    fixture.add_composite("CFC PCTG + X-CCF");
    std::string reason;
    REQUIRE(resolve_continuous_fiber_filament(bundle, fixture.identity, reason));
    // Open the composite in the editor and change it without saving.
    bundle.filaments.select_preset_by_name("CFC PCTG + X-CCF", true);
    auto &edited = bundle.filaments.get_edited_preset().config;
    SECTION("another fiber") {
        edited.set_key_value("fiber_name", new ConfigOptionStrings{"Another fiber"});
        CHECK_FALSE(resolve_continuous_fiber_filament(bundle, fixture.identity, reason));
    }
    SECTION("another matrix") {
        edited.set_key_value("fiber_plastic_type", new ConfigOptionStrings{"PETG"});
        CHECK_FALSE(resolve_continuous_fiber_filament(bundle, fixture.identity, reason));
    }
    SECTION("scoped to another physical printer") {
        edited.set_key_value("tinman_filament_scope_physical_id", new ConfigOptionStrings{"0f0e0d0c-0b0a-0908-0706-050403020100"});
        CHECK_FALSE(resolve_continuous_fiber_filament(bundle, fixture.identity, reason));
    }
    // Discarding the edits makes the saved composite the effective one again.
    bundle.filaments.discard_current_changes();
    CHECK(resolve_continuous_fiber_filament(bundle, fixture.identity, reason));
}

TEST_CASE("Fiber linear density is g/km throughout", "[TinMan][Preset][FibreSeek][CfcUnits]")
{
    CHECK(fiber_linear_density_g_per_km(102.) == Catch::Approx(102.));
    // A value below 1 can only be g/m, the setting's earlier label.
    CHECK(fiber_linear_density_g_per_km(0.102) == Catch::Approx(102.));
    CHECK(fiber_linear_density_g_per_km(0.) == 0.);
    CHECK(fiber_linear_density_g_per_km(-5.) == 0.);
}

TEST_CASE("Strand data is copied only when the composite sets it", "[TinMan][Preset][FibreSeek][CfcCommit]")
{
    Preset composite(Preset::TYPE_FILAMENT, "CFC Custom composite", false);
    composite.config.set_key_value("fiber_name", new ConfigOptionStrings{"Custom fiber"});
    DynamicPrintConfig project;
    project.set_key_value("continuous_fiber_diameter", new ConfigOptionFloat(0.27));
    project.set_key_value("continuous_fiber_linear_density", new ConfigOptionFloat(90.));
    project.set_key_value("continuous_fiber_name", new ConfigOptionString("Old fiber"));
    SECTION("an unset profile keeps the project's explicit values") {
        copy_continuous_fiber_metadata(composite, project);
        CHECK(project.opt_float("continuous_fiber_diameter") == Catch::Approx(0.27));
        CHECK(project.opt_float("continuous_fiber_linear_density") == Catch::Approx(90.));
        CHECK(project.opt_string("continuous_fiber_name") == "Custom fiber"); // the identity always follows
    }
    SECTION("set profile values win, converted to g/km") {
        composite.config.set_key_value("fiber_diameter", new ConfigOptionFloats{0.31});
        composite.config.set_key_value("fiber_linear_density", new ConfigOptionFloats{0.088});
        copy_continuous_fiber_metadata(composite, project);
        CHECK(project.opt_float("continuous_fiber_diameter") == Catch::Approx(0.31));
        CHECK(project.opt_float("continuous_fiber_linear_density") == Catch::Approx(88.));
    }
}

TEST_CASE("The print configuration takes strand data from the effective composite", "[TinMan][Preset][FibreSeek][CfcCommit]")
{
    DynamicPrintConfig config;
    config.set_key_value("filament_type", new ConfigOptionStrings{"PCTG", "CFC"});
    config.set_key_value("fiber_diameter", new ConfigOptionFloats{0., 0.31});
    config.set_key_value("fiber_linear_density", new ConfigOptionFloats{0., 88.});
    config.set_key_value("fiber_feed_factor", new ConfigOptionFloats{0., 0.8});
    config.set_key_value("continuous_fiber_diameter", new ConfigOptionFloat(0.27));
    config.set_key_value("continuous_fiber_linear_density", new ConfigOptionFloat(0.));
    REQUIRE(continuous_fiber_composite_slot(config) == std::optional<size_t>(1));
    CHECK(continuous_fiber_composite_value(config, "fiber_feed_factor") == Catch::Approx(0.8));
    apply_continuous_fiber_strand_data(config);
    CHECK(config.opt_float("continuous_fiber_diameter") == Catch::Approx(0.31));
    CHECK(config.opt_float("continuous_fiber_linear_density") == Catch::Approx(88.));
    SECTION("an unset composite keeps an older project's explicit values") {
        config.set_key_value("fiber_diameter", new ConfigOptionFloats{0., 0.});
        config.set_key_value("continuous_fiber_diameter", new ConfigOptionFloat(0.27));
        apply_continuous_fiber_strand_data(config);
        CHECK(config.opt_float("continuous_fiber_diameter") == Catch::Approx(0.27));
    }
    SECTION("no composite, no change") {
        config.set_key_value("filament_type", new ConfigOptionStrings{"PCTG", "PLA"});
        config.set_key_value("continuous_fiber_diameter", new ConfigOptionFloat(0.27));
        apply_continuous_fiber_strand_data(config);
        CHECK(config.opt_float("continuous_fiber_diameter") == Catch::Approx(0.27));
        CHECK(continuous_fiber_composite_value(config, "fiber_feed_factor") == 0.);
    }
}

TEST_CASE("Slicing reads unsaved edits of any composite, listed or custom", "[TinMan][Preset][FibreSeek][CfcCommit]")
{
    ResolverFixture fixture;
    auto &bundle = fixture.bundle;
    // A custom product the fiber list does not name.
    const ContinuousFiberFilamentIdentity custom{"PCTG", "", "My own carbon tow", "carbon_fiber"};
    auto &composite = fixture.add_composite("CFC PCTG + my tow", &custom);
    composite.config.set_key_value("fiber_diameter", new ConfigOptionFloats{0.3});
    std::string reason;
    REQUIRE(commit_continuous_fiber_choice(bundle, custom, reason));
    CHECK(bundle.full_config().opt_float("continuous_fiber_diameter") == Catch::Approx(0.3));
    // Edit the composite without saving: slicing uses the edit.
    bundle.filaments.select_preset_by_name("CFC PCTG + my tow", true);
    bundle.filaments.get_edited_preset().config.set_key_value("fiber_diameter", new ConfigOptionFloats{0.29});
    CHECK(bundle.full_config().opt_float("continuous_fiber_diameter") == Catch::Approx(0.29));
}

TEST_CASE("Missing strand settings are named", "[TinMan][Preset][FibreSeek][CfcUnits]")
{
    Preset composite(Preset::TYPE_FILAMENT, "CFC Composite", false);
    CHECK((missing_continuous_fiber_settings(composite) ==
           std::vector<std::string>{"Fiber diameter", "Fiber linear density", "Fiber feed factor"}));
    composite.config.set_key_value("fiber_diameter", new ConfigOptionFloats{0.3});
    composite.config.set_key_value("fiber_linear_density", new ConfigOptionFloats{90.});
    composite.config.set_key_value("fiber_feed_factor", new ConfigOptionFloats{0.8});
    CHECK(missing_continuous_fiber_settings(composite).empty());
}

TEST_CASE("A fiber choice rolls back every field it touches", "[TinMan][Preset][FibreSeek][CfcCommit]")
{
    ResolverFixture fixture;
    auto &bundle = fixture.bundle;
    // Adding a slot rewrites the slots, palette and project, as the choice's own
    // expansion does; the snapshot must undo all of it.
    const auto slots   = bundle.filament_presets;
    const auto project = bundle.project_config;
    const auto palette = bundle.ams_multi_color_filment;
    const ContinuousFiberSelectionSnapshot before(bundle);
    bundle.set_num_filaments(3);
    bundle.project_config.set_key_value("continuous_fiber_name", new ConfigOptionString("Changed"));
    REQUIRE(bundle.filament_presets != slots);
    before.restore(bundle);
    CHECK(bundle.filament_presets == slots);
    CHECK(bundle.project_config == project);
    CHECK(bundle.ams_multi_color_filment == palette);
}

TEST_CASE("A composite that is CFC only in its unsaved edits can be chosen", "[TinMan][Preset][FibreSeek][CfcCommit]")
{
    ResolverFixture fixture;
    auto &bundle = fixture.bundle;
    // Saved as an ordinary polymer; the open editor makes it the composite.
    add_material(bundle, "Draft composite", "PCTG");
    bundle.filaments.select_preset_by_name("Draft composite", true);
    auto &edited = bundle.filaments.get_edited_preset().config;
    edited.set_key_value("filament_type", new ConfigOptionStrings{"CFC"});
    edited.set_key_value("fiber_plastic_type", new ConfigOptionStrings{fixture.identity.matrix_type});
    edited.set_key_value("fiber_source_material_id", new ConfigOptionStrings{fixture.identity.source_material_id});
    edited.set_key_value("fiber_material_kind", new ConfigOptionStrings{"continuous_fiber"});
    edited.set_key_value("fiber_name", new ConfigOptionStrings{fixture.identity.fiber_name});
    edited.set_key_value("fiber_type", new ConfigOptionStrings{fixture.identity.fiber_type});
    edited.set_key_value("tinman_filament_scope_physical_id", new ConfigOptionStrings{unit});
    edited.set_key_value("tinman_filament_scope_nozzle_diameter", new ConfigOptionFloats{0.7});
    edited.set_key_value("tinman_filament_scope_tool", new ConfigOptionInts{1});
    std::string reason;
    CHECK(commit_continuous_fiber_choice(bundle, fixture.identity, reason));
    CHECK(reason.empty());
    CHECK(bundle.filament_presets[1] == "Draft composite");
}
