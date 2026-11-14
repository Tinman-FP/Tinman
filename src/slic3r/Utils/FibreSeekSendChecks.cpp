#include "FibreSeekSendChecks.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <memory>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem/fstream.hpp>
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

// "0.4,0.7" -> {0.4, 0.7}, independent of the C locale. Empty on a bad value.
std::vector<double> parse_numbers(const std::string &list)
{
    std::vector<std::string> parts;
    boost::split(parts, list, boost::is_any_of(","));
    std::vector<double> out;
    for (std::string part : parts) {
        boost::trim(part);
        size_t parsed = 0;
        double value  = 0.;
        try {
            value = string_to_double_decimal_point(part, &parsed);
        } catch (...) {
            return {};
        }
        if (part.empty() || parsed != part.size() || !std::isfinite(value) || value <= 0.)
            return {};
        out.push_back(value);
    }
    return out;
}

// Applies one G-code line to the job summary.
void scan_line(std::string line, JobInfo &job)
{
    boost::trim(line);
    // Tinman's config block: "; key = value". Later values win, so the
    // config block at the end of the file overrides any header comment.
    if (boost::starts_with(line, "; printer_model = "))
        job.printer_model = boost::trim_copy(line.substr(18));
    else if (boost::starts_with(line, "; nozzle_diameter = "))
        job.nozzle_diameters = parse_numbers(line.substr(20));
    else if (!job.continuous_fiber) {
        // Fibre load, cut and finish commands (docs/fiberseek-gcode-contract.md).
        for (const char *command : {"M1001", "M1002", "M2800"})
            if (boost::istarts_with(line, command) && (line.size() == 5 || !std::isdigit((unsigned char) line[5])))
                job.continuous_fiber = true;
    }
}

// The named member if it is an object, else an empty object, so that a reply
// of an unexpected shape reads as "nothing reported" instead of throwing.
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

bool flag(const json &object, const char *key)
{
    const auto it = object.find(key);
    return it != object.end() && it->is_boolean() && it->get<bool>();
}

bool parse(const std::string &body, json &out, std::string &reason, const char *what)
{
    try {
        out = json::parse(body);
        return out.is_object();
    } catch (const std::exception &) {
        reason = (boost::format("The printer sent an unreadable reply to the %1% request.") % what).str();
        return false;
    }
}

std::string string_at(const json &object, const char *key)
{
    return object.is_object() && object.contains(key) && object[key].is_string() ? object[key].get<std::string>() : std::string();
}

// Klipper may report a loaded file with its full path or with a root prefix.
bool same_file(std::string reported, const std::string &remote_path)
{
    if (reported.empty())
        return false;
    boost::replace_all(reported, "\\", "/");
    return reported == remote_path || boost::ends_with(reported, "/" + remote_path);
}

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
                reason = "The FibreSeek Seeker 3 prints plain G-code. Turn off binary G-code and 3MF export for this printer.";
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

bool printer_is_idle(const std::string &body, std::string &reason)
{
    json reply;
    if (!parse(body, reply, reason, "printer state"))
        return false;
    const json &webhooks = status_of(reply, "webhooks");
    const json &stats    = status_of(reply, "print_stats");
    const json &sdcard   = status_of(reply, "virtual_sdcard");

    const std::string klipper = string_at(webhooks, "state");
    if (klipper != "ready") {
        reason = "Klipper on the printer is not ready (" + (klipper.empty() ? std::string("no state reported") : klipper) + ")";
        if (const std::string message = string_at(webhooks, "state_message"); !message.empty())
            reason += ": " + message;
        reason += ".";
        return false;
    }
    const std::string state = string_at(stats, "state");
    if (state == "error") {
        reason = "The printer still shows an error from its last job. Clear it on the printer, then send again.";
        return false;
    }
    // Unknown states count as busy: only these mean nothing is loaded or running.
    if (state != "standby" && state != "complete" && state != "cancelled") {
        reason = "The printer is busy (" + (state.empty() ? std::string("no state reported") : state) +
                 "). Wait until it is idle, then send again.";
        return false;
    }
    if (!flag(sdcard, "is_active"))
        return true;
    reason = "The printer is still reading a file. Wait until it is idle, then send again.";
    return false;
}

