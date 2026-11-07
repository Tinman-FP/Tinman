#include <catch2/catch_all.hpp>

#include <boost/filesystem.hpp>
#include <limits>
#include <set>

#include "libslic3r/FilamentSelection.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"

using namespace Slic3r;

namespace {
const std::string unit_a = "80c00526-29f8-47c8-813a-4d36de799571";
const std::string unit_b = "a40b2a80-a38c-4d10-b350-52c0cfd3539d";

struct ScopeFixture {
    Preset printer{Preset::TYPE_PRINTER, "Same model"};
    Preset process{Preset::TYPE_PRINT, "Process"};
    Preset filament{Preset::TYPE_FILAMENT, "Material"};

    ScopeFixture()
    {
        printer.config.set_key_value("tinman_physical_printer_id", new ConfigOptionString(unit_a));
        printer.config.set_key_value("nozzle_diameter", new ConfigOptionFloats{0.4, 0.7});
        printer.config.set_key_value("inherits", new ConfigOptionString("Public stock parent"));
        process.config.set_key_value("tinman_process_scope_physical_id", new ConfigOptionString(unit_a));
        process.config.set_key_value("tinman_process_scope_nozzle_diameters", new ConfigOptionFloats{0.4, 0.7});
        filament.config.set_key_value("tinman_filament_scope_physical_id", new ConfigOptionStrings{unit_a});
        filament.config.set_key_value("tinman_filament_scope_nozzle_diameter", new ConfigOptionFloats{0.7});
        filament.config.set_key_value("tinman_filament_scope_tool", new ConfigOptionInts{1});
    }

    bool compatible(const Preset &preset) const
    {
        return is_compatible_with_printer(PresetWithVendorProfile(preset, nullptr),
                                          PresetWithVendorProfile(printer, nullptr));
    }
};

struct TemporaryScopeDirectory {
    boost::filesystem::path path = boost::filesystem::temp_directory_path() /
                                  boost::filesystem::unique_path("tinman-scope-%%%%-%%%%");
    TemporaryScopeDirectory() { boost::filesystem::create_directories(path); }
    ~TemporaryScopeDirectory()
    {
        boost::system::error_code error;
        boost::filesystem::remove_all(path, error);
    }
};
}

TEST_CASE("Physical scope precedes all ordinary printer compatibility shortcuts", "[TinMan][Preset][PhysicalScope]")
{
    ScopeFixture fixture;
    const bool material = GENERATE(false, true);
    auto &preset = material ? fixture.filament : fixture.process;
    REQUIRE(fixture.compatible(preset));

    SECTION("Identically named same-model unit is not the qualified physical unit") {
        fixture.printer.config.set_key_value("tinman_physical_printer_id", new ConfigOptionString(unit_b));
        CHECK_FALSE(fixture.compatible(preset));
    }
    SECTION("Display names are not identity") {
        fixture.printer.name = "Renamed workshop printer";
        CHECK(fixture.compatible(preset));
    }
    SECTION("Explicit printer list cannot bypass scope") {
        preset.config.set_key_value("compatible_printers", new ConfigOptionStrings{fixture.printer.name});
        fixture.printer.config.set_key_value("tinman_physical_printer_id", new ConfigOptionString(unit_b));
        CHECK_FALSE(fixture.compatible(preset));
    }
    SECTION("Parent-list compatibility cannot bypass scope") {
        preset.config.set_key_value("compatible_printers", new ConfigOptionStrings{"Public stock parent"});
        CHECK(fixture.compatible(preset));
        fixture.printer.config.set_key_value("tinman_physical_printer_id", new ConfigOptionString(unit_b));
        CHECK_FALSE(fixture.compatible(preset));
    }
    SECTION("Default and empty-name compatibility cannot bypass scope") {
        preset.is_default = true;
        fixture.printer.name.clear();
        fixture.printer.config.set_key_value("tinman_physical_printer_id", new ConfigOptionString(unit_b));
        CHECK_FALSE(fixture.compatible(preset));
    }
    SECTION("True compatibility expression cannot bypass scope") {
        preset.compatible_printers_condition() = "nozzle_diameter[0] == 0.4";
        fixture.printer.config.set_key_value("tinman_physical_printer_id", new ConfigOptionString(unit_b));
        CHECK_FALSE(fixture.compatible(preset));
    }
    SECTION("Scope does not bypass existing model constraints") {
        preset.config.set_key_value("compatible_printers", new ConfigOptionStrings{"Another model"});
        CHECK_FALSE(fixture.compatible(preset));
    }
}

