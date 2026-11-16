// Tinman: updating a vendor keeps the presets the user still needs that the new stock
// bundle no longer ships (LocalVendorPresets.hpp), and user presets load when their
// parent is another user preset or they state no version.

#include <string>
#include <vector>

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/nowide/fstream.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

#include "libslic3r/Config.hpp"
#include "libslic3r/LocalVendorPresets.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PresetCacheFormat.hpp"
#include "libslic3r/Semver.hpp"
#include "libslic3r/Utils.hpp"

#include "test_utils.hpp"

using namespace Slic3r;
namespace fs = boost::filesystem;
using nlohmann::json;
using Catch::Matchers::WithinAbs;

namespace {

// A data dir and a resources dir that data_dir() and resources_dir() name for as long
// as the test runs, however it ends.
struct UpdateDirs
{
    ScopedTemporaryDir tmp{ "tinman-local-vendor" };
    std::string        previous_data{ data_dir() }, previous_resources{ resources_dir() };
    fs::path           data     = tmp.path() / "data";
    fs::path           system   = data / PRESET_SYSTEM_DIR;
    fs::path           user     = data / PRESET_USER_DIR / DEFAULT_USER_FOLDER_NAME;
    fs::path           profiles = tmp.path() / "resources" / "profiles";
    fs::path           local    = data / "system_local";

