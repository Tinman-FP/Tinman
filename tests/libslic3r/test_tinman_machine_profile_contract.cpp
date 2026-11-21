#include <catch2/catch_all.hpp>
#include <catch2/catch_test_macros.hpp>

#include "libslic3r/TinManMachineProfileContract.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "slic3r/Utils/MoonrakerStorage.hpp"

#include <string>
#include <vector>

using namespace Slic3r;

TEST_CASE("TinMan curated machines expose only canonical nozzle presets", "[Preset][TinMan]")
{
    CHECK(tinmanx_machine_preset_allowed(
        "Qidi X-Plus 4 0.4 nozzle - TinMan Codex", "Qidi X-Plus 4"));
    CHECK(tinmanx_machine_preset_allowed(
        "Qidi X-Plus 4 1.0 nozzle - TinMan Codex", "Qidi X-Plus 4"));
    CHECK(tinmanx_machine_preset_allowed(
        "__subscribed/team/Qidi X-Plus 4 0.8 nozzle - TinMan Codex", "Qidi X-Plus 4"));
    CHECK(tinmanx_machine_preset_allowed(
        "__subscribed/team/Qidi X-Plus 4 0.6 nozzle - TinMan Codex", "qIdI pLuS 4"));
    CHECK(tinmanx_machine_preset_allowed(
        "Snapmaker U1 Live Mixed - TinMan Codex", "Snapmaker U1"));
    CHECK(tinmanx_machine_preset_allowed(
        "Snapmaker U1 Tooling - TinMan Codex", "Snapmaker U1"));

    CHECK_FALSE(tinmanx_machine_preset_allowed(
        "Qidi X-Plus 4 0.6 nozzle", "Qidi X-Plus 4"));
    CHECK_FALSE(tinmanx_machine_preset_allowed(
        "CURRENT QIDI Copy", "Qidi X-Plus 4"));
    CHECK_FALSE(tinmanx_machine_preset_allowed(
        "Snapmaker U1 (0.8 nozzle) - Codex Tinman", "Snapmaker U1"));
    CHECK_FALSE(tinmanx_machine_preset_allowed(
        "FibreSeek Seeker 3 - Codex", "SEEKER 3"));
    CHECK_FALSE(tinmanx_machine_preset_allowed(
        "RatRig V-Core 4 IDEX 500 COPY MODE 0.5 nozzle", "RatRig V-Core 4 IDEX 500 COPY MODE"));
    CHECK_FALSE(tinmanx_machine_preset_allowed(
        "Qidi X-Plus 4 0.4 nozzle - TinMan Codex Copy", "Qidi X-Plus 4"));

    CHECK(tinmanx_machine_preset_allowed("My Experimental Printer", "My Experimental Printer"));
}

TEST_CASE("TinMan canonicalizes persisted curated printer selections", "[Preset][TinMan]")
{
    CHECK(tinmanx_canonical_machine_preset_name(
        "Prusa CORE One L 0.6 nozzle", "Prusa CORE One L", "0.6") ==
        "Prusa CORE One L 0.6 nozzle - TinMan Codex");
    CHECK(tinmanx_canonical_machine_preset_name(
        "CURRENT Snapmaker U1 (0.8 nozzle)", "fdm_U1") ==
        "Snapmaker U1 0.8 nozzle - TinMan Codex");
    CHECK(tinmanx_canonical_machine_preset_name(
        "Snapmaker U1 0.6 nozzle - TinMan Codex - Copy", "Snapmaker U1", "0.6") ==
        "Snapmaker U1 0.6 nozzle - TinMan Codex");
    CHECK(tinmanx_canonical_machine_preset_name(
        "QIDI Plus 4 1.0 nozzle", "Qidi Plus 4") ==
        "Qidi X-Plus 4 1.0 nozzle - TinMan Codex");

    CHECK(tinmanx_canonical_machine_preset_name("My Experimental Printer", "My Experimental Printer").empty());
    CHECK(tinmanx_canonical_machine_preset_name("Prusa CORE One L 0.5 nozzle", "Prusa CORE One L").empty());
}

TEST_CASE("TinMan curated machines expose only their canonical process presets", "[Preset][TinMan]")
{
    const std::string printer = "Prusa CORE One L 0.4 nozzle - TinMan Codex";

    CHECK(tinmanx_process_preset_allowed(
        "0.20mm Quality @Prusa CORE One L 0.4 nozzle - TinMan Codex", printer));
    CHECK(tinmanx_process_preset_allowed(
        "0.20mm Quality @Prusa CORE One L 0.4 nozzle - TinMan Codex - Copy", printer));
    CHECK(tinmanx_process_preset_allowed(
        "team/process/0.20mm Tank @Prusa CORE One L 0.4 nozzle - TinMan Codex", printer));
    CHECK_FALSE(tinmanx_process_preset_allowed(
        "0.10mm FAST DETAIL @CORE One L 0.4", printer));

    CHECK(tinmanx_process_preset_allowed(
        "0.20mm STRUCTURAL @CORE One L 0.4", "Prusa CORE One L 0.4 nozzle"));
    CHECK(tinmanx_process_preset_allowed(
        "My Experimental Process", "My Experimental Printer"));
}

TEST_CASE("Snapmaker U1 tooling exposes the four mixed nozzle processes", "[Preset][TinMan]")
{
    for (const std::string &printer : {
             "Snapmaker U1 Live Mixed - TinMan Codex",
             "Snapmaker U1 Tooling - TinMan Codex"}) {
        CHECK(tinmanx_process_preset_allowed(
            "0.16mm Tank @Snapmaker U1 Live Mixed - TinMan Codex", printer));
        CHECK(tinmanx_process_preset_allowed(
            "0.20mm Quality @Snapmaker U1 Live Mixed - TinMan Codex", printer));
        CHECK(tinmanx_process_preset_allowed(
            "0.24mm Fast @Snapmaker U1 Live Mixed - TinMan Codex", printer));
        CHECK(tinmanx_process_preset_allowed(
            "0.28mm Draft @Snapmaker U1 Live Mixed - TinMan Codex", printer));
        CHECK_FALSE(tinmanx_process_preset_allowed(
            "0.30mm Quality @Snapmaker U1 0.6 nozzle - TinMan Codex", printer));
    }
}

