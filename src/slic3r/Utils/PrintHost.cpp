#include "PrintHost.hpp"

#include "libslic3r/Config.hpp"
#include <algorithm>
#include <string>
#include <boost/algorithm/string/predicate.hpp>
#include "slic3r/GUI/Gizmos/GLGizmoBase.hpp"
#include <cstddef>
#include <boost/filesystem/path.hpp>
#include <utility>
#include <memory>
#include <boost/filesystem/operations.hpp>
#include "libslic3r/LifecycleEvents.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include <vector>
#include <thread>
#include <exception>
#include <sstream>
#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>
#include <nlohmann/json.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <wx/event.h>
#include <wx/string.h>
#include <wx/app.h>
#include <wx/arrstr.h>

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Channel.hpp"
#include "OctoPrint.hpp"
#include "Duet.hpp"
#include "UltiMaker.hpp"
#include "FlashAir.hpp"
#include "AstroBox.hpp"
#include "Repetier.hpp"
#include "MKS.hpp"
#include "ESP3D.hpp"
#include "CrealityPrint.hpp"
#include "../GUI/PrintHostDialogs.hpp"
#include "../GUI/MainFrame.hpp"
#include "slic3r/plugin/PluginManager.hpp"
#include "Obico.hpp"
#include "Flashforge.hpp"
#include "SimplyPrint.hpp"
#include "ElegooLink.hpp"
#include "3DPrinterOS.hpp"
#include "Moonraker.hpp"
#include "FibreSeek.hpp"

namespace fs = boost::filesystem;
using Slic3r::GUI::PrintHostQueueDialog;

