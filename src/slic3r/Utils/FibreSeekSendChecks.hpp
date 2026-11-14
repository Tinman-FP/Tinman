#ifndef slic3r_FibreSeekSendChecks_hpp_
#define slic3r_FibreSeekSendChecks_hpp_

// Checks that the FibreSeek print host (FibreSeek.cpp) runs before and after
// it sends a job to a FibreSeek Seeker 3.
//
// The Seeker 3 runs Klipper behind Moonraker. Everything here reads only
// replies from Moonraker's public web API
// (https://moonraker.readthedocs.io/en/latest/web_api/) and the G-code file
// Tinman wrote, so these functions are pure and are unit tested without a
// printer. They follow the safety checks in the Codex sessions' one-off
// Seeker 3 send scripts (October 2026). Anything missing, malformed or
// unknown counts as a failed check.

#include <cstdint>
#include <string>
#include <vector>

#include <boost/filesystem/path.hpp>

namespace Slic3r::FibreSeekSend {

// What the host needs to know about the local G-code file. Values come from
// Tinman's config block ("; key = value").
struct JobInfo
{
    std::string         printer_model;
    std::vector<double> nozzle_diameters;        // nozzle_diameter: material 1, material 2
    double              plastic_nozzle   = 0.;   // plastic_nozzle_diameter (T1, polymer)
    double              composite_nozzle = 0.;   // composite_nozzle_diameter (T0, matrix + fibre)
    bool                continuous_fiber = false; // has fibre load (M1001), cut (M2800) or finish (M1002) commands
    uint64_t            size = 0;
    std::string         sha256;                  // lowercase hex digest of the whole file
};

// Reads the file once: its size and SHA-256, the config values above and
// whether it contains continuous-fibre commands. Refuses binary G-code and
// zip (3MF) files: the Seeker 3 prints plain G-code.
bool read_job(const boost::filesystem::path &path, JobInfo &job, std::string &reason);

// True for printer_model names of Seeker 3 profiles ("FibreSeek Seeker 3", with any suffix).
bool is_seeker3_model(const std::string &printer_model);

// Lowercase hex SHA-256 of a buffer.
std::string sha256_hex(const std::string &bytes);

// A remote name unique to this send: the local name reduced to [A-Za-z0-9_-],
// the first 8 hex digits of the file's SHA-256 and a timestamp, e.g.
// "coupon_01-1a2b3c4d-20261114T120000.gcode". Nothing else on the printer can
// have this name, so the file that is verified is the file that is started.
std::string remote_file_name(const std::string &local_name, const std::string &sha256, const std::string &stamp);

// A file path as Klipper or Moonraker reports it, relative to the G-code root.
std::string gcode_root_path(std::string path);

// Reply of GET /printer/objects/query?webhooks&print_stats&virtual_sdcard.
// Idle means Klipper is ready, print_stats is standby, complete or cancelled,
// the SD reader is inactive and no file is loaded.
bool printer_is_idle(const std::string &body, std::string &reason);

// Reply of GET /printer/objects/query?configfile=config. On the Seeker 3,
// T0 is [extruder] (the composite matrix + fibre nozzle) and T1 is
// [extruder1] (the polymer nozzle).
bool printer_tool_nozzles(const std::string &body, double &t0, double &t1, std::string &reason);

// Each tool the job uses must be fitted with the nozzle it was sliced for:
// material 1 on T1 (plastic_nozzle_diameter), material 2 and any fibre on T0
// (composite_nozzle_diameter).
bool tools_match(const JobInfo &job, double t0, double t1, std::string &reason);

// Reply of POST /server/files/upload sent with print=false: the file must be
// stored as exactly `remote_name` in the G-code root, with the local size, and
// neither started nor queued.
bool upload_reply_ok(const std::string &body, uint64_t local_size, const std::string &remote_name, std::string &reason);

// The printer's host name from GET /printer/info, or "" if it reports none.
std::string printer_identity(const std::string &body);

// Reply of GET /printer/objects/query?print_stats after a start request.
// Started: print_stats is "printing" this exact file. Failed: this file
// ended in "error" or "cancelled", or is "paused". Anything else: Waiting.
enum class StartState { Started, Waiting, Failed };
StartState start_state(const std::string &body, const std::string &remote_name, std::string &detail);

} // namespace Slic3r::FibreSeekSend

#endif
