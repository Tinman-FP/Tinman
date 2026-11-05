#include <catch2/catch_all.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <algorithm>
#include <map>

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/ProfileScopeMigration.hpp"

using namespace Slic3r;
namespace fs = boost::filesystem;

namespace {
const std::string id_a = "3f0c8d4e-5b6a-4c7d-8e9f-0a1b2c3d4e5f";
const std::string id_b = "9a8b7c6d-5e4f-4a3b-9c2d-1e0f9a8b7c6d";
const std::string id_t = "0d1c2b3a-4f5e-4d6c-8b7a-695847362514";
const auto        rule = ForwardCompatibilitySubstitutionRule::Disable;

std::string bytes(const std::string &file)
{
    boost::nowide::ifstream stream(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), {});
}

void write(const fs::path &file, const std::string &text)
{
    fs::create_directories(file.parent_path());
    boost::nowide::ofstream stream(file.string(), std::ios::binary);
    stream << text;
}

// A user folder with two printer models from a "vendor", the user's own X machine profile
// and two physical printers made from it (A and B), plus a two-nozzle physical printer T.
// The user's process and filament profiles are tied to printers only by compatible_printers.
struct MigrationFixture {
    fs::path     root = fs::temp_directory_path() / fs::unique_path("tinman-scope-migration-%%%%-%%%%");
    PresetBundle bundle;

    MigrationFixture()
    {
        for (const auto *subdir : {"machine", "process", "filament"})
            fs::create_directories(root / subdir / "base");
        load(bundle);
        const auto machine = [&](const std::string &name, const std::string &model, std::vector<double> nozzles) {
            auto config = bundle.printers.default_preset().config;
            config.set_key_value("printer_model", new ConfigOptionString(model));
            config.set_key_value("nozzle_diameter", new ConfigOptionFloats(nozzles));
            return config;
        };
        // The user's own machine profile saved from the vendor's X 0.4 (so the physical printers
        // made from it inherit "Vendor X 0.4"), and a root two-nozzle machine of model T.
        bundle.printers.select_preset_by_name("Vendor X 0.4", true);
        bundle.printers.save_current_preset("My X");
        bundle.printers.load_preset(bundle.printers.path_from_name("My T", true), "My T", machine("My T", "T", {0.4, 0.6}), true);
        bundle.printers.save_current_preset("My T");
        for (const auto &unit : {std::make_tuple("Printer A", id_a, "My X"), std::make_tuple("Printer B", id_b, "My X"),
                                 std::make_tuple("Printer T", id_t, "My T")}) {
            bundle.printers.select_preset_by_name(std::get<2>(unit), true);
            PhysicalPrinter connection(std::get<0>(unit), bundle.physical_printers.default_config());
            connection.config.set_key_value("tinman_physical_printer_id", new ConfigOptionString(std::get<1>(unit)));
            fs::create_directories(bundle.physical_printers.dir_path());
            std::string unbound;
            bundle.save_physical_unit(bundle.printers.get_edited_preset(), connection, std::get<0>(unit), {}, unbound);
        }

        // Root user profiles (no parent), saved like the app saves them.
        const auto user = [&](PresetCollection &collection, const std::string &name, std::vector<std::string> printers,
                              const std::string &condition = {}) {
            auto config = collection.default_preset().config;
            config.set_key_value("compatible_printers", new ConfigOptionStrings(printers));
            config.set_key_value("compatible_printers_condition", new ConfigOptionString(condition));
            collection.load_preset(collection.path_from_name(name, true), name, config, true);
            collection.save_current_preset(name);
        };
        for (auto *collection : {static_cast<PresetCollection *>(&bundle.prints), static_cast<PresetCollection *>(&bundle.filaments)}) {
            user(*collection, "Tuned on My X", {"My X"});
            user(*collection, "Every X nozzle", {"Vendor X 0.4", "Vendor X 0.6", "Missing machine"});
            user(*collection, "Vendor X only", {"Vendor X 0.4"});      // the parent-printer rule
            user(*collection, "X at 0.6 only", {"Vendor X 0.6"});      // another nozzle
            user(*collection, "X and Y", {"Vendor X 0.4", "Vendor Y 0.4"}); // shared with another model
            user(*collection, "Only missing", {"Missing machine"});
            user(*collection, "By condition", {}, "printer_model == \"X\"");
            user(*collection, "On printer B", {"Printer B"});
            user(*collection, "Tuned on My T", {"My T"});
        }
        select("Printer A");
    }

