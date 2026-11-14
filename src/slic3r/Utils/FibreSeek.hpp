#ifndef slic3r_FibreSeek_hpp_
#define slic3r_FibreSeek_hpp_

#include "Moonraker.hpp"

namespace Slic3r {

// Print host for the FibreSeek Seeker 3 continuous-fibre printer.
//
// The Seeker 3 runs Klipper behind Moonraker, so this reuses the Moonraker
// host for the connection, authentication and the start request, and adds
// the checks a continuous-fibre job needs before it reaches the printer
// (see FibreSeekSendChecks.hpp):
//
//   1. the G-code is plain text and was sliced with a Seeker 3 profile;
//   2. Klipper is ready and idle (GET /printer/objects/query?...);
//   3. the nozzles fitted to the printer match the slice
//      (GET /printer/objects/query?configfile=config);
//   4. no file of the same name is on the printer (GET /server/files/metadata);
//   5. upload with print=false and Moonraker's SHA-256 checksum field
//      (POST /server/files/upload), then confirm nothing started;
//   6. download the stored copy and compare it byte for byte
//      (GET /server/files/gcodes/<name>);
//   7. for "Upload and print", check idle again, send one start request
//      (POST /printer/print/start, never retried) and watch print_stats until
//      the printer takes the file.
//
// Any failed check stops the send before the next step. Only standard
// Moonraker endpoints are used.
class FibreSeek : public Moonraker
{
public:
    explicit FibreSeek(DynamicPrintConfig *config) : Moonraker(config) {}
    ~FibreSeek() override = default;

    const char *get_name() const override;
    wxString    get_test_ok_msg() const override;
    wxString    get_test_failed_msg(wxString &msg) const override;
    bool upload(PrintHostUpload upload_data, ProgressFn progress_fn, ErrorFn error_fn, InfoFn info_fn) const override;
    // The Seeker 3 only prints from its G-code root.
    bool get_storage(wxArrayString &, wxArrayString &) const override { return false; }

    // How long upload() watches for the printer to take a started job.
    // Tests shorten it; the printer itself is not affected.
    void set_start_watch_seconds(int seconds) { m_start_watch_seconds = seconds; }

private:
    int m_start_watch_seconds = 20;

    // GET a Moonraker path. Returns false on a transport error or an HTTP
    // status other than 200, with the status in http_status (0 if none).
    bool get(const std::string &path, std::string &body, unsigned &http_status, wxString &error) const;
    bool check_idle(wxString &error) const;
};

} // namespace Slic3r

#endif
