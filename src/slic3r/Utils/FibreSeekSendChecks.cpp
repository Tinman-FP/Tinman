#include "FibreSeekSendChecks.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <ios>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/format.hpp>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>

#include "libslic3r/LocalesUtils.hpp"

namespace Slic3r::FibreSeekSend {

using json = nlohmann::json;

namespace {

// Incremental SHA-256 over OpenSSL's EVP interface.
class Sha256
{
public:
    Sha256() : m_ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free) { EVP_DigestInit_ex(m_ctx.get(), EVP_sha256(), nullptr); }
    void update(const char *data, size_t size) { EVP_DigestUpdate(m_ctx.get(), data, size); }
    std::string hex()
    {
        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned int  length = 0;
        EVP_DigestFinal_ex(m_ctx.get(), digest, &length);
        std::string out;
        char        byte[3];
        for (unsigned int i = 0; i < length; ++i) {
            std::snprintf(byte, sizeof(byte), "%02x", digest[i]);
            out += byte;
        }
        return out;
    }

private:
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> m_ctx;
};

// One positive number, independent of the C locale; 0 if malformed.
double parse_number(std::string text)
{
    boost::trim(text);
    size_t parsed = 0;
    double value  = 0.;
    try {
        value = string_to_double_decimal_point(text, &parsed);
    } catch (...) {
        return 0.;
    }
    return !text.empty() && parsed == text.size() && std::isfinite(value) && value > 0. ? value : 0.;
}

// "0.4,0.7" -> {0.4, 0.7}. Empty if any value is malformed.
std::vector<double> parse_numbers(const std::string &list)
{
    std::vector<std::string> parts;
    boost::split(parts, list, boost::is_any_of(","));
    std::vector<double> out;
    for (const std::string &part : parts) {
        const double value = parse_number(part);
        if (value == 0.)
            return {};
        out.push_back(value);
    }
    return out;
}

// Applies one G-code line to the job summary.
void scan_line(std::string line, JobInfo &job)
{
    boost::trim(line);
    std::string value;
    const auto  is = [&](const char *key) {
        const std::string prefix = std::string("; ") + key + " = ";
        if (!boost::starts_with(line, prefix))
            return false;
        value = line.substr(prefix.size());
        return true;
    };
    // A tool select: "T0" or "T1", alone or before a comment ("T1 ; switch extruder type to:PLASTIC").
    if (line.size() >= 2 && line[0] == 'T' && (line[1] == '0' || line[1] == '1') &&
        (line.size() == 2 || line[2] == ' ' || line[2] == '\t' || line[2] == ';'))
        (line[1] == '0' ? job.selects_t0 : job.selects_t1) = true;
    // Tinman's config block. Later values win, so the config block at the end
    // of the file overrides any header comment.
    if (is("printer_model"))
        job.printer_model = boost::trim_copy(value);
    else if (is("nozzle_diameter"))
        job.nozzle_diameters = parse_numbers(value);
    else if (is("plastic_nozzle_diameter"))
        job.plastic_nozzle = parse_number(value);
    else if (is("composite_nozzle_diameter"))
        job.composite_nozzle = parse_number(value);
    else if (!job.continuous_fiber) {
        // Fibre load, cut and finish commands (docs/fiberseek-gcode-contract.md).
        for (const char *command : {"M1001", "M1002", "M2800"})
            if (boost::istarts_with(line, command) && (line.size() == 5 || !std::isdigit((unsigned char) line[5])))
                job.continuous_fiber = true;
    }
}

// The named member if it is an object, else an empty object, so a reply of an
// unexpected shape reads as "nothing reported" instead of throwing.
const json &child(const json &object, const char *key)
{
    static const json empty = json::object();
    if (!object.is_object())
        return empty;
    const auto it = object.find(key);
    return it != object.end() && it->is_object() ? *it : empty;
}

// Moonraker wraps most replies in {"result": ...}; the upload reply is not wrapped.
const json &unwrap(const json &reply) { return reply.contains("result") ? child(reply, "result") : reply; }

// Status objects of a /printer/objects/query reply.
const json &status_of(const json &reply, const char *object) { return child(child(unwrap(reply), "status"), object); }

const json *member(const json &object, const char *key)
{
    if (!object.is_object())
        return nullptr;
    const auto it = object.find(key);
    return it == object.end() ? nullptr : &*it;
}

// A string member, or nullptr when it is missing or not a string.
const std::string *string_member(const json &object, const char *key)
{
    const json *value = member(object, key);
    return value && value->is_string() ? value->get_ptr<const std::string *>() : nullptr;
}

bool parse(const std::string &body, json &out, std::string &reason, const char *what)
{
    try {
        out = json::parse(body);
        if (out.is_object())
            return true;
    } catch (const std::exception &) {}
    reason = (boost::format("The printer sent an unreadable reply to the %1% request.") % what).str();
    return false;
}

std::string mm(double value) { return float_to_string_decimal_point(value, 2) + " mm"; }

bool same(double a, double b) { return std::abs(a - b) < 0.01; }

} // namespace

