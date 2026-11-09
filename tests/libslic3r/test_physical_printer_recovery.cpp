#include <catch2/catch_all.hpp>

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrinterConnectionUpdate.hpp"
#include "libslic3r/Utils.hpp"

using namespace Slic3r;

namespace {
namespace fs = boost::filesystem;

struct ConnectionFixture {
    fs::path root = fs::temp_directory_path() / fs::unique_path("tinman-connection-%%%%-%%%%");
    PresetBundle bundle;

    ConnectionFixture()
    {
        fs::create_directories(root / "physical_printer");
        auto config = bundle.printers.default_preset().config;
        config.option<ConfigOptionString>("inherits", true)->value.clear();
        bundle.printers.load_preset("", "Machine A", config, true);
        bundle.printers.load_preset("", "Machine B", config, false);
        PresetsConfigSubstitutions substitutions;
        bundle.physical_printers.load_printers(root.string(), "physical_printer", substitutions,
                                               ForwardCompatibilitySubstitutionRule::Disable);
    }

    ~ConnectionFixture()
    {
        boost::system::error_code error;
        fs::remove_all(root, error);
    }

    DynamicPrintConfig config(const std::string &host = "192.0.2.10") const
    {
        auto result = bundle.physical_printers.default_config();
        result.option<ConfigOptionStrings>("preset_names")->values = {"Machine A"};
        result.opt_string("print_host") = host;
        result.opt_string("printhost_apikey") = "fixture-key-not-a-real-credential";
        return result;
    }

    std::string file(const std::string &name) const
    {
        return bundle.physical_printers.path_from_name(name);
    }

    void add(const std::string &name)
    {
        bundle.physical_printers.load_printer(file(name), name, config(), true, true);
    }
};

std::string read_bytes(const std::string &file)
{
    boost::nowide::ifstream input(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), {});
}
} // namespace

TEST_CASE("Failed connection overwrite retains the stored printer and draft", "[Preset][ConnectionRecovery][TinMan]")
{
    ConnectionFixture fixture;
    fixture.add("Workshop");
    auto &printers = fixture.bundle.physical_printers;
    const auto previous = printers.get_selected_printer();
    const auto old_bytes = read_bytes(fixture.file("Workshop"));
    PhysicalPrinter candidate = previous;
    candidate.config.opt_string("print_host") = std::string(1, char(0xff));
    candidate.add_preset("Machine B");
    const auto pending_config = candidate.config;

    REQUIRE_THROWS(printers.save_printer(candidate));
    CHECK(printers.get_selected_full_printer_name() == "Workshop * Machine A");
    CHECK(printers.get_selected_printer().config == previous.config);
    CHECK(printers.get_selected_printer().preset_names == previous.preset_names);
    CHECK(candidate.config == pending_config);
    CHECK(candidate.preset_names == std::set<std::string>{"Machine A", "Machine B"});
    CHECK(read_bytes(fixture.file("Workshop")) == old_bytes);
}

TEST_CASE("Import with connection persistence stages new and replacement records", "[Preset][ConnectionRecovery][TinMan]")
{
    const bool overwrite = GENERATE(false, true);
    ConnectionFixture fixture;
    fixture.add("Workshop");
    auto &printers = fixture.bundle.physical_printers;
    const auto previous = printers.get_selected_printer();
    const auto size = printers().size();
    const auto bad_path = (fixture.root / "missing-directory" / "Printer.json").string();

    REQUIRE_THROWS(printers.load_printer(bad_path, overwrite ? "Workshop" : "New connection",
                                        fixture.config("192.0.2.20"), true, true));
    CHECK(printers().size() == size);
    CHECK(printers.get_selected_full_printer_name() == "Workshop * Machine A");
    REQUIRE(printers.find_printer("Workshop") != nullptr);
    CHECK(printers.find_printer("Workshop")->config == previous.config);
    CHECK(printers.find_printer("Workshop")->file == previous.file);
    CHECK(printers.find_printer("New connection") == nullptr);
}