TEST_CASE("Process physical scope checks the complete ordered nozzle configuration", "[TinMan][Preset][PhysicalScope]")
{
    ScopeFixture fixture;
    SECTION("Other physical tool changed") {
        fixture.printer.config.set_key_value("nozzle_diameter", new ConfigOptionFloats{0.6, 0.7});
        CHECK_FALSE(fixture.compatible(fixture.process));
    }
    SECTION("Same nozzle sizes in a different order") {
        fixture.printer.config.set_key_value("nozzle_diameter", new ConfigOptionFloats{0.7, 0.4});
        CHECK_FALSE(fixture.compatible(fixture.process));
    }
    SECTION("Nozzle vector length changed") {
        fixture.printer.config.set_key_value("nozzle_diameter", new ConfigOptionFloats{0.4});
        CHECK_FALSE(fixture.compatible(fixture.process));
    }
    SECTION("Round-trip precision is not display rounding") {
        fixture.printer.config.set_key_value("nozzle_diameter", new ConfigOptionFloats{0.4000001, 0.7});
        CHECK(fixture.compatible(fixture.process));
        fixture.printer.config.set_key_value("nozzle_diameter", new ConfigOptionFloats{0.4001, 0.7});
        CHECK_FALSE(fixture.compatible(fixture.process));
    }
}

TEST_CASE("Material scope is tied to a physical tool rather than material-slot number", "[TinMan][Preset][PhysicalScope]")
{
    ScopeFixture fixture;
    REQUIRE(is_compatible_with_physical_printer_scope(fixture.filament, fixture.printer, 1));
    CHECK_FALSE(is_compatible_with_physical_printer_scope(fixture.filament, fixture.printer, 0));
    CHECK_FALSE(is_compatible_with_physical_printer_scope(fixture.filament, fixture.printer, 7));

    SECTION("Equal diameter tools remain distinct") {
        fixture.printer.config.set_key_value("nozzle_diameter", new ConfigOptionFloats{0.7, 0.7});
        CHECK(is_compatible_with_physical_printer_scope(fixture.filament, fixture.printer, 1));
        CHECK_FALSE(is_compatible_with_physical_printer_scope(fixture.filament, fixture.printer, 0));
    }
    SECTION("SEMM slot seven can resolve to physical tool zero") {
        fixture.printer.config.set_key_value("nozzle_diameter", new ConfigOptionFloats{0.7});
        fixture.printer.config.set_key_value("single_extruder_multi_material", new ConfigOptionBool(true));
        fixture.filament.config.set_key_value("tinman_filament_scope_tool", new ConfigOptionInts{0});
        CHECK(is_compatible_with_physical_printer_scope(fixture.filament, fixture.printer, 0));
        CHECK_FALSE(is_compatible_with_physical_printer_scope(fixture.filament, fixture.printer, 7));
    }
    SECTION("The qualified nozzle changed") {
        fixture.printer.config.set_key_value("nozzle_diameter", new ConfigOptionFloats{0.4, 0.8});
        CHECK_FALSE(fixture.compatible(fixture.filament));
    }
}