bool is_seeker3_model(const std::string &printer_model)
{
    // Same rule as the sidebar's Seeker 3 detection: user and beta profiles
    // add suffixes such as "FibreSeek Seeker 3 T0 Matrix 0.7".
    const std::string model = boost::to_lower_copy(printer_model);
    return (model.find("fibreseek") != std::string::npos || model.find("fiberseek") != std::string::npos) &&
           model.find("seeker 3") != std::string::npos;
}

std::string sha256_hex(const std::string &bytes)
{
    Sha256 sha;
    sha.update(bytes.data(), bytes.size());
    return sha.hex();
}

JobTools job_tools(const JobInfo &job)
{
    if (!job.selects_t0 && !job.selects_t1 && !job.continuous_fiber)
        return {};
    return {job.selects_t0 || job.continuous_fiber, job.selects_t1};
}

bool read_job(const boost::filesystem::path &path, JobInfo &job, std::string &reason)
{
    job = JobInfo();
    boost::filesystem::ifstream file(path, std::ios::binary);
    if (!file) {
        reason = "Cannot read the G-code file " + path.string() + ".";
        return false;
    }
    Sha256                  sha;
    std::array<char, 65536> chunk;
    std::string             line;
    bool                    first_chunk = true;
    while (file) {
        file.read(chunk.data(), chunk.size());
        const size_t count = size_t(file.gcount());
        if (count == 0)
            break;
        if (first_chunk) {
            first_chunk = false;
            // Binary G-code ("GCDE") or a zip archive such as .gcode.3mf ("PK").
            if ((count >= 4 && std::string(chunk.data(), 4) == "GCDE") || (count >= 2 && chunk[0] == 'P' && chunk[1] == 'K')) {
                reason = "The FibreSeek Seeker 3 prints plain G-code. Send the .gcode file, not a 3MF or binary G-code.";
                return false;
            }
        }
        sha.update(chunk.data(), count);
        job.size += count;
        for (size_t i = 0; i < count; ++i) {
            const char c = chunk[i];
            if (c == '\n' || c == '\r') {
                scan_line(line, job);
                line.clear();
            } else if (line.size() < 4096) { // long lines are never config or fibre commands
                line.push_back(c);
            }
        }
    }
    scan_line(line, job);
    if (file.bad()) {
        reason = "Reading the G-code file failed.";
        return false;
    }
    if (job.size == 0) {
        reason = "The G-code file is empty.";
        return false;
    }
    job.sha256 = sha.hex();
    return true;
}

std::string random_nonce()
{
    std::random_device device;
    char               text[9];
    std::snprintf(text, sizeof(text), "%08x", unsigned(device()));
    return text;
}

std::string remote_file_name(const std::string &local_name, const std::string &sha256, const std::string &stamp,
                             const std::string &nonce)
{
    std::string stem = local_name;
    if (boost::iends_with(stem, ".gcode"))
        stem.resize(stem.size() - 6);
    // Only ASCII letters, digits, '_' and '-': no path separators, dots,
    // spaces, shell or URL characters.
    std::string clean;
    for (unsigned char c : stem) {
        if ((c < 128 && std::isalnum(c)) || c == '-')
            clean.push_back(char(c));
        else if (clean.empty() || clean.back() != '_')
            clean.push_back('_');
    }
    boost::trim_if(clean, boost::is_any_of("_-"));
    if (clean.size() > 64)
        clean.resize(64);
    if (clean.empty())
        clean = "job";
    return clean + "-" + sha256.substr(0, 8) + "-" + stamp + "-" + nonce + ".gcode";
}

