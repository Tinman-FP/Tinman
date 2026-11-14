#include "FibreSeek.hpp"

#include <chrono>
#include <thread>

#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/format.hpp"
#include "FibreSeekSendChecks.hpp"
#include "Http.hpp"

namespace Slic3r {

namespace {
wxString from_u8(const std::string &text) { return wxString::FromUTF8(text.c_str()); }
} // namespace

const char *FibreSeek::get_name() const { return "FibreSeek"; }

wxString FibreSeek::get_test_ok_msg() const { return _L("Connection to the FibreSeek Seeker 3 is working correctly."); }

wxString FibreSeek::get_test_failed_msg(wxString &msg) const
{
    return GUI::format_wxstr("%s: %s", _L("Could not connect to the FibreSeek Seeker 3"), msg);
}

bool FibreSeek::get(const std::string &path, std::string &body, unsigned &http_status, wxString &error) const
{
    bool ok     = false;
    http_status = 0;
    auto http   = Http::get(make_url(path));
    set_auth(http);
    http.timeout_connect(10)
        .timeout_max(60)
        .on_complete([&](std::string reply, unsigned status) {
            body        = std::move(reply);
            http_status = status;
            ok          = status == 200;
        })
        .on_error([&](std::string reply, std::string message, unsigned status) {
            body        = std::move(reply);
            http_status = status;
            error       = format_error(body, message, status);
        })
#ifdef WIN32
        .ssl_revoke_best_effort(m_ssl_revoke_best_effort)
#endif
        .perform_sync();
    return ok;
}

bool FibreSeek::check_idle(wxString &error) const
{
    std::string body, reason;
    unsigned    status = 0;
    if (!get("printer/objects/query?webhooks&print_stats&virtual_sdcard", body, status, error))
        return false;
    if (FibreSeekSend::printer_is_idle(body, reason))
        return true;
    error = from_u8(reason);
    return false;
}

bool FibreSeek::upload(PrintHostUpload upload_data, ProgressFn progress_fn, ErrorFn error_fn, InfoFn info_fn) const
{
    const char *name = get_name();
    const auto  fail = [&](const wxString &message) {
        BOOST_LOG_TRIVIAL(error) << name << ": send stopped: " << message.ToUTF8().data();
        error_fn(message);
        return false;
    };
    const bool start = upload_data.post_action == PrintHostPostUploadAction::StartPrint;
    if (!start && upload_data.post_action != PrintHostPostUploadAction::None)
        return fail(_L("The FibreSeek Seeker 3 can only upload a job, or upload and start it."));

    // 1. The local file.
    FibreSeekSend::JobInfo job;
    std::string            reason;
    if (!FibreSeekSend::read_job(upload_data.source_path, job, reason))
        return fail(from_u8(reason));
    if (!FibreSeekSend::is_seeker3_model(job.printer_model))
        return fail(job.printer_model.empty() ?
                        _L("This G-code has no Tinman printer settings, so it cannot be checked for the FibreSeek Seeker 3. Slice it again in Tinman.") :
                        GUI::format_wxstr(_L("This G-code was sliced for \"%1%\", not a FibreSeek Seeker 3. Slice it with a Seeker 3 printer profile."),
                                          from_u8(job.printer_model)));
    const std::string remote_name = upload_data.upload_path.filename().string();
    if (remote_name.empty() || remote_name == "." || remote_name == "..")
        return fail(_L("Enter a file name for the upload."));
    BOOST_LOG_TRIVIAL(info) << boost::format("%1%: sending %2% as %3% (%4% bytes, sha256 %5%, continuous fibre: %6%, start: %7%)") % name %
                                   upload_data.source_path % remote_name % job.size % job.sha256 %
                                   (job.continuous_fiber ? "yes" : "no") % (start ? "yes" : "no");

    // 2. Klipper is ready and idle.
    wxString error;
    if (!test(error))
        return fail(error);
    if (!check_idle(error))
        return fail(error);

    // 3. The fitted nozzles match the slice.
    std::string         body;
    unsigned            status = 0;
    std::vector<double> nozzles;
    if (!get("printer/objects/query?configfile=config", body, status, error))
        return fail(error);
    if (!FibreSeekSend::printer_nozzle_diameters(body, nozzles, reason) ||
        !FibreSeekSend::nozzles_match(job.nozzle_diameters, nozzles, reason))
        return fail(from_u8(reason));

    // 4. Never overwrite a file on the printer: Moonraker replaces files of the same name.
    const std::string encoded_name = Http::url_encode(remote_name);
    if (get("server/files/metadata?filename=" + encoded_name, body, status, error))
        return fail(GUI::format_wxstr(_L("A file named \"%1%\" is already on the printer. Rename the upload so it does not replace that file."),
                                      from_u8(remote_name)));
    if (status != 404)
        return fail(error);

    // 5. Upload without starting. Moonraker rejects the upload (HTTP 422) if the
    //    received bytes do not match the checksum.
    std::string remote_path;
    bool        uploaded = false;
    {
        auto http = Http::post(make_url("server/files/upload"));
        set_auth(http);
        http.form_add("root", "gcodes")
            .form_add("print", "false")
            .form_add("checksum", job.sha256)
            .form_add_file("file", upload_data.source_path.string(), remote_name)
            .on_complete([&](std::string reply, unsigned) {
                BOOST_LOG_TRIVIAL(debug) << name << ": upload reply: " << reply;
                uploaded = FibreSeekSend::upload_reply_ok(reply, job.size, remote_path, reason);
                if (!uploaded)
                    error = from_u8(reason);
            })
            .on_error([&](std::string reply, std::string message, unsigned http_status) {
                error = format_error(reply, message, http_status);
            })
            .on_progress([&](Http::Progress progress, bool &cancel) {
                progress_fn(std::move(progress), cancel);
                if (cancel)
                    error = _L("The upload was cancelled.");
            })
#ifdef WIN32
            .ssl_revoke_best_effort(m_ssl_revoke_best_effort)
#endif
            .perform_sync();
    }
    if (!uploaded)
        return fail(error);

    // 6. The stored copy is exactly the file Tinman wrote.
    {
        bool same = false;
        error.clear();
        auto http = Http::get(make_url("server/files/gcodes/" + Http::url_encode(remote_path)));
        set_auth(http);
        http.size_limit(size_t(job.size) + 1024)
            .on_complete([&](std::string reply, unsigned) {
                same = reply.size() == job.size && FibreSeekSend::sha256_hex(reply) == job.sha256;
            })
            .on_error([&](std::string reply, std::string message, unsigned http_status) {
                error = format_error(reply, message, http_status);
            })
#ifdef WIN32
            .ssl_revoke_best_effort(m_ssl_revoke_best_effort)
#endif
            .perform_sync();
        if (!same)
            return fail(GUI::format_wxstr(_L("The copy of \"%1%\" on the printer does not match the file Tinman sent, so it was not started. "
                                             "Delete it on the printer and send again."),
                                          from_u8(remote_path)) +
                        (error.empty() ? wxString() : "\n" + error));
    }
    BOOST_LOG_TRIVIAL(info) << name << ": stored and verified " << remote_path;
    if (!start)
        return true;

    // 7. Start once. A failed or unclear start is never retried: the user
    //    checks the printer before trying again.
    if (!check_idle(error))
        return fail(GUI::format_wxstr(_L("\"%1%\" is on the printer but was not started: %2%"), from_u8(remote_path), error));
    if (!start_print(error, remote_path))
        return fail(GUI::format_wxstr(_L("\"%1%\" is on the printer, but the start request failed. Check the printer before trying again: %2%"),
                                      from_u8(remote_path), error));
    for (int second = 0; second < m_start_watch_seconds; ++second) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        wxString query_error;
        if (!get("printer/objects/query?print_stats", body, status, query_error))
            continue;
        std::string detail;
        switch (FibreSeekSend::start_state(body, remote_path, detail)) {
        case FibreSeekSend::StartState::Started:
            BOOST_LOG_TRIVIAL(info) << name << ": printer took " << remote_path << " (" << detail << ")";
            info_fn(L"complete", from_u8(detail));
            return true;
        case FibreSeekSend::StartState::Failed:
            return fail(GUI::format_wxstr(_L("The printer stopped \"%1%\" right after the start (%2%). Check the printer."),
                                          from_u8(remote_path), from_u8(detail)));
        case FibreSeekSend::StartState::Waiting: break;
        }
    }
    // The start request was accepted; the printer may still be preparing.
    BOOST_LOG_TRIVIAL(warning) << name << ": start accepted, but the printer has not reported " << remote_path << " yet";
    return true;
}

} // namespace Slic3r