    ~MigrationFixture()
    {
        boost::system::error_code error;
        fs::remove_all(root, error);
    }

    // Vendor profiles first, as the app loads them before the user folder.
    void load(PresetBundle &target)
    {
        const auto vendor = [&](PresetCollection &collection, const std::string &name, DynamicPrintConfig config) {
            collection.load_preset("", name, config, false).is_system = true;
        };
        for (const auto &model : {std::make_tuple("Vendor X 0.4", "X", 0.4), std::make_tuple("Vendor X 0.6", "X", 0.6),
                                  std::make_tuple("Vendor Y 0.4", "Y", 0.4)}) {
            auto config = target.printers.default_preset().config;
            config.set_key_value("printer_model", new ConfigOptionString(std::get<1>(model)));
            config.set_key_value("nozzle_diameter", new ConfigOptionFloats{std::get<2>(model)});
            vendor(target.printers, std::get<0>(model), config);
        }
        for (auto *collection : {static_cast<PresetCollection *>(&target.prints), static_cast<PresetCollection *>(&target.filaments)}) {
            auto config = collection->default_preset().config;
            config.set_key_value("compatible_printers", new ConfigOptionStrings{"Vendor X 0.4"});
            vendor(*collection, "Vendor profile", config);
        }
        PresetsConfigSubstitutions substitutions;
        target.printers.load_presets(root.string(), "machine", substitutions, rule);
        target.prints.load_presets(root.string(), "process", substitutions, rule);
        target.filaments.load_presets(root.string(), "filament", substitutions, rule);
        target.load_physical_units(root.string(), substitutions, rule);
        apply_hidden_profiles(target);
    }

    void select(const std::string &printer)
    {
        bundle.printers.select_preset_by_name(printer, true);
        if (bundle.physical_printers.find_printer(printer, false))
            bundle.physical_printers.select_printer(printer, printer);
        else
            bundle.physical_printers.unselect_printer();
        bundle.update_compatible(PresetSelectCompatibleType::Never);
    }

    ProfileScopeSaveSelection target()
    {
        const auto target = current_profile_scope_save_target(bundle);
        if (!target)
            throw std::runtime_error("Fixture target missing");
        return *target;
    }

    static std::vector<std::string> names(const std::vector<ProfileMoveCandidate> &candidates, Preset::Type type)
    {
        std::vector<std::string> result;
        for (const auto &candidate : candidates)
            if (candidate.source.type == type)
                result.push_back(candidate.source.name);
        std::sort(result.begin(), result.end());
        return result;
    }

    static const ProfileMoveCandidate &find(const std::vector<ProfileMoveCandidate> &candidates, Preset::Type type,
                                            const std::string &name)
    {
        for (const auto &candidate : candidates)
            if (candidate.source.type == type && candidate.source.name == name)
                return candidate;
        throw std::runtime_error("No candidate " + name);
    }

    // Every profile file in the user folder, to prove the originals stay byte for byte.
    std::map<std::string, std::string> snapshot() const
    {
        std::map<std::string, std::string> result;
        for (fs::recursive_directory_iterator it(root), end; it != end; ++it)
            if (fs::is_regular_file(it->path()))
                result[it->path().string()] = bytes(it->path().string());
        return result;
    }
};
} // namespace

TEST_CASE("Only profiles tied to the printer's model and nozzle are offered for moving", "[TinMan][Preset][ScopeMigration]")
{
    MigrationFixture fixture;
    const auto       candidates = profiles_to_move(fixture.bundle, fixture.target());
    const std::vector<std::string> expected{"Every X nozzle", "Tuned on My X", "Vendor X only"};
    CHECK(MigrationFixture::names(candidates, Preset::TYPE_PRINT) == expected);
    CHECK(MigrationFixture::names(candidates, Preset::TYPE_FILAMENT) == expected);

    const auto &every = MigrationFixture::find(candidates, Preset::TYPE_FILAMENT, "Every X nozzle");
    CHECK(every.made_for == std::vector<std::string>{"Vendor X 0.4", "Vendor X 0.6"}); // the missing machine is ignored
    CHECK(every.copy_name == "Every X nozzle - Printer A");
    CHECK(every.existing_copy.empty());
    CHECK(every.problem.empty());

    SECTION("The two-nozzle printer is offered its own profiles") {
        fixture.select("Printer T");
        const auto on_t = profiles_to_move(fixture.bundle, fixture.target());
        CHECK(MigrationFixture::names(on_t, Preset::TYPE_FILAMENT) == std::vector<std::string>{"Tuned on My T"});
    }
    SECTION("A stale printer selection is refused") {
        auto target = fixture.target();
        fixture.select("Printer B");
        CHECK_THROWS(profiles_to_move(fixture.bundle, target));
    }
}

