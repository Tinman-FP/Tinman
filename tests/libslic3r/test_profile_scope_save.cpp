#include <catch2/catch_all.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <utility>

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/PresetBundle.hpp"

using namespace Slic3r;

namespace {
const std::string id_a = "80c00526-29f8-47c8-813a-4d36de799571";
const std::string id_b = "a40b2a80-a38c-4d10-b350-52c0cfd3539d";

std::string bytes(const std::string &file)
{
    boost::nowide::ifstream stream(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), {});
}

// Two physical printers of the same model ("Unit A", "Unit B"), each with its own
// machine preset carrying its identity and a 0.4 + 0.7 mm tool head.
struct ScopeSaveFixture {
    boost::filesystem::path root = boost::filesystem::temp_directory_path() /
                                  boost::filesystem::unique_path("tinman-scope-save-%%%%-%%%%");
    PresetBundle bundle;

    ScopeSaveFixture()
    {
        for (const auto *subdir : {"process", "filament", "physical_printer"})
            boost::filesystem::create_directories(root / subdir);
        boost::filesystem::create_directories(root / "process" / "base");
        boost::filesystem::create_directories(root / "filament" / "base");
        PresetsConfigSubstitutions substitutions;
        bundle.prints.load_presets(root.string(), "process", substitutions, ForwardCompatibilitySubstitutionRule::Disable);
        bundle.filaments.load_presets(root.string(), "filament", substitutions, ForwardCompatibilitySubstitutionRule::Disable);
        bundle.physical_printers.load_printers(root.string(), "physical_printer", substitutions, ForwardCompatibilitySubstitutionRule::Disable);
        for (const auto &unit : {std::make_pair("A", id_a), std::make_pair("B", id_b)}) {
            const std::string machine_name = std::string("Machine ") + unit.first;
            auto machine = bundle.printers.default_preset().config;
            machine.set_key_value("tinman_physical_printer_id", new ConfigOptionString(unit.second));
            machine.set_key_value("nozzle_diameter", new ConfigOptionFloats{0.4, 0.7});
            bundle.printers.load_preset("", machine_name, machine, unit.first == std::string("A"));
            auto physical = bundle.physical_printers.default_config();
            physical.set_key_value("tinman_physical_printer_id", new ConfigOptionString(unit.second));
            physical.set_key_value("preset_names", new ConfigOptionStrings{machine_name});
            const std::string name = std::string("Unit ") + unit.first;
            bundle.physical_printers.load_printer(bundle.physical_printers.path_from_name(name), name, std::move(physical), false, false);
        }
        bundle.physical_printers.select_printer("Unit A", "Machine A");
        auto process = bundle.prints.default_preset().config;
        bundle.prints.load_preset(bundle.prints.path_from_name("Generic process", true), "Generic process", process, true);
        bundle.prints.save_current_preset("Generic process");
        auto material = bundle.filaments.default_preset().config;
        bundle.filaments.load_preset(bundle.filaments.path_from_name("Generic material", true), "Generic material", material, true);
        bundle.filaments.save_current_preset("Generic material");
        bundle.filament_presets = {"Generic material"};
    }

    ~ScopeSaveFixture()
    {
        boost::system::error_code error;
        boost::filesystem::remove_all(root, error);
    }

    ProfileScopeSaveSelection target()
    {
        const auto target = current_profile_scope_save_target(bundle);
        if (!target)
            throw std::runtime_error("Fixture target missing");
        return *target;
    }
};
} // namespace