TEST_CASE("TinMan machine catalog replaces cloud-restored variants", "[Preset][TinMan]")
{
    AppConfig config;
    config.set_variant("Qidi", "Qidi X-Plus 4", "0.2", true);
    config.set_variant("COSMOS", "Centauri COSMOS Tinman", "0.4", true);
    config.set_str("nozzle_volume_types", "Bambu Lab X1 Carbon 0.6 nozzle - TinMan Codex", "Standard");
    config.set_str("nozzle_volume_types", "Bambu Lab H2D 0.6 nozzle", "Standard,Standard");
    config.set_str("nozzle_volume_types", "Snapmaker U1 0.6 nozzle - TinMan Codex", "High Flow,High Flow,High Flow,High Flow");

    tinmanx_apply_machine_catalog(config);

    REQUIRE(config.vendors().size() == 9);
    CHECK_FALSE(config.get_variant("Qidi", "Qidi X-Plus 4", "0.2"));
    CHECK_FALSE(config.get_variant("COSMOS", "Centauri COSMOS Tinman", "0.4"));
    for (const char *nozzle : {"0.4", "0.6", "0.8", "1.0"}) {
        CHECK(config.get_variant("Qidi", "Qidi X-Plus 4", nozzle));
        CHECK(config.get_variant("Tinman", "FibreSeek Seeker 3", nozzle));
        CHECK_FALSE(config.get_variant("TinManX1", "FibreSeek Seeker 3", nozzle));
    }
    CHECK(config.get("nozzle_volume_types", "Bambu Lab X1 Carbon 0.6 nozzle - TinMan Codex") == "High Flow");
    CHECK(config.get("nozzle_volume_types", "Bambu Lab H2D 0.6 nozzle") == "High Flow,High Flow");
    CHECK(config.get("nozzle_volume_types", "RatRig V-Core 4 IDEX 500 0.8 nozzle - TinMan Codex") == "Standard,Standard");
    CHECK(config.get("nozzle_volume_types", "Prusa CORE One L 0.6 nozzle - TinMan Codex") == "Standard");
    CHECK(config.get("nozzle_volume_types", "Qidi X-Plus 4 0.6 nozzle - TinMan Codex") == "Standard");
    CHECK(config.get("nozzle_volume_types", "Snapmaker U1 0.6 nozzle - TinMan Codex") == "Standard,Standard,Standard,Standard");
}

TEST_CASE("Public Tinman does not preselect personal machine variants", "[Preset][TinMan]")
{
    AppConfig config;
    CHECK(config.vendors().empty());
}

TEST_CASE("TinMan machine hardware overrides stale project nozzle flow", "[Preset][TinMan]")
{
    DynamicPrintConfig project;
    project.set_key_value("nozzle_volume_type",
        new ConfigOptionEnumsGeneric({NozzleVolumeType::nvtStandard}));

    DynamicPrintConfig x1c;
    x1c.set_key_value("printer_model", new ConfigOptionString("Bambu Lab X1 Carbon"));
    x1c.set_key_value("printer_settings_id", new ConfigOptionString(
        "Bambu Lab X1 Carbon 0.6 nozzle - TinMan Codex"));
    x1c.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.6}));
    REQUIRE(tinmanx_apply_nozzle_volume_contract(
        "Bambu Lab X1 Carbon 0.6 nozzle - TinMan Codex", x1c, project));
    CHECK(project.option<ConfigOptionEnumsGeneric>("nozzle_volume_type")->values ==
          std::vector<int>{NozzleVolumeType::nvtHighFlow});

    DynamicPrintConfig qidi;
    qidi.set_key_value("printer_model", new ConfigOptionString("Qidi X-Plus 4"));
    qidi.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.6}));
    project.option<ConfigOptionEnumsGeneric>("nozzle_volume_type")->values = {
        NozzleVolumeType::nvtHighFlow};
    REQUIRE(tinmanx_apply_nozzle_volume_contract(
        "Qidi X-Plus 4 0.6 nozzle - TinMan Codex", qidi, project));
    CHECK(project.option<ConfigOptionEnumsGeneric>("nozzle_volume_type")->values ==
          std::vector<int>{NozzleVolumeType::nvtStandard});

    DynamicPrintConfig export_config;
    REQUIRE(tinmanx_apply_nozzle_volume_contract(
        "Bambu Lab X1 Carbon 0.6 nozzle - TinMan Codex", x1c, export_config));
    CHECK(export_config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type")->values ==
          std::vector<int>{NozzleVolumeType::nvtHighFlow});

    project.option<ConfigOptionEnumsGeneric>("nozzle_volume_type")->values.assign(
        4, NozzleVolumeType::nvtHighFlow);
    DynamicPrintConfig u1;
    u1.set_key_value("printer_model", new ConfigOptionString("Snapmaker U1"));
    u1.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.6, 0.6, 0.6, 0.6}));
    REQUIRE(tinmanx_apply_nozzle_volume_contract(
        "Snapmaker U1 0.6 nozzle - TinMan Codex", u1, project));
    CHECK(project.option<ConfigOptionEnumsGeneric>("nozzle_volume_type")->values ==
          std::vector<int>(4, NozzleVolumeType::nvtStandard));

    project.option<ConfigOptionEnumsGeneric>("nozzle_volume_type")->values = {
        NozzleVolumeType::nvtStandard};
    REQUIRE(tinmanx_apply_nozzle_volume_contract(
        "imported-project.3mf", x1c, project));
    CHECK(project.option<ConfigOptionEnumsGeneric>("nozzle_volume_type")->values ==
          std::vector<int>{NozzleVolumeType::nvtHighFlow});

    x1c.set_key_value("printer_settings_id", new ConfigOptionString(
        "Bambu Lab X1 Carbon 0.6 nozzle"));
    CHECK_FALSE(tinmanx_apply_nozzle_volume_contract(
        "Bambu Lab X1 Carbon 0.6 nozzle", x1c, project));
}

TEST_CASE("TinMan multi-tool contract separates material slots from physical tools", "[Preset][TinMan][MultiTool]")
{
    DynamicPrintConfig config;
    config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.6, 0.4, 0.4, 0.6}));
    config.set_key_value("filament_map", new ConfigOptionInts({0, 2, 9}));
    config.set_key_value("nozzle_volume_type", new ConfigOptionEnumsGeneric({NozzleVolumeType::nvtHighFlow}));
    config.set_key_value("flush_multiplier", new ConfigOptionFloats({0.8, 1.2}));
    config.set_key_value("extruder_ams_count", new ConfigOptionStrings({"0:1"}));

    REQUIRE(tinmanx_normalize_multitool_config(config, 3));
    CHECK((config.option<ConfigOptionInts>("filament_map")->values == std::vector<int>{1, 2, 1}));
    CHECK((config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type")->values ==
          std::vector<int>{NozzleVolumeType::nvtHighFlow,
                           NozzleVolumeType::nvtStandard,
                           NozzleVolumeType::nvtStandard,
                           NozzleVolumeType::nvtStandard}));
    CHECK((config.option<ConfigOptionFloats>("flush_multiplier")->values ==
          std::vector<double>{0.8, 1.2, 1.0, 1.0}));
    CHECK((config.option<ConfigOptionStrings>("extruder_ams_count")->values ==
          std::vector<std::string>{"0:1", "", "", ""}));
    CHECK_FALSE(tinmanx_normalize_multitool_config(config, 3));
}