namespace Slic3r {

PrintHost::~PrintHost() {}

PrintHost* PrintHost::get_print_host(DynamicPrintConfig *config)
{
    PrinterTechnology tech = ptFFF;

    {
        const auto opt = config->option<ConfigOptionEnum<PrinterTechnology>>("printer_technology");
        if (opt != nullptr) {
            tech = opt->value;
        }
    }

    if (tech == ptFFF) {
        const auto opt = config->option<ConfigOptionEnum<PrintHostType>>("host_type");
        const auto host_type = opt != nullptr ? opt->value : htOctoPrint;

        switch (host_type) {
            case htOctoPrint: return new OctoPrint(config);
            case htDuet:      return new Duet(config);
            case htUltiMaker: return new UltiMaker(config);
            case htFlashAir:  return new FlashAir(config);
            case htAstroBox:  return new AstroBox(config);
            case htRepetier:  return new Repetier(config);
            case htPrusaLink: return new PrusaLink(config);
            case htPrusaConnect: return new PrusaConnect(config);
            case htMKS:       return new MKS(config);
            case htESP3D:       return new ESP3D(config);
            case htCrealityPrint:    return new CrealityPrint(config);
            case htObico:     return new Obico(config);
            case htFlashforge: return new Flashforge(config);
            case htSimplyPrint: return new SimplyPrint(config);
            case htElegooLink: return new ElegooLink(config);
            case ht3DPrinterOS: return new C3DPrinterOS(config);
            case htMoonraker: return new Moonraker(config);
            case htFibreSeek: return new FibreSeek(config);
            default:          return nullptr;
        }
    } else {
        return new SL1Host(config);
    }
}

std::string PrintHost::get_print_host_webui(DynamicPrintConfig* config)
{
    if (config == nullptr)
        return {};

    std::string webui_url;
    const auto* host_type_opt = config->option<ConfigOptionEnum<PrintHostType>>("host_type");
    const auto  host_type     = host_type_opt != nullptr ? host_type_opt->value : htOctoPrint;

    switch (host_type) {
    case htElegooLink: {
        webui_url = ElegooLink::get_print_host_webui(config);
        break;
    }
    default: break;
    }

    if (webui_url.empty()) {
        webui_url = config->opt_string("print_host_webui");
        if (webui_url.empty())
            webui_url = config->opt_string("print_host");
        if (webui_url.empty())
            return webui_url;
    }

    if (host_type == htCrealityPrint)
        return CrealityPrint::get_device_webui_url(std::move(webui_url));

    const bool has_http_scheme = boost::algorithm::istarts_with(webui_url, "http");
    const bool has_file_scheme = boost::algorithm::istarts_with(webui_url, "file:");

    if (!has_http_scheme && !has_file_scheme)
        webui_url = "http://" + webui_url;

    return webui_url;
}

namespace {

// Moonraker (Klipper's API server) reports a raised exception as { "error": { "code", "message", "traceback" } }
// under every host type that connects to it, often with the cause only in the traceback. Returns the reason to show,
// or empty for any other body.
std::string moonraker_error_reason(const std::string &body)
{
    const auto root = nlohmann::json::parse(body, nullptr, false);
    const auto err  = root.find("error");
    if (err == root.end())
        return {};
    const auto message   = err->find("message");
    const auto traceback = err->find("traceback");
    if (message == err->end() || traceback == err->end() || !message->is_string() || !traceback->is_string())
        return {};

    const auto &msg = message->get_ref<const std::string &>();
    const auto &tb  = traceback->get_ref<const std::string &>();
    if (msg.empty())
        return {};
    const auto end = tb.find_last_not_of(" \t\r\n");
    if (end == std::string::npos)
        return msg;

    // Chained exceptions each start a new traceback; the one that failed the request is the last.
    const auto header = tb.rfind("Traceback (most recent call last):", end);

    // Tornado renders a raised HTTPError as "HTTP <code>: <reason>[ (<detail>)]", and the detail may span lines.
    const auto code = err->find("code");
    if (code != err->end() && code->is_number_integer()) {
        const std::string marker = "HTTP " + std::to_string(code->get<int>()) + ": ";
        const auto        pos    = tb.rfind(marker, end);
        if (pos != std::string::npos && (header == std::string::npos || pos > header) && pos + marker.size() <= end) {
            const std::string reason = tb.substr(pos + marker.size(), end + 1 - pos - marker.size());
            // An HTTPError whose detail equals its reason, like HTTPError(401, "Unauthorized"), renders the phrase twice.
            return reason == msg + " (" + msg + ")" ? msg : reason;
        }
    }

    // Any other exception's type and message are everything from the first unindented line after its frames.
    auto begin = (header == std::string::npos) ? std::string::npos : tb.find('\n', header);
    while (begin != std::string::npos && begin < end) {
        ++begin;
        if (tb[begin] != ' ' && tb[begin] != '\r' && tb[begin] != '\n')
            break;
        begin = tb.find('\n', begin);
    }
    if (begin == std::string::npos || begin > end) {
        const auto nl = tb.rfind('\n', end);
        begin         = (nl == std::string::npos) ? 0 : nl + 1;
    }
    return msg + " (" + tb.substr(begin, end + 1 - begin) + ")";
}

} // namespace

int PrintHost::get_err_code_from_body(const std::string &body)
{
    boost::property_tree::ptree root;
    std::istringstream iss(body);
    try {
        boost::property_tree::read_json(iss, root);
    } catch (const std::exception &ex) {
        BOOST_LOG_TRIVIAL(error) << "PrintHost: response is not valid JSON: " << ex.what();
        return -1;
    }

    return root.get<int>("err", 0);
}

wxString PrintHost::format_error(const std::string &body, const std::string &error, unsigned status) const
{
    if (status != 0) {
        const std::string reason = moonraker_error_reason(body);
        auto wxbody = wxString::FromUTF8(reason.empty() ? body : reason);
        return wxString::Format("HTTP %u: %s", status, wxbody);
    } else {
        if (error.find("curl:Timeout was reached") != std::string::npos) {
            return timed_out_message();
        }else if(error.find("curl:Couldn't resolve host name")!= std::string::npos){
            return _L("The Hostname/IP/URL could not be parsed, please check it and try again.");
        } else if (error.find("Connection was reset") != std::string::npos){
            return _L("File/data transfer interrupted. Please check the printer and network, then try it again.");
        }
        else {
            return wxString::FromUTF8(error.data());
        }
    }
}

wxString PrintHost::timed_out_message()
{
    return _L("Connection timed out. Please check if the printer and computer network are functioning properly, and confirm that they are on the same network.");
}


struct PrintHostJobQueue::priv
{
    // XXX: comment on how bg thread works

