#include <catch2/catch_test_macros.hpp>
#include <string>
#include <wx/string.h>

#include "libslic3r/Preset.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "slic3r/GUI/SavePresetDialog.hpp"

using namespace Slic3r;
using namespace Slic3r::GUI;

namespace {

// A standalone printer collection, built the way PresetBundle builds its own.
struct PrinterTestCollection : public PrinterPresetCollection
{
    PrinterTestCollection()
        : PrinterPresetCollection(Preset::TYPE_PRINTER, Preset::printer_options(),
                                  static_print_config_ref(static_cast<const PrintRegionConfig &>(FullPrintConfig::defaults())))
    {}
};

// Loads one preset; system presets get the flag a vendor bundle would give them.
void add_preset(PresetCollection &printers, const std::string &name, bool is_system)
{
    printers.load_preset(std::string(), name, printers.default_preset().config, /*select=*/false);
    for (auto it = printers.begin(); it != printers.end(); ++it)
        if (it->name == name) {
            it->is_system  = is_system;
            it->is_default = false;
        }
}

} // namespace

TEST_CASE("A user profile's existing name passes the name check even with characters new names can't use",
          "[PresetName]")
{
    PrinterTestCollection printers;
    add_preset(printers, "Qidi Plus4 0.4mm - Codex | PET-CF r1", /*is_system=*/false);
    add_preset(printers, "Vendor | Printer 0.4", /*is_system=*/true);
    wxString info;

    // Saving over the user's own profile (e.g. setting its connection) keeps its name.
    CHECK(check_preset_name(printers, "Qidi Plus4 0.4mm - Codex | PET-CF r1", info) != PresetNameStatus::Invalid);

    // A new name still can't use them.
    CHECK(check_preset_name(printers, "Qidi Plus4 0.4mm - Codex | PET-CF r2", info) == PresetNameStatus::Invalid);
    CHECK(check_preset_name(printers, "New: printer", info) == PresetNameStatus::Invalid);

    // A system profile's name is no exception.
    CHECK(check_preset_name(printers, "Vendor | Printer 0.4", info) == PresetNameStatus::Invalid);

    // Ordinary names are unaffected.
    CHECK(check_preset_name(printers, "Qidi Plus4 0.4mm - Codex PET-CF r2", info) == PresetNameStatus::Valid);
}
