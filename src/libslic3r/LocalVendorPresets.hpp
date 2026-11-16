#ifndef slic3r_LocalVendorPresets_hpp_
#define slic3r_LocalVendorPresets_hpp_

#include <optional>
#include <set>
#include <string>

#include <boost/filesystem/path.hpp>

namespace Slic3r {

// Tinman: a vendor's local presets, the ones its installed profile listed that a newer
// stock bundle of the vendor no longer ships and that the user still needs: presets the
// user added to the vendor (the TinMan Codex printers and processes) and stock presets
// upstream has since dropped. Installing a vendor replaces everything under
// system/<vendor>, so they are kept apart, in an index in the vendor profile's format,
// local_vendor_presets_dir()/<vendor>.json, and the files it lists under
// local_vendor_presets_dir()/<vendor>/. Installs and updates never touch it, and the
// presets it lists load with the vendor, after the vendor's own.
boost::filesystem::path local_vendor_presets_dir();

// Moves a vendor's still-needed presets into its local presets before the vendor is
// replaced. One keeper serves one batch of installs, reading the user's presets once.
class LocalVendorPresetKeeper
{
public:
    // Before the installed `vendor` is replaced by `incoming` (a vendor profile, or a
    // vendor cache when it ends in .opc), move into the vendor's local presets every
    // preset its installed profile lists that `incoming` neither lists nor was renamed
    // to, and that is still needed:
    //  - a preset a user preset inherits or names as compatible,
    //  - a printer that Tinman's machine contract manages,
    //  - and for each of those, the presets it inherits or includes and the printers it
    //    is made for, and for a printer, the presets made for it, where `incoming` lacks
    //    them too.
    // A vendor installed as its cache alone, or not at all, has nothing to move, and a
    // vendor with nothing to move is replaced exactly as before. False when the presets
    // could not all be kept; the caller must then leave the installed vendor in place.
    bool keep(const std::string& vendor, const boost::filesystem::path& incoming);

private:
    const std::set<std::string>& user_references();

    // The names user presets inherit or name as compatible, read on first use.
    std::optional<std::set<std::string>> m_user_references;
};

} // namespace Slic3r

#endif // slic3r_LocalVendorPresets_hpp_
