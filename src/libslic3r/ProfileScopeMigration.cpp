#include "ProfileScopeMigration.hpp"
#include "PresetBundle.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <utility>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>

namespace Slic3r {
namespace {
PresetCollection &collection_of(PresetBundle &bundle, Preset::Type type)
{
    return type == Preset::TYPE_PRINT ? static_cast<PresetCollection &>(bundle.prints) : static_cast<PresetCollection &>(bundle.filaments);
}

const PresetCollection &collection_of(const PresetBundle &bundle, Preset::Type type)
{
    return collection_of(const_cast<PresetBundle &>(bundle), type);
}

// A printer without an identity accepts exactly the unscoped profiles.
bool is_unscoped(const Preset &preset)
{
    static const Preset no_identity(Preset::TYPE_PRINTER, "");
    return is_compatible_with_physical_printer_scope(preset, no_identity);
}

std::string identity_of(const Preset &machine)
{
    const auto *id = machine.config.option<ConfigOptionString>("tinman_physical_printer_id");
    return id && is_valid_physical_printer_id(id->value) ? id->value : std::string();
}

const std::string &printer_model(const Preset &machine)
{
    static const std::string none;
    const auto *model = machine.config.option<ConfigOptionString>("printer_model");
    return model ? model->value : none;
}

bool same_nozzle_diameters(const Preset &machine, const std::vector<double> &diameters)
{
    const auto *nozzles = machine.config.option<ConfigOptionFloats>("nozzle_diameter");
    if (!nozzles || nozzles->values.size() != diameters.size())
        return false;
    for (size_t i = 0; i < diameters.size(); ++i)
        if (!(std::abs(nozzles->values[i] - diameters[i]) <= 1e-6))
            return false;
    return true;
}

// Without a printer_model on either side, only the target's own profile and its parent count.
bool same_printer_model(const Preset &machine, const Preset &target)
{
    if (!printer_model(machine).empty() && !printer_model(target).empty())
        return printer_model(machine) == printer_model(target);
    return machine.name == target.name || (!target.inherits().empty() && machine.name == target.inherits());
}

std::vector<std::string> compatible_printers(const Preset &preset)
{
    const auto *names = preset.config.option<ConfigOptionStrings>("compatible_printers");
    return names ? names->values : std::vector<std::string>();
}

// current_profile_scope_save_target() pairs the target with the edited machine profile.
const Preset &target_machine(const PresetBundle &bundle, const ProfileScopeSaveSelection &target)
{
    const Preset &machine = bundle.printers.get_edited_preset();
    if (machine.name != target.machine_preset_name || identity_of(machine) != target.physical_id || target.physical_id.empty())
        throw std::runtime_error("The selected physical printer changed. Reopen the dialog.");
    return machine;
}

// Propose "<name> - <printer>", or "<name> - <printer> (2)" and so on when that name is taken.
void propose_copy_names(const PresetBundle &bundle, const Preset &machine, const ProfileScopeSaveSelection &target,
                        std::vector<ProfileMoveCandidate> &candidates)
{
    std::set<std::pair<Preset::Type, std::string>> proposed;
    for (auto &candidate : candidates) {
        if (!candidate.problem.empty())
            continue;
        const PresetCollection &collection = collection_of(bundle, candidate.source.type);
        const std::string       base       = profile_copy_name(candidate.source.name, target.physical_printer_name);
        const Preset           *existing   = collection.find_preset(base, false, true);
        if (existing && !is_unscoped(*existing) && is_compatible_with_physical_printer_scope(*existing, machine))
            candidate.existing_copy = existing->name;
        const auto is_free = [&](const std::string &name) {
            return collection.find_preset(name, false, true) == nullptr && collection.get_preset_name_by_alias(name) == name &&
                   proposed.count({candidate.source.type, name}) == 0;
        };
        std::string name = base;
        for (int n = 2; !is_free(name); ++n)
            name = base + " (" + std::to_string(n) + ")";
        proposed.insert({candidate.source.type, name});
        candidate.copy_name = std::move(name);
    }
}

// Read a profile file and resolve it against its installed parent, as importing it would
// (PresetBundle::import_json_presets()). Throws the reason it cannot be used.
void read_profile_file(const PresetBundle &bundle, ProfileMoveCandidate &candidate)
{
    DynamicPrintConfig                 config;
    std::map<std::string, std::string> key_values;
    std::string                        reason;
    try {
        config.load_from_json(candidate.source_file, ForwardCompatibilitySubstitutionRule::EnableSilent, key_values, reason);
    } catch (const std::exception &error) {
        reason = error.what();
    }
    if (!reason.empty())
        throw std::runtime_error("The file cannot be read as a profile: " + reason);

    // The settings id decides, as when importing; vendor-style files only have a "type".
    const std::string &kind = key_values[BBL_JSON_KEY_TYPE];
    if (config.has("printer_settings_id") || (!config.has("print_settings_id") && !config.has("filament_settings_id") && kind == "machine"))
        throw std::runtime_error("This is a machine profile. Give a printer its own machine profile with "
                                 "\"Add a separate physical printer\" instead.");
    const Preset::Type type = config.has("print_settings_id")    ? Preset::TYPE_PRINT :
                              config.has("filament_settings_id") ? Preset::TYPE_FILAMENT :
                              kind == "process"                  ? Preset::TYPE_PRINT :
                              kind == "filament"                 ? Preset::TYPE_FILAMENT :
                                                                   Preset::TYPE_INVALID;
    if (type == Preset::TYPE_INVALID)
        throw std::runtime_error("This file is not a process or filament profile.");
    const std::string name = boost::algorithm::trim_copy(key_values[BBL_JSON_KEY_NAME]);
    if (name.empty())
        throw std::runtime_error("The profile has no name.");
    candidate.source = Preset(type, name);
    if (const auto *listed = config.option<ConfigOptionStrings>("compatible_printers"))
        candidate.made_for = listed->values;

    const PresetCollection &collection = collection_of(bundle, type);
    const auto             *inherits   = config.option<ConfigOptionString>(BBL_JSON_KEY_INHERITS);
    const Preset           *parent     = nullptr;
    if (inherits && !inherits->value.empty()) {
        parent = collection.find_preset2(inherits->value, true);
        if (!parent)
            throw std::runtime_error("Its parent profile \"" + inherits->value + "\" is not installed.");
        Preset::normalize_inherits(config, parent);
    }
    const Preset      &defaults = collection.default_preset_for(config);
    DynamicPrintConfig resolved = parent ? parent->config : defaults.config;
    resolved.apply(std::move(config));
    if (!parent)
        extend_default_config_length(resolved, true, defaults.config);
    Preset::normalize(resolved);
    Preset::remove_invalid_keys(resolved, collection.default_preset_for(resolved).config);

    Preset &preset     = candidate.source;
    preset.config      = std::move(resolved);
    preset.file        = candidate.source_file;
    preset.is_external = true; // not part of the library
    preset.loaded      = true;
    if (const auto it = key_values.find(BBL_JSON_KEY_FILAMENT_ID); it != key_values.end())
        preset.filament_id = it->second;
    else if (parent)
        preset.filament_id = parent->filament_id;
    if (parent)
        preset.base_id = parent->setting_id;
    // Loading skips a saved profile without a valid version; an old file may not have one.
    if (const auto version = Semver::parse(key_values[BBL_JSON_KEY_VERSION]))
        preset.version = *version;
    else if (parent)
        preset.version = parent->version;
    candidate.made_for = compatible_printers(preset);
    if (!is_unscoped(preset))
        throw std::runtime_error("This profile already belongs to a physical printer.");
}

boost::filesystem::path hidden_profiles_file(const PresetBundle &bundle)
{
    // physical_printers.dir_path() is <user folder>/physical_printer once the user presets are loaded.
    const std::string &dir = bundle.physical_printers.dir_path();
    return dir.empty() ? boost::filesystem::path() : boost::filesystem::path(dir).parent_path() / "tinman_hidden_profiles.json";
}

const char *hidden_profiles_key(Preset::Type type) { return type == Preset::TYPE_PRINT ? "process" : "filament"; }

nlohmann::json read_hidden_profiles(const boost::filesystem::path &file)
{
    boost::system::error_code error;
    if (file.empty() || !boost::filesystem::exists(file, error))
        return nlohmann::json::object();
    try {
        boost::nowide::ifstream stream(file.string());
        nlohmann::json          list = nlohmann::json::parse(stream);
        if (list.is_object())
            return list;
    } catch (const std::exception &err) {
        BOOST_LOG_TRIVIAL(error) << "Hidden profile list " << file.string() << " cannot be read: " << err.what();
    }
    return nlohmann::json::object();
}

std::set<std::string> hidden_profile_names(const nlohmann::json &list, Preset::Type type)
{
    std::set<std::string> names;
    const auto            it = list.find(hidden_profiles_key(type));
    if (it != list.end() && it->is_array())
        for (const auto &name : *it)
            if (name.is_string())
                names.insert(name.get<std::string>());
    return names;
}
} // namespace

std::string profile_copy_name(const std::string &name, const std::string &printer)
{
    std::string result = name;
    for (char &c : result) {
        if (c == '[')
            c = '(';
        else if (c == ']')
            c = ')';
        else if (std::string("<>:/\\|?*\"").find(c) != std::string::npos)
            c = '-';
        else if (c == '\r' || c == '\n' || c == '\t')
            c = ' ';
    }
    boost::algorithm::trim(result);
    return result + " - " + printer;
}

std::vector<ProfileMoveCandidate> profiles_to_move(const PresetBundle &bundle, const ProfileScopeSaveSelection &target)
{
    const Preset                     &machine = target_machine(bundle, target);
    const nlohmann::json              hidden  = read_hidden_profiles(hidden_profiles_file(bundle));
    std::vector<ProfileMoveCandidate> result;
    for (const Preset::Type type : {Preset::TYPE_PRINT, Preset::TYPE_FILAMENT}) {
        // A hidden profile that is still selected stays visible in the selectors, but it was moved already.
        const auto hidden_names = hidden_profile_names(hidden, type);
        for (const Preset &preset : collection_of(bundle, type)) {
            if (!preset.is_user() || preset.is_external || !preset.is_visible || !is_unscoped(preset) || hidden_names.count(preset.name))
                continue;
            ProfileMoveCandidate candidate;
            bool                 shared = false, fits_nozzles = false;
            for (const auto &name : compatible_printers(preset)) {
                const Preset *listed = bundle.printers.find_preset(name, false, true);
                if (!listed)
                    continue; // names no installed machine profile, so it cannot make the profile usable anywhere
                const std::string id = identity_of(*listed);
                if (!same_printer_model(*listed, machine) || (!id.empty() && id != target.physical_id)) {
                    shared = true;
                    break;
                }
                fits_nozzles = fits_nozzles || same_nozzle_diameters(*listed, target.nozzle_diameters);
                candidate.made_for.push_back(listed->name);
            }
            if (shared || !fits_nozzles)
                continue;
            candidate.source = preset;
            result.push_back(std::move(candidate));
        }
    }
    propose_copy_names(bundle, machine, target, result);
    return result;
}

std::vector<ProfileMoveCandidate> profiles_to_import(const PresetBundle &bundle, const ProfileScopeSaveSelection &target,
                                                     const std::string &folder)
{
    namespace fs = boost::filesystem;
    const Preset            &machine = target_machine(bundle, target);
    std::vector<fs::path>    files;
    boost::system::error_code error;
    if (!fs::is_directory(folder, error))
        throw std::runtime_error("The folder cannot be read: " + folder);
    for (fs::recursive_directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
        const std::string file_name = it->path().filename().string();
        // Skip hidden files, including the "._" companions macOS writes on other file systems.
        if (!file_name.empty() && file_name.front() != '.' && boost::iequals(it->path().extension().string(), ".json") &&
            fs::is_regular_file(it->path(), error))
            files.push_back(it->path());
    }
    if (error)
        throw std::runtime_error("The folder cannot be read: " + error.message());
    std::sort(files.begin(), files.end());

    std::vector<ProfileMoveCandidate> result;
    for (const auto &file : files) {
        ProfileMoveCandidate candidate;
        candidate.source_file = file.string();
        try {
            read_profile_file(bundle, candidate);
        } catch (const std::exception &err) {
            candidate.problem = err.what();
        }
        result.push_back(std::move(candidate));
    }
    propose_copy_names(bundle, machine, target, result);
    return result;
}

std::vector<std::string> copy_profiles_to_physical_printer(PresetBundle &bundle, const ProfileScopeSaveSelection &target,
                                                           const std::vector<ProfileMoveRequest> &requests)
{
    const Preset &machine = target_machine(bundle, target);
    struct Copy {
        PresetCollection *collection;
        Preset            preset;
        std::string       name;
        bool              detach;
    };
    std::vector<Copy>                              copies;
    std::set<std::pair<Preset::Type, std::string>> names;
    for (const auto &request : requests) {
        if (!request.source || (request.source->type != Preset::TYPE_PRINT && request.source->type != Preset::TYPE_FILAMENT))
            throw std::runtime_error("Only process and filament profiles can be copied to a physical printer.");
        const Preset     &source     = *request.source;
        PresetCollection &collection = collection_of(bundle, source.type);
        if (!is_unscoped(source))
            throw std::runtime_error("\"" + source.name + "\" already belongs to a physical printer.");
        if (!is_valid_profile_name(request.copy_name) || collection.get_preset_name_by_alias(request.copy_name) != request.copy_name)
            throw std::runtime_error("\"" + request.copy_name + "\" cannot be used as a profile name.");
        if (!names.insert({source.type, request.copy_name}).second)
            throw std::runtime_error("Two copies would be named \"" + request.copy_name + "\".");

        Copy copy{&collection, source, request.copy_name, false};
        // The scope binds the copy to the printer; the list makes it visible on that printer's machine profile.
        copy.preset.config.set_key_value("compatible_printers", new ConfigOptionStrings{machine.name});
        ProfileScopeSaveSelection selection = target;
        selection.mode                      = ProfileScopeSaveMode::ThisPhysicalPrinter;
        if (request.nozzle_tool_index)
            selection.nozzle_tool_index = request.nozzle_tool_index;
        try {
            prepare_profile_scope_for_save(copy.preset, bundle, request.copy_name, selection, &source);
        } catch (const std::exception &err) {
            throw std::runtime_error(source.name + ": " + err.what());
        }
        // Inheriting from a user profile (or being a copy of a root user profile, which
        // save_current_preset() would make the parent) would let edits made for other
        // printers flow into this printer's copy.
        const Preset *parent = collection.get_preset_parent(source);
        copy.detach          = !(parent && parent->is_system);
        if (copy.detach)
            copy.preset.base_id.clear(); // root presets have no base_id
        copies.push_back(std::move(copy));
    }

    std::vector<std::string> saved;
    for (auto &copy : copies) {
        std::string reason;
        bool        ok = false;
        try {
            ok = copy.collection->save_preset_copy(copy.name, copy.detach, copy.preset);
        } catch (const std::exception &err) {
            reason = err.what();
        }
        if (!ok) {
            bundle.update_compatible(PresetSelectCompatibleType::Never);
            std::string message = "\"" + copy.name + "\" could not be saved" + (reason.empty() ? "." : ": " + reason);
            if (!saved.empty()) {
                message += " Already saved:";
                for (const auto &name : saved)
                    message += " \"" + name + "\"";
            }
            throw std::runtime_error(message);
        }
        saved.push_back(copy.name);
    }
    bundle.update_compatible(PresetSelectCompatibleType::Never);
    return saved;
}

std::vector<std::string> hidden_profiles(const PresetBundle &bundle, Preset::Type type)
{
    std::vector<std::string> result;
    const PresetCollection  &collection = collection_of(bundle, type);
    for (const auto &name : hidden_profile_names(read_hidden_profiles(hidden_profiles_file(bundle)), type))
        if (const Preset *preset = collection.find_preset(name, false, true); preset && preset->is_user())
            result.push_back(name);
    return result;
}

void set_profiles_hidden(PresetBundle &bundle, Preset::Type type, const std::vector<std::string> &names, bool hidden)
{
    namespace fs    = boost::filesystem;
    const auto file = hidden_profiles_file(bundle);
    if (file.empty() || !fs::is_directory(file.parent_path()))
        throw std::runtime_error("The user profile folder is unavailable.");
    PresetCollection &collection = collection_of(bundle, type);
    nlohmann::json    list       = read_hidden_profiles(file);
    auto              current    = hidden_profile_names(list, type);
    for (const auto &name : names) {
        const Preset *preset = collection.find_preset(name, false, true);
        if (!hidden)
            current.erase(name);
        else if (preset && preset->is_user()) // vendor profiles have their own visibility settings
            current.insert(name);
    }
    if (current.empty())
        list.erase(hidden_profiles_key(type));
    else
        list[hidden_profiles_key(type)] = std::vector<std::string>(current.begin(), current.end());

    boost::system::error_code error;
    if (list.empty()) {
        fs::remove(file, error);
    } else {
        const fs::path temporary = file.string() + ".tmp";
        {
            boost::nowide::ofstream stream(temporary.string(), std::ios::binary | std::ios::trunc);
            stream << list.dump(4) << '\n';
            if (!stream.good())
                error = boost::system::errc::make_error_code(boost::system::errc::io_error);
        }
        if (!error)
            fs::rename(temporary, file, error);
        if (error)
            fs::remove(temporary);
    }
    if (error)
        throw std::runtime_error("The hidden profile list could not be saved: " + error.message());

    if (!hidden)
        for (const auto &name : names)
            if (Preset *preset = collection.find_preset(name, false, true); preset && preset->is_user())
                preset->is_visible = true;
    apply_hidden_profiles(bundle);
}

void apply_hidden_profiles(PresetBundle &bundle)
{
    const auto file = hidden_profiles_file(bundle);
    boost::system::error_code error;
    if (file.empty() || !boost::filesystem::exists(file, error))
        return;
    const nlohmann::json list = read_hidden_profiles(file);
    for (const Preset::Type type : {Preset::TYPE_PRINT, Preset::TYPE_FILAMENT}) {
        PresetCollection &collection = collection_of(bundle, type);
        const std::string selected   = collection.get_selected_preset_name();
        for (const auto &name : hidden_profile_names(list, type))
            if (Preset *preset = collection.find_preset(name, false, true); preset && preset->is_user() && preset->name != selected)
                preset->is_visible = false;
    }
}
} // namespace Slic3r
