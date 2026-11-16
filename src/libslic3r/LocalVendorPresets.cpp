#include "LocalVendorPresets.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/filesystem/directory.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/system/error_code.hpp>
#include <nlohmann/json.hpp>

#include "Preset.hpp"
#include "PresetCacheFormat.hpp"
#include "Semver.hpp"
#include "TinManMachineProfileContract.hpp"
#include "Utils.hpp"

namespace Slic3r {

namespace fs = boost::filesystem;
using nlohmann::json;

namespace {

// The vendor profile's preset lists. Printer models stay with the stock bundle.
const char* const PROCESS_LIST  = "process_list";
const char* const FILAMENT_LIST = "filament_list";
const char* const MACHINE_LIST  = "machine_list";
const char* const PRESET_LISTS[] = { PROCESS_LIST, FILAMENT_LIST, MACHINE_LIST };

// The preset names of a vendor bundle, by list.
using PresetNames = std::map<std::string, std::set<std::string>>;

// A JSON file's content, or null when it cannot be read or parsed.
json read_json(const fs::path& path)
{
    boost::nowide::ifstream ifs(path.string());
    if (! ifs)
        return json();
    json content = json::parse(ifs, nullptr, false);
    return content.is_discarded() ? json() : content;
}

// The names a preset file states under `key`, as one string or an array of them.
std::vector<std::string> names_in(const json& preset, const char* key)
{
    std::vector<std::string> names;
    if (! preset.is_object())
        return names;
    const auto it = preset.find(key);
    if (it == preset.end())
        return names;
    auto add = [&names](const json& value) {
        if (value.is_string() && ! value.get_ref<const std::string&>().empty())
            names.push_back(value.get<std::string>());
    };
    if (it->is_array())
        std::for_each(it->begin(), it->end(), add);
    else
        add(*it);
    return names;
}

// The {name, sub_path} entries of one list of a vendor profile, skipping malformed ones.
std::vector<std::pair<std::string, std::string>> listed(const json& profile, const char* list)
{
    std::vector<std::pair<std::string, std::string>> entries;
    const auto it = profile.is_object() ? profile.find(list) : profile.end();
    if (it == profile.end() || ! it->is_array())
        return entries;
    for (const json& item : *it) {
        if (! item.is_object())
            continue;
        const auto name = item.find("name"), sub_path = item.find("sub_path");
        if (name != item.end() && name->is_string() && sub_path != item.end() && sub_path->is_string())
            entries.emplace_back(name->get<std::string>(), sub_path->get<std::string>());
    }
    return entries;
}

// The presets `incoming` ships, and with `renames`, every name one of them was renamed
// from: a user preset that names an old one follows the rename, so it is not missing.
// Nothing when `incoming` cannot be read.
std::optional<PresetNames> incoming_names(const std::string& vendor, const fs::path& incoming, bool renames)
{
    PresetNames names;
    if (incoming.extension() == ".opc") {
        VendorCacheData data;
        if (! VendorCacheFile::load(incoming.string(), vendor, Semver::inf(), data))
            return std::nullopt;
        auto add = [&](const char* list, const std::vector<CachedPreset>& entries) {
            for (const CachedPreset& entry : entries) {
                names[list].insert(entry.name);
                if (renames)
                    names[list].insert(entry.renamed_from.begin(), entry.renamed_from.end());
            }
        };
        add(PROCESS_LIST, data.process_entries);
        add(FILAMENT_LIST, data.filament_entries);
        add(MACHINE_LIST, data.machine_entries);
        return names;
    }
    const json profile = read_json(incoming);
    if (! profile.is_object())
        return std::nullopt;
    const fs::path dir = incoming.parent_path() / vendor;
    for (const char* list : PRESET_LISTS)
        for (const auto& [name, sub_path] : listed(profile, list)) {
            names[list].insert(name);
            if (renames && is_path_within_root(sub_path, dir))
                for (std::string& old_name : names_in(read_json(dir / sub_path), "renamed_from"))
                    names[list].insert(std::move(old_name));
        }
    return names;
}

// One preset the installed profile lists that the incoming bundle does not.
struct Missing
{
    const char* list;
    std::string name;
    std::string sub_path;
    json        preset;
    bool        kept { false };
};

} // namespace

fs::path local_vendor_presets_dir()
{
    return fs::path(data_dir()) / "system_local";
}

const std::set<std::string>& LocalVendorPresetKeeper::user_references()
{
    if (m_user_references)
        return *m_user_references;
    m_user_references.emplace();
    const fs::path user = fs::path(data_dir()) / PRESET_USER_DIR;
    boost::system::error_code ec;
    if (! fs::is_directory(user, ec))
        return *m_user_references;
    // Every user folder, its base/ roots and the presets of its physical printers alike.
    for (fs::recursive_directory_iterator it(user, ec), end; ! ec && it != end; it.increment(ec)) {
        if (! is_json_file(it->path().string()) || ! fs::is_regular_file(it->status()))
            continue;
        const json preset = read_json(it->path());
        for (const char* key : { "inherits", "compatible_printers", "compatible_prints" })
            for (std::string& name : names_in(preset, key))
                m_user_references->insert(std::move(name));
    }
    if (ec)
        BOOST_LOG_TRIVIAL(warning) << "Reading the user presets in " << user.string() << " stopped early: " << ec.message();
    return *m_user_references;
}

bool LocalVendorPresetKeeper::keep(const std::string& vendor, const fs::path& incoming)
{
    try {
        const fs::path installed_dir = fs::path(data_dir()) / PRESET_SYSTEM_DIR;
        const fs::path installed_profile = installed_dir / (vendor + ".json");
        const fs::path presets_dir = installed_dir / vendor;
        // A vendor installed as its cache alone lists nothing beside its own presets, and
        // a profile that cannot be read lists nothing that could be kept.
        if (! fs::exists(installed_profile))
            return true;
        const json installed = read_json(installed_profile);
        if (! installed.is_object())
            return true;

        // What the user still needs: presets their own build on, and the printers the
        // Tinman machine contract shows in place of the stock ones of a curated model.
        auto needed = [this](const char* list, const std::string& name) {
            return user_references().count(name) != 0 || (list == MACHINE_LIST && tinmanx_managed_machine_preset(name));
        };
        // Every preset installed, until the ones the incoming bundle ships are dropped.
        std::vector<Missing> missing;
        for (const char* list : PRESET_LISTS)
            for (auto& [name, sub_path] : listed(installed, list))
                missing.push_back({ list, std::move(name), std::move(sub_path) });
        // Most vendors list nothing the user needs, and are replaced without the incoming
        // bundle being read.
        if (std::none_of(missing.begin(), missing.end(), [&](const Missing& preset) { return needed(preset.list, preset.name); }))
            return true;

        // A cache carries the names its presets were renamed from. A profile's presets are
        // read for them only if, without them, something would be kept.
        const bool from_cache = incoming.extension() == ".opc";
        std::optional<PresetNames> names = incoming_names(vendor, incoming, from_cache);
        if (! names)
            BOOST_LOG_TRIVIAL(warning) << "Cannot read the presets of " << incoming.string()
                                       << "; keeping every preset of " << vendor << " that is still needed";
        auto drop_shipped = [&]() {
            missing.erase(std::remove_if(missing.begin(), missing.end(), [&](const Missing& preset) {
                              const auto it = names->find(preset.list);
                              return it != names->end() && it->second.count(preset.name) != 0;
                          }), missing.end());
        };
        if (names)
            drop_shipped();
        if (std::none_of(missing.begin(), missing.end(), [&](const Missing& preset) { return needed(preset.list, preset.name); }))
            return true;
        if (names && ! from_cache) {
            names = incoming_names(vendor, incoming, true);
            if (names)
                drop_shipped();
        }
        for (Missing& preset : missing)
            if (is_path_within_root(preset.sub_path, presets_dir))
                preset.preset = read_json(presets_dir / preset.sub_path);

        auto find = [&missing](const char* list, const std::string& name) -> Missing* {
            const auto it = std::find_if(missing.begin(), missing.end(),
                                         [&](const Missing& preset) { return preset.list == list && preset.name == name; });
            return it == missing.end() ? nullptr : &*it;
        };
        std::vector<Missing*> todo;
        auto keep_preset = [&todo](Missing* preset) {
            if (preset != nullptr && ! preset->kept && ! preset->preset.is_null()) {
                preset->kept = true;
                todo.push_back(preset);
            }
        };
        for (Missing& preset : missing)
            if (needed(preset.list, preset.name))
                keep_preset(&preset);
        while (! todo.empty()) {
            const Missing& preset = *todo.back();
            todo.pop_back();
            for (const char* key : { "inherits", "includes" })
                for (const std::string& name : names_in(preset.preset, key))
                    keep_preset(find(preset.list, name));
            for (const std::string& name : names_in(preset.preset, "compatible_printers"))
                keep_preset(find(MACHINE_LIST, name));
            for (const std::string& name : names_in(preset.preset, "compatible_prints"))
                keep_preset(find(PROCESS_LIST, name));
            if (preset.list == MACHINE_LIST)
                for (Missing& other : missing) {
                    const std::vector<std::string> printers = names_in(other.preset, "compatible_printers");
                    if (std::find(printers.begin(), printers.end(), preset.name) != printers.end())
                        keep_preset(&other);
                }
        }

        // Into the local presets, in the installed profile's order, which lists every
        // preset after the ones it builds on.
        const fs::path local_dir = local_vendor_presets_dir();
        const fs::path local_profile = local_dir / (vendor + ".json");
        json local = json::object();
        if (fs::exists(local_profile)) {
            local = read_json(local_profile);
            if (! local.is_object()) {
                BOOST_LOG_TRIVIAL(error) << "Cannot read " << local_profile.string() << "; " << vendor << " is left as installed";
                return false;
            }
        }
        local["name"] = vendor;
        size_t kept = 0;
        for (const Missing& preset : missing) {
            if (! preset.kept)
                continue;
            json& list = local[preset.list];
            if (! list.is_array())
                list = json::array();
            // A preset kept before under this name is replaced. The sub-path it keeps is
            // its own, unless another local preset already has it.
            json* entry = nullptr;
            std::set<std::string> taken;
            for (json& item : list) {
                if (! item.is_object())
                    continue;
                if (item.value("name", std::string()) == preset.name)
                    entry = &item;
                else
                    taken.insert(boost::algorithm::to_lower_copy(item.value("sub_path", std::string())));
            }
            std::string sub_path = preset.sub_path;
            const fs::path original(preset.sub_path);
            for (int n = 2; taken.count(boost::algorithm::to_lower_copy(sub_path)) != 0; ++n)
                sub_path = (original.parent_path() / (original.stem().string() + " (" + std::to_string(n) + ")" +
                                                      original.extension().string())).generic_string();
            const fs::path target = local_dir / vendor / sub_path;
            fs::create_directories(target.parent_path());
            std::string error;
            if (copy_file((presets_dir / preset.sub_path).string(), target.string(), error, true) != SUCCESS) {
                BOOST_LOG_TRIVIAL(error) << "Cannot keep " << preset.name << " of " << vendor << ": " << error
                                         << "; " << vendor << " is left as installed";
                return false;
            }
            if (entry != nullptr)
                (*entry)["sub_path"] = sub_path;
            else
                list.push_back({ { "name", preset.name }, { "sub_path", sub_path } });
            ++kept;
        }
        if (kept == 0)
            return true;
        // Written whole or not at all, so an interrupted write cannot lose what was kept before.
        const fs::path temp = local_profile.string() + ".tmp";
        save_string_file(temp, local.dump(4));
        fs::rename(temp, local_profile);
        BOOST_LOG_TRIVIAL(info) << "Kept " << kept << " presets of " << vendor << " that " << incoming.string()
                                << " does not ship in " << local_profile.string();
        return true;
    } catch (const std::exception& err) {
        BOOST_LOG_TRIVIAL(error) << "Cannot keep the presets of " << vendor << " that are still needed: " << err.what()
                                 << "; " << vendor << " is left as installed";
        return false;
    }
}

} // namespace Slic3r