TEST_CASE("Malformed or partial physical scope fails closed", "[TinMan][Preset][PhysicalScope]")
{
    ScopeFixture fixture;
    const auto check_both = [&] {
        CHECK_FALSE(fixture.compatible(fixture.process));
        CHECK_FALSE(fixture.compatible(fixture.filament));
    };
    SECTION("Missing target UUID") {
        fixture.printer.config.erase("tinman_physical_printer_id");
        check_both();
    }
    SECTION("Malformed target UUID") {
        const std::string bad = GENERATE(std::string("Same model"), std::string("00000000-0000-0000-0000-000000000000"),
                                        std::string("80C00526-29f8-47c8-813a-4d36de799571"));
        fixture.printer.config.set_key_value("tinman_physical_printer_id", new ConfigOptionString(bad));
        check_both();
    }
    SECTION("Wrong target UUID type") {
        fixture.printer.config.set_key_value("tinman_physical_printer_id", new ConfigOptionInts{1});
        check_both();
    }
    SECTION("Wrong target nozzle type") {
        fixture.printer.config.set_key_value("nozzle_diameter", new ConfigOptionString("0.7"));
        check_both();
    }
    SECTION("Nonfinite or nonpositive target nozzle") {
        const double bad = GENERATE(0.0, -0.4, std::numeric_limits<double>::infinity(),
                                    std::numeric_limits<double>::quiet_NaN());
        fixture.printer.config.set_key_value("nozzle_diameter", new ConfigOptionFloats{bad, 0.7});
        check_both();
    }
    SECTION("Process has only its UUID") {
        fixture.process.config.erase("tinman_process_scope_nozzle_diameters");
        CHECK_FALSE(fixture.compatible(fixture.process));
    }
    SECTION("Process has only its nozzle tuple") {
        fixture.process.config.erase("tinman_process_scope_physical_id");
        CHECK_FALSE(fixture.compatible(fixture.process));
    }
    SECTION("Bound process cannot use the unscoped zero sentinel") {
        fixture.process.config.set_key_value("tinman_process_scope_nozzle_diameters", new ConfigOptionFloats{0.0});
        CHECK_FALSE(fixture.compatible(fixture.process));
    }
    SECTION("Process scope option has the wrong type") {
        fixture.process.config.set_key_value("tinman_process_scope_physical_id", new ConfigOptionStrings{unit_a});
        CHECK_FALSE(fixture.compatible(fixture.process));
    }
    SECTION("Material scope tool is missing or invalid") {
        fixture.filament.config.erase("tinman_filament_scope_tool");
        CHECK_FALSE(fixture.compatible(fixture.filament));
        for (int tool : {-1, 2}) {
            fixture.filament.config.set_key_value("tinman_filament_scope_tool", new ConfigOptionInts{tool});
            CHECK_FALSE(fixture.compatible(fixture.filament));
        }
    }
    SECTION("Packed project vector is not a single material preset") {
        fixture.filament.config.set_key_value("tinman_filament_scope_physical_id", new ConfigOptionStrings{unit_a, unit_b});
        CHECK_FALSE(fixture.compatible(fixture.filament));
    }
    SECTION("Material option has the wrong type even if its value looks empty") {
        fixture.filament.config.set_key_value("tinman_filament_scope_tool", new ConfigOptionString(""));
        CHECK_FALSE(fixture.compatible(fixture.filament));
    }
    SECTION("A nozzle tuple alone cannot invent a material binding") {
        fixture.filament.config.erase("tinman_filament_scope_physical_id");
        CHECK_FALSE(fixture.compatible(fixture.filament));
    }
    SECTION("Malformed material nozzle values are rejected") {
        const double bad = GENERATE(0.0, -0.7, std::numeric_limits<double>::infinity(),
                                    std::numeric_limits<double>::quiet_NaN());
        fixture.filament.config.set_key_value("tinman_filament_scope_nozzle_diameter", new ConfigOptionFloats{bad});
        CHECK_FALSE(fixture.compatible(fixture.filament));
    }
}

TEST_CASE("Unscoped stock templates and SLA retain legacy compatibility", "[TinMan][Preset][PhysicalScope]")
{
    PresetBundle bundle;
    auto &printer = bundle.printers.get_edited_preset();
    const auto &process = bundle.prints.get_edited_preset();
    const auto &filament = bundle.filaments.get_edited_preset();
    CHECK(is_compatible_with_physical_printer_scope(process, printer));
    CHECK(is_compatible_with_physical_printer_scope(filament, printer));
    CHECK(is_compatible_with_printer(PresetWithVendorProfile(process, nullptr), PresetWithVendorProfile(printer, nullptr)));
    CHECK(is_compatible_with_printer(PresetWithVendorProfile(filament, nullptr), PresetWithVendorProfile(printer, nullptr)));
    REQUIRE(filament.config.option<ConfigOptionStrings>("tinman_filament_scope_physical_id"));
    CHECK(filament.config.option<ConfigOptionStrings>("tinman_filament_scope_physical_id")->values ==
          std::vector<std::string>{""});
    CHECK(filament.config.option<ConfigOptionInts>("tinman_filament_scope_tool")->values == std::vector<int>{-1});

    ScopeFixture fixture;
    fixture.printer.printer_technology_ref() = ptSLA;
    fixture.printer.config.set_key_value("tinman_physical_printer_id", new ConfigOptionString(unit_b));
    CHECK(is_compatible_with_physical_printer_scope(fixture.process, fixture.printer));
    CHECK(is_compatible_with_physical_printer_scope(fixture.filament, fixture.printer));
}