TEST_CASE("Portable exports omit connections without changing local profiles", "[Preset][TinMan][CLI]")
{
    DynamicPrintConfig config;
    config.set_key_value("print_host", new ConfigOptionString("printer.local"));
    config.set_key_value("printhost_apikey", new ConfigOptionString("test-secret"));
    config.set_key_value("printer_agent", new ConfigOptionString("moonraker"));
    config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.6, 0.4}));
    config.set_key_value("filament_map", new ConfigOptionInts({2, 1}));
    const auto portable = tinmanx_portable_project_config(config);
    CHECK_FALSE(portable.has("print_host"));
    CHECK_FALSE(portable.has("printhost_apikey"));
    CHECK_FALSE(portable.has("printer_agent"));
    CHECK(portable.opt_serialize("nozzle_diameter") == config.opt_serialize("nozzle_diameter"));
    CHECK(portable.opt_serialize("filament_map") == config.opt_serialize("filament_map"));
    CHECK(config.opt_string("printhost_apikey") == "test-secret");
}

TEST_CASE("TinMan multi-tool contract rejects a profile without a physical nozzle", "[Preset][TinMan][MultiTool]")
{
    DynamicPrintConfig config;
    config.set_key_value("filament_map", new ConfigOptionInts({2}));

    CHECK_FALSE(tinmanx_normalize_multitool_config(config, 1));
    CHECK(config.option<ConfigOptionInts>("filament_map")->values == std::vector<int>{2});
}

TEST_CASE("Mixed nozzle bridge percentages validate against the emitting tool", "[Config][TinMan][MultiTool]")
{
    FullPrintConfig config;
    config.nozzle_diameter.values = {0.6, 0.4, 0.4, 0.6};
    config.bridge_line_width.value = 100.;
    config.bridge_line_width.percent = true;

    const auto percentage_errors = validate(config);
    CHECK(percentage_errors.find("bridge_line_width") == percentage_errors.end());

    config.bridge_line_width.value = 0.5;
    config.bridge_line_width.percent = false;
    const auto absolute_errors = validate(config);
    CHECK(absolute_errors.find("bridge_line_width") != absolute_errors.end());
}

TEST_CASE("TinMan connection overlay follows a machine across nozzle presets", "[Preset][TinMan]")
{
    AppConfig app_config;
    DynamicPrintConfig source;
    source.set_key_value("printer_model", new ConfigOptionString("Qidi X-Plus 4"));
    source.set_key_value("printer_agent", new ConfigOptionString("qidi"));
    source.set_key_value("print_host", new ConfigOptionString("192.0.2.145"));
    source.set_key_value("print_host_webui", new ConfigOptionString("http://192.0.2.145"));

    CHECK(tinmanx_remember_machine_connection(
        app_config, "Qidi X-Plus 4 0.6 nozzle - TinMan Codex - Copy", source));
    CHECK(app_config.get("ip_address", "Qidi X-Plus 4 1.0 nozzle - TinMan Codex") ==
          "192.0.2.145");

    DynamicPrintConfig target;
    target.set_key_value("print_host", new ConfigOptionString());
    target.set_key_value("print_host_webui", new ConfigOptionString());
    target.set_key_value("printer_model", new ConfigOptionString("Qidi X-Plus 4"));
    target.set_key_value("printer_agent", new ConfigOptionString("qidi"));
    CHECK(tinmanx_restore_machine_connection(
        app_config, "Qidi X-Plus 4 1.0 nozzle - TinMan Codex", target));
    CHECK(target.opt_string("print_host") == "192.0.2.145");
    CHECK(target.opt_string("print_host_webui") == "http://192.0.2.145");
    CHECK(target.opt_string("printer_agent") == "qidi");
}

TEST_CASE("TinMan connection overlay skips a physical printer's own machine profile", "[Preset][TinMan][PhysicalUnit]")
{
    AppConfig app_config;
    DynamicPrintConfig shared;
    shared.set_key_value("printer_model", new ConfigOptionString("Qidi X-Plus 4"));
    shared.set_key_value("printer_agent", new ConfigOptionString("qidi"));
    shared.set_key_value("print_host", new ConfigOptionString("192.0.2.145"));
    shared.set_key_value("print_host_webui", new ConfigOptionString("http://192.0.2.145"));
    REQUIRE(tinmanx_remember_machine_connection(app_config, "Qidi X-Plus 4 0.4 nozzle - TinMan Codex", shared));

    DynamicPrintConfig unit(shared);
    unit.set_key_value("tinman_physical_printer_id", new ConfigOptionString("12345678-1234-4234-8234-123456789abc"));
    unit.set_key_value("print_host", new ConfigOptionString("192.0.2.150"));
    unit.set_key_value("print_host_webui", new ConfigOptionString("http://192.0.2.150"));
    // Saving printer B's connection must not become the address of every printer of the model ...
    CHECK_FALSE(tinmanx_remember_machine_connection(app_config, "Workshop B", unit));
    CHECK(app_config.get("ip_address", "Qidi X-Plus 4 0.4 nozzle - TinMan Codex") == "192.0.2.145");
    // ... and the model-wide address must not replace printer B's own.
    tinmanx_restore_machine_connection(app_config, "Workshop B", unit);
    CHECK(unit.opt_string("print_host") == "192.0.2.150");
    CHECK(unit.opt_string("print_host_webui") == "http://192.0.2.150");
}

TEST_CASE("TinMan connection routing cannot downgrade specialized printer agents", "[Preset][TinMan]")
{
    CHECK(tinmanx_expected_printer_agent("Creality K2 Plus 0.6 nozzle - TinMan Codex") == "crealityprint");
    CHECK(tinmanx_expected_printer_agent("Qidi X-Plus 4", "Qidi X-Plus 4") == "qidi");
    CHECK(tinmanx_expected_printer_agent("Snapmaker U1", "Snapmaker U1") == "snapmaker");
    CHECK(tinmanx_expected_printer_agent("My Experimental Printer").empty());

    AppConfig app_config;
    DynamicPrintConfig source;
    source.set_key_value("printer_model", new ConfigOptionString("Creality K2 Plus"));
    source.set_key_value("printer_agent", new ConfigOptionString("moonraker"));
    source.set_key_value("host_type", new ConfigOptionEnum<PrintHostType>(htMoonraker));
    source.set_key_value("print_host", new ConfigOptionString("192.0.2.174:7125"));
    source.set_key_value("print_host_webui", new ConfigOptionString("http://192.0.2.174:4408/"));

    const std::string preset = "Creality K2 Plus 0.6 nozzle - TinMan Codex - Copy";
    CHECK(tinmanx_remember_machine_connection(app_config, preset, source));
    CHECK(app_config.get("tinman_machine_connections", "Creality K2 Plus::printer_agent") == "crealityprint");
    CHECK(app_config.get("tinman_machine_connections", "Creality K2 Plus::host_type") == "crealityprint");
    CHECK(app_config.get("tinman_machine_connections", "Creality K2 Plus::print_host") == "192.0.2.174");
    CHECK(app_config.get("tinman_machine_connections", "Creality K2 Plus::print_host_webui") ==
          "http://192.0.2.174:4408/");
    CHECK(app_config.get("ip_address", "Creality K2 Plus 0.4 nozzle - TinMan Codex") == "192.0.2.174");

    DynamicPrintConfig restored = source;
    CHECK(tinmanx_restore_machine_connection(app_config, preset, restored));
    CHECK(restored.opt_string("printer_agent") == "crealityprint");
    CHECK(restored.opt_enum<PrintHostType>("host_type") == htCrealityPrint);
    CHECK(restored.opt_string("print_host") == "192.0.2.174");
    CHECK(restored.opt_string("print_host_webui") == "http://192.0.2.174:4408/");
}

