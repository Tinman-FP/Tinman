#ifndef slic3r_FibreSeek_hpp_
#define slic3r_FibreSeek_hpp_

#include "Moonraker.hpp"

namespace Slic3r {

// Print host for the FibreSeek Seeker 3 continuous-fibre printer.
//
// The Seeker 3 runs Klipper behind Moonraker, so this reuses the Moonraker
// host's address and API-key handling and adds the checks a continuous-fibre
// job needs before it reaches the printer (see FibreSeekSendChecks.hpp):
//
//   1. the G-code is plain text sliced with a Seeker 3 profile;
//   2. Klipper is ready and idle with no file loaded
//      (GET /printer/objects/query?webhooks&print_stats&virtual_sdcard);
//   3. T0 and T1 carry the nozzles the job was sliced for
//      (GET /printer/objects/query?configfile=config);
//   4. the job goes up under a name unique to this send
//      (POST /server/files/upload with print=false and Moonraker's SHA-256
//      checksum), and the printer must report exactly that name, unstarted;
//   5. the stored copy is downloaded and compared byte for byte
//      (GET /server/files/gcodes/<name>);
//   6. for "Upload and print": idle and the printer's host name
//      (GET /printer/info) are checked again, one start request is sent
//      (POST /printer/print/start, never retried) and print_stats is watched
//      until it reports "printing" for that file. If the printer does not
//      confirm, the result is "outcome unknown, check the printer".
//
// Every request has a bounded timeout, follows no redirects (an API key or a
// start must not move to another host) and verifies TLS for https addresses.
// The upload queue's Cancel is honoured between all phases until the start
// request is dispatched. Only standard Moonraker endpoints are used.
class FibreSeek : public Moonraker
{
public:
    // Request timeouts in seconds, and how long to watch for the start.
    struct Timeouts
    {
        long connect     = 10;
        long query       = 30;
        long upload      = 30 * 60;
        long download    = 10 * 60;
        long start       = 30;
        int  start_watch = 10 * 60; // preparing a fibre job (heating, levelling) takes minutes
        int  poll_ms     = 2000;
    };

    explicit FibreSeek(DynamicPrintConfig *config) : Moonraker(config) {}
    ~FibreSeek() override = default;

    const char *get_name() const override;
    bool        test(wxString &msg) const override;
    wxString    get_test_ok_msg() const override;
    wxString    get_test_failed_msg(wxString &msg) const override;
    bool upload(PrintHostUpload upload_data, ProgressFn progress_fn, ErrorFn error_fn, InfoFn info_fn) const override;
    // The Seeker 3 only prints from its G-code root.
    bool get_storage(wxArrayString &, wxArrayString &) const override { return false; }

    // Tests shorten these; the printer itself is not affected.
    void set_timeouts(const Timeouts &timeouts) { m_timeouts = timeouts; }

private:
    Timeouts m_timeouts;

    // Auth, CA file, TLS verification for https, no redirects, and timeouts.
    void prepare(Http &http, long timeout) const;
    // GET a Moonraker path. True only for HTTP 200; http_status is 0 when the
    // request got no HTTP answer. stop() is polled to cancel the request.
    bool get(const std::string &path, std::string &body, unsigned &http_status, wxString &error,
             const std::function<bool()> &stop) const;
};

} // namespace Slic3r

#endif