TEST_CASE("Scoped saves require the exact unique physical association and an explicit nozzle", "[TinMan][Preset][ScopeSave]")
{
    ScopeSaveFixture fixture;
    auto target = fixture.target();
    CHECK_FALSE(target.nozzle_tool_index);
    auto candidate = fixture.bundle.filaments.get_edited_preset();
    const auto before = candidate.config;
    CHECK_THROWS(prepare_profile_scope_for_save(candidate, fixture.bundle, "A material", target));
    CHECK(fixture.bundle.filaments.get_edited_preset().config == before);
    target.nozzle_tool_index = 1;
    REQUIRE_NOTHROW(prepare_profile_scope_for_save(candidate, fixture.bundle, "A material", target));
    CHECK(is_compatible_with_physical_printer_scope(candidate, fixture.bundle.printers.get_edited_preset(), 1));
    CHECK_FALSE(is_compatible_with_physical_printer_scope(candidate, fixture.bundle.printers.get_edited_preset(), 0));

    SECTION("Changed selected unit makes the dialog snapshot stale") {
        fixture.bundle.printers.select_preset_by_name("Machine B", true);
        fixture.bundle.physical_printers.select_printer("Unit B", "Machine B");
        CHECK_THROWS(prepare_profile_scope_for_save(candidate, fixture.bundle, "stale", target));
    }
    SECTION("Changed tooling makes the dialog snapshot stale") {
        fixture.bundle.printers.get_edited_preset().config.set_key_value("nozzle_diameter", new ConfigOptionFloats{0.6, 0.7});
        CHECK_THROWS(prepare_profile_scope_for_save(candidate, fixture.bundle, "stale", target));
    }
    SECTION("Unselected physical destination is not inferred from a machine name") {
        fixture.bundle.physical_printers.unselect_printer();
        CHECK_FALSE(current_profile_scope_save_target(fixture.bundle));
        CHECK_THROWS(prepare_profile_scope_for_save(candidate, fixture.bundle, "stale", target));
    }
    SECTION("Duplicate physical UUIDs are ambiguous even with another label") {
        auto peer = fixture.bundle.physical_printers.default_config();
        peer.set_key_value("tinman_physical_printer_id", new ConfigOptionString(id_a));
        peer.set_key_value("preset_names", new ConfigOptionStrings{"Machine A"});
        fixture.bundle.physical_printers.load_printer(
            fixture.bundle.physical_printers.path_from_name("Duplicate A"), "Duplicate A", std::move(peer), false, false);
        CHECK_FALSE(current_profile_scope_save_target(fixture.bundle));
        CHECK_THROWS(prepare_profile_scope_for_save(candidate, fixture.bundle, "ambiguous", target));
    }
}

TEST_CASE("Unsaved-change saves create a scoped copy without rewriting the generic source", "[TinMan][Preset][ScopeSave]")
{
    const auto type = GENERATE(Preset::TYPE_PRINT, Preset::TYPE_FILAMENT);
    ScopeSaveFixture fixture;
    auto target = fixture.target();
    target.nozzle_tool_index = 1;
    PresetCollection &collection = type == Preset::TYPE_PRINT ?
        static_cast<PresetCollection &>(fixture.bundle.prints) : static_cast<PresetCollection &>(fixture.bundle.filaments);
    const auto source = collection.get_selected_preset();
    const auto original = bytes(source.file);
    const std::string name = type == Preset::TYPE_PRINT ? "A process" : "A material";
    fixture.bundle.save_changes_for_preset(name, type, {}, false, target);
    const auto saved = collection.get_selected_preset();
    CHECK(bytes(source.file) == original);
    CHECK(boost::filesystem::is_regular_file(saved.file));
    CHECK(saved.description.find("requires validation") != std::string::npos);
    CHECK(is_compatible_with_physical_printer_scope(saved, fixture.bundle.printers.get_edited_preset(),
                                                   type == Preset::TYPE_FILAMENT ? std::optional<size_t>(1) : std::nullopt));
    DynamicPrintConfig reopened;
    std::map<std::string, std::string> metadata;
    std::string reason;
    REQUIRE_NOTHROW(reopened.load_from_json(saved.file, ForwardCompatibilitySubstitutionRule::Disable, metadata, reason));
    REQUIRE(reason.empty());
    if (type == Preset::TYPE_PRINT) {
        REQUIRE(reopened.option<ConfigOptionString>("tinman_process_scope_physical_id"));
        REQUIRE(reopened.option<ConfigOptionFloats>("tinman_process_scope_nozzle_diameters"));
        CHECK(reopened.option<ConfigOptionString>("tinman_process_scope_physical_id")->value == id_a);
    } else {
        REQUIRE(reopened.option<ConfigOptionStrings>("tinman_filament_scope_physical_id"));
        REQUIRE(reopened.option<ConfigOptionFloats>("tinman_filament_scope_nozzle_diameter"));
        REQUIRE(reopened.option<ConfigOptionInts>("tinman_filament_scope_tool"));
        CHECK(reopened.option<ConfigOptionInts>("tinman_filament_scope_tool")->values == std::vector<int>{1});
    }
    PresetBundle fresh;
    PresetsConfigSubstitutions substitutions;
    PresetCollection &fresh_collection = type == Preset::TYPE_PRINT ?
        static_cast<PresetCollection &>(fresh.prints) : static_cast<PresetCollection &>(fresh.filaments);
    fresh_collection.load_presets(fixture.root.string(), type == Preset::TYPE_PRINT ? "process" : "filament",
                                  substitutions, ForwardCompatibilitySubstitutionRule::Disable);
    const auto *loaded = fresh_collection.find_preset(name, false, true);
    REQUIRE(loaded);
    CHECK(loaded->inherits() == source.name);
    CHECK(is_compatible_with_physical_printer_scope(*loaded, fixture.bundle.printers.get_edited_preset()));
}