bool printer_nozzle_diameters(const std::string &body, std::vector<double> &diameters, std::string &reason)
{
    diameters.clear();
    json reply;
    if (!parse(body, reply, reason, "printer configuration"))
        return false;
    const json &config = child(status_of(reply, "configfile"), "config");
    for (const auto &[section, values] : config.items()) {
        // [extruder], [extruder1], ... Klipper keeps raw config values as strings.
        if (!boost::starts_with(section, "extruder") ||
            !std::all_of(section.begin() + 8, section.end(), [](unsigned char c) { return std::isdigit(c); }))
            continue;
        const std::vector<double> value = parse_numbers(string_at(values, "nozzle_diameter"));
        if (value.size() == 1)
            diameters.push_back(value.front());
    }
    if (!diameters.empty())
        return true;
    reason = "The printer did not report its nozzle sizes, so Tinman cannot check that they match the slice.";
    return false;
}

bool nozzles_match(const std::vector<double> &job, const std::vector<double> &printer, std::string &reason)
{
    if (job.empty()) {
        reason = "The G-code does not say which nozzles it was sliced for. Slice it again in Tinman.";
        return false;
    }
    const auto list = [](const std::vector<double> &values) {
        std::string out;
        for (double value : values)
            out += (out.empty() ? "" : ", ") + float_to_string_decimal_point(value, 2) + " mm";
        return out;
    };
    for (double wanted : job) {
        if (std::none_of(printer.begin(), printer.end(), [wanted](double fitted) { return std::abs(fitted - wanted) < 0.01; })) {
            reason = "The printer reports nozzles of " + list(printer) + ", but this job was sliced for " + list(job) +
                     ". Fit the right nozzles or slice with the matching printer profile.";
            return false;
        }
    }
    return true;
}

bool upload_reply_ok(const std::string &body, uint64_t local_size, std::string &remote_path, std::string &reason)
{
    json reply;
    if (!parse(body, reply, reason, "upload"))
        return false;
    const json &result = unwrap(reply);
    if (flag(result, "print_started") || flag(result, "print_queued")) {
        reason = "The printer started or queued the file on upload although Tinman asked it not to. Check the printer now.";
        return false;
    }
    const json &item = child(result, "item");
    remote_path     = string_at(item, "path");
    const std::string root = string_at(item, "root");
    if (remote_path.empty() || (!root.empty() && root != "gcodes")) {
        reason = "The printer did not store the upload in its G-code folder.";
        return false;
    }
    if (item.contains("size") && item["size"].is_number_unsigned() && item["size"].get<uint64_t>() != local_size) {
        reason = "The printer stored a file of a different size than the one sent.";
        return false;
    }
    return true;
}

StartState start_state(const std::string &body, const std::string &remote_path, std::string &detail)
{
    json reply;
    if (!parse(body, reply, detail, "printer state"))
        return StartState::Waiting;
    const json       &stats = status_of(reply, "print_stats");
    const std::string state = string_at(stats, "state");
    detail                  = state;
    // Until Klipper loads our file, print_stats still describes the previous job.
    if (!same_file(string_at(stats, "filename"), remote_path))
        return StartState::Waiting;
    if (state == "error" || state == "cancelled") {
        if (const std::string message = string_at(stats, "message"); !message.empty())
            detail += ": " + message;
        return StartState::Failed;
    }
    // Klipper reports "printing"; a printer may first report its own preparation state.
    return state == "standby" || state.empty() ? StartState::Waiting : StartState::Started;
}

} // namespace Slic3r::FibreSeekSend