TEST_CASE("Malformed physical printer files are retained but never published", "[Preset][ConnectionRecovery][TinMan]")
{
    const std::string bad_json = GENERATE(std::string("{ broken"), std::string("[]"),
        std::string("{\"version\":\"invalid\",\"print_host\":\"192.0.2.20\"}"));
    ConnectionFixture fixture;
    fixture.add("Workshop");
    write_file_with_replace(fixture.file("Broken"), bad_json);
    auto &printers = fixture.bundle.physical_printers;
    const auto previous = printers.get_selected_printer();
    PresetsConfigSubstitutions substitutions;

    CHECK_THROWS(printers.load_printers(fixture.root.string(), "physical_printer", substitutions,
                                       ForwardCompatibilitySubstitutionRule::Disable));
    CHECK(printers.find_printer("Broken") == nullptr);
    CHECK(read_bytes(fixture.file("Broken")) == bad_json);
    CHECK(printers.get_selected_full_printer_name() == "Workshop * Machine A");
    CHECK(printers.get_selected_printer().config == previous.config);
}

TEST_CASE("Connection save and rename round-trip credentials and nozzle associations", "[Preset][ConnectionRecovery][TinMan]")
{
    const bool rename = GENERATE(false, true);
    ConnectionFixture fixture;
    fixture.add("Workshop");
    auto &printers = fixture.bundle.physical_printers;
    PhysicalPrinter candidate = printers.get_selected_printer();
    candidate.config.opt_string("print_host") = "192.0.2.20";
    candidate.add_preset("Machine B");
    if (rename)
        candidate.name = "Renamed Workshop";
    const auto pending_config = candidate.config;
    REQUIRE_NOTHROW(printers.save_printer(candidate, rename ? "Workshop" : ""));
    CHECK(candidate.config == pending_config);
    CHECK(printers().size() == 1);
    CHECK(printers.get_selected_printer_name() == candidate.name);
    CHECK(printers.get_selected_printer_preset_name() == "Machine A");
    CHECK(printers.get_selected_printer().config.opt_string("print_host") == "192.0.2.20");
    CHECK(printers.get_selected_printer().config.opt_string("printhost_apikey") == "fixture-key-not-a-real-credential");
    CHECK(printers.get_selected_printer().preset_names == std::set<std::string>{"Machine A", "Machine B"});
    CHECK(fs::is_regular_file(fixture.file(candidate.name)));
    if (rename)
        CHECK_FALSE(fs::exists(fixture.file("Workshop")));

    PhysicalPrinterCollection reloaded(PhysicalPrinter::printer_options(), &fixture.bundle);
    PresetsConfigSubstitutions substitutions;
    REQUIRE_NOTHROW(reloaded.load_printers(fixture.root.string(), "physical_printer", substitutions,
                                          ForwardCompatibilitySubstitutionRule::Disable));
    REQUIRE(reloaded.find_printer(candidate.name) != nullptr);
    CHECK(reloaded.find_printer(candidate.name)->config == printers.get_selected_printer().config);
    reloaded.select_printer(candidate.name, "Machine B");
    CHECK(reloaded.has_selection());
    CHECK(reloaded.get_selected_printer_preset_name() == "Machine B");
}

TEST_CASE("Failed connection rename preserves the original file and identity", "[Preset][ConnectionRecovery][TinMan]")
{
    const bool collision = GENERATE(false, true);
    ConnectionFixture fixture;
    if (collision)
        fixture.add("Occupied");
    fixture.add("Workshop");
    auto &printers = fixture.bundle.physical_printers;
    const auto previous = printers.get_selected_printer();
    const auto old_bytes = read_bytes(fixture.file("Workshop"));
    const auto other_bytes = read_bytes(fixture.file("Occupied"));
    PhysicalPrinter candidate = previous;
    candidate.name = collision ? "Occupied" : "Renamed";
    if (!collision)
        candidate.config.opt_string("print_host") = std::string(1, char(0xff));
    const auto pending_config = candidate.config;
    REQUIRE_THROWS(printers.save_printer(candidate, "Workshop"));
    CHECK(candidate.config == pending_config);
    CHECK(printers.get_selected_full_printer_name() == "Workshop * Machine A");
    CHECK(printers.get_selected_printer().config == previous.config);
    CHECK(printers.get_selected_printer().file == previous.file);
    CHECK(read_bytes(fixture.file("Workshop")) == old_bytes);
    CHECK(read_bytes(fixture.file("Occupied")) == other_bytes);
    CHECK(printers.find_printer("Renamed") == nullptr);
    CHECK_FALSE(fs::exists(fixture.file("Renamed")));
}

