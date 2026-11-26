#include "PrinterWebViewCamera.hpp"

#include "PrinterWebViewAuth.hpp"

#include <boost/algorithm/string.hpp>

namespace Slic3r {
namespace GUI {

std::string normalize_prusa_camera_url(std::string url)
{
    boost::algorithm::trim(url);
    if (url.empty())
        return {};
    if (!boost::algorithm::istarts_with(url, "rtsp://") &&
        !boost::algorithm::istarts_with(url, "rtsps://"))
        url = "rtsp://" + url;

    const size_t authority = url.find("://") + 3;
    if (url.find('/', authority) == std::string::npos)
        url += "/live";
    return url;
}

PrusaCameraDiscovery choose_discovered_prusa_camera(const std::vector<std::string>&          rtsp_hosts,
                                                    const std::map<std::string, std::string>& saved_cameras,
                                                    const std::string&                        printer_host)
{
    PrusaCameraDiscovery discovery;
    std::string          choice;
    for (const std::string& host : rtsp_hosts) {
        bool this_printer  = false;
        bool other_printer = false;
        for (const auto& [saved_printer, camera_url] : saved_cameras) {
            if (!camera_url.empty() && boost::algorithm::iequals(printer_url_authority(camera_url).host, host))
                (saved_printer == printer_host ? this_printer : other_printer) = true;
        }
        if (other_printer && !this_printer) {
            ++discovery.other_printers;
        } else {
            ++discovery.available;
            choice = host;
        }
    }
    if (discovery.available == 1)
        discovery.url = normalize_prusa_camera_url(choice);
    return discovery;
}

} // namespace GUI
} // namespace Slic3r