TEST_CASE("TinMan Qidi connections cannot fall back to OctoPrint", "[Preset][TinMan]")
{
    AppConfig app_config;
    DynamicPrintConfig source;
    source.set_key_value("printer_model", new ConfigOptionString("Qidi X-Plus 4"));
    source.set_key_value("printer_agent", new ConfigOptionString("qidi"));
    source.set_key_value("host_type", new ConfigOptionEnum<PrintHostType>(htOctoPrint));
    source.set_key_value("print_host", new ConfigOptionString("192.0.2.145"));
    source.set_key_value("print_host_webui", new ConfigOptionString("192.0.2.145"));

    const std::string preset = "Qidi X-Plus 4 0.6 nozzle - TinMan Codex - Copy";
    CHECK(tinmanx_enforce_machine_connection_contract(preset, source));
    CHECK(source.opt_enum<PrintHostType>("host_type") == htMoonraker);

    CHECK(tinmanx_remember_machine_connection(app_config, preset, source));
    CHECK(app_config.get("tinman_machine_connections", "Qidi X-Plus 4::host_type") == "moonraker");

    source.set_key_value("host_type", new ConfigOptionEnum<PrintHostType>(htOctoPrint));
    CHECK(tinmanx_restore_machine_connection(app_config, preset, source));
    CHECK(source.opt_enum<PrintHostType>("host_type") == htMoonraker);
}

TEST_CASE("TinMan Prusa CORE One L keeps PrusaLink across dialog saves and restores", "[Preset][TinMan][PrusaLink]")
{
    const std::string section = "tinman_machine_connections";
    const std::string model   = "Prusa CORE One L";
    const std::string preset  = "Prusa CORE One L 0.6 nozzle - TinMan Codex - Copy";
    CHECK(tinmanx_expected_printer_agent(preset, model) == "orca");

    // Dialog state left by older builds: the catalog forced the Moonraker agent.
    DynamicPrintConfig dialog;
    dialog.set_key_value("printer_model", new ConfigOptionString(model));
    dialog.set_key_value("printer_agent", new ConfigOptionString("moonraker"));
    dialog.set_key_value("host_type", new ConfigOptionEnum<PrintHostType>(htPrusaLink));
    dialog.set_key_value("print_host", new ConfigOptionString("192.0.2.177"));
    dialog.set_key_value("print_host_webui", new ConfigOptionString());
    dialog.set_key_value("printhost_apikey", new ConfigOptionString("prusalink-key"));

    // Physical Printer dialog OK.
    CHECK(tinmanx_prepare_connection_for_save(preset, dialog));
    CHECK(dialog.opt_string("printer_agent") == "orca");
    CHECK(dialog.opt_enum<PrintHostType>("host_type") == htPrusaLink);
    CHECK(dialog.opt_string("print_host_webui").empty());
    CHECK_FALSE(tinmanx_prepare_connection_for_save(preset, dialog));

    AppConfig app_config;
    CHECK(tinmanx_remember_machine_connection(app_config, preset, dialog));
    CHECK(app_config.get(section, model + "::host_type") == "prusalink");
    CHECK(app_config.get(section, model + "::printer_agent") == "orca");

    // Restart: the overlay is restored onto another nozzle's stale profile.
    DynamicPrintConfig restored = dialog;
    restored.set_key_value("printer_agent", new ConfigOptionString("moonraker"));
    restored.set_key_value("host_type", new ConfigOptionEnum<PrintHostType>(htMoonraker));
    restored.set_key_value("print_host", new ConfigOptionString());
    CHECK(tinmanx_restore_machine_connection(app_config, "Prusa CORE One L 0.4 nozzle - TinMan Codex", restored));
    CHECK(restored.opt_string("printer_agent") == "orca");
    CHECK(restored.opt_enum<PrintHostType>("host_type") == htPrusaLink);
    CHECK(restored.opt_string("print_host") == "192.0.2.177");
    CHECK(restored.opt_string("printhost_apikey") == "prusalink-key");
}

TEST_CASE("TinMan agent repairs on a user printer copy are not unsaved changes", "[Preset][TinMan][PrusaLink]")
{
    // A user copy of a catalog printer, saved while the catalog still routed it to Moonraker.
    PrinterPresetCollection printers(Preset::TYPE_PRINTER, Preset::printer_options(),
                                     static_print_config_ref(static_cast<const PrintRegionConfig &>(FullPrintConfig::defaults())));
    const std::string copy = "Prusa CORE One L 0.6 nozzle - TinMan Codex - Copy";
    DynamicPrintConfig config(printers.default_preset().config);
    config.set_key_value("printer_model", new ConfigOptionString("Prusa CORE One L"));
    config.set_key_value("printer_agent", new ConfigOptionString("moonraker"));
    printers.load_preset(std::string(), copy, config, /*select=*/false);
    REQUIRE(printers.select_preset_by_name(copy, true));
    REQUIRE_FALSE(printers.get_selected_preset().is_system);
    REQUIRE_FALSE(tinmanx_managed_machine_preset(copy, "Prusa CORE One L")); // not a canonical preset

    // Selecting it moves both the edited copy and the saved preset to the catalog's agent.
    const AppConfig app_config;
    tinmanx_restore_selected_printer_connection(app_config, printers);
    CHECK(printers.get_edited_preset().config.opt_string("printer_agent") == "orca");
    CHECK(printers.get_selected_preset().config.opt_string("printer_agent") == "orca");
    CHECK_FALSE(printers.current_is_dirty());

    // The user's own edits still count.
    printers.get_edited_preset().config.set_key_value("printer_notes", new ConfigOptionString("edited"));
    CHECK(printers.current_is_dirty());
}

TEST_CASE("TinMan treats an empty printer agent as Orca", "[Preset][TinMan][PrusaLink]")
{
    // The agent picker stores Orca, the default for non-Bambu printers, as an empty value.
    const std::string copy = "Prusa CORE One L 0.6 nozzle - TinMan Codex - Copy";
    DynamicPrintConfig config;
    config.set_key_value("printer_model", new ConfigOptionString("Prusa CORE One L"));
    config.set_key_value("printer_agent", new ConfigOptionString());
    CHECK_FALSE(tinmanx_enforce_machine_connection_contract(copy, config));
    CHECK(config.opt_string("printer_agent").empty());

    AppConfig app_config;
    app_config.set_str("tinman_machine_connections", "Prusa CORE One L::printer_agent", "orca");
    tinmanx_restore_machine_connection(app_config, copy, config);
    CHECK(config.opt_string("printer_agent").empty());
}

