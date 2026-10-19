#pragma once

#include "s376/airport.hpp"
#include "s376/config.hpp"
#include "s376/state.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace s376 {

enum class CheckStatus { Pass, Warn, Fail, Skip };

struct Check {
    std::string name;
    CheckStatus status = CheckStatus::Pass;
    std::string detail;
    std::string fix;
};

struct DoctorReport {
    std::vector<Check> checks;
    int fails = 0;
    int warns = 0;
};

struct AirportCacheSize {
    std::string icao;
    std::string path;
    std::uint64_t bytes = 0;
    std::size_t tiles = 0;
};

struct ChannelFrameTime {
    std::string name;
    double cpu_ms = 0;  // make_channel_view cost
    double gpu_ms = -1; // CompositeViewer share; <0 if not measured
};

DoctorReport run_doctor(const AppConfig& cfg, const std::string& config_path, bool listen,
                        bool bench = true);
std::string format_report(const DoctorReport& r);
std::string diagnostics_dump(const AppConfig& cfg, const HostState* host = nullptr,
                             const std::vector<std::string>& selected = {});

std::string format_byte_count(std::uint64_t n);
// Known public airports plus any extra packed dirs under `root`.
std::vector<AirportCacheSize> scan_airport_caches(const std::string& root,
                                                  const std::string& extra_dir = {});
// CPU view cost always; GPU frames only when `try_gpu` and DISPLAY is set.
std::vector<ChannelFrameTime> bench_channel_times(const AppConfig& cfg, bool try_gpu);

struct ApproachBenchSample {
    double nm = 0;
    float gpu_ms = -1;
    float cpu_ms = -1;
};

// Text table for --bench (04R sample distances + budgeted pass list).
std::string format_approach_bench(const std::vector<ApproachBenchSample>& rows, float budget_ms,
                                  const std::string& quality, const std::string& dropped);
// One pasteable line. Example:
// SUMMARY  s376 --bench quality=gtx980ti budget=12.20ms max=8.40ms@2.99nm avg=7.61ms dropped=none result=UNDER 60Hz=YES
std::string format_approach_bench_summary(const std::vector<ApproachBenchSample>& rows,
                                          float budget_ms, const std::string& quality,
                                          const std::string& dropped);
std::string format_gpu_pass_table(float measured_ms, bool shadows_on);
// Joey-pasteable GTA V stack lines (same 12.20 ms budget, named for the demo).
std::string format_gta_stack_lines(bool shadows_on);

// Eyepoint bearings for the KJFK pilot-view checklist (04R / 13L 6 nm).
std::string format_pilot_view_bearings(const Airport& apt, const ShotPose& pose);

} // namespace s376
