// Generated from Tinmans Tuning Guide wiki/routes.json; edit that contract first.
// Contract SHA-256: 80aa885dc5e3e86514d876859ba45df4edf09c0d6d45b92939af710aa738cb92
#pragma once
#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <utility>

namespace Slic3r {
inline std::string tuning_guide_url(std::string path)
{
    const std::string base = "https://github.com/Tinman-FP/tinmans-tuning-guide/wiki/";
    for (const std::string prefix : {"https://www.orcaslicer.com/wiki/", "https://orcaslicer.com/wiki/"})
        if (path.compare(0, prefix.size(), prefix) == 0) path.erase(0, prefix.size());
    const auto fragment = path.find('#');
    if (fragment != std::string::npos) {
        std::transform(path.begin() + fragment, path.end(), path.begin() + fragment,
                       [](unsigned char c) { return c == ' ' ? '-' : std::tolower(c); });
    }
    // Newer Orca paths add folders; setting identifiers remain the final component.
    const auto slash = path.rfind('/', fragment);
    if (slash != std::string::npos) path.erase(0, slash + 1);
    static const std::map<std::string, std::string> routes = {
        {"", "Home"},
        {"calibration_guide", "Home#start-here"},
        {"cornering_calib", "19-motion-diagnostics"},
        {"flow_ratio_calib", "05-flow-ratio"},
        {"import_export", "23-application-and-project-help#import-and-export"},
        {"input_shaping_calib", "19-motion-diagnostics#input-shaping-is-not-an-acceleration-limit"},
        {"material_basic_information", "22-filament-and-printer-settings#filament-identity-and-physical-properties"},
        {"material_basic_information#shrinkage-xy", "07-dimensional-accuracy"},
        {"material_basic_information#shrinkage-z", "07-dimensional-accuracy"},
        {"material_cooling", "06-cooling-and-retraction"},
        {"material_dependencies", "20-profile-integrity-and-experimental-resets#the-four-identities-that-must-agree"},
        {"material_flow_ratio_and_pressure_advance", "05-flow-ratio"},
        {"material_flow_ratio_and_pressure_advance#enable-adaptive-pressure-advance-beta", "04-pressure-advance"},
        {"material_flow_ratio_and_pressure_advance#enable-adaptive-pressure-advance-for-overhangs-beta", "04-pressure-advance"},
        {"material_flow_ratio_and_pressure_advance#pressure-advance", "04-pressure-advance"},
        {"material_flow_ratio_and_pressure_advance#pressure-advance-for-bridges", "04-pressure-advance"},
        {"material_multimaterial", "22-filament-and-printer-settings#loading-unloading-and-multimaterial"},
        {"material_temperatures", "02-conditioning-and-temperature"},
        {"material_temperatures#print-chamber-temperature", "22-filament-and-printer-settings#nozzle-bed-and-chamber-temperatures"},
        {"material_volumetric_speed_limitation", "03-volumetric-flow"},
        {"multimaterial_settings_advanced", "16-multimaterial-tab#advanced-multimaterial"},
        {"multimaterial_settings_filament_for_features", "16-multimaterial-tab#filament-for-features"},
        {"multimaterial_settings_flush_options", "16-multimaterial-tab#flush-options"},
        {"multimaterial_settings_ooze_prevention", "16-multimaterial-tab#ooze-prevention"},
        {"multimaterial_settings_prime_tower", "16-multimaterial-tab#prime-tower"},
        {"others_settings_brim", "17-others-tab#brim"},
        {"others_settings_fuzzy_skin", "17-others-tab#fuzzy-skin"},
        {"others_settings_g_code_output", "17-others-tab#g-code-output"},
        {"others_settings_notes", "17-others-tab#notes"},
        {"others_settings_post_processing_scripts", "17-others-tab#post-processing-scripts"},
        {"others_settings_skirt", "17-others-tab#skirt"},
        {"others_settings_special_mode", "17-others-tab#special-mode"},
        {"preferences_sync_switch", "23-application-and-project-help#profiles-sync-and-preservation"},
        {"pressure_advance_calib", "04-pressure-advance"},
        {"printer_basic_information_accessory", "22-filament-and-printer-settings#accessories-and-tool-changes"},
        {"printer_basic_information_adaptive_bed_mesh", "22-filament-and-printer-settings#bed-mesh-and-first-layer-baseline"},
        {"printer_basic_information_advanced", "22-filament-and-printer-settings#firmware-and-g-code"},
        {"printer_basic_information_cooling_fan", "22-filament-and-printer-settings#fans-and-air-filtration"},
        {"printer_basic_information_extruder_clearance", "22-filament-and-printer-settings#clearance-and-sequential-printing"},
        {"printer_basic_information_printable_space", "22-filament-and-printer-settings#build-volume-and-machine-identity"},
        {"printer_extruder_basic_information", "22-filament-and-printer-settings#nozzle-and-extruder-contract"},
        {"printer_extruder_retraction", "06-cooling-and-retraction"},
        {"printer_extruder_z_hop", "06-cooling-and-retraction"},
        {"printer_machine_gcode", "22-filament-and-printer-settings#firmware-and-g-code"},
        {"printer_motion_ability", "19-motion-diagnostics"},
        {"printer_motion_ability#input-shaping", "19-motion-diagnostics#input-shaping-is-not-an-acceleration-limit"},
        {"printer_multimaterial_advanced", "16-multimaterial-tab#advanced-multimaterial"},
        {"printer_multimaterial_semm_parameters", "22-filament-and-printer-settings#loading-unloading-and-multimaterial"},
        {"printer_multimaterial_setup", "22-filament-and-printer-settings#accessories-and-tool-changes"},
        {"printer_multimaterial_wipe_tower", "16-multimaterial-tab#prime-tower"},
        {"printer_notes", "22-filament-and-printer-settings#notes-and-profile-dependencies"},
        {"quality_settings_bridging", "12-quality-tab#bridging"},
        {"quality_settings_ironing", "12-quality-tab#ironing"},
        {"quality_settings_layer_height", "12-quality-tab#layer-height"},
        {"quality_settings_line_width", "12-quality-tab#line-width"},
        {"quality_settings_overhangs", "12-quality-tab#overhangs"},
        {"quality_settings_precision", "12-quality-tab#precision"},
        {"quality_settings_seam", "12-quality-tab#seam"},
        {"quality_settings_wall_and_surfaces", "12-quality-tab#walls-and-surfaces"},
        {"quality_settings_wall_generator", "12-quality-tab#wall-generator"},
        {"quality_settings_z_contouring", "12-quality-tab#z-contouring"},
        {"retraction_calib", "06-cooling-and-retraction#retraction-test"},
        {"speed_settings_acceleration", "14-speed-tab#acceleration"},
        {"speed_settings_advanced", "14-speed-tab#advanced-speed"},
        {"speed_settings_initial_layer_speed", "14-speed-tab#first-layer-speed"},
        {"speed_settings_jerk_xy", "14-speed-tab#jerk-xy"},
        {"speed_settings_jerk_xy#junction-deviation", "14-speed-tab#junction-deviation"},
        {"speed_settings_other_layers_speed", "14-speed-tab#other-layers-speed"},
        {"speed_settings_overhang_speed", "14-speed-tab#overhang-speed"},
        {"speed_settings_travel", "14-speed-tab#travel-speed"},
        {"strength_settings_advanced", "13-strength-tab#advanced-strength"},
        {"strength_settings_infill", "13-strength-tab#infill"},
        {"strength_settings_infill_rotation_template_metalanguage", "13-strength-tab#infill"},
        {"strength_settings_patterns", "13-strength-tab#infill"},
        {"strength_settings_top_bottom_shells", "13-strength-tab#top-and-bottom-shells"},
        {"strength_settings_walls", "13-strength-tab#walls"},
        {"support_settings_advanced", "15-support-tab#advanced-support"},
        {"support_settings_filament", "15-support-tab#support-filament"},
        {"support_settings_ironing", "15-support-tab#support-ironing"},
        {"support_settings_raft", "15-support-tab#raft"},
        {"support_settings_support", "15-support-tab#support"},
        {"support_settings_tree", "15-support-tab#tree-supports"},
        {"temp_calib", "02-conditioning-and-temperature"},
        {"transfer_discard_changes", "23-application-and-project-help#transferring-or-discarding-changes"},
        {"vfa_calib", "19-motion-diagnostics#use-a-vfa-sweep-for-speed-dependent-texture"},
        {"volumetric_speed_calib", "03-volumetric-flow"},
    };
    auto found = routes.find(path);
    if (found != routes.end()) return base + found->second;
    path.erase(path.find('#') == std::string::npos ? path.size() : path.find('#'));
    found = routes.find(path);
    if (found != routes.end()) return base + found->second;
    static const std::pair<const char*, const char*> prefixes[] = {
        {"preferences_", "23-application-and-project-help"},
        {"printer_", "22-filament-and-printer-settings"},
        {"material_", "22-filament-and-printer-settings"},
        {"quality_settings_", "12-quality-tab"},
        {"strength_settings_", "13-strength-tab"},
        {"speed_settings_", "14-speed-tab"},
        {"support_settings_", "15-support-tab"},
        {"multimaterial_settings_", "16-multimaterial-tab"},
        {"others_settings_", "17-others-tab"},
    };
    for (const auto& prefix : prefixes)
        if (path.compare(0, std::char_traits<char>::length(prefix.first), prefix.first) == 0)
            return base + prefix.second;
    return base + "11-orca-tinmanx1-settings-reference";
}
} // namespace Slic3r