TEST_CASE("Deferred connection actions preserve existing associations until applied", "[Preset][ConnectionRecovery][TinMan]")
{
    using Action = PendingPhysicalPrinterUpdate::Action;
    const auto action = GENERATE(Action::None, Action::Switch, Action::Replace, Action::Add);
    ConnectionFixture fixture;
    fixture.add("Workshop");
    auto &printers = fixture.bundle.physical_printers;
    const auto before = read_bytes(fixture.file("Workshop"));
    PendingPhysicalPrinterUpdate request{action, "Workshop", "Machine A", "Machine B"};
    CHECK(read_bytes(fixture.file("Workshop")) == before);
    CHECK(printers.get_selected_full_printer_name() == "Workshop * Machine A");
    REQUIRE_NOTHROW(request.apply(fixture.bundle));
    const PhysicalPrinter *saved = printers.find_printer("Workshop");
    REQUIRE(saved != nullptr);
    CHECK(saved->config.opt_string("printhost_apikey") == "fixture-key-not-a-real-credential");
    if (action == Action::None || action == Action::Switch) {
        CHECK(read_bytes(fixture.file("Workshop")) == before);
        CHECK(saved->preset_names == std::set<std::string>{"Machine A"});
        CHECK(printers.has_selection() == (action == Action::None));
    } else {
        CHECK(printers.get_selected_full_printer_name() == "Workshop * Machine B");
        CHECK(saved->preset_names.count("Machine B") == 1);
        CHECK(saved->preset_names.count("Machine A") == (action == Action::Add ? 1 : 0));
        CHECK(saved->config.option<ConfigOptionStrings>("preset_names")->values.size() == saved->preset_names.size());
    }
}

TEST_CASE("Deferred connection updates reject stale choices and preserve failed changes", "[Preset][ConnectionRecovery][TinMan]")
{
    const int failure = GENERATE(0, 1, 2);
    ConnectionFixture fixture;
    fixture.add("Other workshop");
    fixture.add("Workshop");
    auto &printers = fixture.bundle.physical_printers;
    PendingPhysicalPrinterUpdate request{PendingPhysicalPrinterUpdate::Action::Replace, "Workshop", "Machine A", "Machine B"};
    if (failure == 0)
        request.new_preset_name = "Missing Machine";
    else if (failure == 1)
        printers.select_printer("Other workshop", "Machine A");
    else {
        fs::rename(fixture.file("Workshop"), fixture.root / "original.json");
        fs::create_directory(fixture.file("Workshop"));
    }
    const auto before = printers.get_selected_printer();
    const auto full_name = printers.get_selected_full_printer_name();
    REQUIRE_THROWS(request.apply(fixture.bundle));
    CHECK(printers.get_selected_full_printer_name() == full_name);
    CHECK(printers.get_selected_printer().config == before.config);
    CHECK(printers.get_selected_printer().preset_names == before.preset_names);
    if (failure == 2) {
        fs::remove(fixture.file("Workshop"));
        fs::rename(fixture.root / "original.json", fixture.file("Workshop"));
        REQUIRE_NOTHROW(request.apply(fixture.bundle));
        CHECK(printers.get_selected_full_printer_name() == "Workshop * Machine B");
    }
}