TEST_CASE("TinMan repairs PrusaLink connections persisted as Moonraker", "[Preset][TinMan][PrusaLink]")
{
    const std::string section = "tinman_machine_connections";
    const std::string model   = "Prusa CORE One L";
    const std::string preset  = "Prusa CORE One L 0.6 nozzle - TinMan Codex";

    AppConfig app_config;
    app_config.set_str(section, model + "::host_type", "moonraker");
    app_config.set_str(section, model + "::printer_agent", "moonraker");
    app_config.set_str(section, model + "::print_host", "192.0.2.177");

    DynamicPrintConfig profile;
    profile.set_key_value("printer_model", new ConfigOptionString(model));
    profile.set_key_value("printer_agent", new ConfigOptionString());
    profile.set_key_value("host_type", new ConfigOptionEnum<PrintHostType>(htOctoPrint));
    profile.set_key_value("print_host", new ConfigOptionString());
    CHECK(tinmanx_restore_machine_connection(app_config, preset, profile));
    CHECK(profile.opt_enum<PrintHostType>("host_type") == htPrusaLink);
    CHECK(profile.opt_string("printer_agent") == "orca");
    CHECK(profile.opt_string("print_host") == "192.0.2.177");

    profile.set_key_value("host_type", new ConfigOptionEnum<PrintHostType>(htMoonraker));
    CHECK(tinmanx_remember_machine_connection(app_config, preset, profile));
    CHECK(app_config.get(section, model + "::host_type") == "prusalink");
    CHECK(app_config.get(section, model + "::printer_agent") == "orca");

    // PrusaConnect is the other Buddy firmware transport and stays selectable.
    profile.set_key_value("host_type", new ConfigOptionEnum<PrintHostType>(htPrusaConnect));
    CHECK_FALSE(tinmanx_prepare_connection_for_save(preset, profile));
    CHECK(profile.opt_enum<PrintHostType>("host_type") == htPrusaConnect);
    CHECK(tinmanx_remember_machine_connection(app_config, preset, profile));
    CHECK(app_config.get(section, model + "::host_type") == "prusaconnect");
}

TEST_CASE("TinMan dialog saves keep Klipper printers on their existing transports", "[Preset][TinMan]")
{
    const auto dialog_config = [](const std::string &model, const std::string &agent, PrintHostType host_type) {
        DynamicPrintConfig config;
        config.set_key_value("printer_model", new ConfigOptionString(model));
        config.set_key_value("printer_agent", new ConfigOptionString(agent));
        config.set_key_value("host_type", new ConfigOptionEnum<PrintHostType>(host_type));
        config.set_key_value("print_host", new ConfigOptionString("192.0.2.50"));
        config.set_key_value("print_host_webui", new ConfigOptionString());
        return config;
    };

    for (const char *model : {"Sovol SV08 MAX", "RatRig V-Core 4 IDEX 500"}) {
        INFO(model);
        DynamicPrintConfig config = dialog_config(model, "orca", htOctoPrint);
        CHECK(tinmanx_prepare_connection_for_save(std::string(model) + " 0.4 nozzle - TinMan Codex", config));
        CHECK(config.opt_string("printer_agent") == "moonraker");
        CHECK(config.opt_enum<PrintHostType>("host_type") == htMoonraker);
        CHECK(config.opt_string("print_host_webui") == "192.0.2.50");
    }

    DynamicPrintConfig snapmaker = dialog_config("Snapmaker U1", "snapmaker", htOctoPrint);
    CHECK(tinmanx_prepare_connection_for_save("Snapmaker U1 0.4 nozzle - TinMan Codex", snapmaker));
    CHECK(snapmaker.opt_enum<PrintHostType>("host_type") == htMoonraker);
    CHECK(snapmaker.opt_string("print_host_webui") == "192.0.2.50");

    DynamicPrintConfig qidi = dialog_config("Qidi X-Plus 4", "qidi", htOctoPrint);
    CHECK(tinmanx_prepare_connection_for_save("Qidi X-Plus 4 0.4 nozzle - TinMan Codex", qidi));
    CHECK(qidi.opt_string("printer_agent") == "qidi");
    CHECK(qidi.opt_enum<PrintHostType>("host_type") == htMoonraker);
    CHECK(qidi.opt_string("print_host_webui").empty());

    DynamicPrintConfig k2 = dialog_config("Creality K2 Plus", "moonraker", htMoonraker);
    CHECK(tinmanx_prepare_connection_for_save("Creality K2 Plus 0.4 nozzle - TinMan Codex", k2));
    CHECK(k2.opt_string("printer_agent") == "crealityprint");
    CHECK(k2.opt_enum<PrintHostType>("host_type") == htCrealityPrint);

    // Uncatalogued printers keep the agent the user selected.
    DynamicPrintConfig klipper = dialog_config("My Klipper Printer", "moonraker", htOctoPrint);
    CHECK(tinmanx_prepare_connection_for_save("My Klipper Printer 0.4 nozzle", klipper));
    CHECK(klipper.opt_enum<PrintHostType>("host_type") == htMoonraker);

    DynamicPrintConfig mk4s = dialog_config("MK4S", "", htPrusaLink);
    CHECK_FALSE(tinmanx_prepare_connection_for_save("Prusa MK4S 0.4 nozzle", mk4s));
    CHECK(mk4s.opt_enum<PrintHostType>("host_type") == htPrusaLink);
}

TEST_CASE("TinMan agent name fallback does not route Prusa printers to Moonraker", "[Preset][TinMan][PrusaLink]")
{
    CHECK(tinmanx_infer_printer_agent("Prusa CORE One L 192.0.2.177").empty());
    CHECK(tinmanx_infer_printer_agent("Prusa MK4S HF0.4 nozzle").empty());
    CHECK(tinmanx_infer_printer_agent("My Experimental Printer").empty());

    CHECK(tinmanx_infer_printer_agent("QIDI Plus 4") == "qidi");
    CHECK(tinmanx_infer_printer_agent("Snapmaker U1") == "snapmaker");
    CHECK(tinmanx_infer_printer_agent("Creality K2 Plus") == "crealityprint");
    CHECK(tinmanx_infer_printer_agent("K2 Plus 192.0.2.174") == "crealityprint");
    CHECK(tinmanx_infer_printer_agent("RatRig V-Core 4 IDEX 500") == "moonraker");
    CHECK(tinmanx_infer_printer_agent("v-core 4.1") == "moonraker");
    CHECK(tinmanx_infer_printer_agent("Sovol SV08 MAX") == "moonraker");
}

TEST_CASE("TinMan connection overlay migrates legacy canonical IP entries", "[Preset][TinMan]")
{
    AppConfig app_config;
    app_config.set_str("ip_address", "Snapmaker U1 0.4 nozzle - TinMan Codex", "192.0.2.3");

    DynamicPrintConfig target;
    target.set_key_value("printer_model", new ConfigOptionString("Snapmaker U1"));
    target.set_key_value("print_host", new ConfigOptionString());
    target.set_key_value("print_host_webui", new ConfigOptionString());
    CHECK(tinmanx_restore_machine_connection(
        app_config, "Snapmaker U1 0.8 nozzle - TinMan Codex", target));
    CHECK(target.opt_string("print_host") == "192.0.2.3");
    CHECK(target.opt_string("print_host_webui") == "192.0.2.3");
}