std::string gcode_root_from_roots(const std::string &body)
{
    json        reply;
    std::string ignored;
    if (!parse(body, reply, ignored, "storage roots"))
        return {};
    const json *roots = member(reply, "result");
    if (!roots || !roots->is_array())
        return {};
    for (const json &root : *roots) {
        const std::string *name = string_member(root, "name");
        const std::string *path = string_member(root, "path");
        if (name && *name == "gcodes" && path && boost::starts_with(*path, "/"))
            return boost::trim_right_copy_if(*path, boost::is_any_of("/"));
    }
    return {};
}

bool printer_is_idle(const std::string &body, const std::string &gcode_root, std::string &reason)
{
    json reply;
    if (!parse(body, reply, reason, "printer state"))
        return false;
    const json &webhooks = status_of(reply, "webhooks");
    const json &stats    = status_of(reply, "print_stats");
    const json &sdcard   = status_of(reply, "virtual_sdcard");
    const json &pause    = status_of(reply, "pause_resume");

    const std::string *klipper = string_member(webhooks, "state");
    if (!klipper || *klipper != "ready") {
        reason = "Klipper on the printer is not ready (" + (klipper ? *klipper : std::string("no state reported")) + ")";
        if (const std::string *message = string_member(webhooks, "state_message"); message && !message->empty())
            reason += ": " + *message;
        reason += ".";
        return false;
    }
    const std::string *state    = string_member(stats, "state");
    const std::string *filename = string_member(stats, "filename");
    const json        *active   = member(sdcard, "is_active");
    const json        *path     = member(sdcard, "file_path");
    const json        *paused   = member(pause, "is_paused");
    if (!state || !filename || !active || !active->is_boolean() || !path || !(path->is_null() || path->is_string()) ||
        !paused || !paused->is_boolean()) {
        reason = "The printer did not report its print state completely, so Tinman cannot tell whether it is idle.";
        return false;
    }
    if (active->get<bool>()) {
        reason = "The printer is still reading a file. Wait until it is idle, then send again.";
        return false;
    }
    if (paused->get<bool>()) {
        reason = "The printer is paused. Resume or cancel on the printer, then send again.";
        return false;
    }
    const std::string selected = path->is_string() ? path->get<std::string>() : std::string();
    if (*state == "standby") {
        // A file named or selected without printing (M23) would be replaced by the start.
        if (filename->empty() && selected.empty())
            return true;
        reason = "The printer has \"" + (filename->empty() ? selected : *filename) +
                 "\" selected but not started. Clear it on the printer, then send again.";
        return false;
    }
    if (*state == "complete" || *state == "cancelled") {
        // print_stats keeps the finished job's name; that alone is history. A
        // still-selected file must be that same job, by its exact path.
        if (selected.empty())
            return true;
        const std::string prefix = gcode_root.empty() ? std::string() : gcode_root + "/";
        if (!prefix.empty() && !filename->empty() && selected == prefix + *filename)
            return true;
        reason = "The printer still has \"" + selected + "\" selected. Clear it on the printer, then send again.";
        return false;
    }
    if (*state == "error") {
        reason = "The printer still shows an error from its last job. Clear it on the printer, then send again.";
        return false;
    }
    reason = "The printer is busy (" + *state + "). Wait until it is idle, then send again.";
    return false;
}

bool printer_tool_nozzles(const std::string &body, double &t0, double &t1, std::string &reason)
{
    t0 = t1 = 0.;
    json reply;
    if (!parse(body, reply, reason, "printer configuration"))
        return false;
    const json &config = child(status_of(reply, "configfile"), "config");
    const auto  nozzle = [&config](const char *section) {
        const std::string *value = string_member(child(config, section), "nozzle_diameter");
        return value ? parse_number(*value) : 0.;
    };
    t0 = nozzle("extruder");
    t1 = nozzle("extruder1");
    if (t0 > 0. && t1 > 0.)
        return true;
    reason = "The printer did not report the nozzles of both tools ([extruder] for T0, [extruder1] for T1), so Tinman cannot check them.";
    return false;
}