TEST_CASE("Creating a physical printer identity saves its own machine profile without switching selections",
          "[Preset][ConnectionRecovery][PhysicalUnit]")
{
    ConnectionFixture fixture;
    fs::create_directories(fixture.root / "machine");
    PresetsConfigSubstitutions substitutions;
    fixture.bundle.printers.load_presets(fixture.root.string(), "machine", substitutions,
                                        ForwardCompatibilitySubstitutionRule::Disable);
    fixture.add("Previous Unit");
    const std::string previous = fixture.bundle.physical_printers.get_selected_full_printer_name();
    const std::string id = "12345678-1234-4234-8234-123456789abc";
    auto &machines = fixture.bundle.printers;
    machines.get_edited_preset().config.opt_string("print_host") = "old-transport.invalid";
    machines.get_edited_preset().config.opt_string("printhost_apikey") = "old-private-key-fixture";
    machines.update_dirty();
    const Preset source = machines.get_edited_preset();
    PhysicalPrinter connection("Unit B", fixture.config("192.0.2.22"));
    connection.config.set_key_value("tinman_physical_printer_id", new ConfigOptionString(id));
    const std::string machine_name = "Unit B machine";
    std::string unbound;

    SECTION("normal creation keeps source edits and parent and stores no machine credential") {
        REQUIRE_NOTHROW(fixture.bundle.save_physical_unit(source, connection, machine_name, {}, unbound));
        CHECK(unbound.empty());
        CHECK(machines.get_edited_preset().config == source.config);
        CHECK(machines.get_edited_preset().name == source.name);
        CHECK(machines.get_edited_preset().is_dirty == source.is_dirty);
        CHECK(fixture.bundle.physical_printers.get_selected_full_printer_name() == previous);
        const Preset *saved = machines.find_preset(machine_name, false, true);
        REQUIRE(saved != nullptr);
        CHECK(saved->inherits() == source.name);
        CHECK(saved->config.opt_string("tinman_physical_printer_id") == id);
        CHECK(saved->config.opt_string("print_host").empty());
        CHECK(saved->config.opt_string("printhost_apikey").empty());
        CHECK(read_bytes(saved->file).find("old-private-key-fixture") == std::string::npos);
        const auto *unit = fixture.bundle.physical_printers.find_printer("Unit B");
        REQUIRE(unit != nullptr);
        CHECK(unit->preset_names == std::set<std::string>{machine_name});
        CHECK(unit->config.opt_string("tinman_physical_printer_id") == id);
        CHECK(unit->config.opt_string("print_host") == "192.0.2.22");

        PhysicalPrinter renamed(*unit);
        renamed.name = "Unit B renamed";
        REQUIRE_NOTHROW(fixture.bundle.physical_printers.save_printer(renamed, "Unit B"));
        CHECK(fixture.bundle.physical_printers.get_selected_printer().config.opt_string("tinman_physical_printer_id") == id);
        PhysicalPrinterCollection reloaded(PhysicalPrinter::printer_options(), &fixture.bundle);
        REQUIRE_NOTHROW(reloaded.load_printers(fixture.root.string(), "physical_printer", substitutions,
                                               ForwardCompatibilitySubstitutionRule::Disable));
        REQUIRE(reloaded.find_printer("Unit B renamed") != nullptr);
        CHECK(reloaded.find_printer("Unit B renamed")->config.opt_string("tinman_physical_printer_id") == id);
    }
    SECTION("failed connection save keeps the previous selection and reports the saved machine profile") {
        connection.config.opt_string("printhost_apikey") = std::string(1, char(0xff)); // not valid UTF-8
        REQUIRE_THROWS(fixture.bundle.save_physical_unit(source, connection, machine_name, {}, unbound));
        REQUIRE_FALSE(unbound.empty());
        CHECK(fs::is_regular_file(unbound));
        CHECK(machines.find_preset(machine_name, false, true) == nullptr);
        CHECK(machines.get_edited_preset().config == source.config);
        CHECK(fixture.bundle.physical_printers.get_selected_full_printer_name() == previous);
        CHECK(fixture.bundle.physical_printers.find_printer("Unit B") == nullptr);
        CHECK_FALSE(fs::exists(fixture.file("Unit B")));
    }
    SECTION("a child template keeps its direct parent rather than being detached") {
        auto child_config = source.config;
        child_config.opt_string("inherits") = source.name;
        machines.load_preset({}, "Child template", child_config, true);
        const Preset child = machines.get_edited_preset();
        REQUIRE_NOTHROW(fixture.bundle.save_physical_unit(child, connection, machine_name, {}, unbound));
        const Preset *saved = machines.find_preset(machine_name, false, true);
        REQUIRE(saved != nullptr);
        CHECK(saved->inherits() == source.name);
        CHECK(machines.get_edited_preset().config == child.config);
    }
    SECTION("malformed identity and stale source fail before either save") {
        connection.config.opt_string("tinman_physical_printer_id") = "not-an-identity";
        REQUIRE_THROWS(fixture.bundle.save_physical_unit(source, connection, machine_name, {}, unbound));
        CHECK(unbound.empty());
        connection.config.opt_string("tinman_physical_printer_id") = id;
        machines.get_edited_preset().config.opt_string("printer_model") = "changed while dialog open";
        REQUIRE_THROWS(fixture.bundle.save_physical_unit(source, connection, machine_name, {}, unbound));
        CHECK_FALSE(fs::exists(machines.path_from_name(machine_name)));
        CHECK_FALSE(fs::exists(fixture.file("Unit B")));
    }
    SECTION("assigning an identity to the selected existing printer keeps its machine profiles") {
        connection = fixture.bundle.physical_printers.get_selected_printer();
        connection.config.set_key_value("tinman_physical_printer_id", new ConfigOptionString(id));
        REQUIRE_NOTHROW(fixture.bundle.save_physical_unit(source, connection, machine_name, "Previous Unit", unbound));
        CHECK(fixture.bundle.physical_printers.get_selected_full_printer_name() == previous);
        const auto &unit = fixture.bundle.physical_printers.get_selected_printer();
        CHECK(unit.preset_names.count(source.name) == 1);
        CHECK(unit.preset_names.count(machine_name) == 1);
        CHECK(unit.config.opt_string("tinman_physical_printer_id") == id);
    }
    SECTION("duplicate UUID and duplicate physical name cannot overwrite another printer") {
        auto peer = fixture.bundle.physical_printers.get_selected_printer();
        peer.config.set_key_value("tinman_physical_printer_id", new ConfigOptionString(id));
        fixture.bundle.physical_printers.save_printer(peer);
        REQUIRE_THROWS(fixture.bundle.save_physical_unit(source, connection, machine_name, {}, unbound));
        CHECK(unbound.empty());
        connection.config.opt_string("tinman_physical_printer_id") = "abcdef01-1234-4234-8234-123456789abc";
        connection.name = peer.name;
        REQUIRE_THROWS(fixture.bundle.save_physical_unit(source, connection, machine_name, {}, unbound));
        CHECK_FALSE(fs::exists(machines.path_from_name(machine_name)));
    }
}