TEST_CASE("Scope changes cannot overwrite the original or a profile for another printer", "[TinMan][Preset][ScopeSave]")
{
    ScopeSaveFixture fixture;
    auto target = fixture.target();
    target.nozzle_tool_index = 1;
    auto candidate = fixture.bundle.filaments.get_edited_preset();
    CHECK_THROWS(prepare_profile_scope_for_save(candidate, fixture.bundle, candidate.name, target));
    fixture.bundle.save_changes_for_preset("A material", Preset::TYPE_FILAMENT, {}, false, target);
    const auto scoped = fixture.bundle.filaments.get_selected_preset();
    const auto original = bytes(scoped.file);
    fixture.bundle.printers.select_preset_by_name("Machine B", true);
    fixture.bundle.physical_printers.select_printer("Unit B", "Machine B");
    auto b = fixture.target();
    b.nozzle_tool_index = 1;
    CHECK_THROWS(fixture.bundle.save_changes_for_preset(scoped.name, Preset::TYPE_FILAMENT, {}, false, b));
    CHECK(bytes(scoped.file) == original);
    REQUIRE_NOTHROW(fixture.bundle.save_changes_for_preset("B material", Preset::TYPE_FILAMENT, {}, false, b));
    CHECK(bytes(scoped.file) == original);
}

TEST_CASE("Preserving scope is independent of the current unit and selected-options filtering", "[TinMan][Preset][ScopeSave]")
{
    ScopeSaveFixture fixture;
    auto target = fixture.target();
    fixture.bundle.save_changes_for_preset("A process", Preset::TYPE_PRINT, {}, false, target);
    const auto qualified = fixture.bundle.prints.get_edited_preset();
    fixture.bundle.printers.select_preset_by_name("Machine B", true);
    fixture.bundle.physical_printers.select_printer("Unit B", "Machine B");
    Preset candidate = qualified;
    candidate.config.erase("tinman_process_scope_physical_id");
    candidate.config.set_key_value("tinman_process_scope_nozzle_diameters", new ConfigOptionFloats{0.0});
    REQUIRE_NOTHROW(prepare_profile_scope_for_save(candidate, fixture.bundle, qualified.name, {}));
    CHECK(candidate.config.option<ConfigOptionString>("tinman_process_scope_physical_id")->value == id_a);
    CHECK((candidate.config.option<ConfigOptionFloats>("tinman_process_scope_nozzle_diameters")->values ==
           std::vector<double>{0.4, 0.7}));
    CHECK_FALSE(is_compatible_with_physical_printer_scope(candidate, fixture.bundle.printers.get_edited_preset()));
    ProfileScopeSaveSelection generic;
    generic.mode = ProfileScopeSaveMode::GenericTemplate;
    CHECK_THROWS(prepare_profile_scope_for_save(candidate, fixture.bundle, qualified.name, generic));
    REQUIRE_NOTHROW(prepare_profile_scope_for_save(candidate, fixture.bundle, "Generic copy", generic));
    CHECK(candidate.config.option<ConfigOptionString>("tinman_process_scope_physical_id")->value.empty());
    CHECK(candidate.config.option<ConfigOptionFloats>("tinman_process_scope_nozzle_diameters")->values == std::vector<double>{0.0});
}