TEST_CASE("Typed physical scope survives JSON persistence and material-slot packing", "[TinMan][Preset][PhysicalScope]")
{
    TemporaryScopeDirectory directory;
    ScopeFixture fixture;
    for (const Preset *preset : {&fixture.printer, &fixture.process, &fixture.filament}) {
        const std::string file = (directory.path / (preset->name + ".json")).string();
        preset->config.save_to_json(file, preset->name, "User", "2.4.2");
        DynamicPrintConfig restored;
        std::map<std::string, std::string> metadata;
        std::string reason;
        REQUIRE_NOTHROW(restored.load_from_json(file, ForwardCompatibilitySubstitutionRule::Disable, metadata, reason));
        REQUIRE(reason.empty());
        for (const auto &key : preset->config.keys())
            if (key.find("tinman_") == 0) {
                REQUIRE(restored.option(key));
                CHECK(restored.option(key)->type() == preset->config.option(key)->type());
                CHECK(restored.option(key)->serialize() == preset->config.option(key)->serialize());
            }
    }

    PresetBundle bundle;
    auto machine = bundle.printers.default_preset().config;
    machine.apply(fixture.printer.config);
    auto process = bundle.prints.default_preset().config;
    process.apply(fixture.process.config);
    auto material = bundle.filaments.default_preset().config;
    material.apply(fixture.filament.config);
    bundle.printers.load_preset("", "Scoped machine", machine, true);
    bundle.prints.load_preset("", "Scoped process", process, true);
    bundle.filaments.load_preset("", "Scoped material", material, true);
    bundle.filament_presets = {"Scoped material", "Scoped material"};
    bundle.project_config.set_key_value("filament_map", new ConfigOptionInts{2, 2});
    const auto full = bundle.full_config(false);
    CHECK(full.option<ConfigOptionString>("tinman_physical_printer_id")->value == unit_a);
    CHECK(full.option<ConfigOptionString>("tinman_process_scope_physical_id")->value == unit_a);
    CHECK((full.option<ConfigOptionStrings>("tinman_filament_scope_physical_id")->values ==
           std::vector<std::string>{unit_a, unit_a}));
    CHECK((full.option<ConfigOptionFloats>("tinman_filament_scope_nozzle_diameter")->values ==
           std::vector<double>{0.7, 0.7}));
    CHECK((full.option<ConfigOptionInts>("tinman_filament_scope_tool")->values == std::vector<int>{1, 1}));

    std::vector<Preset> materials{bundle.filaments.get_edited_preset(), bundle.filaments.get_edited_preset()};
    const auto constructed = PresetBundle::construct_full_config(bundle.printers.get_edited_preset(),
        bundle.prints.get_edited_preset(), bundle.project_config, materials, false, std::nullopt);
    for (const auto &key : full.keys())
        if (key.find("tinman_") == 0) {
            REQUIRE(constructed.option(key));
            CHECK(constructed.option(key)->serialize() == full.option(key)->serialize());
        }
}

TEST_CASE("Unscoped default process scope survives an actual JSON save and reopen", "[TinMan][Preset][PhysicalScope]")
{
    TemporaryScopeDirectory directory;
    PresetBundle bundle;
    const auto file = (directory.path / "unscoped-process.json").string();
    const auto &original = bundle.prints.default_preset();
    REQUIRE(original.config.option<ConfigOptionFloats>("tinman_process_scope_nozzle_diameters"));
    CHECK(original.config.option<ConfigOptionFloats>("tinman_process_scope_nozzle_diameters")->values ==
          std::vector<double>{0.0});
    original.config.save_to_json(file, "Unscoped process", "User", "2.4.2");
    std::map<std::string, std::string> metadata;
    std::string reason;
    DynamicPrintConfig loaded;
    REQUIRE_NOTHROW(loaded.load_from_json(file, ForwardCompatibilitySubstitutionRule::Disable, metadata, reason));
    REQUIRE(reason.empty());
    auto &restored = bundle.prints.load_preset("", "Unscoped reopened", loaded, true);
    CHECK(restored.config.option<ConfigOptionString>("tinman_process_scope_physical_id")->value.empty());
    CHECK(restored.config.option<ConfigOptionFloats>("tinman_process_scope_nozzle_diameters")->values ==
          std::vector<double>{0.0});
    const auto &printer = bundle.printers.get_edited_preset();
    CHECK(is_compatible_with_physical_printer_scope(restored, printer));
    CHECK(is_compatible_with_printer(PresetWithVendorProfile(restored, nullptr),
                                     PresetWithVendorProfile(printer, nullptr)));
}