TEST_CASE("TinMan startup migration preserves a newer family connection", "[Preset][TinMan]")
{
    AppConfig app_config;
    const std::string section = "tinman_machine_connections";
    const std::string model = "Prusa CORE One L";
    const std::string current_host = "prusa-core-one-l.local";
    app_config.set_str(section, model + "::print_host", current_host);
    app_config.set_str(section, model + "::print_host_webui", current_host);

    DynamicPrintConfig stale_profile;
    stale_profile.set_key_value("printer_model", new ConfigOptionString(model));
    stale_profile.set_key_value("print_host", new ConfigOptionString("192.0.2.169"));
    stale_profile.set_key_value("print_host_webui", new ConfigOptionString("192.0.2.169"));
    stale_profile.set_key_value("printhost_apikey", new ConfigOptionString("legacy-api-key"));

    CHECK(tinmanx_remember_machine_connection(
        app_config, "Prusa CORE One L 0.6 nozzle - TinMan Codex", stale_profile, false));
    CHECK(app_config.get(section, model + "::print_host") == current_host);
    CHECK(app_config.get(section, model + "::print_host_webui") == current_host);
    CHECK(app_config.get(section, model + "::printhost_apikey") == "legacy-api-key");
    for (const char *nozzle : {"0.4", "0.6", "0.8", "1.0"}) {
        const std::string preset = model + " " + nozzle + " nozzle - TinMan Codex";
        CHECK(app_config.get("ip_address", preset) == current_host);
    }
}

TEST_CASE("TinMan runtime connection overlay has explicit dirty-state boundaries", "[Preset][TinMan]")
{
    CHECK(tinmanx_managed_machine_preset(
        "Bambu Lab H2D 0.6 nozzle - TinMan Codex", "Bambu Lab H2D"));
    CHECK_FALSE(tinmanx_managed_machine_preset("Bambu Lab H2D 0.6 nozzle", "Bambu Lab H2D"));
    CHECK_FALSE(tinmanx_managed_machine_preset("Unrelated Printer 0.6 nozzle", "Unrelated Printer"));

    CHECK(tinmanx_runtime_connection_option("print_host"));
    CHECK(tinmanx_runtime_connection_option("printhost_apikey"));
    CHECK(tinmanx_runtime_connection_option("printer_agent"));
    CHECK_FALSE(tinmanx_runtime_connection_option("nozzle_diameter"));
    CHECK_FALSE(tinmanx_runtime_connection_option("bed_shape"));
}

TEST_CASE("TinMan runtime connection overlay does not dirty managed printer presets", "[Preset][TinMan]")
{
    const std::string name = "Bambu Lab H2D 0.6 nozzle - TinMan Codex";
    Preset saved(Preset::TYPE_PRINTER, name);
    saved.config.set_key_value("printer_model", new ConfigOptionString("Bambu Lab H2D"));
    saved.config.set_key_value("printer_agent", new ConfigOptionString("bbl"));
    saved.config.set_key_value("print_host", new ConfigOptionString());
    saved.config.set_key_value("printhost_apikey", new ConfigOptionString());
    saved.config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.6, 0.6}));

    Preset edited = saved;
    edited.config.set_key_value("print_host", new ConfigOptionString("192.0.2.120"));
    edited.config.set_key_value("printhost_apikey", new ConfigOptionString("lan-access-code"));

    CHECK_FALSE(PresetCollection::is_dirty(&edited, &saved));
    CHECK(PresetCollection::dirty_options(&edited, &saved).empty());

    edited.config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.4, 0.4}));
    CHECK(PresetCollection::is_dirty(&edited, &saved));
    CHECK(PresetCollection::dirty_options(&edited, &saved) == std::vector<std::string>{"nozzle_diameter"});
}

TEST_CASE("A user-named printer of a catalog machine is not modified by its restored connection", "[Preset][TinMan]")
{
    // A print report: switching away from "Qidi Plus4 0.4mm - Codex - ABS r1" showed an empty
    // "Transfer or discard changes" dialog, because the address restored on selection differed
    // from the one saved in the preset and connection settings have no rows in that dialog.
    PrinterPresetCollection printers(Preset::TYPE_PRINTER, Preset::printer_options(),
                                     static_print_config_ref(static_cast<const PrintRegionConfig &>(FullPrintConfig::defaults())));
    const std::string name = "Qidi Plus4 0.4mm - Codex - ABS r1";
    DynamicPrintConfig config(printers.default_preset().config);
    config.set_key_value("printer_model", new ConfigOptionString("Qidi X-Plus 4"));
    config.set_key_value("printer_agent", new ConfigOptionString("qidi"));
    config.set_key_value("host_type", new ConfigOptionEnum<PrintHostType>(htMoonraker));
    config.set_key_value("print_host", new ConfigOptionString("192.0.2.10"));
    printers.load_preset(std::string(), name, config, /*select=*/false);
    REQUIRE(printers.select_preset_by_name(name, true));
    REQUIRE_FALSE(tinmanx_managed_machine_preset(name, "Qidi X-Plus 4"));
    REQUIRE(tinmanx_runtime_connection_machine(name, "Qidi X-Plus 4"));

    AppConfig app_config;
    app_config.set_str("tinman_machine_connections", "Qidi X-Plus 4::print_host", "http://192.0.2.11:7125");
    app_config.set_str("tinman_machine_connections", "Qidi X-Plus 4::print_host_webui", "http://192.0.2.11");
    tinmanx_restore_selected_printer_connection(app_config, printers);
    REQUIRE(printers.get_edited_preset().config.opt_string("print_host") == "http://192.0.2.11:7125");
    CHECK(printers.get_selected_preset().config.opt_string("print_host") == "192.0.2.10");
    CHECK_FALSE(printers.current_is_dirty());
    CHECK(printers.current_dirty_options(true).empty());

    // The user's own edits still count, and they are the only ones listed.
    printers.get_edited_preset().config.set_key_value("machine_start_gcode", new ConfigOptionString("EXTRUDER=3"));
    CHECK(printers.current_is_dirty());
    CHECK(printers.current_dirty_options(true) == std::vector<std::string>{"machine_start_gcode"});
}

TEST_CASE("Connection values still count as changes on a printer outside the catalog", "[Preset][TinMan]")
{
    Preset saved(Preset::TYPE_PRINTER, "Unrelated Printer 0.4 nozzle");
    saved.config.set_key_value("printer_model", new ConfigOptionString("Unrelated Printer"));
    saved.config.set_key_value("print_host", new ConfigOptionString());
    REQUIRE_FALSE(tinmanx_runtime_connection_machine(saved.name, "Unrelated Printer"));

    Preset edited = saved;
    edited.config.set_key_value("print_host", new ConfigOptionString("192.0.2.12"));
    CHECK(PresetCollection::is_dirty(&edited, &saved));
    CHECK(PresetCollection::dirty_options(&edited, &saved) == std::vector<std::string>{"print_host"});
}