TEST_CASE("Scoped copies keep an existing child's direct parent across a fresh reload", "[TinMan][Preset][ScopeSave]")
{
    ScopeSaveFixture fixture;
    auto parent_config = fixture.bundle.filaments.default_preset().config;
    parent_config.set_key_value("filament_flow_ratio", new ConfigOptionFloats{0.98});
    fixture.bundle.filaments.load_preset("", "Public material parent", parent_config, false).is_system = true;
    auto child = parent_config;
    child.set_key_value("inherits", new ConfigOptionString("Public material parent"));
    child.set_key_value("filament_flow_ratio", new ConfigOptionFloats{0.96});
    fixture.bundle.filaments.load_preset(fixture.bundle.filaments.path_from_name("User child"), "User child", child, true);
    REQUIRE(fixture.bundle.filaments.save_current_preset("User child"));
    const auto child_file = fixture.bundle.filaments.get_selected_preset().file;
    const auto child_bytes = bytes(child_file);
    auto target = fixture.target();
    target.nozzle_tool_index = 1;
    fixture.bundle.save_changes_for_preset("A child", Preset::TYPE_FILAMENT, {}, false, target);
    CHECK(fixture.bundle.filaments.get_selected_preset().inherits() == "Public material parent");
    CHECK(bytes(child_file) == child_bytes);
    CHECK(fixture.bundle.filaments.find_preset("Public material parent", false, true)->config == parent_config);
    PresetBundle fresh;
    fresh.filaments.load_preset("", "Public material parent", parent_config, false).is_system = true;
    PresetsConfigSubstitutions substitutions;
    fresh.filaments.load_presets(fixture.root.string(), "filament", substitutions,
                                 ForwardCompatibilitySubstitutionRule::Disable);
    const auto *loaded = fresh.filaments.find_preset("A child", false, true);
    REQUIRE(loaded);
    CHECK(loaded->inherits() == "Public material parent");
    CHECK(loaded->config.option<ConfigOptionFloats>("filament_flow_ratio")->values == std::vector<double>{0.96});
    CHECK(is_compatible_with_physical_printer_scope(*loaded, fixture.bundle.printers.get_edited_preset(), 1));
}

