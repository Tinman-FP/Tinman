#ifndef slic3r_FibreSeekSendChecks_hpp_
#define slic3r_FibreSeekSendChecks_hpp_

// Checks that the FibreSeek print host (FibreSeek.cpp) runs before and after
// it sends a job to a FibreSeek Seeker 3.
//
// The Seeker 3 runs Klipper behind Moonraker. Everything here reads only
// replies from Moonraker's public web API
// (https://moonraker.readthedocs.io/en/latest/web_api/) and the G-code file
// Tinman wrote, so these functions are pure and can be unit tested without a
// printer. The checks follow the safety checks in the Codex sessions' one-off
// Seeker 3 send scripts (October 2026): send only to an idle printer whose
// nozzles match the slice, never overwrite a file on the printer, upload
// without starting, compare the stored copy byte for byte, and start once.

#include <cstdint>
#include <string>
#include <vector>

#include <boost/filesystem/path.hpp>

namespace Slic3r::FibreSeekSend {

// What the host needs to know about the local G-code file.
struct JobInfo
{
    std::string         printer_model;    // "; printer_model = ..." in Tinman's config block
    std::vector<double> nozzle_diameters; // "; nozzle_diameter = 0.4,0.7"
    bool                continuous_fiber = false; // has fibre load (M1001), cut (M2800) or finish (M1002) commands
    uint64_t            size = 0;
    std::string         sha256;           // lowercase hex digest of the whole file
};

// Reads the file once: its size and SHA-256, the printer model and nozzle
// sizes from Tinman's config block, and whether it contains continuous-fibre
// commands. Refuses binary G-code and zip (3MF) files: the Seeker 3 prints
// plain G-code.
bool read_job(const boost::filesystem::path &path, JobInfo &job, std::string &reason);

// True for printer_model names of Seeker 3 profiles ("FibreSeek Seeker 3", with any suffix).
bool is_seeker3_model(const std::string &printer_model);

// Lowercase hex SHA-256 of a buffer.
std::string sha256_hex(const std::string &bytes);

// Reply of GET /printer/objects/query?webhooks&print_stats&virtual_sdcard.
// Idle means Klipper is ready, no file is loaded or printing, and the last
// job did not end in an error that is still showing.
bool printer_is_idle(const std::string &body, std::string &reason);

// Reply of GET /printer/objects/query?configfile=config: the nozzle_diameter
// of every [extruder], [extruder1], ... section.
bool printer_nozzle_diameters(const std::string &body, std::vector<double> &diameters, std::string &reason);

// Every nozzle the job was sliced for must be fitted to the printer.
bool nozzles_match(const std::vector<double> &job, const std::vector<double> &printer, std::string &reason);

// Reply of POST /server/files/upload sent with print=false. Returns the
// stored path. Fails if the printer started or queued the file, or stored a
// different size or root.
bool upload_reply_ok(const std::string &body, uint64_t local_size, std::string &remote_path, std::string &reason);

// Reply of GET /printer/objects/query?print_stats after a start request.
enum class StartState { Started, Waiting, Failed };
StartState start_state(const std::string &body, const std::string &remote_path, std::string &detail);

} // namespace Slic3r::FibreSeekSend

#endif