TEST_CASE("Moved copies belong to the printer and the originals stay untouched", "[TinMan][Preset][ScopeMigration]")
{
    MigrationFixture fixture;
    const auto before     = fixture.snapshot();
    const auto candidates = profiles_to_move(fixture.bundle, fixture.target());
    // Unsaved edits of the selected profiles survive the copy.
    fixture.bundle.prints.select_preset_by_name("By condition", true);
    fixture.bundle.prints.get_edited_preset().config.set_key_value("layer_height", new ConfigOptionFloat(0.123));
    std::vector<ProfileMoveRequest> requests;
    for (const auto &candidate : candidates)
        requests.push_back({&candidate.source, candidate.copy_name, std::nullopt});

    const auto saved = copy_profiles_to_physical_printer(fixture.bundle, fixture.target(), requests);
    CHECK(saved.size() == 6);
    CHECK(fixture.bundle.prints.get_selected_preset_name() == "By condition");
    CHECK(fixture.bundle.prints.get_edited_preset().config.opt_float("layer_height") == Catch::Approx(0.123));
    CHECK(fixture.bundle.printers.get_edited_preset().name == "Printer A");

    // Originals: every file that existed is byte for byte the same, and still offered on My X.
    const auto after = fixture.snapshot();
    for (const auto &file : before) {
        REQUIRE(after.count(file.first) == 1);
        CHECK(after.at(file.first) == file.second);
    }
    const Preset *original = fixture.bundle.filaments.find_preset("Tuned on My X", false, true);
    REQUIRE(original);
    CHECK(original->config.option<ConfigOptionStrings>("compatible_printers")->values == std::vector<std::string>{"My X"});

    const auto offered = [&](const std::string &printer, const std::string &name) {
        fixture.select(printer);
        const Preset *preset = fixture.bundle.filaments.find_preset(name, false, true);
        return preset != nullptr && preset->is_compatible;
    };
    CHECK(offered("Printer A", "Tuned on My X - Printer A"));
    CHECK(offered("Printer A", "Vendor X only - Printer A"));
    CHECK_FALSE(offered("Printer B", "Tuned on My X - Printer A"));
    CHECK_FALSE(offered("Printer B", "Vendor X only - Printer A"));
    CHECK_FALSE(offered("My X", "Tuned on My X - Printer A"));
    CHECK(offered("My X", "Tuned on My X"));
    CHECK(offered("Printer B", "Vendor X only")); // the shared original still serves B

    // A copy of a root user profile is detached: editing the original cannot reach it.
    const Preset *copy = fixture.bundle.filaments.find_preset("Tuned on My X - Printer A", false, true);
    REQUIRE(copy);
    CHECK(copy->inherits().empty());
    CHECK(copy->config.option<ConfigOptionStrings>("compatible_printers")->values == std::vector<std::string>{"Printer A"});
    CHECK(copy->config.option<ConfigOptionStrings>("tinman_filament_scope_physical_id")->values == std::vector<std::string>{id_a});
    CHECK(copy->config.option<ConfigOptionInts>("tinman_filament_scope_tool")->values == std::vector<int>{0});
    CHECK(copy->description.find("Copied from Tuned on My X") != std::string::npos);

    // After a restart the copies load with their scope; listing again proposes new names.
    PresetBundle reloaded;
    fixture.load(reloaded);
    const Preset *reloaded_copy = reloaded.prints.find_preset("Every X nozzle - Printer A", false, true);
    REQUIRE(reloaded_copy);
    CHECK(reloaded_copy->config.opt_string("tinman_process_scope_physical_id") == id_a);
    fixture.select("Printer A");
    const auto again = profiles_to_move(fixture.bundle, fixture.target());
    const auto &tuned = MigrationFixture::find(again, Preset::TYPE_PRINT, "Tuned on My X");
    CHECK(tuned.existing_copy == "Tuned on My X - Printer A");
    CHECK(tuned.copy_name == "Tuned on My X - Printer A (2)");
}