    UpdateDirs()
    {
        fs::create_directories(system);
        fs::create_directories(user);
        fs::create_directories(profiles);
        set_data_dir(data.string());
        set_resources_dir((tmp.path() / "resources").string());
    }
    ~UpdateDirs()
    {
        set_data_dir(previous_data);
        set_resources_dir(previous_resources);
    }
};

struct PresetFile
{
    std::string list;     // "process_list", "filament_list" or "machine_list"
    std::string sub_path;
    json        content;  // must state its "name"
};

void write_json(const fs::path& file, const json& content)
{
    fs::create_directories(file.parent_path());
    boost::nowide::ofstream(file.string()) << content.dump(4);
}

json read_json(const fs::path& file)
{
    boost::nowide::ifstream ifs(file.string());
    return json::parse(ifs);
}

// <dir>/<vendor>.json at `version`, listing `presets` in order, and their files.
void write_vendor(const fs::path& dir, const std::string& vendor, const std::string& version,
                  const std::vector<PresetFile>& presets)
{
    json profile = { { "name", vendor }, { "version", version },
                     { "process_list", json::array() }, { "filament_list", json::array() }, { "machine_list", json::array() } };
    for (const PresetFile& preset : presets) {
        profile[preset.list].push_back({ { "name", preset.content.at("name") }, { "sub_path", preset.sub_path } });
        write_json(dir / vendor / preset.sub_path, preset.content);
    }
    write_json(dir / (vendor + ".json"), profile);
}

json process(const std::string& name, const std::string& inherits, const std::string& layer_height,
             const std::string& instantiation = "true")
{
    json preset = { { "type", "process" }, { "name", name }, { "from", "system" }, { "instantiation", instantiation },
                    { "layer_height", layer_height } };
    if (! inherits.empty())
        preset["inherits"] = inherits;
    return preset;
}

PresetFile base(const std::string& wall_loops)
{
    json preset = process("fdm_process_acme_common", "", "0.2", "false");
    preset["wall_loops"] = wall_loops;
    return { "process_list", "process/fdm_process_acme_common.json", preset };
}

PresetFile standard() { return { "process_list", "process/standard.json", process("0.20mm Standard @Acme", "fdm_process_acme_common", "0.2") }; }

// The Acme the user has installed: the stock presets of 1.0.0, a preset they added that
// builds on the stock base, and a stock preset upstream later drops that nothing uses.
void write_installed_acme(const UpdateDirs& dirs)
{
    write_vendor(dirs.system, "Acme", "1.0.0",
                 { base("2"), standard(),
                   { "process_list", "process/Local/tuned.json",
                     process("0.16mm Tuned @Acme - Local", "fdm_process_acme_common", "0.16") },
                   { "process_list", "process/dropped.json", process("0.30mm Dropped @Acme", "fdm_process_acme_common", "0.3") } });
}

// What Acme 1.1.0 ships: the same stock presets, with a new value in the base.
void write_shipped_acme(const fs::path& dir) { write_vendor(dir, "Acme", "1.1.0", { base("4"), standard() }); }

// The user's own preset built on the one they added to Acme.
void write_user_process(const UpdateDirs& dirs, const std::string& name, const std::string& inherits,
                        const std::string& wall_loops, const std::string& version = "1.0.0")
{
    json preset = { { "type", "process" }, { "name", name }, { "from", "User" }, { "inherits", inherits }, { "wall_loops", wall_loops } };
    if (! version.empty())
        preset["version"] = version;
    write_json(dirs.user / PRESET_PRINT_NAME / (name + ".json"), preset);
}

std::vector<std::string> listed(const json& profile, const std::string& list)
{
    std::vector<std::string> names;
    if (profile.contains(list))
        for (const json& entry : profile.at(list))
            names.push_back(entry.at("name").get<std::string>());
    return names;
}

// Load `vendor` as installed, from the directory load_system_presets_from_json names.
void load_installed(PresetBundle& bundle, const std::string& vendor)
{
    bundle.load_vendor_configs_from_json((fs::path(data_dir()) / PRESET_SYSTEM_DIR).make_preferred().string(), vendor,
                                         PresetBundle::LoadSystem, ForwardCompatibilitySubstitutionRule::EnableSilent);
}

void load_user_processes(PresetBundle& bundle, const UpdateDirs& dirs)
{
    PresetsConfigSubstitutions substitutions;
    bundle.prints.load_presets(dirs.user.string(), PRESET_PRINT_NAME, substitutions, ForwardCompatibilitySubstitutionRule::EnableSilent);
}

double layer_height(const Preset& preset) { return preset.config.opt_float("layer_height"); }
int    wall_loops(const Preset& preset) { return preset.config.opt_int("wall_loops"); }

// The user's tuned preset and the one it builds on both load after an update to Acme
// 1.1.0, the added preset now on top of 1.1.0's base.
void check_tuned_presets_load(const UpdateDirs& dirs)
{
    PresetBundle bundle;
    load_installed(bundle, "Acme");
    const Preset* kept = bundle.prints.find_preset("0.16mm Tuned @Acme - Local");
    REQUIRE(kept != nullptr);
    CHECK(kept->is_system);
    REQUIRE(kept->vendor != nullptr);
    CHECK(kept->vendor->id == "Acme");
    CHECK(fs::equivalent(kept->file, dirs.local / "Acme" / "process" / "Local" / "tuned.json"));
    CHECK_THAT(layer_height(*kept), WithinAbs(0.16, 1e-9));
    CHECK(wall_loops(*kept) == 4);
    CHECK(bundle.prints.find_preset("0.20mm Standard @Acme") != nullptr);
    // Upstream dropped it and nothing uses it, so it goes, as before.
    CHECK(bundle.prints.find_preset("0.30mm Dropped @Acme") == nullptr);

    load_user_processes(bundle, dirs);
    const Preset* mine = bundle.prints.find_preset("My Tuned");
    REQUIRE(mine != nullptr);
    CHECK(mine->inherits() == "0.16mm Tuned @Acme - Local");
    CHECK_THAT(layer_height(*mine), WithinAbs(0.16, 1e-9));
    CHECK(wall_loops(*mine) == 3);
    CHECK(bundle.prints.error_count() == 0);
}

} // namespace