TEST_CASE("Only physical printers with an identity are loaded from the user folder", "[Preset][ConnectionRecovery][PhysicalUnit]")
{
    ConnectionFixture fixture;
    const std::string id = "12345678-1234-4234-8234-123456789abc";
    auto identified = fixture.config();
    identified.set_key_value("tinman_physical_printer_id", new ConfigOptionString(id));
    fixture.bundle.physical_printers.load_printer(fixture.file("Unit A"), "Unit A", std::move(identified), false, true);
    fixture.add("Legacy");
    write_file_with_replace(fixture.file("Broken"), "{ broken");
    const auto legacy_bytes = read_bytes(fixture.file("Legacy"));
    PresetsConfigSubstitutions substitutions;
    auto &printers = fixture.bundle.physical_printers;

    REQUIRE_NOTHROW(fixture.bundle.load_physical_units(fixture.root.string(), substitutions,
                                                       ForwardCompatibilitySubstitutionRule::Disable));
    CHECK(printers().size() == 1);
    REQUIRE(printers.find_printer("Unit A") != nullptr);
    CHECK(printers.find_printer("Unit A")->config.opt_string("tinman_physical_printer_id") == id);
    CHECK(printers.find_printer("Legacy") == nullptr);
    CHECK(printers.find_printer("Broken") == nullptr);
    CHECK(read_bytes(fixture.file("Legacy")) == legacy_bytes);
    CHECK_FALSE(printers.has_selection());
    CHECK(printers.path_from_name("Unit B") == fixture.file("Unit B"));

    printers.select_printer("Unit A", "Machine A");
    REQUIRE_NOTHROW(fixture.bundle.load_physical_units(fixture.root.string(), substitutions,
                                                       ForwardCompatibilitySubstitutionRule::Disable));
    CHECK(printers.get_selected_full_printer_name() == "Unit A * Machine A");

    // Another account folder starts without the previous account's printers or selection.
    const fs::path other = fixture.root / "other-account";
    fs::create_directories(other / "physical_printer");
    REQUIRE_NOTHROW(fixture.bundle.load_physical_units(other.string(), substitutions,
                                                       ForwardCompatibilitySubstitutionRule::Disable));
    CHECK(printers.empty());
    CHECK_FALSE(printers.has_selection());
}
