#pragma once

// Tinman: move existing process and filament profiles onto one physical printer.
//
// Before per-printer scope (ProfileScopeSave.hpp), a profile tuned on one printer was tied
// to it only by its compatible_printers list, so every printer of that model shared it.
// The functions below list such profiles, or the profiles in a folder of JSON files, and
// save scoped copies of the ones the user ticks. The originals are never rewritten; they
// can only be hidden from the selectors, which is recorded outside the profiles.

#include "Preset.hpp"
#include "ProfileScopeSave.hpp"

#include <optional>
#include <string>
#include <vector>

namespace Slic3r {
class PresetBundle;

struct ProfileMoveCandidate {
    // The profile as it would be loaded: a library preset, or one read from a file and
    // resolved against its installed parent.
    Preset                   source{Preset::TYPE_INVALID, ""};
    std::string              source_file;   // imports only
    std::vector<std::string> made_for;      // existing machine profiles in its compatible_printers
    std::string              copy_name;     // "<name> - <printer>", unique in the library
    std::string              existing_copy; // "<name> - <printer>" already saved for this printer
    std::string              problem;       // why it cannot be copied (imports only); empty when it can
};

// The user's own unscoped process and filament profiles that today can be used only on the
// target's printer model at its nozzle diameter(s): their compatible_printers is not empty,
// every machine profile it names that exists is of the target's printer model and not
// another physical printer's own, and at least one of them has the target's nozzle
// diameters. That covers profiles naming the machine profile the printer was created from,
// its other nozzle variants, and its system parent (the parent-printer rule). Hidden
// profiles are not listed. Profiles listing other models or only conditions are shared by
// design and are left alone.
std::vector<ProfileMoveCandidate> profiles_to_move(const PresetBundle &bundle, const ProfileScopeSaveSelection &target);

// The process and filament profiles in the *.json files under folder (recursively), each
// read and resolved the way importing it would. A file that cannot be copied has a problem:
// unreadable, a machine profile, a parent that is not installed, or already scoped.
std::vector<ProfileMoveCandidate> profiles_to_import(const PresetBundle &bundle, const ProfileScopeSaveSelection &target,
                                                     const std::string &folder);

struct ProfileMoveRequest {
    const Preset         *source = nullptr; // ProfileMoveCandidate::source
    std::string           copy_name;
    std::optional<size_t> nozzle_tool_index; // a filament on a multi-nozzle printer: the nozzle it was tuned on
};

// Save a copy of each source scoped to the target printer (and, for a filament, its nozzle)
// through the scope engine: prepare_profile_scope_for_save() and then the collection's save.
// Each copy lists only the target's machine profile in compatible_printers. It keeps
// inheriting from a system parent; any other copy is saved detached, with the full
// settings, so that later edits of a shared user profile cannot change it. Every request
// is checked before anything is written: an invalid one throws and nothing is saved.
// Selections and unsaved edits are kept. Returns the saved names. A failure part-way throws
// a message that names the copies already saved.
std::vector<std::string> copy_profiles_to_physical_printer(PresetBundle &bundle, const ProfileScopeSaveSelection &target,
                                                           const std::vector<ProfileMoveRequest> &requests);

// "<name> - <printer>" with the characters the save dialog refuses replaced
// ("[" and "]" by "(" and ")", the others by "-").
std::string profile_copy_name(const std::string &name, const std::string &printer);

// User process and filament profiles hidden from the selectors, for example originals
// whose copies now belong to a physical printer. The list lives in
// <user folder>/tinman_hidden_profiles.json; the profiles themselves are not changed.
std::vector<std::string> hidden_profiles(const PresetBundle &bundle, Preset::Type type);
void                     set_profiles_hidden(PresetBundle &bundle, Preset::Type type, const std::vector<std::string> &names, bool hidden);
// Re-applied whenever the user presets are loaded. Without the file, nothing changes.
void                     apply_hidden_profiles(PresetBundle &bundle);
} // namespace Slic3r