TEST_CASE("A preset a user preset builds on survives an update of its vendor that drops it", "[LocalVendorPresets]")
{
    UpdateDirs dirs;
    write_installed_acme(dirs);
    write_user_process(dirs, "My Tuned", "0.16mm Tuned @Acme - Local", "3");
    write_shipped_acme(dirs.profiles);

    REQUIRE(install_vendor_bundles_from_resources({ "Acme" }));
    CHECK(installed_vendor_version("Acme") == Semver(1, 1, 0));
    // The installed vendor is the stock one; the added preset is kept apart from it.
    CHECK(! fs::exists(dirs.system / "Acme" / "process" / "Local"));
    CHECK(listed(read_json(dirs.system / "Acme.json"), "process_list") ==
          std::vector<std::string>{ "fdm_process_acme_common", "0.20mm Standard @Acme" });
    const json local = read_json(dirs.local / "Acme.json");
    CHECK(listed(local, "process_list") == std::vector<std::string>{ "0.16mm Tuned @Acme - Local" });
    CHECK(fs::exists(dirs.local / "Acme" / "process" / "Local" / "tuned.json"));

    check_tuned_presets_load(dirs);

    // The next update leaves the kept preset be.
    write_vendor(dirs.profiles, "Acme", "1.2.0", { base("4"), standard() });
    REQUIRE(install_vendor_bundles_from_resources({ "Acme" }));
    CHECK(installed_vendor_version("Acme") == Semver(1, 2, 0));
    CHECK(listed(read_json(dirs.local / "Acme.json"), "process_list") == std::vector<std::string>{ "0.16mm Tuned @Acme - Local" });
    check_tuned_presets_load(dirs);
}

TEST_CASE("A preset a user preset builds on survives an update to a vendor shipped as its cache alone", "[LocalVendorPresets]")
{
    UpdateDirs dirs;
    write_installed_acme(dirs);
    write_user_process(dirs, "My Tuned", "0.16mm Tuned @Acme - Local", "3");

    // A packaged build ships the vendor as its cache, with no profile beside it.
    const fs::path staging = dirs.tmp.path() / "staging";
    write_shipped_acme(staging);
    {
        PresetBundle generator;
        generator.set_generate_vendor_caches(true);
        generator.load_vendor_configs_from_json(staging.string(), "Acme", PresetBundle::LoadSystem,
                                                ForwardCompatibilitySubstitutionRule::EnableSilent);
    }
    REQUIRE(fs::exists(staging / "Acme.opc"));
    fs::copy_file(staging / "Acme.opc", dirs.profiles / "Acme.opc");

    REQUIRE(install_vendor_bundles_from_resources({ "Acme" }));
    CHECK(fs::exists(dirs.system / "Acme.opc"));
    CHECK(! fs::exists(dirs.system / "Acme.json"));
    CHECK(! fs::exists(dirs.system / "Acme"));
    CHECK(installed_vendor_version("Acme") == Semver(1, 1, 0));
    CHECK(listed(read_json(dirs.local / "Acme.json"), "process_list") == std::vector<std::string>{ "0.16mm Tuned @Acme - Local" });

    check_tuned_presets_load(dirs);
}

TEST_CASE("An update that drops only presets nobody uses replaces the vendor as before", "[LocalVendorPresets]")
{
    UpdateDirs dirs;
    write_installed_acme(dirs);
    write_user_process(dirs, "My Standard", "0.20mm Standard @Acme", "3");
    write_shipped_acme(dirs.profiles);

    REQUIRE(install_vendor_bundles_from_resources({ "Acme" }));
    CHECK(! fs::exists(dirs.local));
    CHECK(! fs::exists(dirs.system / "Acme" / "process" / "Local"));
    CHECK(! fs::exists(dirs.system / "Acme" / "process" / "dropped.json"));

    PresetBundle bundle;
    load_installed(bundle, "Acme");
    CHECK(bundle.prints.find_preset("0.16mm Tuned @Acme - Local") == nullptr);
    CHECK(bundle.prints.find_preset("0.30mm Dropped @Acme") == nullptr);
}