TEST_CASE("Moonraker print uploads use only the virtual SD gcode root", "[Preset][TinMan][Moonraker]")
{
    using namespace Slic3r::MoonrakerStorage;

    CHECK(is_printable_root("gcodes", "rw"));
    CHECK(is_printable_root("gcodes", "rwd"));
    CHECK_FALSE(is_printable_root("gcodes", "r"));
    CHECK_FALSE(is_printable_root("config", "rw"));
    CHECK_FALSE(is_printable_root("timelapse", "rw"));

    CHECK(print_root("") == "gcodes");
    CHECK(print_root("gcodes") == "gcodes");
    CHECK(print_root("config") == "gcodes");
    CHECK(print_root("timelapse") == "gcodes");

    boost::property_tree::ptree standard_response;
    standard_response.put("result.item.path", "renamed-standard.gcode");
    CHECK(uploaded_path(standard_response, "fallback.gcode") == "renamed-standard.gcode");

    boost::property_tree::ptree snapmaker_response;
    snapmaker_response.put("item.path", "renamed-snapmaker.gcode");
    CHECK(uploaded_path(snapmaker_response, "fallback.gcode") == "renamed-snapmaker.gcode");

    const boost::property_tree::ptree empty_response;
    CHECK(uploaded_path(empty_response, "fallback.gcode") == "fallback.gcode");
}

namespace {

// The printers of the 2026-11-17 Snapmaker U1 print report: a K2 Plus on a 0.6 mm nozzle (the
// project open before), the U1's four TinMan Codex nozzle profiles and its declared Live Mixed
// profile, and the user's "Tooling" profile that the U1's per-tool nozzle controls save.
struct U1PrinterPick
{
    PresetBundle bundle;
    AppConfig    app_config;

    U1PrinterPick()
    {
        add("Creality K2 Plus 0.6 nozzle - TinMan Codex", "Creality K2 Plus", "0.6", {0.6}, true);
        add("Snapmaker U1 0.4 nozzle - TinMan Codex", "Snapmaker U1", "0.4", {0.4, 0.4, 0.4, 0.4}, true);
        add("Snapmaker U1 0.6 nozzle - TinMan Codex", "Snapmaker U1", "0.6", {0.6, 0.6, 0.6, 0.6}, true);
        add("Snapmaker U1 0.8 nozzle - TinMan Codex", "Snapmaker U1", "0.8", {0.8, 0.8, 0.8, 0.8}, true);
        add("Snapmaker U1 1.0 nozzle - TinMan Codex", "Snapmaker U1", "1.0", {1.0, 1.0, 1.0, 1.0}, true);
        add("Snapmaker U1 Live Mixed - TinMan Codex", "Snapmaker U1", "0.6", {0.6, 0.4, 0.4, 0.6}, true);
        add("Snapmaker U1 Tooling - TinMan Codex", "Snapmaker U1", "0.4", {0.4, 0.6, 0.6, 0.6}, false);
        select("Creality K2 Plus 0.6 nozzle - TinMan Codex");
        // export_selections writes the project's per-filament arrays, which this sizes.
        bundle.set_num_filaments(1u, std::string("#FFFFFF"));
    }

    void add(const std::string &name, const std::string &model, const std::string &variant,
             const std::vector<double> &nozzles, bool system)
    {
        DynamicPrintConfig config(bundle.printers.default_preset().config);
        config.set_key_value("printer_model", new ConfigOptionString(model));
        config.set_key_value("printer_variant", new ConfigOptionString(variant));
        config.set_key_value("nozzle_diameter", new ConfigOptionFloats(nozzles));
        Preset &preset = bundle.printers.load_preset(std::string(), name, config, false);
        preset.is_system = system;
        // load_user_preset gives a user profile its own name as alias, which Orca's nozzle switch looks up.
        if (!system)
            preset.alias = name;
    }

    void select(const std::string &name) { REQUIRE(bundle.printers.select_preset_by_name(name, true)); }

    // What choosing a model in the sidebar's printer selector opens (Plater::priv::on_select_preset).
    std::string pick_model(const std::string &model)
    {
        const Preset *preset = bundle.get_similar_printer_preset(model, {}, &app_config);
        return preset != nullptr ? preset->name : std::string();
    }

    // What choosing a nozzle in the sidebar's variant box opens (Sidebar::priv::switch_diameter_to).
    std::string pick_nozzle(const std::string &nozzle)
    {
        const Preset *preset = bundle.get_similar_printer_preset({}, nozzle);
        return preset != nullptr ? preset->name : std::string();
    }
};

} // namespace

TEST_CASE("Choosing the Snapmaker U1 opens its own nozzle profile, not the previous printer's nozzle", "[Preset][TinMan][MachinePick]")
{
    U1PrinterPick pick;
    // The report: with the 0.6 mm K2 selected, choosing "Snapmaker U1" opened the U1's 0.6 profile.
    CHECK(pick.pick_model("Snapmaker U1") == "Snapmaker U1 0.4 nozzle - TinMan Codex");
    const Preset *without_memory = pick.bundle.get_similar_printer_preset("Snapmaker U1", {});
    REQUIRE(without_memory != nullptr);
    CHECK(without_memory->name == "Snapmaker U1 0.4 nozzle - TinMan Codex");

    SECTION("the nozzle the U1 was last used with comes back") {
        pick.select("Snapmaker U1 0.8 nozzle - TinMan Codex");
        pick.bundle.export_selections(pick.app_config);
        pick.select("Creality K2 Plus 0.6 nozzle - TinMan Codex");
        pick.bundle.export_selections(pick.app_config);
        CHECK(pick.pick_model("Snapmaker U1") == "Snapmaker U1 0.8 nozzle - TinMan Codex");
    }
    SECTION("the tooling profile is not remembered as the U1's nozzle") {
        pick.select("Snapmaker U1 Tooling - TinMan Codex");
        pick.bundle.export_selections(pick.app_config);
        pick.select("Creality K2 Plus 0.6 nozzle - TinMan Codex");
        CHECK(pick.pick_model("Snapmaker U1") == "Snapmaker U1 0.4 nozzle - TinMan Codex");
    }
    SECTION("choosing the model again keeps the U1's current nozzle") {
        pick.select("Snapmaker U1 Tooling - TinMan Codex");
        CHECK(pick.pick_model("Snapmaker U1") == "Snapmaker U1 0.4 nozzle - TinMan Codex");
        pick.select("Snapmaker U1 Live Mixed - TinMan Codex");
        CHECK(pick.pick_model("Snapmaker U1") == "Snapmaker U1 0.6 nozzle - TinMan Codex");
    }
}