    PrintHostJobQueue *q;

    Channel<PrintHostJob> channel_jobs;
    // Cancels of a job that has left channel_jobs: the running one, or one about to run.
    Channel<size_t> channel_cancels;
    size_t job_id = 0;      // the running job's id (bg thread)
    size_t next_job_id = 0; // the next enqueued job's id (enqueue() caller)
    int prev_progress = -1;
    fs::path source_to_remove;

    std::thread bg_thread;
    bool bg_exit = false;

    PrintHostQueueDialog *queue_dialog = nullptr;
    PrintHostJobQueue::Events events; // used when there is no dialog
    // The running job's host sent DispatchedInfoTag: Cancel can no longer hide its outcome.
    bool job_dispatched = false;

    priv(PrintHostJobQueue *q) : q(q) {}

    void emit_progress(int progress);
    void emit_error(wxString error);
    void emit_cancel(size_t id);
    void emit_info(wxString tag, wxString status);
    void emit_device_tab();
    void start_bg_thread();
    void stop_bg_thread();
    void bg_thread_main();
    bool take_cancel_request();
    void progress_fn(Http::Progress progress, bool &cancel);
    void error_fn(wxString error);
    void info_fn(wxString tag, wxString status);
    void remove_source(const fs::path &path);
    void remove_source();
    void perform_job(PrintHostJob the_job);
};

PrintHostJobQueue::PrintHostJobQueue(PrintHostQueueDialog *queue_dialog)
    : p(new priv(this))
{
    p->queue_dialog = queue_dialog;
}

PrintHostJobQueue::PrintHostJobQueue(Events events)
    : p(new priv(this))
{
    p->events = std::move(events);
}

PrintHostJobQueue::~PrintHostJobQueue()
{
    if (p) { p->stop_bg_thread(); }
}

void PrintHostJobQueue::priv::emit_progress(int progress)
{
    if (!queue_dialog) {
        if (events.progress) events.progress(job_id, progress);
        return;
    }
    auto evt = new PrintHostQueueDialog::Event(GUI::EVT_PRINTHOST_PROGRESS, queue_dialog->GetId(), job_id, progress);
    wxQueueEvent(queue_dialog, evt);
}

void PrintHostJobQueue::priv::emit_error(wxString error)
{
    if (!queue_dialog) {
        if (events.error) events.error(job_id, std::move(error));
        return;
    }
    auto evt = new PrintHostQueueDialog::Event(GUI::EVT_PRINTHOST_ERROR, queue_dialog->GetId(), job_id, std::move(error));
    wxQueueEvent(queue_dialog, evt);
}

void PrintHostJobQueue::priv::emit_info(wxString tag, wxString status)
{
    if (!queue_dialog) {
        if (events.info) events.info(job_id, std::move(tag), std::move(status));
        return;
    }
    auto evt = new PrintHostQueueDialog::Event(GUI::EVT_PRINTHOST_INFO, queue_dialog->GetId(), job_id, std::move(tag), std::move(status));
    wxQueueEvent(queue_dialog, evt);
}

void PrintHostJobQueue::priv::emit_cancel(size_t id)
{
    if (!queue_dialog) {
        if (events.cancel) events.cancel(id);
        return;
    }
    auto evt = new PrintHostQueueDialog::Event(GUI::EVT_PRINTHOST_CANCEL, queue_dialog->GetId(), id);
    wxQueueEvent(queue_dialog, evt);
}

void PrintHostJobQueue::priv::emit_device_tab()
{
    if (!queue_dialog) {
        if (events.device_tab) events.device_tab(job_id);
        return;
    }
    GUI::wxGetApp().mainframe->request_device_tab_after_upload();
}

void PrintHostJobQueue::priv::start_bg_thread()
{
    if (bg_thread.joinable()) { return; }

    std::shared_ptr<priv> p2 = q->p;
    bg_thread = std::thread([p2]() {
        p2->bg_thread_main();
    });
}

void PrintHostJobQueue::priv::stop_bg_thread()
{
    if (bg_thread.joinable()) {
        bg_exit = true;
        channel_jobs.push(PrintHostJob()); // Push an empty job to wake up bg_thread in case it's sleeping
        bg_thread.detach();                // Let the background thread go, it should exit on its own
    }
}

void PrintHostJobQueue::priv::bg_thread_main()
{
    // bg thread entry point

    try {
        // Pick up jobs from the job channel:
        while (! bg_exit) {
            auto job = channel_jobs.pop();   // Sleeps in a cond var if there are no jobs
            if (job.empty()) {
                // This happens when the thread is being stopped
                break;
            }

            job_id           = job.id;
            source_to_remove = job.upload_data.source_path;
            // Canceled after it left the queue but before it started: it must not start.
            if (!job.cancelled && take_cancel_request()) {
                job.cancelled = true;
                emit_cancel(job_id);
            }

            BOOST_LOG_TRIVIAL(debug) << boost::format("PrintHostJobQueue/bg_thread: Received job: [%1%]: `%2%` -> `%3%`, cancelled: %4%")
                % job_id
                % job.upload_data.upload_path
                % job.printhost->get_host()
                % job.cancelled;

            if (! job.cancelled) {
                perform_job(std::move(job));
            }

            remove_source();
        }
    } catch (const std::exception &e) {
        emit_error(e.what());
    }

    // Cleanup leftover files, if any
    remove_source();
    auto jobs = channel_jobs.lock_rw();
    for (const PrintHostJob &job : *jobs) {
        remove_source(job.upload_data.source_path);
    }
}

void PrintHostJobQueue::priv::progress_fn(Http::Progress progress, bool &cancel)
{
    if (cancel) {
        // When cancel is true from the start, Http indicates request has been cancelled.
        // After dispatch the host reports the outcome itself; it is never "Canceled".
        if (!job_dispatched)
            emit_cancel(job_id);
        return;
    }

    if (bg_exit) {
        cancel = true;
        return;
    }

    if (take_cancel_request())
        cancel = true;

    if (! cancel) {
        // Tinman: 100 is the finished state (COMPLETED in the queue dialog and the notification),
        // so only perform_job() sends it, once upload() has succeeded. After the last byte a
        // printer can still refuse the file, or take a while to store and start it (~90 s on a U1).
        int gui_progress = progress.ultotal > 0 ? std::min(99, int(100 * progress.ulnow / progress.ultotal)) : 0;
        if (gui_progress != prev_progress) {
            emit_progress(gui_progress);
            prev_progress = gui_progress;
        }
    }
}

void PrintHostJobQueue::priv::error_fn(wxString error)
{
    // A job canceled before its error shows Canceled, unless it was dispatched: then its
    // outcome (unknown, warning) must reach the user.
    if (take_cancel_request() && !job_dispatched)
        emit_cancel(job_id);
    else
        emit_error(std::move(error));
}

// Takes the pending cancels; true when one is for the running job. Any other one is stale: its job
// has finished, and a waiting job's cancel never comes here (see cancel()).
bool PrintHostJobQueue::priv::take_cancel_request()
{
    if (channel_cancels.size_hint() == 0)
        return false;
    auto       cancels = channel_cancels.lock_rw();
    const bool found   = std::find(cancels->begin(), cancels->end(), job_id) != cancels->end();
    cancels->clear();
    if (found)
        BOOST_LOG_TRIVIAL(debug) << boost::format("PrintHostJobQueue: Job id %1% cancelled") % job_id;
    return found;
}

void PrintHostJobQueue::priv::info_fn(wxString tag, wxString status)
{
    if (tag == PrintHostJobQueue::DispatchedInfoTag)
        job_dispatched = true;
    emit_info(tag, status);
}

void PrintHostJobQueue::priv::remove_source(const fs::path &path)
{
    if (! path.empty()) {
        boost::system::error_code ec;
        fs::remove(path, ec);
        if (ec) {
            BOOST_LOG_TRIVIAL(error) << boost::format("PrintHostJobQueue: Error removing file `%1%`: %2%") % path % ec;
        }
    }
}

void PrintHostJobQueue::priv::remove_source()
{
    remove_source(source_to_remove);
    source_to_remove.clear();
}

bool PrintHostJobQueue::upload_job(PrintHostJob &job, PrintHost::ProgressFn progress_fn, PrintHost::ErrorFn error_fn, PrintHost::InfoFn info_fn)
{
    // Captured before upload_data is moved into upload() below.
    const std::string upload_filename = job.upload_data.source_path.filename().string();

    {
        LifecycleEventContext ctx;
        ctx.name = upload_filename;
        ctx.code = LifecycleEvtCode::Ok;
        fire_lifecycle_event(LifecycleEvent::UploadStarted, ctx);
    }

    bool success = false;
    std::string error;
    // A throwing upload must not stop the worker, or later jobs would stay queued forever.
    try {
        success = job.printhost->upload(std::move(job.upload_data), std::move(progress_fn), error_fn, std::move(info_fn));
    } catch (const std::exception &e) {
        error = e.what();
        error_fn(error);
    }

    {
        LifecycleEventContext ctx;
        ctx.name  = upload_filename;
        ctx.code  = success ? LifecycleEvtCode::Ok : LifecycleEvtCode::Error;
        ctx.msg   = error;
        fire_lifecycle_event(LifecycleEvent::UploadFinished, ctx);
    }

    return success;
}

void PrintHostJobQueue::priv::perform_job(PrintHostJob the_job)
{
    job_dispatched = false;
    emit_progress(0);   // Indicate the upload is starting

    bool success = PrintHostJobQueue::upload_job(the_job,
        [this](Http::Progress progress, bool &cancel)   { this->progress_fn(std::move(progress), cancel); },
        [this](wxString error)                          { this->error_fn(std::move(error)); },
        [this](wxString tag, wxString host)             { this->info_fn(std::move(tag), std::move(host)); }
    );

    if (success) {
        emit_progress(100);
        if (the_job.switch_to_device_tab)
            emit_device_tab();
    }
}

void PrintHostJobQueue::enqueue(PrintHostJob job)
{
    p->start_bg_thread();
    job.id = p->next_job_id++;
    if (p->queue_dialog)
        p->queue_dialog->append_job(job);
    p->channel_jobs.push(std::move(job));
}

void PrintHostJobQueue::cancel(size_t id)
{
    // Tinman: a waiting job is dropped here rather than when the running upload next reports
    // progress, which a slow printer may not do for minutes. It then never starts.
    bool waiting = false;
    bool dropped = false;
    {
        auto jobs = p->channel_jobs.lock_rw();
        for (PrintHostJob &job : *jobs)
            if (!job.empty() && job.id == id) {
                waiting       = true;
                dropped       = !job.cancelled;
                job.cancelled = true;
                break;
            }
    }
    if (dropped) {
        BOOST_LOG_TRIVIAL(debug) << boost::format("PrintHostJobQueue: Queued job id %1% cancelled") % id;
        p->emit_cancel(id);
    } else if (!waiting)
        p->channel_cancels.push(id);
}

}