TEST_CASE("A preset the update renames is followed, not kept", "[LocalVendorPresets]")
{
    UpdateDirs dirs;
    write_vendor(dirs.system, "Acme", "1.0.0",
                 { base("2"), { "process_list", "process/fine.json", process("0.12mm Old Fine @Acme", "fdm_process_acme_common", "0.12") } });
    write_user_process(dirs, "My Fine", "0.12mm Old Fine @Acme", "3");
    json renamed = process("0.12mm Fine @Acme", "fdm_process_acme_common", "0.12");
    renamed["renamed_from"] = "0.12mm Old Fine @Acme";
    write_vendor(dirs.profiles, "Acme", "1.1.0", { base("4"), { "process_list", "process/fine.json", renamed } });

    REQUIRE(install_vendor_bundles_from_resources({ "Acme" }));
    CHECK(! fs::exists(dirs.local / "Acme.json"));
}

TEST_CASE("The printers Tinman's machine contract shows are kept with the presets made for them", "[LocalVendorPresets]")
{
    UpdateDirs dirs;
    const json printer = { { "type", "machine" }, { "name", "Prusa CORE One L 0.4 nozzle - TinMan Codex" }, { "from", "system" },
                           { "instantiation", "true" }, { "inherits", "Prusa CORE One L 0.4 nozzle" },
                           { "printer_model", "Prusa CORE One L" } };
    json made_for_it = process("0.20mm Quality @Prusa CORE One L 0.4 nozzle - TinMan Codex", "0.20mm Stock @CORE One L", "0.2");
    made_for_it["compatible_printers"] = { "Prusa CORE One L 0.4 nozzle - TinMan Codex" };
    const json stock_printer = { { "type", "machine" }, { "name", "Prusa CORE One L 0.4 nozzle" }, { "from", "system" },
                                 { "instantiation", "true" }, { "printer_model", "Prusa CORE One L" } };
    const std::vector<PresetFile> stock = {
        { "process_list", "process/stock.json", process("0.20mm Stock @CORE One L", "", "0.2") },
        { "machine_list", "machine/stock.json", stock_printer } };
    std::vector<PresetFile> installed = stock;
    installed.push_back({ "process_list", "process/TinMan Codex/quality.json", made_for_it });
    installed.push_back({ "machine_list", "machine/TinMan Codex/printer.json", printer });
    // A process nothing uses that is made for a stock printer goes with the update.
    json unused = process("0.28mm Gone @CORE One L", "0.20mm Stock @CORE One L", "0.28");
    unused["compatible_printers"] = { "Prusa CORE One L 0.4 nozzle" };
    installed.push_back({ "process_list", "process/gone.json", unused });
    write_vendor(dirs.system, "Prusa", "1.0.0", installed);
    write_vendor(dirs.profiles, "Prusa", "1.1.0", stock);

    REQUIRE(install_vendor_bundles_from_resources({ "Prusa" }));
    const json local = read_json(dirs.local / "Prusa.json");
    CHECK(listed(local, "machine_list") == std::vector<std::string>{ "Prusa CORE One L 0.4 nozzle - TinMan Codex" });
    CHECK(listed(local, "process_list") == std::vector<std::string>{ "0.20mm Quality @Prusa CORE One L 0.4 nozzle - TinMan Codex" });
}