TEST_CASE("Malformed persisted physical scope cannot disappear into unscoped defaults", "[TinMan][Preset][PhysicalScope]")
{
    TemporaryScopeDirectory directory;
    const std::string key = GENERATE(std::string("tinman_physical_printer_id"),
        std::string("tinman_process_scope_physical_id"), std::string("tinman_process_scope_nozzle_diameters"),
        std::string("tinman_filament_scope_physical_id"), std::string("tinman_filament_scope_nozzle_diameter"),
        std::string("tinman_filament_scope_tool"));
    const std::string bad_json_value = GENERATE(std::string("123"), std::string("true"), std::string("null"),
        std::string("{}"), std::string("[]"), std::string("[[\"0\"]]"), std::string("[\"0\",null]"));
    const auto file = (directory.path / "malformed-scope.json").string();
    write_file_with_replace(file, "{\"" + key + "\":" + bad_json_value + "}");
    PresetBundle bundle;
    DynamicPrintConfig config = bundle.prints.default_preset().config;
    const auto before = config;
    std::map<std::string, std::string> metadata;
    std::string reason;
    ConfigSubstitutionContext context(ForwardCompatibilitySubstitutionRule::Disable);
    CHECK(config.load_from_json(file, context, true, metadata, reason) != 0);
    CHECK_FALSE(reason.empty());
    CHECK(reason.find(key) != std::string::npos);
    CHECK(config == before);

    DynamicPrintConfig other = before;
    reason.clear();
    REQUIRE_NOTHROW(other.load_from_json(file, ForwardCompatibilitySubstitutionRule::Disable, metadata, reason));
    CHECK_FALSE(reason.empty());
    CHECK(other == before);
}

TEST_CASE("Physical connection records preserve the same non-display unit identity", "[TinMan][Preset][PhysicalScope]")
{
    TemporaryScopeDirectory directory;
    PresetBundle bundle;
    auto machine = bundle.printers.default_preset().config;
    machine.set_key_value("tinman_physical_printer_id", new ConfigOptionString(unit_a));
    bundle.printers.load_preset("", "Machine A", machine, true);
    boost::filesystem::create_directory(directory.path / "physical_printer");
    PresetsConfigSubstitutions substitutions;
    bundle.physical_printers.load_printers(directory.path.string(), "physical_printer", substitutions,
                                           ForwardCompatibilitySubstitutionRule::Disable);
    auto connection = bundle.physical_printers.default_config();
    REQUIRE(connection.option<ConfigOptionString>("tinman_physical_printer_id"));
    CHECK(connection.option<ConfigOptionString>("tinman_physical_printer_id")->value.empty());
    connection.set_key_value("tinman_physical_printer_id", new ConfigOptionString(unit_a));
    connection.set_key_value("preset_names", new ConfigOptionStrings{"Machine A"});
    const auto file = bundle.physical_printers.path_from_name("Renamable label");
    bundle.physical_printers.load_printer(file, "Renamable label", std::move(connection), true, true);
    PresetBundle reopened;
    reopened.printers.load_preset("", "Machine A", machine, true);
    reopened.physical_printers.load_printers(directory.path.string(), "physical_printer", substitutions,
                                             ForwardCompatibilitySubstitutionRule::Disable);
    const auto *restored = reopened.physical_printers.find_printer("Renamable label");
    REQUIRE(restored);
    CHECK(restored->config.option<ConfigOptionString>("tinman_physical_printer_id")->value == unit_a);
    CHECK(reopened.printers.get_edited_preset().config.option<ConfigOptionString>("tinman_physical_printer_id")->value == unit_a);
}