TEST_CASE("Choosing a model outside the TinMan catalog keeps Orca's nozzle", "[Preset][TinMan][MachinePick]")
{
    U1PrinterPick pick;
    pick.add("Test Model 0.4 nozzle", "Test Model", "0.4", {0.4}, true);
    pick.add("Test Model 0.6 nozzle", "Test Model", "0.6", {0.6}, true);
    pick.select("Creality K2 Plus 0.6 nozzle - TinMan Codex");
    CHECK(pick.pick_model("Test Model") == "Test Model 0.6 nozzle");
}

TEST_CASE("A nozzle choice on the Snapmaker U1 opens its TinMan Codex profile, not the tooling profile", "[Preset][TinMan][MachinePick]")
{
    U1PrinterPick pick;
    pick.select("Snapmaker U1 Tooling - TinMan Codex");
    // Orca looks for the selected profile's name with the nozzle swapped first, and "Tooling" names none.
    CHECK(pick.pick_nozzle("0.4") == "Snapmaker U1 0.4 nozzle - TinMan Codex");
    CHECK(pick.pick_nozzle("0.6") == "Snapmaker U1 0.6 nozzle - TinMan Codex");
}

TEST_CASE("A per-tool nozzle change that leaves every U1 tool equal opens the TinMan Codex profile", "[Preset][TinMan][MachinePick]")
{
    // Setting the last tool of a 0.4 set saved "Tooling" as an all-0.4 printer without the 0.4 profile's start G-code.
    CHECK(tinmanx_uniform_tool_nozzle_preset_name("Snapmaker U1", {0.4, 0.4, 0.4, 0.4}) == "Snapmaker U1 0.4 nozzle - TinMan Codex");
    CHECK(tinmanx_uniform_tool_nozzle_preset_name("Snapmaker U1", {1.0, 1.0, 1.0, 1.0}) == "Snapmaker U1 1.0 nozzle - TinMan Codex");
    // A mixed set, or a nozzle the catalog doesn't list, stays in the tooling profile.
    CHECK(tinmanx_uniform_tool_nozzle_preset_name("Snapmaker U1", {0.4, 0.6, 0.6, 0.6}).empty());
    CHECK(tinmanx_uniform_tool_nozzle_preset_name("Snapmaker U1", {0.2, 0.2, 0.2, 0.2}).empty());
    CHECK(tinmanx_uniform_tool_nozzle_preset_name("Snapmaker U1", {}).empty());
    CHECK(tinmanx_uniform_tool_nozzle_preset_name("My Experimental Printer", {0.4, 0.4}).empty());
}

namespace {

// The printers of the 2026-11-17 Prusa CORE One L and Snapmaker U1 print reports: the curated
// profiles, stock vendor profiles, the user's own "Prusa CORE One L 0.4 HT", and the copies earlier
// connection saves made of a curated or stock profile.
struct PrinterList
{
    PresetBundle bundle;

    PrinterList()
    {
        add("Prusa CORE One L 0.4 nozzle - TinMan Codex", "Prusa CORE One L", "0.4", true);
        add("Prusa CORE One L 0.4 nozzle", "Prusa CORE One L", "0.4", true);
        add("Snapmaker U1 0.4 nozzle - TinMan Codex", "Snapmaker U1", "0.4", true);
        add("Prusa CORE One L 0.4 HT", "Prusa CORE One L", "0.4", false, "Prusa CORE One L 0.4 nozzle - TinMan Codex");
        add("Prusa CORE One L 0.4 nozzle - TinMan Codex - Copy", "Prusa CORE One L", "0.4", false);
        add("Snapmaker U1 0.4 nozzle - TinMan Codex - Copy", "Snapmaker U1", "0.4", false, "Snapmaker U1 0.4 nozzle - TinMan Codex");
        add("Creality K2 Plus 0.4 nozzle - Copy", "Creality K2 Plus", "0.4", false, "Creality K2 Plus 0.4 nozzle");
        add("Snapmaker U1 Tooling - TinMan Codex", "Snapmaker U1", "0.4", false, "Snapmaker U1 Live Mixed - TinMan Codex");
        add("My Experimental Printer", "My Experimental Printer", "0.4", false);
    }

    void add(const std::string &name, const std::string &model, const std::string &variant, bool system,
             const std::string &inherits = {})
    {
        DynamicPrintConfig config(bundle.printers.default_preset().config);
        config.set_key_value("printer_model", new ConfigOptionString(model));
        config.set_key_value("printer_variant", new ConfigOptionString(variant));
        config.set_key_value("inherits", new ConfigOptionString(inherits));
        bundle.printers.load_preset(std::string(), name, config, false).is_system = system;
    }

    bool listed(const std::string &name) const
    {
        const Preset *preset = bundle.printers.find_preset(name, false);
        REQUIRE(preset != nullptr);
        return tinmanx_machine_preset_listed(*preset);
    }
};

} // namespace

TEST_CASE("The printer lists offer a user's own profile of a curated machine", "[Preset][TinMan][PrinterList]")
{
    PrinterList list;
    // The report: "Prusa CORE One L 0.4 HT" was missing from the Prepare list's user presets.
    CHECK(list.listed("Prusa CORE One L 0.4 HT"));
    CHECK(list.listed("Prusa CORE One L 0.4 nozzle - TinMan Codex"));
    CHECK(list.listed("Snapmaker U1 Tooling - TinMan Codex"));
    CHECK(list.listed("My Experimental Printer"));

    // Copies named after the profile they copy would list the machine twice: its model entry opens
    // the canonical profile. So would the stock vendor profiles of a curated machine.
    CHECK_FALSE(list.listed("Prusa CORE One L 0.4 nozzle - TinMan Codex - Copy"));
    CHECK_FALSE(list.listed("Snapmaker U1 0.4 nozzle - TinMan Codex - Copy"));
    CHECK_FALSE(list.listed("Creality K2 Plus 0.4 nozzle - Copy"));
    CHECK_FALSE(list.listed("Prusa CORE One L 0.4 nozzle"));
}

TEST_CASE("A restart keeps a user's own profile of a curated machine selected", "[Preset][TinMan][PrinterList]")
{
    PrinterList list;
    DynamicPrintConfig filament(list.bundle.filaments.default_preset().config);
    list.bundle.filaments.load_preset(std::string(), "Test Filament", filament, false);
    AppConfig app_config;

    SECTION("the user's own profile stays selected") {
        app_config.set("presets", PRESET_PRINTER_NAME, "Prusa CORE One L 0.4 HT");
        list.bundle.load_selections(app_config);
        CHECK(list.bundle.printers.get_selected_preset_name() == "Prusa CORE One L 0.4 HT");
    }
    SECTION("a copy of the canonical profile still moves to the canonical profile") {
        app_config.set("presets", PRESET_PRINTER_NAME, "Prusa CORE One L 0.4 nozzle - TinMan Codex - Copy");
        list.bundle.load_selections(app_config);
        CHECK(list.bundle.printers.get_selected_preset_name() == "Prusa CORE One L 0.4 nozzle - TinMan Codex");
    }
}