TEST_CASE("A kept printer of a nozzle size its stock model no longer lists still loads", "[LocalVendorPresets]")
{
    UpdateDirs dirs;
    auto model = [](const std::string& nozzles) {
        return PresetFile{ "machine_model_list", "machine/Test Model.json",
                           { { "type", "machine_model" }, { "name", "Test Model" }, { "nozzle_diameter", nozzles } } };
    };
    auto printer = [](const std::string& name, const std::string& variant) {
        return json{ { "type", "machine" }, { "name", name }, { "from", "system" }, { "instantiation", "true" },
                     { "printer_model", "Test Model" }, { "printer_variant", variant }, { "nozzle_diameter", { variant } } };
    };
    const PresetFile stock_printer{ "machine_list", "machine/stock.json", printer("Test Model 0.4 nozzle", "0.4") };
    // The user added a 1.0 nozzle to the stock model, and a printer for it.
    write_vendor(dirs.system, "Acme", "1.0.0",
                 { model("0.4;1.0"), stock_printer, { "machine_list", "machine/Local/big.json", printer("Test Model 1.0 nozzle - Mine", "1.0") } });
    write_json(dirs.user / "machine" / "My Big.json",
               { { "type", "machine" }, { "name", "My Big" }, { "from", "User" }, { "version", "1.0.0" },
                 { "inherits", "Test Model 1.0 nozzle - Mine" } });
    write_vendor(dirs.profiles, "Acme", "1.1.0", { model("0.4"), stock_printer });

    REQUIRE(install_vendor_bundles_from_resources({ "Acme" }));
    PresetBundle bundle;
    bundle.set_generate_vendor_caches(true);
    load_installed(bundle, "Acme");
    CHECK(bundle.printers.find_preset("Test Model 1.0 nozzle - Mine") != nullptr);
    REQUIRE(bundle.vendors.at("Acme").models.size() == 1);
    CHECK(bundle.vendors.at("Acme").models.front().variant("1.0") != nullptr);
    // The vendor's cache, written beside it, holds the vendor as it ships.
    VendorProfile cached;
    REQUIRE(VendorCacheFile::load_vendor_profile((dirs.system / "Acme.opc").string(), "Acme", Semver(1, 1, 0), cached));
    REQUIRE(cached.models.size() == 1);
    CHECK(cached.models.front().variant("1.0") == nullptr);
}

TEST_CASE("A kept preset brings the dropped presets it builds on, in an order they load in", "[LocalVendorPresets]")
{
    UpdateDirs dirs;
    // The user's preset builds on one they added, which builds on a stock preset the
    // update drops: both are kept, the dropped stock one listed first.
    write_vendor(dirs.system, "Acme", "1.0.0",
                 { base("2"),
                   { "process_list", "process/old.json", process("0.24mm Old @Acme", "fdm_process_acme_common", "0.24") },
                   { "process_list", "process/Local/mine.json", process("0.24mm Mine @Acme", "0.24mm Old @Acme", "0.25") } });
    write_user_process(dirs, "My Own", "0.24mm Mine @Acme", "5");
    write_shipped_acme(dirs.profiles);

    REQUIRE(install_vendor_bundles_from_resources({ "Acme" }));
    CHECK(listed(read_json(dirs.local / "Acme.json"), "process_list") ==
          std::vector<std::string>{ "0.24mm Old @Acme", "0.24mm Mine @Acme" });

    PresetBundle bundle;
    load_installed(bundle, "Acme");
    load_user_processes(bundle, dirs);
    const Preset* mine = bundle.prints.find_preset("My Own");
    REQUIRE(mine != nullptr);
    CHECK_THAT(layer_height(*mine), WithinAbs(0.25, 1e-9));
    CHECK(wall_loops(*mine) == 5);
    CHECK(bundle.prints.error_count() == 0);
}

TEST_CASE("A local preset that cannot load is left out and its vendor still loads", "[LocalVendorPresets]")
{
    UpdateDirs dirs;
    write_shipped_acme(dirs.profiles);
    REQUIRE(install_vendor_bundles_from_resources({ "Acme" }));
    // Local presets the vendor no longer has a parent for, or ships again itself, or
    // whose file is gone.
    write_vendor(dirs.local, "Acme", "1.0.0",
                 { { "process_list", "process/orphan.json", process("0.20mm Orphan @Acme", "0.20mm Gone @Acme", "0.2") },
                   { "process_list", "process/standard.json", process("0.20mm Standard @Acme", "fdm_process_acme_common", "0.5") },
                   { "process_list", "process/fine.json", process("0.12mm Local Fine @Acme", "fdm_process_acme_common", "0.12") } });
    json index = read_json(dirs.local / "Acme.json");
    index["process_list"].push_back({ { "name", "0.10mm Missing @Acme" }, { "sub_path", "process/missing.json" } });
    index["process_list"].push_back({ { "name", "0.10mm Outside @Acme" }, { "sub_path", "../../outside.json" } });
    write_json(dirs.local / "Acme.json", index);

    PresetBundle bundle;
    REQUIRE_NOTHROW(load_installed(bundle, "Acme"));
    CHECK(bundle.prints.find_preset("0.20mm Orphan @Acme") == nullptr);
    CHECK(bundle.prints.find_preset("0.10mm Missing @Acme") == nullptr);
    CHECK(bundle.prints.find_preset("0.10mm Outside @Acme") == nullptr);
    const Preset* standard_preset = bundle.prints.find_preset("0.20mm Standard @Acme");
    REQUIRE(standard_preset != nullptr);
    CHECK_THAT(layer_height(*standard_preset), WithinAbs(0.2, 1e-9)); // the vendor's own, not the local copy
    CHECK(bundle.prints.find_preset("0.12mm Local Fine @Acme") != nullptr);
}