bool tools_match(const JobInfo &job, double t0, double t1, std::string &reason)
{
    const auto &nozzles = job.nozzle_diameters;
    if (job.plastic_nozzle <= 0. || nozzles.empty() || nozzles.size() > 2) {
        reason = "This G-code does not describe the Seeker 3's two tools (plastic_nozzle_diameter and nozzle_diameter). "
                 "Slice it with a Seeker 3 profile.";
        return false;
    }
    // The profile's nozzle list must agree with its role diameters.
    const bool uses_t0 = nozzles.size() == 2 || job.continuous_fiber;
    if (!same(nozzles[0], job.plastic_nozzle) ||
        (uses_t0 && (job.composite_nozzle <= 0. || nozzles.size() != 2 || !same(nozzles[1], job.composite_nozzle)))) {
        reason = "The printer profile's nozzle list does not match its polymer (T1) and composite (T0) nozzle sizes. "
                 "Fix the printer profile and slice again.";
        return false;
    }
    if (!same(t1, job.plastic_nozzle)) {
        reason = "The printer's T1 polymer nozzle is " + mm(t1) + ", but this job was sliced for " + mm(job.plastic_nozzle) + ".";
        return false;
    }
    if (uses_t0 && !same(t0, job.composite_nozzle)) {
        reason = "The printer's T0 matrix and fibre nozzle is " + mm(t0) + ", but this job was sliced for " + mm(job.composite_nozzle) + ".";
        return false;
    }
    return true;
}

bool upload_reply_ok(const std::string &body, uint64_t local_size, const std::string &remote_name, std::string &reason)
{
    json reply;
    if (!parse(body, reply, reason, "upload"))
        return false;
    const json &result  = unwrap(reply);
    const json *started = member(result, "print_started");
    const json *queued  = member(result, "print_queued");
    if (!started || !started->is_boolean() || started->get<bool>() || !queued || !queued->is_boolean() || queued->get<bool>()) {
        reason = "The printer did not confirm that it left the upload unstarted. Check the printer now.";
        return false;
    }
    const json        &item = child(result, "item");
    const std::string *path = string_member(item, "path");
    const std::string *root = string_member(item, "root");
    const json        *size = member(item, "size");
    if (!path || *path != remote_name || !root || *root != "gcodes") {
        reason = "The printer did not store the upload as \"" + remote_name + "\" in its G-code folder.";
        return false;
    }
    if (!size || !size->is_number_unsigned() || size->get<uint64_t>() != local_size) {
        reason = "The printer stored a file of a different size than the one sent.";
        return false;
    }
    return true;
}

std::string printer_identity(const std::string &body)
{
    json        reply;
    std::string ignored;
    if (!parse(body, reply, ignored, "printer info"))
        return {};
    const std::string *hostname = string_member(unwrap(reply), "hostname");
    return hostname ? *hostname : std::string();
}

bool start_refused_before_execution(unsigned http_status, const std::string &body)
{
    if (http_status != 400 && http_status != 401 && http_status != 403 && http_status != 404)
        return false;
    json        reply;
    std::string ignored;
    if (!parse(body, reply, ignored, "start"))
        return false;
    const json &error = child(reply, "error");
    const json *code  = member(error, "code");
    return code && code->is_number_unsigned() && code->get<unsigned>() == http_status && string_member(error, "message");
}

StartState start_state(const std::string &body, const std::string &remote_name, std::string &detail)
{
    json reply;
    if (!parse(body, reply, detail, "printer state"))
        return StartState::Waiting;
    const json        &stats    = status_of(reply, "print_stats");
    const std::string *state    = string_member(stats, "state");
    const std::string *filename = string_member(stats, "filename");
    detail                      = state ? *state : std::string("no state reported");
    // Until Klipper loads our file, print_stats still describes the previous
    // job. print_stats.filename is relative to the G-code root: compare exactly.
    if (!state || !filename || *filename != remote_name)
        return StartState::Waiting;
    if (*state == "printing")
        return StartState::Started;
    if (*state == "error" || *state == "cancelled" || *state == "paused") {
        if (const std::string *message = string_member(stats, "message"); message && !message->empty())
            detail += ": " + *message;
        return StartState::Failed;
    }
    return StartState::Waiting;
}

} // namespace Slic3r::FibreSeekSend
