#include "s376/config.hpp"
#include "s376/diagnostics.hpp"
#include "s376/terrain.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

extern int g_fails;
extern int g_passes;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (cond) {                                                                                \
            ++g_passes;                                                                            \
        } else {                                                                                   \
            ++g_fails;                                                                             \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #cond << "\n";          \
        }                                                                                          \
    } while (0)
#define CHECK_NEAR(a, b, eps)                                                                      \
    do {                                                                                           \
        const double _a = static_cast<double>(a);                                                  \
        const double _b = static_cast<double>(b);                                                  \
        if (std::fabs(_a - _b) <= (eps)) {                                                         \
            ++g_passes;                                                                            \
        } else {                                                                                   \
            ++g_fails;                                                                             \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #a << "=" << _a << " "  \
                      << #b << "=" << _b << " eps=" << (eps) << "\n";                              \
        }                                                                                          \
    } while (0)

void test_diagnostics() {
    CHECK(s376::format_byte_count(0) == "0 B");
    CHECK(s376::format_byte_count(512) == "512 B");
    const auto kib = s376::format_byte_count(2048);
    CHECK(kib.find("KiB") != std::string::npos);
    const auto mib = s376::format_byte_count(2ull * 1024 * 1024);
    CHECK(mib.find("MiB") != std::string::npos);

    const auto tmp = (std::filesystem::temp_directory_path() / "s376-doctor-cache").string();
    std::filesystem::remove_all(tmp);
    auto empty = s376::scan_airport_caches(tmp, "");
    CHECK(empty.size() >= 3);
    bool saw_sea = false, saw_sfo = false, saw_las = false;
    for (const auto& s : empty) {
        if (s.icao == "KSEA") {
            saw_sea = true;
            CHECK(s.bytes == 0);
        }
        if (s.icao == "KSFO")
            saw_sfo = true;
        if (s.icao == "KLAS")
            saw_las = true;
    }
    CHECK(saw_sea && saw_sfo && saw_las);

    auto region = s376::generate_ksea_cache();
    s376::write_cache(region, tmp + "/ksea");
    std::filesystem::create_directories(tmp + "/egll");
    {
        std::ofstream extra(tmp + "/egll/elev.s376dem");
        extra << "not-a-real-dem";
    }
    auto packed = s376::scan_airport_caches(tmp, "");
    bool ksea_ok = false, egll_ok = false;
    for (const auto& s : packed) {
        if (s.icao == "KSEA") {
            ksea_ok = s.bytes > 100;
            auto g = std::filesystem::path(s.path).generic_string();
            std::transform(g.begin(), g.end(), g.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            CHECK(g.find("ksea") != std::string::npos);
        }
        if (s.icao == "EGLL")
            egll_ok = s.bytes > 0;
    }
    CHECK(ksea_ok);
    CHECK(egll_ok);
    {
        auto packed_cfg = s376::default_config();
        packed_cfg.render.cache_dir = tmp + "/ksea";
        auto packed_rep = s376::run_doctor(packed_cfg, "", false, false);
        const auto packed_text = s376::format_report(packed_rep);
        CHECK(packed_text.find("cache-tiles") != std::string::npos);
        CHECK(packed_text.find("rss") != std::string::npos);
        CHECK(packed_text.find("paged mesh") != std::string::npos);
    }
    std::filesystem::remove_all(tmp);

    auto cfg = s376::default_config();
    auto times = s376::bench_channel_times(cfg, false);
    CHECK(times.size() == 4);
    bool fl = false, ml = false, mr = false, fr = false;
    for (const auto& t : times) {
        CHECK(t.cpu_ms >= 0);
        CHECK(t.gpu_ms < 0);
        if (t.name == "far_left")
            fl = true;
        if (t.name == "mid_left")
            ml = true;
        if (t.name == "mid_right")
            mr = true;
        if (t.name == "far_right")
            fr = true;
    }
    CHECK(fl && ml && mr && fr);
    {
        std::vector<s376::ApproachBenchSample> rows = {{8.0, 7.2f, 2.1f}, {2.99, 8.4f, 2.3f}};
        const auto table = s376::format_approach_bench(rows, 12.20f, "gtx980ti", "ssao");
        CHECK(table.find("2.99") != std::string::npos);
        CHECK(table.find("gtx980ti") != std::string::npos);
        CHECK(table.find("dropped") != std::string::npos);
        const auto summary = s376::format_approach_bench_summary(rows, 12.20f, "gtx980ti", "ssao");
        CHECK(summary.find("SUMMARY  s376 --bench") != std::string::npos);
        CHECK(summary.find("quality=gtx980ti") != std::string::npos);
        CHECK(summary.find("budget=12.20ms") != std::string::npos);
        CHECK(summary.find("max=8.40ms@2.99nm") != std::string::npos);
        CHECK(summary.find("min=7.20ms@8.00nm") != std::string::npos);
        CHECK(summary.find("dropped=ssao") != std::string::npos);
        CHECK(summary.find("result=UNDER") != std::string::npos);
        CHECK(summary.find("60Hz=YES") != std::string::npos);
        CHECK(summary.find('\n') != std::string::npos);
        std::vector<s376::ApproachBenchSample> over = {{1.0, 18.0f, 3.0f}};
        const auto over_s = s376::format_approach_bench_summary(over, 12.20f, "gtx980ti", "");
        CHECK(over_s.find("result=OVER") != std::string::npos);
        CHECK(over_s.find("60Hz=NO") != std::string::npos);
        CHECK(over_s.find("dropped=none") != std::string::npos);
        const auto nogl = s376::format_approach_bench_summary({}, 12.20f, "gtx980ti", "");
        CHECK(nogl.find("result=NO_GL") != std::string::npos);
        CHECK(nogl.find("60Hz=SKIP") != std::string::npos);
        const auto passes = s376::format_gpu_pass_table(8.4f, false);
        CHECK(passes.find("atmosphere") != std::string::npos);
        CHECK(passes.find("total") != std::string::npos);
        CHECK(passes.find("estimate") != std::string::npos);
        CHECK(passes.find("gpu_timer") != std::string::npos);
        CHECK(passes.find("ESTIMATE") != std::string::npos);
        CHECK(passes.find("\tshadows\t") == std::string::npos);
        CHECK(passes.find("gta") != std::string::npos);
        CHECK(passes.find("csm_shadows") != std::string::npos);
        CHECK(passes.find("hdr_bloom") != std::string::npos);
        CHECK(passes.find("wet_runway") != std::string::npos);
        CHECK(passes.find("haze") != std::string::npos);
        CHECK(passes.find("stable_aa") != std::string::npos);
        const auto gta_on = s376::format_gta_stack_lines(true);
        CHECK(gta_on.find("csm_shadows") != std::string::npos);
        CHECK(gta_on.find("0.85") != std::string::npos);
        const auto gta_off = s376::format_gta_stack_lines(false);
        CHECK(gta_off.find("csm_shadows") != std::string::npos);
        CHECK(gta_off.find("0.00") != std::string::npos);
    }

    const char* old_dpy = std::getenv("DISPLAY");
    std::string saved_dpy = old_dpy ? old_dpy : "";
#ifdef _WIN32
    _putenv_s("DISPLAY", "");
#else
    unsetenv("DISPLAY");
#endif
    auto report = s376::run_doctor(cfg, "", false, true);
    if (!saved_dpy.empty()) {
#ifdef _WIN32
        _putenv_s("DISPLAY", saved_dpy.c_str());
#else
        setenv("DISPLAY", saved_dpy.c_str(), 1);
#endif
    }
    const auto text = s376::format_report(report);
    CHECK(text.find("cache-ksea") != std::string::npos);
    CHECK(text.find("cache-ksfo") != std::string::npos);
    CHECK(text.find("cache-klas") != std::string::npos);
    CHECK(text.find("mesh-build") != std::string::npos);
    CHECK(text.find("frame-far_left") != std::string::npos);
    CHECK(text.find("frame-mid_left") != std::string::npos);
    CHECK(text.find("frame-mid_right") != std::string::npos);
    CHECK(text.find("frame-far_right") != std::string::npos);
    CHECK(text.find("rss") != std::string::npos);
    CHECK(text.find("budget") != std::string::npos);
    CHECK(text.find("vs FAA 2026-10-01") != std::string::npos);
    CHECK(text.find("vs DFS 2026-10-02") != std::string::npos);
    CHECK(text.find("vs NATS 2025-04-17") != std::string::npos);
    CHECK(text.find("vs ENAIRE 2026-10-02") != std::string::npos);
    CHECK(text.find("vs DGAC Ecuador 2026-10-02") != std::string::npos);
    CHECK(text.find("vs Aerocivil 2016-01-01") != std::string::npos);
    CHECK(text.find("vs Aerocivil 2024-11-28") != std::string::npos);
    CHECK(text.find("vs CAAN 2024-01-01") != std::string::npos);
    CHECK(text.find("vs FAA 2016") == std::string::npos);
    CHECK(text.find("vs FAA 2024") == std::string::npos);
    CHECK(text.find("aip-stale-skbo") != std::string::npos);
    CHECK(text.find("aip-stale-vnkt") != std::string::npos);
    CHECK(text.find("aip-stale-skrg") == std::string::npos);
    CHECK(report.fails == 0);
    CHECK(report.warns >= 2);
    CHECK(s376::current_rss_bytes() > 0);
    CHECK(s376::peak_rss_bytes() >= s376::current_rss_bytes());
    CHECK(s376::peak_rss_bytes() < (2ull << 30));

    {
        s376::HostState shot;
        shot.have_eyepoint = true;
        shot.eyepoints = 1;
        shot.datagrams = 0;
        shot.eye.alt_ft = 12000;
        shot.have_ownship = true;
        shot.ownship.gear = 1;
        shot.ownship.flaps = 30;
        const auto dump = s376::diagnostics_dump(cfg, &shot, {"mid_left"});
        CHECK(dump.find("selected=mid_left") != std::string::npos);
        CHECK(dump.find("channel mid_left") != std::string::npos);
        CHECK(dump.find("channel far_right") == std::string::npos);
        CHECK(dump.find("eyepoints=1") != std::string::npos);
        CHECK(dump.find("alt_ft=12000") != std::string::npos);
        CHECK(dump.find("ownship gear=1 flaps=30") != std::string::npos);
        CHECK(dump.find("host.reply_port=2301") != std::string::npos);
    }
}