TEST_CASE("Local presets load only with the vendor as installed", "[LocalVendorPresets]")
{
    UpdateDirs dirs;
    write_shipped_acme(dirs.profiles);
    REQUIRE(install_vendor_bundles_from_resources({ "Acme" }));
    write_vendor(dirs.local, "Acme", "1.0.0",
                 { { "process_list", "process/fine.json", process("0.12mm Local Fine @Acme", "fdm_process_acme_common", "0.12") } });

    PresetBundle shipped;
    shipped.load_vendor_configs_from_json(dirs.profiles.string(), "Acme", PresetBundle::LoadSystem,
                                          ForwardCompatibilitySubstitutionRule::EnableSilent);
    CHECK(shipped.prints.find_preset("0.12mm Local Fine @Acme") == nullptr);
    PresetBundle installed;
    load_installed(installed, "Acme");
    CHECK(installed.prints.find_preset("0.12mm Local Fine @Acme") != nullptr);
}

TEST_CASE("A user preset whose parent is another user preset loads", "[LocalVendorPresets][Preset]")
{
    UpdateDirs dirs;
    write_shipped_acme(dirs.system);
    // Listed before the presets they build on, so each waits for its parent.
    write_user_process(dirs, "A Grandchild", "B Child", "7");
    write_user_process(dirs, "B Child", "C Parent", "6");
    write_user_process(dirs, "C Parent", "0.20mm Standard @Acme", "5");
    write_user_process(dirs, "D Stray", "E Missing", "5");

    PresetBundle bundle;
    load_installed(bundle, "Acme");
    load_user_processes(bundle, dirs);
    for (const char* name : { "A Grandchild", "B Child", "C Parent" }) {
        const Preset* preset = bundle.prints.find_preset(name);
        REQUIRE(preset != nullptr);
        CHECK(! preset->is_system);
    }
    CHECK(bundle.prints.find_preset("A Grandchild")->inherits() == "B Child");
    CHECK(wall_loops(*bundle.prints.find_preset("A Grandchild")) == 7);
    CHECK(wall_loops(*bundle.prints.find_preset("B Child")) == 6);
    CHECK_THAT(layer_height(*bundle.prints.find_preset("A Grandchild")), WithinAbs(0.2, 1e-9));
    // A parent that is nowhere is still an error.
    CHECK(bundle.prints.find_preset("D Stray") == nullptr);
    CHECK(bundle.prints.error_count() == 1);
}

TEST_CASE("A user preset that states no version loads with its parent's", "[LocalVendorPresets][Preset]")
{
    UpdateDirs dirs;
    write_shipped_acme(dirs.system);
    write_user_process(dirs, "No Version", "0.20mm Standard @Acme", "3", "");
    write_user_process(dirs, "Bad Version", "0.20mm Standard @Acme", "3", "not a version");

    PresetBundle bundle;
    load_installed(bundle, "Acme");
    load_user_processes(bundle, dirs);
    const Preset* preset = bundle.prints.find_preset("No Version");
    REQUIRE(preset != nullptr);
    CHECK(preset->version == Semver(1, 1, 0));
    CHECK(wall_loops(*preset) == 3);
    CHECK(bundle.prints.find_preset("Bad Version") == nullptr);
}