TEST_CASE("A copy of a profile with a vendor parent keeps inheriting from it", "[TinMan][Preset][ScopeMigration]")
{
    MigrationFixture fixture;
    auto &filaments = fixture.bundle.filaments;
    filaments.select_preset_by_name("Vendor profile", true);
    filaments.get_edited_preset().config.set_key_value("compatible_printers", new ConfigOptionStrings{"My X"});
    filaments.get_edited_preset().config.set_key_value("filament_flow_ratio", new ConfigOptionFloats{0.93});
    filaments.save_current_preset("Vendor child");
    const auto candidates = profiles_to_move(fixture.bundle, fixture.target());
    const auto &child     = MigrationFixture::find(candidates, Preset::TYPE_FILAMENT, "Vendor child");
    copy_profiles_to_physical_printer(fixture.bundle, fixture.target(), {{&child.source, child.copy_name, std::nullopt}});
    const Preset *copy = filaments.find_preset("Vendor child - Printer A", false, true);
    REQUIRE(copy);
    CHECK(copy->inherits() == "Vendor profile");
    CHECK(copy->config.option<ConfigOptionFloats>("filament_flow_ratio")->values == std::vector<double>{0.93});
}

TEST_CASE("Copy names avoid collisions and invalid requests save nothing", "[TinMan][Preset][ScopeMigration]")
{
    MigrationFixture fixture;
    // An unrelated profile already uses the natural copy name.
    auto config = fixture.bundle.prints.default_preset().config;
    fixture.bundle.prints.load_preset(fixture.bundle.prints.path_from_name("Tuned on My X - Printer A", true),
                                      "Tuned on My X - Printer A", config, true);
    fixture.bundle.prints.save_current_preset("Tuned on My X - Printer A");
    const auto  candidates = profiles_to_move(fixture.bundle, fixture.target());
    const auto &tuned      = MigrationFixture::find(candidates, Preset::TYPE_PRINT, "Tuned on My X");
    CHECK(tuned.copy_name == "Tuned on My X - Printer A (2)");
    CHECK(tuned.existing_copy.empty()); // that profile is not a copy for Printer A
    const auto &vendor_only = MigrationFixture::find(candidates, Preset::TYPE_PRINT, "Vendor X only");

    const auto before = fixture.snapshot();
    const auto refused = [&](std::vector<ProfileMoveRequest> requests) {
        CHECK_THROWS(copy_profiles_to_physical_printer(fixture.bundle, fixture.target(), requests));
        CHECK(fixture.snapshot() == before);
    };
    // The taken name, the same name twice, and an invalid name are refused before anything is written.
    refused({{&vendor_only.source, "Vendor X only - Printer A", std::nullopt}, {&tuned.source, "Tuned on My X - Printer A", std::nullopt}});
    refused({{&vendor_only.source, "Same name", std::nullopt}, {&tuned.source, "Same name", std::nullopt}});
    refused({{&tuned.source, "Bad | name", std::nullopt}});

    CHECK(profile_copy_name("CANDIDATE | U1 | PLA [Pika]", "Printer A") == "CANDIDATE - U1 - PLA (Pika) - Printer A");
}

TEST_CASE("A filament for a multi-nozzle printer needs the nozzle it was tuned on", "[TinMan][Preset][ScopeMigration]")
{
    MigrationFixture fixture;
    fixture.select("Printer T");
    const auto target = fixture.target();
    CHECK_FALSE(target.nozzle_tool_index);
    const auto  candidates = profiles_to_move(fixture.bundle, target);
    const auto &filament   = MigrationFixture::find(candidates, Preset::TYPE_FILAMENT, "Tuned on My T");
    const auto &process    = MigrationFixture::find(candidates, Preset::TYPE_PRINT, "Tuned on My T");

    const auto before = fixture.snapshot();
    CHECK_THROWS_WITH(copy_profiles_to_physical_printer(fixture.bundle, target,
                                                        {{&process.source, process.copy_name, std::nullopt},
                                                         {&filament.source, filament.copy_name, std::nullopt}}),
                      Catch::Matchers::ContainsSubstring("Choose the nozzle"));
    CHECK(fixture.snapshot() == before); // not even the process was saved
    CHECK_THROWS(copy_profiles_to_physical_printer(fixture.bundle, target, {{&filament.source, filament.copy_name, size_t(2)}}));

    copy_profiles_to_physical_printer(fixture.bundle, target,
                                      {{&process.source, process.copy_name, std::nullopt}, {&filament.source, filament.copy_name, size_t(1)}});
    const Preset *copy = fixture.bundle.filaments.find_preset("Tuned on My T - Printer T", false, true);
    REQUIRE(copy);
    CHECK(copy->config.option<ConfigOptionInts>("tinman_filament_scope_tool")->values == std::vector<int>{1});
    CHECK(copy->config.option<ConfigOptionFloats>("tinman_filament_scope_nozzle_diameter")->values == std::vector<double>{0.6});
    const Preset *process_copy = fixture.bundle.prints.find_preset("Tuned on My T - Printer T", false, true);
    REQUIRE(process_copy);
    CHECK(process_copy->config.option<ConfigOptionFloats>("tinman_process_scope_nozzle_diameters")->values ==
          std::vector<double>{0.4, 0.6});
}