TEST_CASE("Persisted numeric scope requires one complete finite token per element", "[TinMan][Preset][PhysicalScope]")
{
    TemporaryScopeDirectory directory;
    const std::string key = GENERATE(std::string("tinman_process_scope_nozzle_diameters"),
        std::string("tinman_filament_scope_nozzle_diameter"), std::string("tinman_filament_scope_tool"));
    const std::string token = GENERATE(std::string(""), std::string(" "), std::string("nonsense"),
        std::string("0.7junk"), std::string("1junk"), std::string("0.4,0.7"),
        std::string("nan"), std::string("inf"), std::string("1e9999"));
    const auto file = (directory.path / "bad-number.json").string();
    write_file_with_replace(file, "{\"" + key + "\":[\"" + token + "\"]}");
    PresetBundle bundle;
    auto config = bundle.prints.default_preset().config;
    const auto before = config;
    std::map<std::string, std::string> metadata;
    std::string reason;
    ConfigSubstitutionContext context(ForwardCompatibilitySubstitutionRule::Disable);
    CHECK(config.load_from_json(file, context, true, metadata, reason) != 0);
    CHECK(reason.find(key) != std::string::npos);
    CHECK(config == before);
}

TEST_CASE("Persisted scope tool tokens must fit an integer", "[TinMan][Preset][PhysicalScope]")
{
    TemporaryScopeDirectory directory;
    const std::string token = GENERATE(std::string("1.5"), std::string("2147483648"),
                                      std::string("-2147483649"), std::string("1e0"));
    const auto file = (directory.path / "bad-tool.json").string();
    write_file_with_replace(file, "{\"tinman_filament_scope_tool\":[\"" + token + "\"]}");
    DynamicPrintConfig config;
    std::map<std::string, std::string> metadata;
    std::string reason;
    ConfigSubstitutionContext context(ForwardCompatibilitySubstitutionRule::Disable);
    CHECK(config.load_from_json(file, context, true, metadata, reason) != 0);
    CHECK(reason.find("tinman_filament_scope_tool") != std::string::npos);
    CHECK(config.keys().empty());
}

TEST_CASE("Slicing keeps a scoped filament on its qualified nozzle", "[TinMan][Print][PhysicalScope]")
{
    // Packed full config: filament 1 is scoped to nozzle index 1 of unit A, filament 2 is unscoped.
    DynamicPrintConfig config;
    config.set_key_value("tinman_physical_printer_id", new ConfigOptionString(unit_a));
    config.set_key_value("nozzle_diameter", new ConfigOptionFloats{0.4, 0.7});
    config.set_key_value("filament_colour", new ConfigOptionStrings{"#FFFFFF", "#000000"});
    config.set_key_value("tinman_filament_scope_physical_id", new ConfigOptionStrings{unit_a, ""});
    config.set_key_value("tinman_filament_scope_nozzle_diameter", new ConfigOptionFloats{0.7, 0.0});
    config.set_key_value("tinman_filament_scope_tool", new ConfigOptionInts{1, -1});
    std::string reason;

    CHECK(profile_scope_filament_map_valid(config, {2, 1}, true, reason));
    CHECK(profile_scope_filament_map_valid(config, {}, false, reason));
    CHECK_FALSE(profile_scope_filament_map_valid(config, {1, 1}, true, reason));
    CHECK_FALSE(reason.empty());

    std::vector<std::set<int>> exclusions(2);
    REQUIRE(add_profile_scope_tool_exclusions(config, exclusions, reason));
    CHECK(exclusions[0] == std::set<int>{0});
    CHECK(exclusions[1].empty());

    SECTION("Another physical printer of the same model") {
        config.set_key_value("tinman_physical_printer_id", new ConfigOptionString(unit_b));
        CHECK_FALSE(profile_scope_filament_map_valid(config, {}, false, reason));
        std::vector<std::set<int>> other(2);
        CHECK_FALSE(add_profile_scope_tool_exclusions(config, other, reason));
    }
    SECTION("A process scoped to another printer") {
        config.set_key_value("tinman_process_scope_physical_id", new ConfigOptionString(unit_b));
        config.set_key_value("tinman_process_scope_nozzle_diameters", new ConfigOptionFloats{0.4, 0.7});
        CHECK_FALSE(profile_scope_filament_map_valid(config, {2, 1}, true, reason));
    }
    SECTION("Scope vectors that do not match the filament count") {
        config.set_key_value("tinman_filament_scope_tool", new ConfigOptionInts{1});
        CHECK_FALSE(profile_scope_filament_map_valid(config, {2, 1}, true, reason));
    }
    SECTION("A config written before scoping existed") {
        for (const char *key : {"tinman_filament_scope_physical_id", "tinman_filament_scope_nozzle_diameter", "tinman_filament_scope_tool"})
            config.erase(key);
        CHECK(profile_scope_filament_map_valid(config, {1, 1}, true, reason));
    }
}