// The calls the connection dialog and the save dialog make, end to end: two printers created
// from one machine profile, a reload, scoped saves on printer A, and a stale dialog choice.
TEST_CASE("Two printers created from one machine profile keep their own profiles", "[TinMan][Preset][ScopeSave][PhysicalUnit]")
{
    namespace fs = boost::filesystem;
    const auto rule = ForwardCompatibilitySubstitutionRule::Disable;
    const fs::path root = fs::temp_directory_path() / fs::unique_path("tinman-two-units-%%%%-%%%%");
    struct Cleanup {
        fs::path path;
        ~Cleanup() { boost::system::error_code error; fs::remove_all(path, error); }
    } cleanup{root};
    for (const auto *subdir : {"machine", "process", "filament"})
        fs::create_directories(root / subdir / "base"); // root user presets live in base/
    const auto load_user_folder = [&](PresetBundle &bundle) {
        PresetsConfigSubstitutions substitutions;
        bundle.printers.load_presets(root.string(), "machine", substitutions, rule);
        bundle.prints.load_presets(root.string(), "process", substitutions, rule);
        bundle.filaments.load_presets(root.string(), "filament", substitutions, rule);
        bundle.load_physical_units(root.string(), substitutions, rule);
        // Stands in for a vendor profile shared by every printer of the model.
        auto vendor = bundle.filaments.default_preset().config;
        bundle.filaments.load_preset("", "Vendor material", vendor, false).is_system = true;
    };

    AppConfig app_config;
    {
        PresetBundle bundle;
        load_user_folder(bundle);
        CHECK(bundle.physical_printers.empty());
        auto machine = bundle.printers.default_preset().config;
        machine.set_key_value("nozzle_diameter", new ConfigOptionFloats{0.4});
        bundle.printers.load_preset(bundle.printers.path_from_name("Model X", true), "Model X", machine, true);
        bundle.printers.save_current_preset("Model X");
        for (auto *collection : {static_cast<PresetCollection *>(&bundle.prints), static_cast<PresetCollection *>(&bundle.filaments)}) {
            auto config = collection->default_preset().config;
            collection->load_preset(collection->path_from_name("Shared", true), "Shared", config, true);
            collection->save_current_preset("Shared");
        }
        for (const auto &unit : {std::make_pair("Printer A", id_a), std::make_pair("Printer B", id_b)}) {
            bundle.printers.select_preset_by_name("Model X", true);
            PhysicalPrinter connection(unit.first, bundle.physical_printers.default_config());
            connection.config.set_key_value("tinman_physical_printer_id", new ConfigOptionString(unit.second));
            fs::create_directories(bundle.physical_printers.dir_path());
            std::string unbound;
            REQUIRE_NOTHROW(bundle.save_physical_unit(bundle.printers.get_edited_preset(), connection, unit.first, {}, unbound));
        }
        bundle.printers.select_preset_by_name("Printer B", true);
        bundle.physical_printers.select_printer("Printer B", "Printer B");
        bundle.filament_presets = {"Shared"};
        bundle.export_selections(app_config);
    }
    CHECK(app_config.get("presets", "physical_printer") == "Printer B * Printer B");

    PresetBundle bundle; // after a restart
    load_user_folder(bundle);
    REQUIRE(bundle.physical_printers().size() == 2);
    bundle.load_selections(app_config);
    CHECK(bundle.printers.get_edited_preset().name == "Printer B");
    CHECK(bundle.physical_printers.get_selected_full_printer_name() == "Printer B * Printer B");
    {
        // A saved pairing that does not exist is dropped, not replaced by the printer's first
        // machine profile (select_printer()'s fallback would pick "Printer A * Printer A").
        AppConfig mismatched_config(app_config);
        mismatched_config.set("presets", "physical_printer", "Printer A * Printer B");
        PresetBundle mismatched;
        load_user_folder(mismatched);
        mismatched.load_selections(mismatched_config);
        CHECK(mismatched.printers.get_edited_preset().name == "Printer B");
        CHECK_FALSE(mismatched.physical_printers.has_selection());
    }
    bundle.filament_presets = {"Shared"};
    const auto select = [&](const std::string &printer) {
        bundle.printers.select_preset_by_name(printer, true);
        bundle.physical_printers.select_printer(printer, printer);
        bundle.prints.select_preset_by_name("Shared", true);
        bundle.filaments.select_preset_by_name("Shared", true);
        bundle.update_compatible(PresetSelectCompatibleType::Never);
    };
    const auto offered = [&](const PresetCollection &collection, const std::string &name) {
        const Preset *preset = collection.find_preset(name, false, true);
        return preset != nullptr && preset->is_compatible;
    };

    select("Printer A");
    auto target = current_profile_scope_save_target(bundle);
    REQUIRE(target);
    CHECK(target->physical_printer_name == "Printer A");
    CHECK(target->nozzle_tool_index == std::optional<size_t>(0));
    // A scope change needs a new name; the shared profile is kept.
    CHECK_THROWS(bundle.save_changes_for_preset("Shared", Preset::TYPE_FILAMENT, {}, false, *target));
    REQUIRE_NOTHROW(bundle.save_changes_for_preset("Shared - Printer A", Preset::TYPE_FILAMENT, {}, false, *target));
    REQUIRE_NOTHROW(bundle.save_changes_for_preset("Shared - Printer A", Preset::TYPE_PRINT, {}, false, *target));

    select("Printer A");
    CHECK(offered(bundle.prints, "Shared - Printer A"));
    CHECK(offered(bundle.filaments, "Shared - Printer A"));
    CHECK(offered(bundle.prints, "Shared"));
    CHECK(offered(bundle.filaments, "Shared"));
    CHECK(offered(bundle.filaments, "Vendor material"));
    select("Printer B");
    CHECK_FALSE(offered(bundle.prints, "Shared - Printer A"));
    CHECK_FALSE(offered(bundle.filaments, "Shared - Printer A"));
    CHECK(offered(bundle.prints, "Shared"));
    CHECK(offered(bundle.filaments, "Shared"));
    CHECK(offered(bundle.filaments, "Vendor material"));

    // The save dialog was opened on printer A; printer B is selected by the time it saves.
    select("Printer A");
    const auto stale = *current_profile_scope_save_target(bundle);
    select("Printer B");
    try {
        bundle.save_changes_for_preset("Stale copy", Preset::TYPE_FILAMENT, {}, false, stale);
        FAIL("A stale scope choice was saved");
    } catch (const std::runtime_error &error) {
        CHECK(std::string(error.what()) == "The selected physical printer or nozzle changed. Reopen the save dialog.");
    }
    CHECK(bundle.filaments.find_preset("Stale copy", false, true) == nullptr);
}