TEST_CASE("Profiles in a folder are imported onto the printer without touching the folder", "[TinMan][Preset][ScopeMigration]")
{
    MigrationFixture fixture;
    const fs::path folder = fixture.root.parent_path() / fs::unique_path("tinman-harvest-%%%%-%%%%");
    struct Cleanup {
        fs::path path;
        ~Cleanup() { boost::system::error_code error; fs::remove_all(path, error); }
    } cleanup{folder};
    write(folder / "filament" / "child.json", R"({"name": "CANDIDATE | X | PLA [Pika] | r01", "inherits": "Vendor profile",
        "from": "User", "version": "2.4.0.1", "filament_settings_id": ["CANDIDATE | X | PLA [Pika] | r01"],
        "compatible_printers": ["Vendor X 0.4"], "filament_flow_ratio": ["0.95"]})");
    write(folder / "process" / "root.json", R"({"name": "Root process - TUNED", "from": "User",
        "print_settings_id": "Root process - TUNED", "layer_height": "0.16"})"); // no version, no parent
    write(folder / "process" / "orphan.json", R"({"name": "Orphan", "inherits": "Not installed", "version": "2.4.0.1",
        "print_settings_id": "Orphan"})");
    write(folder / "machine" / "machine.json", R"({"name": "Some machine", "version": "2.4.0.1", "printer_settings_id": "Some machine"})");
    write(folder / "process" / "vendor-style.json", R"({"type": "process", "name": "Vendor style", "inherits": "Vendor profile",
        "from": "system", "instantiation": "true", "layer_height": "0.12"})"); // no settings id, only a type
    write(folder / "filament" / "scoped.json", R"({"name": "Scoped", "version": "2.4.0.1", "filament_settings_id": ["Scoped"],
        "tinman_filament_scope_physical_id": [")" + id_b + R"("], "tinman_filament_scope_nozzle_diameter": ["0.4"],
        "tinman_filament_scope_tool": ["0"]})");
    write(folder / "filament" / "broken.json", "{ not json");
    write(folder / "filament" / "._child.json", "macOS resource fork");
    write(folder / "README.txt", "not a profile");
    std::map<std::string, std::string> folder_before;
    for (fs::recursive_directory_iterator it(folder), end; it != end; ++it)
        if (fs::is_regular_file(it->path()))
            folder_before[it->path().string()] = bytes(it->path().string());

    const auto candidates = profiles_to_import(fixture.bundle, fixture.target(), folder.string());
    REQUIRE(candidates.size() == 7); // the README and the "._" file are skipped
    std::map<std::string, const ProfileMoveCandidate *> by_file;
    for (const auto &candidate : candidates)
        by_file[fs::path(candidate.source_file).filename().string()] = &candidate;
    CHECK(by_file.at("child.json")->problem.empty());
    CHECK(by_file.at("child.json")->copy_name == "CANDIDATE - X - PLA (Pika) - r01 - Printer A");
    CHECK(by_file.at("child.json")->made_for == std::vector<std::string>{"Vendor X 0.4"});
    CHECK(by_file.at("root.json")->problem.empty());
    CHECK(by_file.at("vendor-style.json")->problem.empty());
    CHECK(by_file.at("vendor-style.json")->source.type == Preset::TYPE_PRINT);
    CHECK(by_file.at("orphan.json")->problem.find("Not installed") != std::string::npos);
    CHECK(by_file.at("machine.json")->problem.find("machine profile") != std::string::npos);
    CHECK(by_file.at("scoped.json")->problem.find("already belongs") != std::string::npos);
    CHECK_FALSE(by_file.at("broken.json")->problem.empty());

    const auto *child = by_file.at("child.json");
    const auto *root  = by_file.at("root.json");
    copy_profiles_to_physical_printer(fixture.bundle, fixture.target(),
                                      {{&child->source, child->copy_name, std::nullopt}, {&root->source, root->copy_name, std::nullopt}});
    for (const auto &file : folder_before)
        CHECK(bytes(file.first) == file.second);

    PresetBundle reloaded;
    fixture.load(reloaded);
    const Preset *filament = reloaded.filaments.find_preset("CANDIDATE - X - PLA (Pika) - r01 - Printer A", false, true);
    REQUIRE(filament);
    CHECK(filament->inherits() == "Vendor profile");
    CHECK(filament->config.option<ConfigOptionFloats>("filament_flow_ratio")->values == std::vector<double>{0.95});
    CHECK(filament->config.option<ConfigOptionStrings>("tinman_filament_scope_physical_id")->values == std::vector<std::string>{id_a});
    const Preset *process = reloaded.prints.find_preset("Root process - TUNED - Printer A", false, true);
    REQUIRE(process);
    CHECK(process->inherits().empty());
    CHECK(process->config.opt_float("layer_height") == Catch::Approx(0.16));
    CHECK(reloaded.prints.find_preset("Root process - TUNED", false, true) == nullptr); // nothing unscoped was imported
}

