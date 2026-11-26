#pragma once

// Tinman: the camera the Device tab's camera strip shows for a Prusa printer. The app config's
// "prusa_camera" section saves one RTSP address per printer host. A blank address means the
// printer has no camera, so discovery doesn't run for it again.

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace Slic3r {
namespace GUI {

// The RTSP address for what a user typed or discovery found: "rtsp://" is added when there is no
// scheme, and "/live" when there is no path. A blank address stays blank.
std::string normalize_prusa_camera_url(std::string url);

// Discovery's choice for one printer.
struct PrusaCameraDiscovery
{
    // The camera to save for the printer, or empty when there isn't exactly one to save.
    std::string url;
    // Hosts that answered on the RTSP port and aren't another printer's camera.
    std::size_t available{0};
    // Hosts that answered but are saved as another printer's camera, and not as this one's.
    std::size_t other_printers{0};
};

// Chooses among the hosts that answered on the RTSP port. A host saved as another printer's camera
// is skipped, so a printer without a camera never takes another printer's. One saved for this
// printer too is kept, since it may be this printer's own. The single host left, if there is one,
// is the choice.
PrusaCameraDiscovery choose_discovered_prusa_camera(const std::vector<std::string>&          rtsp_hosts,
                                                    const std::map<std::string, std::string>& saved_cameras,
                                                    const std::string&                        printer_host);

} // namespace GUI
} // namespace Slic3r
