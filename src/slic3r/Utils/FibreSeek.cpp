#include "FibreSeek.hpp"

#include <chrono>
#include <thread>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <nlohmann/json.hpp>

#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/format.hpp"
#include "FibreSeekSendChecks.hpp"
#include "Http.hpp"

namespace Slic3r {

namespace {

wxString from_u8(const std::string &text) { return wxString::FromUTF8(text.c_str()); }

// The upload queue reports Cancel (and app shutdown) only through the progress
// callback, so a send polls it between phases and during every request.
class CancelPoll
{
public:
    explicit CancelPoll(const PrintHost::ProgressFn &progress_fn) : m_progress_fn(progress_fn) {}

    // `uploaded` keeps the queue's progress bar at 100% after the upload.
    bool operator()(bool uploaded = false)
    {
        if (!m_cancelled) {
            static const std::string none;
            bool                     cancel = false;
            m_progress_fn(Http::Progress(0, 0, uploaded ? 1 : 0, uploaded ? 1 : 0, none), cancel);
            if (cancel) {
                m_cancelled = true;
                bool confirm = true; // a call with cancel set marks the job cancelled in the queue
                m_progress_fn(Http::Progress(0, 0, 0, 0, none), confirm);
            }
        }
        return m_cancelled;
    }
    bool cancelled() const { return m_cancelled; }
    void set_cancelled() { m_cancelled = true; }

private:
    const PrintHost::ProgressFn &m_progress_fn;
    bool                         m_cancelled = false;
};

} // namespace

const char *FibreSeek::get_name() const { return "FibreSeek"; }

wxString FibreSeek::get_test_ok_msg() const { return _L("Connection to the FibreSeek Seeker 3 is working correctly."); }

wxString FibreSeek::get_test_failed_msg(wxString &msg) const
{
    return GUI::format_wxstr("%s: %s", _L("Could not connect to the FibreSeek Seeker 3"), msg);
}

void FibreSeek::prepare(Http &http, long timeout) const
{
    set_auth(http); // API key and CA file
    // Libcurl's default here is to follow redirects (keeping POST) with TLS
    // checks off. Following could carry the API key or a start to another
    // host, so a redirect is an error; https addresses are verified.
    http.follow_redirects(false).timeout_connect(m_timeouts.connect).timeout_max(timeout);
    if (boost::istarts_with(m_host, "https://"))
        http.tls_verify(true);
#ifdef WIN32
    http.ssl_revoke_best_effort(m_ssl_revoke_best_effort);
#endif
}

bool FibreSeek::get(const std::string &path, std::string &body, unsigned &http_status, wxString &error,
                    const std::function<bool()> &stop) const
{
    bool answered = false, ok = false;
    http_status   = 0;
    error.clear();
    auto http = Http::get(make_url(path));
    prepare(http, m_timeouts.query);
    http.on_complete([&](std::string reply, unsigned status) {
            answered = true;
            body        = std::move(reply);
            http_status = status;
            ok          = status == 200;
        })
        .on_error([&](std::string reply, std::string message, unsigned status) {
            answered = true;
            body        = std::move(reply);
            http_status = status;
            error       = format_error(body, message, status);
        })
        .on_progress([&](Http::Progress, bool &cancel) {
            if (!cancel && stop)
                cancel = stop();
        })
        .perform_sync();
    if (!answered && error.empty())
        error = _L("The printer sent an unexpected reply, such as a redirect, which Tinman does not follow. Check the printer's address.");
    if (answered && !ok && error.empty())
        error = GUI::format_wxstr(_L("The printer answered with HTTP %1%."), http_status);
    return ok;
}

bool FibreSeek::test(wxString &msg) const
{
    std::string body;
    unsigned    status = 0;
    if (!get("server/info", body, status, msg, nullptr))
        return false;
    try {
        const auto reply = nlohmann::json::parse(body);
        if (reply.contains("result") && reply["result"].is_object())
            return true;
    } catch (const std::exception &) {}
    msg = _L("The address did not answer like Moonraker.");
    return false;
}

bool FibreSeek::upload(PrintHostUpload upload_data, ProgressFn progress_fn, ErrorFn error_fn, InfoFn info_fn) const
{
    const char *name = get_name();
    CancelPoll  cancelled(progress_fn);
    const auto  stop = [&cancelled] { return cancelled(); };
    const auto  fail = [&](const wxString &message) {
        BOOST_LOG_TRIVIAL(error) << name << ": send stopped: " << message.ToUTF8().data();
        error_fn(message);
        return false;
    };
    const auto cancel_stop = [&](const char *phase) {
        BOOST_LOG_TRIVIAL(info) << name << ": send cancelled " << phase;
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
    const std::string remote_name = FibreSeekSend::remote_file_name(
        upload_data.upload_path.filename().string(), job.sha256,
        boost::posix_time::to_iso_string(boost::posix_time::second_clock::universal_time()));
    BOOST_LOG_TRIVIAL(info) << boost::format("%1%: sending %2% as %3% (%4% bytes, sha256 %5%, continuous fibre: %6%, start: %7%)") % name %
                                   upload_data.source_path % remote_name % job.size % job.sha256 %
                                   (job.continuous_fiber ? "yes" : "no") % (start ? "yes" : "no");

    // 2. The printer answers, is ready and idle. Its host name is kept to
    //    check that the start goes to the same printer.
    wxString    error;
    std::string body;
    unsigned    status = 0;
    if (cancelled())
        return cancel_stop("before contacting the printer");
    if (!test(error))
        return fail(error);
    const std::string identity = get("printer/info", body, status, error, stop) ? FibreSeekSend::printer_identity(body) : std::string();
    BOOST_LOG_TRIVIAL(info) << name << ": printer host name: " << (identity.empty() ? "(not reported)" : identity);
    const auto check_idle = [&]() {
        if (!get("printer/objects/query?webhooks&print_stats&virtual_sdcard", body, status, error, stop))
            return false;
        if (FibreSeekSend::printer_is_idle(body, reason))
            return true;
        error = from_u8(reason);
        return false;
    };
    if (!check_idle())
        return cancelled.cancelled() ? cancel_stop("while checking the printer") : fail(error);

    // 3. T0 and T1 carry the nozzles the job was sliced for.
    double t0 = 0., t1 = 0.;
    if (!get("printer/objects/query?configfile=config", body, status, error, stop))
        return cancelled.cancelled() ? cancel_stop("while checking the nozzles") : fail(error);
    if (!FibreSeekSend::printer_tool_nozzles(body, t0, t1, reason) || !FibreSeekSend::tools_match(job, t0, t1, reason))
        return fail(from_u8(reason));

    // 4. The unique name must be free: Moonraker would replace a file of that name.
    if (get("server/files/metadata?filename=" + Http::url_encode(remote_name), body, status, error, stop))
        return fail(GUI::format_wxstr(_L("A file named \"%1%\" is already on the printer."), from_u8(remote_name)));
    if (cancelled.cancelled())
        return cancel_stop("before the upload");
    if (status != 404)
        return fail(error);

    // 5. Upload without starting. Moonraker rejects the upload (HTTP 422) if the
    //    received bytes do not match the checksum.
    if (cancelled())
        return cancel_stop("before the upload");
    bool uploaded = false, answered = false;
    error.clear();
    {
        auto http = Http::post(make_url("server/files/upload"));
        prepare(http, m_timeouts.upload);
        http.form_add("root", "gcodes")
            .form_add("print", "false")
            .form_add("checksum", job.sha256)
            .form_add_file("file", upload_data.source_path.string(), remote_name)
            .on_complete([&](std::string reply, unsigned) {
                answered = true;
                BOOST_LOG_TRIVIAL(debug) << name << ": upload reply: " << reply;
                uploaded = FibreSeekSend::upload_reply_ok(reply, job.size, remote_name, reason);
                if (!uploaded)
                    error = from_u8(reason);
            })
            .on_error([&](std::string reply, std::string message, unsigned http_status) {
                answered = true;
                error    = format_error(reply, message, http_status);
            })
            .on_progress([&](Http::Progress progress, bool &cancel) {
                progress_fn(std::move(progress), cancel);
                if (cancel)
                    cancelled.set_cancelled();
            })
            .perform_sync();
    }
    if (cancelled.cancelled())
        return cancel_stop("during the upload");
    if (!uploaded)
        return fail(!answered ? _L("The printer sent an unexpected reply to the upload, such as a redirect, which Tinman does not follow.") : error);

    // 6. The stored copy is exactly the file Tinman wrote.
    if (cancelled(true))
        return cancel_stop("after the upload");
    {
        bool same = false;
        error.clear();
        auto http = Http::get(make_url("server/files/gcodes/" + Http::url_encode(remote_name)));
        prepare(http, m_timeouts.download);
        http.size_limit(size_t(job.size) + 1024)
            .on_complete([&](std::string reply, unsigned) {
                same = reply.size() == job.size && FibreSeekSend::sha256_hex(reply) == job.sha256;
            })
            .on_error([&](std::string reply, std::string message, unsigned http_status) {
                error = format_error(reply, message, http_status);
            })
            .on_progress([&](Http::Progress, bool &cancel) {
                if (!cancel)
                    cancel = cancelled(true);
            })
            .perform_sync();
        if (cancelled.cancelled())
            return cancel_stop("while checking the stored copy");
        if (!same)
            return fail(GUI::format_wxstr(_L("The copy of \"%1%\" on the printer does not match the file Tinman sent, so it was not started. "
                                             "Delete it on the printer and send again."),
                                          from_u8(remote_name)) +
                        (error.empty() ? wxString() : "\n" + error));
    }
    BOOST_LOG_TRIVIAL(info) << name << ": stored and verified " << remote_name;
    if (!start)
        return true;

    // 7. Start once, on the same idle printer. Cancel is honoured until the
    //    request is sent; after that nothing is retried.
    const auto not_started = [&](const wxString &why) {
        return fail(GUI::format_wxstr(_L("\"%1%\" is on the printer but was not started: %2%"), from_u8(remote_name), why));
    };
    if (cancelled(true))
        return cancel_stop("before the start");
    if (!check_idle())
        return cancelled.cancelled() ? cancel_stop("before the start") : not_started(error);
    if (!identity.empty()) {
        const std::string now = get("printer/info", body, status, error, stop) ? FibreSeekSend::printer_identity(body) : std::string();
        if (cancelled.cancelled())
            return cancel_stop("before the start");
        if (now != identity)
            return not_started(GUI::format_wxstr(_L("the printer at this address changed from \"%1%\" to \"%2%\"."), from_u8(identity),
                                                 from_u8(now)));
    }
    if (cancelled(true))
        return cancel_stop("before the start");

    enum class Dispatch { Accepted, Refused, Unknown } dispatch = Dispatch::Unknown;
    error.clear();
    {
        nlohmann::json request = {{"filename", remote_name}};
        auto           http    = Http::post(make_url("printer/print/start"));
        prepare(http, m_timeouts.start);
        http.header("Content-Type", "application/json")
            .set_post_body(request.dump())
            .on_complete([&](std::string, unsigned) { dispatch = Dispatch::Accepted; })
            .on_error([&](std::string reply, std::string message, unsigned http_status) {
                // An HTTP error status means Moonraker refused it. No status
                // (timeout, dropped connection) means it may have started.
                dispatch = http_status >= 400 ? Dispatch::Refused : Dispatch::Unknown;
                error    = format_error(reply, message, http_status);
            })
            .perform_sync();
    }
    BOOST_LOG_TRIVIAL(info) << name << ": start request for " << remote_name << " sent, outcome "
                            << (dispatch == Dispatch::Accepted ? "accepted" : dispatch == Dispatch::Refused ? "refused" : "unknown");
    if (dispatch == Dispatch::Refused)
        return not_started(GUI::format_wxstr(_L("the printer refused the start (%1%). Check the printer before trying again."), error));
    if (dispatch == Dispatch::Unknown)
        return fail(GUI::format_wxstr(_L("The start request for \"%1%\" got no clear answer, so the outcome is unknown. "
                                         "Check the printer; Tinman does not send the start again.%2%"),
                                      from_u8(remote_name), error.empty() ? wxString() : "\n" + error));

    // 8. Watch until the printer reports printing this exact file.
    std::string last_state = "no state reported";
    for (auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(m_timeouts.start_watch);
         std::chrono::steady_clock::now() < deadline;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(m_timeouts.poll_ms));
        if (cancelled(true)) {
            BOOST_LOG_TRIVIAL(warning) << name << ": the start was sent; stopped watching " << remote_name << " on cancel";
            return false;
        }
        wxString query_error;
        if (!get("printer/objects/query?print_stats", body, status, query_error, stop))
            continue;
        std::string detail;
        switch (FibreSeekSend::start_state(body, remote_name, detail)) {
        case FibreSeekSend::StartState::Started:
            BOOST_LOG_TRIVIAL(info) << name << ": printer is printing " << remote_name;
            info_fn(L"complete", from_u8(detail));
            return true;
        case FibreSeekSend::StartState::Failed:
            return fail(GUI::format_wxstr(_L("The printer stopped \"%1%\" right after the start (%2%). Check the printer."),
                                          from_u8(remote_name), from_u8(detail)));
        case FibreSeekSend::StartState::Waiting: last_state = detail; break;
        }
    }
    return fail(GUI::format_wxstr(_L("The printer accepted the start of \"%1%\" but has not reported printing it (last state: %2%). "
                                     "The outcome is unknown: check the printer. Tinman does not send the start again."),
                                  from_u8(remote_name), from_u8(last_state)));
}

} // namespace Slic3r