TEST_CASE("Hidden originals stay in the library and can be shown again", "[TinMan][Preset][ScopeMigration]")
{
    MigrationFixture fixture;
    const fs::path list = fixture.root / "tinman_hidden_profiles.json";
    CHECK_FALSE(fs::exists(list)); // nothing is written for users who never hide a profile
    const auto before = bytes(fixture.bundle.filaments.find_preset("Tuned on My X", false, true)->file);

    // A hidden profile that is selected stays visible (the selectors show it) but is not offered again.
    fixture.bundle.prints.select_preset_by_name("Vendor X only", true);
    set_profiles_hidden(fixture.bundle, Preset::TYPE_PRINT, {"Vendor X only"}, true);
    CHECK(fixture.bundle.prints.find_preset("Vendor X only", false, true)->is_visible);
    CHECK(MigrationFixture::names(profiles_to_move(fixture.bundle, fixture.target()), Preset::TYPE_PRINT) ==
          std::vector<std::string>{"Every X nozzle", "Tuned on My X"});
    set_profiles_hidden(fixture.bundle, Preset::TYPE_PRINT, {"Vendor X only"}, false);

    set_profiles_hidden(fixture.bundle, Preset::TYPE_FILAMENT, {"Tuned on My X", "Vendor profile"}, true);
    CHECK(fs::exists(list));
    CHECK(hidden_profiles(fixture.bundle, Preset::TYPE_FILAMENT) == std::vector<std::string>{"Tuned on My X"}); // system profiles cannot be hidden
    CHECK_FALSE(fixture.bundle.filaments.find_preset("Tuned on My X", false, true)->is_visible);
    CHECK(fixture.bundle.filaments.find_preset("Vendor profile", false, true)->is_visible);
    CHECK(hidden_profiles(fixture.bundle, Preset::TYPE_PRINT).empty());
    // Hidden profiles are not offered for moving again.
    CHECK(MigrationFixture::names(profiles_to_move(fixture.bundle, fixture.target()), Preset::TYPE_FILAMENT) ==
          std::vector<std::string>{"Every X nozzle", "Vendor X only"});

    PresetBundle reloaded;
    fixture.load(reloaded);
    CHECK_FALSE(reloaded.filaments.find_preset("Tuned on My X", false, true)->is_visible);
    CHECK(bytes(reloaded.filaments.find_preset("Tuned on My X", false, true)->file) == before);

    set_profiles_hidden(fixture.bundle, Preset::TYPE_FILAMENT, {"Tuned on My X"}, false);
    CHECK(fixture.bundle.filaments.find_preset("Tuned on My X", false, true)->is_visible);
    CHECK_FALSE(fs::exists(list)); // the list is removed once it is empty
}
