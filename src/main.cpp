#include "s376/airport.hpp"
#include "s376/config.hpp"
#include "s376/control.hpp"
#include "s376/diagnostics.hpp"
#include "s376/fakehost.hpp"
#include "s376/host.hpp"
#include "s376/landmarks.hpp"
#include "s376/missiles.hpp"
#include "s376/image.hpp"
#include "s376/log.hpp"
#include "s376/render.hpp"
#include "s376/safety.hpp"
#include "s376/synth.hpp"
#include "s376/terrain.hpp"
#include "s376/version.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_stop{false};

void on_sig(int) {
    g_stop = true;
}

void usage() {
    std::cerr
        << "s376-visual " << s376::version()
        << " — S376 image generator\n"
           "usage: s376-visual [options]\n"
           "  -c, --config PATH     TOML config (default config/s376-visual.toml if present)\n"
           "  --bind HOST:PORT      override host.bind (default 0.0.0.0:2300)\n"
           "  --channel NAME        far_left|mid_left|mid_right|far_right|all|strip|wrap\n"
           "                        aliases: left|front|right|front_fo\n"
           "                        (default: all enabled, or $S376_CHANNEL)\n"
           "                        --channel all|strip --record/--screenshot writes a\n"
           "                        4-channel 50°×32.5° strip (far_left|mid_left|mid_right|far_right)\n"
           "                        --channel wrap: unique Left|Front|Right (drops FO front)\n"
           "  --cache-dir PATH      packed DEM/imagery from s376-cache\n"
           "  --headless            run the host receiver with no GL window\n"
           "  --demo [kjfk]         bare --demo: in-process synth to 127.0.0.1 (CI)\n"
           "                        --demo kjfk: fakehost loopback, six-mile-final, 6 nm\n"
           "  --rwy ID              --demo kjfk runway (04R default, 13L)\n"
           "  --wet                 --demo / shot: runway condition wet (same as --rwy-cond 2)\n"
           "  --six-mile-final      --demo kjfk: gear down / flaps 30 from frame 1 (default)\n"
           "  --start DIST          --demo kjfk start: 6nm (default), 6sm, 10nm, 11km\n"
           "  --reply-port PORT     type-200 HAT reply port (default host.reply_port=2301)\n"
           "  --mem-budget MB       CPU resident cap (overrides render.ram_mb; 980 Ti=2048)\n"
           "  --demo-weather        demo plus 124/125/126/127/128 weather and a storm cell\n"
           "  --duration SEC        exit after SEC seconds (tests)\n"
           "  -v, --verbose         console DEBUG\n"
           "  --debug               packet dumps + DEBUG\n"
           "  --gl-debug            OSG per-attribute GL error checks (off in Release)\n"
           "  --quality NAME        gtx980ti | rtx | custom\n"
           "  --bench               fly 04R (or --approach) and print per-pass GPU ms\n"
           "                        (GL_TIME_ELAPSED). Ends with a SUMMARY line Joey can paste.\n"
           "                        gtx980ti auto-drops extras if over budget\n"
           "  --offscreen           pbuffer first (no projector window). Shots still use the FBO\n"
           "  --reply-master NAME   only this --channel sends the type-200 HAT reply\n"
           "  --no-hat              never send HAT (overrides reply_master)\n"
           "  --screenshot PATH     write the first channel (PPM+PNG) after warmup and exit\n"
           "                        (--channel all: Left|Front|Right strip)\n"
           "  --screenshot-wait SEC settle time before capture (default 3; pager idle or this)\n"
           "  --self-test-shot DIR  render a known solid/gradient, check pixels, write files\n"
           "  --approach ICAO:RWY   scripted straight glidepath to touchdown (KJFK:04R)\n"
           "  --start-agl FT        approach start AGL (default 2000)\n"
           "  --gs DEG              approach glidepath degrees (default 3.0)\n"
           "  --kts KT              approach groundspeed (default 140)\n"
           "  --fps N               record frame rate (default 30)\n"
           "  --record PATH         offscreen FBO flight to PATH (.mp4 via ffmpeg, or PNG dir)\n"
           "                        --channel all: stitched Left|Front|Right strip\n"
           "  --at ICAO[:RWY[:nm]]  pose the eye (KJFK:04R:0 lined-up, KJFK:04R:8, or KJFK)\n"
           "                        does not wait for host packets\n"
           "  --alt FT              override shot altitude (MSL feet)\n"
           "  --bank DEG            override shot roll (right-wing-down)\n"
           "  --hdg DEG             override shot true heading\n"
           "  --pitch DEG           override shot pitch\n"
           "  --tod HOURS           shot time-of-day 0..24 (host type 124; default 14.5)\n"
           "                        visual sun: 8–16 day, 16–20 dusk, 20–5 night, 5–8 dawn\n"
           "  --solar-astronomy     date/lon sun_ecef instead of visual TOD buckets\n"
           "  --rwy-cond N          shot runway condition 0 dry / 1 damp / 2 wet / ≥3 flooded\n"
           "  --era current|classic session-only NYC skyline easter egg (not saved)\n"
           "  --control HOST:PORT   local operator control (default 127.0.0.1:2376; not xT4)\n"
           "  --no-control          do not listen for s376-wizard\n"
           "  --diagnostics         print a dump and exit\n"
           "  --doctor              environment checks and exit\n"
           "  --version             print version\n"
           "  -h, --help\n";
}

s376::HostState host_from_shot(const s376::ShotPose& p) {
    s376::HostState h;
    h.have_eyepoint = true;
    h.eye.csn = s376::xt4::kEyepointCsn;
    h.eye.minor_csn = -1;
    h.eye.lat_deg = p.lat;
    h.eye.lon_deg = p.lon;
    h.eye.alt_ft = p.alt_ft;
    h.eye.heading_deg = static_cast<float>(-p.hdg_true_deg);
    h.eye.pitch_deg = static_cast<float>(p.pitch_deg);
    h.eye.roll_deg = static_cast<float>(p.roll_deg);
    h.eyepoints = 1;
    h.datagrams = 0;
    h.last_peer = "shot";
    h.have_env = true;
    h.env.tod_select = -1;
    h.env.tod_hours = 14.5f;
    h.env.visible_range_ft = 528000.f;
    h.env.month = 6;
    h.env.day = 21;
    h.env.year = 2024;
    h.light_steps.fill(3);
    const s376::Airport apt = s376::airport_by_icao(p.icao);
    const float elev_ft = static_cast<float>(apt.elev_m * s376::geo::kMToFt);
    h.terrain_elev_ft = elev_ft;
    h.hat_ft = static_cast<float>(p.alt_ft) - elev_ft;
    h.have_hat = true;
    return h;
}

} // namespace

int main(int argc, char** argv) {
    std::string config_path;
    std::string bind_override;
    std::string channel_name;
    std::string cache_override;
    bool headless = false;
    bool demo = false;
    bool demo_kjfk = false;
    bool demo_weather = false;
    std::string demo_preset;
    bool verbose = false;
    bool debug = false;
    bool gl_debug = false;
    bool do_diag = false;
    bool do_doctor = false;
    double duration = -1;
    std::string quality_override;
    std::string screenshot_path;
    std::string self_test_shot;
    double screenshot_wait = 3.0;
    std::string at_spec;
    std::string approach_spec;
    std::string record_path;
    double start_agl_ft = 2000.0;
    double gs_deg = 3.0;
    double kts = 140.0;
    double record_fps = 30.0;
    s376::ShotOverrides shot_over;
    std::string reply_master_override;
    int reply_port_override = -1;
    bool no_hat = false;
    bool six_mile = false;
    bool six_mile_set = false;
    std::string start_dist;
    std::string era_override;
    std::string control_override;
    bool no_control = false;
    float shot_tod = 14.5f;
    bool have_shot_tod = false;
    bool solar_astronomy = false;
    int shot_rwy_cond = 0;
    bool have_shot_rwy = false;
    std::string rwy_override;
    bool do_bench = false;
    bool offscreen = false;
    int mem_budget_mb = -1;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto need = [&](const char* flag) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << flag << " needs an argument\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "-h" || a == "--help") {
            usage();
            return 0;
        }
        if (a == "--version") {
            std::cout << s376::version() << "\n";
            return 0;
        }
        if (a == "-c" || a == "--config")
            config_path = need(a.c_str());
        else if (a == "--bind")
            bind_override = need(a.c_str());
        else if (a == "--channel")
            channel_name = need(a.c_str());
        else if (a == "--cache-dir")
            cache_override = need(a.c_str());
        else if (a == "--headless")
            headless = true;
        else if (a == "--demo") {
            demo = true;
            if (i + 1 < argc && argv[i + 1][0] != '-')
                demo_preset = argv[++i];
        } else if (a.rfind("--demo=", 0) == 0) {
            demo = true;
            demo_preset = a.substr(7);
        } else if (a == "--demo-weather") {
            demo = true;
            demo_weather = true;
        }
        else if (a == "--duration")
            duration = std::stod(need(a.c_str()));
        else if (a == "-v" || a == "--verbose")
            verbose = true;
        else if (a == "--debug")
            debug = true;
        else if (a == "--gl-debug")
            gl_debug = true;
        else if (a == "--quality")
            quality_override = need(a.c_str());
        else if (a == "--reply-master")
            reply_master_override = need(a.c_str());
        else if (a == "--reply-port")
            reply_port_override = std::stoi(need(a.c_str()));
        else if (a == "--six-mile-final") {
            six_mile = true;
            six_mile_set = true;
        } else if (a == "--no-six-mile-final") {
            six_mile = false;
            six_mile_set = true;
        } else if (a == "--start")
            start_dist = need(a.c_str());
        else if (a == "--no-hat")
            no_hat = true;
        else if (a == "--era")
            era_override = need(a.c_str());
        else if (a == "--control")
            control_override = need(a.c_str());
        else if (a == "--no-control")
            no_control = true;
        else if (a == "--screenshot")
            screenshot_path = need(a.c_str());
        else if (a == "--screenshot-wait")
            screenshot_wait = std::stod(need(a.c_str()));
        else if (a == "--self-test-shot")
            self_test_shot = need(a.c_str());
        else if (a == "--at")
            at_spec = need(a.c_str());
        else if (a == "--approach")
            approach_spec = need(a.c_str());
        else if (a == "--start-agl")
            start_agl_ft = std::stod(need(a.c_str()));
        else if (a == "--gs")
            gs_deg = std::stod(need(a.c_str()));
        else if (a == "--kts")
            kts = std::stod(need(a.c_str()));
        else if (a == "--fps")
            record_fps = std::stod(need(a.c_str()));
        else if (a == "--record")
            record_path = need(a.c_str());
        else if (a == "--alt") {
            shot_over.have_alt = true;
            shot_over.alt_ft = std::stod(need(a.c_str()));
        } else if (a == "--bank") {
            shot_over.have_bank = true;
            shot_over.bank_deg = std::stod(need(a.c_str()));
        } else if (a == "--hdg") {
            shot_over.have_hdg = true;
            shot_over.hdg_deg = std::stod(need(a.c_str()));
        } else if (a == "--pitch") {
            shot_over.have_pitch = true;
            shot_over.pitch_deg = std::stod(need(a.c_str()));
        } else if (a == "--tod") {
            have_shot_tod = true;
            shot_tod = std::stof(need(a.c_str()));
        } else if (a == "--solar-astronomy") {
            solar_astronomy = true;
        } else if (a == "--rwy") {
            rwy_override = need(a.c_str());
        } else if (a == "--wet") {
            have_shot_rwy = true;
            shot_rwy_cond = 2;
        } else if (a == "--rwy-cond") {
            have_shot_rwy = true;
            shot_rwy_cond = std::stoi(need(a.c_str()));
        } else if (a == "--diagnostics")
            do_diag = true;
        else if (a == "--doctor")
            do_doctor = true;
        else if (a == "--bench")
            do_bench = true;
        else if (a == "--offscreen")
            offscreen = true;
        else if (a == "--mem-budget")
            mem_budget_mb = std::stoi(need(a.c_str()));
        else {
            std::cerr << "unknown option " << a << "\n";
            usage();
            return 2;
        }
    }

    s376::AppConfig cfg = s376::default_config();
    if (config_path.empty()) {
        std::ifstream probe("config/s376-visual.toml");
        if (probe)
            config_path = "config/s376-visual.toml";
    }
    if (!config_path.empty()) {
        try {
            cfg = s376::load_config(config_path);
        } catch (const std::exception& ex) {
            std::cerr << ex.what() << "\n";
            return 2;
        }
    }
    if (!bind_override.empty())
        cfg.host.bind = bind_override;
    if (!cache_override.empty())
        cfg.render.cache_dir = cache_override;
    if (verbose)
        cfg.log.verbose = true;
    if (debug)
        cfg.log.debug = true;
    if (gl_debug)
        cfg.render.gl_debug = true;
    if (!quality_override.empty()) {
        if (quality_override != "gtx980ti" && quality_override != "rtx" &&
            quality_override != "custom") {
            std::cerr << "--quality must be gtx980ti|rtx|custom\n";
            return 2;
        }
        cfg.render.quality = quality_override;
        if (quality_override != "custom")
            s376::apply_quality_preset(cfg.render, quality_override);
    }
    if (solar_astronomy)
        cfg.render.solar_astronomy = true;
    if (offscreen)
        cfg.render.offscreen = true;
    if (mem_budget_mb >= 256)
        cfg.render.ram_mb = mem_budget_mb;
    if (!reply_master_override.empty())
        cfg.host.reply_master = reply_master_override;
    if (reply_port_override > 0 && reply_port_override < 65536)
        cfg.host.reply_port = static_cast<std::uint16_t>(reply_port_override);
    if (no_hat)
        cfg.host.hat_responder = false;
    if (!era_override.empty()) {
        try {
            const auto e = s376::parse_skyline_era(era_override);
            s376::set_session_era(e, e == s376::SkylineEra::Classic);
        } catch (const std::exception& ex) {
            std::cerr << ex.what() << "\n";
            return 2;
        }
    }
    if (channel_name.empty()) {
        if (const char* env_ch = std::getenv("S376_CHANNEL"))
            channel_name = env_ch;
    }
    if (!control_override.empty())
        cfg.control.bind = control_override;
    if (no_control)
        cfg.control.enabled = false;
    if (!demo_preset.empty()) {
        std::string p = demo_preset;
        for (char& c : p)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (p == "KJFK" || p == "JFK")
            demo_kjfk = true;
        else {
            std::cerr << "--demo preset must be kjfk (bare --demo is the synth CI path)\n";
            return 2;
        }
    }
    if (demo_kjfk) {
        demo = true;
        if (!rwy_override.empty() && approach_spec.empty()) {
            std::string rw = rwy_override;
            for (char& c : rw)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            approach_spec = "KJFK:" + rw;
        }
        if (approach_spec.empty() && at_spec.empty())
            approach_spec = "KJFK:04R";
        if (quality_override.empty()) {
            quality_override = "gtx980ti";
            cfg.render.quality = "gtx980ti";
            s376::apply_quality_preset(cfg.render, "gtx980ti");
        }
        if (channel_name.empty())
            channel_name = "mid_left";
        if (!have_shot_tod) {
            have_shot_tod = true;
            shot_tod = 12.f;
        }
        cfg.render.frame_rate = 60.0;
        cfg.render.sync_to_host = true;
        if (record_fps == 30.0)
            record_fps = 60.0;
        if (!six_mile_set)
            six_mile = true;
        if (start_dist.empty())
            start_dist = "6nm";
        if (bind_override.empty())
            cfg.host.bind = "127.0.0.1:2300";
        cfg.host.allow_sim_lan = false;
        cfg.host.allow_lan = false;
        if (mem_budget_mb >= 256)
            cfg.render.ram_mb = mem_budget_mb;
    }
    double demo_start_nm = 6.0;
    if (demo_kjfk) {
        try {
            demo_start_nm = s376::fakehost::parse_distance_nm(start_dist);
        } catch (const std::exception& ex) {
            std::cerr << "--start: " << ex.what() << "\n";
            return 2;
        }
        if (demo_start_nm < 0.2 || demo_start_nm > 30.0) {
            std::cerr << "--start must be 0.2..30 nm\n";
            return 2;
        }
    }
    if (demo && !demo_kjfk) {
        cfg.host.bind = "127.0.0.1:2300";
        cfg.host.allow_sim_lan = false;
        cfg.host.allow_lan = false;
    }

    s376::log::Options lo;
    lo.verbose = cfg.log.verbose;
    lo.debug = cfg.log.debug;
    lo.log_dir = cfg.log.dir;
    s376::log::setup(lo, "visual");

    if (do_doctor) {
        auto rep = s376::run_doctor(cfg, config_path, true);
        std::cout << s376::format_report(rep);
        s376::log::shutdown();
        return rep.fails ? 1 : 0;
    }
    if (do_diag) {
        const auto chans = s376::select_channels(cfg, channel_name);
        std::vector<std::string> names;
        for (const auto& ch : chans)
            names.push_back(ch.name);
        std::cout << s376::diagnostics_dump(cfg, nullptr, names);
        s376::log::shutdown();
        return 0;
    }

    if (do_bench && approach_spec.empty() && at_spec.empty())
        approach_spec = "KJFK:04R";
    if (!approach_spec.empty() && !at_spec.empty()) {
        std::cerr << "use --approach or --at, not both\n";
        s376::log::shutdown();
        return 2;
    }
    if (!record_path.empty() || !screenshot_path.empty()) {
        cfg.render.msaa = 0;
        cfg.render.taa = false;
    }
    if (!record_path.empty() && approach_spec.empty()) {
        std::cerr << "--record needs --approach ICAO:RWY\n";
        s376::log::shutdown();
        return 2;
    }
    if (kts < 30.0 || kts > 400.0) {
        std::cerr << "--kts must be 30..400\n";
        s376::log::shutdown();
        return 2;
    }
    if (record_fps < 1.0 || record_fps > 120.0) {
        std::cerr << "--fps must be 1..120\n";
        s376::log::shutdown();
        return 2;
    }

    s376::ShotPose shot;
    s376::HostState shot_host;
    s376::ApproachPlan approach;
    bool have_shot = !at_spec.empty();
    if (!approach_spec.empty()) {
        try {
            if (!s376::plan_approach(approach_spec, gs_deg, start_agl_ft, approach)) {
                std::cerr << "bad --approach " << approach_spec << " (need ICAO:RWY, --gs 0.25..10, "
                             "--start-agl 20..20000)\n";
                s376::log::shutdown();
                return 2;
            }
        } catch (const std::exception& ex) {
            std::cerr << ex.what() << "\n";
            s376::log::shutdown();
            return 2;
        }
        approach.kts = kts;
        approach.fps = record_fps;
        shot = s376::approach_sample(approach, 0);
        s376::apply_shot_overrides(shot, shot_over);
        have_shot = true;
    } else if (have_shot) {
        try {
            shot = s376::parse_shot_at(at_spec);
            s376::apply_shot_overrides(shot, shot_over);
        } catch (const std::exception& ex) {
            std::cerr << ex.what() << "\n";
            s376::log::shutdown();
            return 2;
        }
    }
    auto apply_shot_env = [&](s376::HostState& h) {
        if (have_shot_tod) {
            h.env.tod_hours = shot_tod;
            h.env.tod_select = -1;
        }
        if (have_shot_rwy)
            h.env.runway_condition = shot_rwy_cond;
    };
    if (have_shot) {
        shot_host = host_from_shot(shot);
        apply_shot_env(shot_host);
        if (!screenshot_path.empty())
            shot_host.script_dt = 1.f / 60.f;
        S376_LOG_INFO("ig",
                      "shot %s:%s at %.4f,%.4f  %.0fft  hdg %.1f  pitch %.1f  bank %.1f  %.2fnm",
                      shot.icao.c_str(), shot.rwy.empty() ? "-" : shot.rwy.c_str(), shot.lat,
                      shot.lon, shot.alt_ft, shot.hdg_true_deg, shot.pitch_deg, shot.roll_deg,
                      shot.dist_nm);
        if (!approach_spec.empty()) {
            S376_LOG_INFO("ig",
                          "approach %s:%s  gs %.2f  start-agl %.0f  %.0f kts  %.0f fps  "
                          "%.2f nm  %.1fs  %d frames",
                          approach.icao.c_str(), approach.rwy.c_str(), approach.gs_deg,
                          approach.start_agl_ft, approach.kts, approach.fps,
                          approach.start_dist_m / 1852.0, s376::approach_time_s(approach),
                          s376::approach_frame_count(approach));
        }
        if (cfg.render.cache_dir.empty()) {
            const auto found = s376::resolve_cache_dir({}, shot.icao);
            if (!found.empty()) {
                cfg.render.cache_dir = found;
                S376_LOG_INFO("ig", "auto cache_dir %s (from --at %s)", found.c_str(),
                              shot.icao.c_str());
            } else {
                S376_LOG_WARN("ig",
                              "no cache/%s — renderer will use a synth KSEA DEM. "
                              "Run `s376-cache fetch %s` or pass --cache-dir",
                              shot.icao.c_str(), shot.icao.c_str());
            }
        }
    }
    if (!screenshot_path.empty() && !have_shot && self_test_shot.empty()) {
        std::cerr << "--screenshot needs --at ICAO[:RWY[:dist_nm]] or --approach ICAO:RWY\n";
        s376::log::shutdown();
        return 2;
    }
    if ((!screenshot_path.empty() || !record_path.empty()) && channel_name.empty())
        channel_name = s376::default_shot_channel(cfg);
    if (!screenshot_path.empty()) {
        const auto parent = std::filesystem::path(screenshot_path).parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent);
    }
    if (!self_test_shot.empty())
        std::filesystem::create_directories(self_test_shot);

    auto channels = s376::select_channels(cfg, channel_name == "strip" || channel_name == "wrap"
                                                   ? "all"
                                                   : channel_name);
    const bool capture_mode = !screenshot_path.empty() || !record_path.empty();
    if (capture_mode &&
        (channel_name == "all" || channel_name == "strip" || channel_name == "wrap")) {
        const bool unique = channel_name == "wrap";
        auto strip = unique ? s376::wrap_channels(s376::select_channels(cfg, "all"))
                            : s376::strip4_channels(s376::select_channels(cfg, "all"));
        if (strip.size() >= 2) {
            channels = std::move(strip);
            int x = 0;
            for (auto& ch : channels) {
                ch.window_x = x;
                ch.window_y = 0;
                x += std::max(8, ch.width);
            }
            int sw = 0, sh = 0;
            s376::wrap_strip_size(channels, sw, sh);
            S376_LOG_INFO("ig", "strip %s  %zu ch  %dx%d",
                          unique ? "Left|Front|Right" : "L|C|Cfo|R 50x32.5", channels.size(), sw,
                          sh);
        }
    }
    if (channels.empty()) {
        S376_LOG_ERROR("ig", "no channel matched '%s'",
                       channel_name.empty() ? "all" : channel_name.c_str());
        return 2;
    }
    const bool hat_on = s376::hat_reply_enabled(cfg.host, channels);
    cfg.host.hat_responder = hat_on;
    std::vector<std::string> selected_names;
    for (const auto& ch : channels) {
        selected_names.push_back(ch.name);
        S376_LOG_INFO("ig", "selected %s  hdg=%.3f  %dx%d  fov=%.1fx%.1f%s", ch.name.c_str(),
                      ch.heading_offset_deg, ch.width, ch.height, ch.hfov_deg, ch.vfov_deg,
                      ch.flip_vertical ? "  flip" : "");
    }
    if (channels.size() > 1) {
        S376_LOG_INFO("ig",
                      "opening %zu channels on one process (laptop/desk). "
                      "Production is one projector PC: --channel far_left|mid_left|mid_right|far_right",
                      channels.size());
    }
    S376_LOG_INFO("ig", "HAT reply %s  master='%s'  this=%s", hat_on ? "on" : "off",
                  cfg.host.reply_master.c_str(),
                  channels.size() == 1 ? channels.front().name.c_str() : "all");

    s376::control_hub().attach(cfg, channels);
    s376::ControlServer control;
    if (cfg.control.enabled && !cfg.control.bind.empty()) {
        try {
            control.start(cfg.control.bind, cfg.host.allow_sim_lan, cfg.host.allow_lan);
        } catch (const std::exception& ex) {
            S376_LOG_WARN("wizard", "control not listening: %s", ex.what());
        }
    }

    if (!self_test_shot.empty()) {
        std::string err;
        if (!s376::self_test_shot_files(self_test_shot, err)) {
            S376_LOG_ERROR("ig", "self-test-shot failed: %s", err.c_str());
            s376::log::shutdown();
            return 1;
        }
        S376_LOG_INFO("ig", "self-test-shot CPU ok  %s", self_test_shot.c_str());
        if (headless) {
            const s376::HostState* hp = have_shot ? &shot_host : nullptr;
            S376_LOG_INFO("ig", "%s", s376::diagnostics_dump(cfg, hp, selected_names).c_str());
            s376::log::shutdown();
            return 0;
        }
    }

    std::signal(SIGINT, on_sig);
    std::signal(SIGTERM, on_sig);

    s376::SharedState state;
    s376::HostReceiver rx(state, cfg.host);
    auto dem = s376::load_or_synth_dem(cfg.render.cache_dir);
    auto heights = std::make_shared<s376::DemHeightSource>(dem);
    rx.set_heights(heights);
    s376::PttWatch ptt;
    S376_LOG_INFO("ig", "HAT DEM %dx%d  %.3f,%.3f .. %.3f,%.3f", dem->w, dem->h, dem->west,
                  dem->south, dem->east, dem->north);
    std::thread synth_th;
    // Bare --demo and --demo kjfk both open a loopback host socket.
    // --at / --screenshot / --self-test-shot pose the eye themselves.
    const bool live_host =
        (demo && record_path.empty()) ||
        (!have_shot && screenshot_path.empty() && self_test_shot.empty() && record_path.empty());
    if (have_shot && !live_host)
        S376_LOG_INFO("ig", "--at: no host socket (datagrams not required)");
    if (live_host) {
        try {
            rx.start();
        } catch (const std::exception& ex) {
            S376_LOG_ERROR("ig", "host socket: %s", ex.what());
            s376::log::shutdown();
            return 1;
        }
    }

    s376::SynthConfig demo_cfg;
    demo_cfg.weather = demo_weather;
    const double demo_duration = duration < 0 ? 3600.0 : duration;
    if (demo_kjfk && have_shot) {
        S376_LOG_INFO("ig",
                      "demo kjfk: fakehost %s:%s  start=%.2fnm  six-mile=%s  tod=%.1f  "
                      "quality=%s  dest=%s  hat=%u  cache=%s",
                      approach.icao.empty() ? "KJFK" : approach.icao.c_str(),
                      approach.rwy.empty() ? "04R" : approach.rwy.c_str(), demo_start_nm,
                      six_mile ? "on" : "off", shot_tod, cfg.render.quality.c_str(),
                      cfg.host.bind.c_str(), static_cast<unsigned>(cfg.host.reply_port),
                      cfg.render.cache_dir.empty() ? "(auto/synth)" : cfg.render.cache_dir.c_str());
    }
    if (demo_kjfk && live_host) {
        std::string dest = cfg.host.bind;
        if (dest.rfind("0.0.0.0:", 0) == 0)
            dest = "127.0.0.1:" + dest.substr(8);
        const std::string listen = "127.0.0.1:" + std::to_string(cfg.host.reply_port);
        const double fps = std::max(1.0, cfg.render.frame_rate);
        s376::fakehost::ProfileConfig pc;
        try {
            pc.rwy = s376::fakehost::runway_from_faa(
                approach.icao.empty() ? "KJFK" : approach.icao,
                approach.rwy.empty() ? "04R" : approach.rwy);
        } catch (const std::exception& ex) {
            S376_LOG_ERROR("ig", "fakehost runway: %s", ex.what());
            s376::log::shutdown();
            return 2;
        }
        pc.start_nm = demo_start_nm;
        pc.gs_deg = gs_deg;
        pc.landing_config_at_start = six_mile;
        s376::fakehost::Environment env;
        env.tod_hours = have_shot_tod ? shot_tod : 12.f;
        S376_LOG_INFO("ig", "demo kjfk: fakehost -> %s  listen %s  for %.1fs", dest.c_str(),
                      listen.c_str(), demo_duration);
        synth_th = std::thread([dest, listen, pc, env, fps, demo_duration] {
            try {
                s376::fakehost::run_approach_sender(dest, listen, pc, env, fps, demo_duration,
                                                    &g_stop);
            } catch (const std::exception& ex) {
                S376_LOG_ERROR("fakehost", "%s", ex.what());
            }
        });
    } else if (demo && live_host) {
        S376_LOG_INFO("ig", "demo: in-process synth -> 127.0.0.1:2300 for %.1fs", demo_duration);
        synth_th = std::thread([demo_cfg, demo_duration] {
            try {
                s376::run_synth_sender("127.0.0.1:2300", demo_cfg, demo_duration, false, false,
                                       &g_stop);
            } catch (const std::exception& ex) {
                S376_LOG_ERROR("synth", "%s", ex.what());
            }
        });
    }

    std::unique_ptr<s376::Renderer> rend;
    int rc = 0;
    if (headless && record_path.empty() && screenshot_path.empty() && !do_bench)
        rend = std::make_unique<s376::NullRenderer>();
    else if (headless && do_bench)
        rend = std::make_unique<s376::NullRenderer>();
    else
        rend = s376::make_renderer(cfg.render.engine);
    if (!rend->begin(cfg, channels)) {
        S376_LOG_ERROR("ig", "renderer %s failed to start", rend->name().c_str());
        g_stop = true;
        rc = 1;
    } else {
        S376_LOG_INFO("ig", "renderer=%s views=%zu quality=%s", rend->name().c_str(),
                      rend->view_count(), cfg.render.quality.c_str());
    }

    if (rc == 0 && do_bench) {
        const std::string icao = approach.icao.empty() ? "KJFK" : approach.icao;
        const std::string rwy = approach.rwy.empty() ? "04R" : approach.rwy;
        const s376::Airport apt = s376::airport_by_icao(icao);
        const double nms[] = {8.0, 4.0, 2.99, 1.0, 0.6, 0.15, 0.0};
        std::vector<s376::ApproachBenchSample> rows;
        for (double nm : nms) {
            s376::ShotPose pose;
            if (!s376::runway_shot_pose(apt, rwy, nm, pose))
                continue;
            s376::apply_shot_overrides(pose, shot_over);
            shot_host = host_from_shot(pose);
            apply_shot_env(shot_host);
            for (int warm = 0; warm < 4 && !rend->done(); ++warm)
                rend->frame(shot_host);
            rend->frame(shot_host);
            const float gpu = rend->measure_gpu_ms();
            s376::ApproachBenchSample s;
            s.nm = nm;
            s.gpu_ms = gpu;
            s.cpu_ms = rend->last_cpu_ms();
            rows.push_back(s);
        }
        std::cout << s376::format_approach_bench(rows, cfg.render.gpu_budget_ms, cfg.render.quality,
                                                 rend->autodrop_log());
        float last = rows.empty() ? -1.f : rows.back().gpu_ms;
        for (const auto& s : rows) {
            if (s.gpu_ms > 0.f)
                last = s.gpu_ms;
        }
        std::cout << s376::format_gpu_pass_table(last, cfg.render.shadows);
        std::cout << s376::format_approach_bench_summary(rows, cfg.render.gpu_budget_ms,
                                                         cfg.render.quality, rend->autodrop_log());
        rend->request_stop();
        g_stop = true;
    }

    const auto t0 = std::chrono::steady_clock::now();
    auto t_fx = t0;
    const double period = 1.0 / std::max(cfg.render.frame_rate, 1.0);
    std::uint64_t frames = 0;
    std::uint64_t last_eyepoints = 0;
    if (rc == 0 && !record_path.empty()) {
        const int n = s376::approach_frame_count(approach);
        const float dt = static_cast<float>(1.0 / std::max(1.0, approach.fps));
        int w = 0, h = 0;
        if (channels.size() > 1)
            s376::wrap_strip_size(channels, w, h);
        else {
            w = std::max(8, channels.front().width);
            h = std::max(8, channels.front().height);
        }
        s376::RecordWriter writer;
        if (!writer.open(record_path, w, h, approach.fps)) {
            S376_LOG_ERROR("ig", "record open failed: %s", record_path.c_str());
            rc = 1;
        } else {
            S376_LOG_INFO("ig", "record %s  %dx%d  %d frames  sink=%s  rss=%.1f MiB",
                          record_path.c_str(), w, h, n,
                          writer.used_ffmpeg() ? "ffmpeg" : "png",
                          static_cast<double>(s376::current_rss_bytes()) / (1024.0 * 1024.0));
            s376::RgbImage im;
            shot_host = host_from_shot(s376::approach_sample(approach, 0));
            apply_shot_env(shot_host);
            shot_host.script_dt = dt;
            const auto t_warm = std::chrono::steady_clock::now();
            for (int warm = 0; !g_stop && !rend->done(); ++warm) {
                rend->frame(shot_host);
                ++frames;
                const double waited =
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - t_warm)
                        .count();
                if (warm >= 7 && (rend->pager_idle() || waited >= screenshot_wait))
                    break;
            }
            auto write_one = [&](const s376::HostState& h, int i, int total, double alt,
                                 double nm) -> bool {
                rend->frame(h);
                ++frames;
                if (!rend->capture_rgb(im) || !writer.write(im)) {
                    S376_LOG_ERROR("ig", "record frame %d failed", i);
                    return false;
                }
                if (i == 0 || i + 1 == total || i % 60 == 0) {
                    S376_LOG_INFO("ig", "record %d/%d  %.0fft  %.2fnm  rss=%.1f MiB", i + 1, total,
                                  alt, nm,
                                  static_cast<double>(s376::current_rss_bytes()) /
                                      (1024.0 * 1024.0));
                }
                return true;
            };
            if (demo_kjfk) {
                s376::fakehost::ProfileConfig pc;
                pc.rwy = s376::fakehost::runway_from_faa(
                    approach.icao.empty() ? "KJFK" : approach.icao,
                    approach.rwy.empty() ? "04R" : approach.rwy);
                pc.start_nm = demo_start_nm;
                pc.gs_deg = gs_deg;
                pc.landing_config_at_start = six_mile;
                s376::fakehost::ApproachProfile ap(pc);
                int i = 0;
                while (!ap.done() && !g_stop && !rend->done()) {
                    const auto f = ap.step(dt);
                    s376::ShotPose pose;
                    pose.icao = pc.rwy.icao;
                    pose.rwy = pc.rwy.ident;
                    pose.lat = f.lat;
                    pose.lon = f.lon;
                    pose.alt_ft = f.alt_ft;
                    pose.hdg_true_deg = f.hdg;
                    pose.pitch_deg = f.pitch;
                    pose.roll_deg = f.roll;
                    shot_host = host_from_shot(pose);
                    apply_shot_env(shot_host);
                    shot_host.ownship.gear = f.gear_down ? 1 : 0;
                    shot_host.ownship.flaps = f.flaps;
                    shot_host.have_ownship = true;
                    shot_host.script_dt = dt;
                    if (!write_one(shot_host, i, 0, f.alt_ft, f.wheel_agl_ft / 6076.115)) {
                        rc = 1;
                        break;
                    }
                    ++i;
                }
            } else {
            for (int i = 0; i < n && !g_stop && !rend->done(); ++i) {
                const double t = static_cast<double>(i) / std::max(1.0, approach.fps);
                const auto pose = s376::approach_sample(approach, t);
                shot_host = host_from_shot(pose);
                apply_shot_env(shot_host);
                shot_host.script_dt = dt;
                if (!write_one(shot_host, i, n, pose.alt_ft, pose.dist_nm)) {
                    rc = 1;
                    break;
                }
            }
            }
            if (!writer.close() && writer.used_ffmpeg()) {
                S376_LOG_ERROR("ig", "ffmpeg mux failed for %s", record_path.c_str());
                rc = 1;
            }
            if (!writer.mux_cmd().empty()) {
                S376_LOG_INFO("ig", "wrote %d PNG frames in %s", writer.frames(),
                              writer.frames_dir().c_str());
                S376_LOG_INFO("ig", "mux: %s", writer.mux_cmd().c_str());
                std::cout << writer.mux_cmd() << "\n";
            } else if (writer.used_ffmpeg()) {
                S376_LOG_INFO("ig", "wrote %s  %d frames", record_path.c_str(), writer.frames());
            }
        }
        rend->request_stop();
    }
    while (!g_stop && !rend->done()) {
        if (g_stop) {
            rend->request_stop();
            break;
        }
        if (duration >= 0) {
            const double elapsed =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (elapsed >= duration) {
                rend->request_stop();
                break;
            }
        }
        s376::HostState host = state.load();
        if (!demo_kjfk && have_shot)
            host = shot_host;
        if (host.have_eyepoint && ptt.edge(host, cfg.egg, rend->ptt_key_down())) {
            (void)s376::try_fire_missile(host.eye.lat_deg, host.eye.lon_deg, host.eye.alt_ft,
                                         host.eye.true_heading_deg(), host.eye.pitch_deg);
        }
        {
            const auto now = std::chrono::steady_clock::now();
            const double dt = std::chrono::duration<double>(now - t_fx).count();
            t_fx = now;
            s376::step_missiles(heights.get(), dt);
        }
        if (!have_shot && cfg.render.sync_to_host && host.have_eyepoint &&
            host.eyepoints == last_eyepoints) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        last_eyepoints = host.eyepoints;
        {
            const double t = std::chrono::duration<double>(
                                 std::chrono::steady_clock::now().time_since_epoch())
                                 .count();
            s376::control_hub().note_host(host, t);
            s376::control_hub().note_frame(t);
        }
        if (s376::control_hub().restart_requested()) {
            S376_LOG_INFO("wizard", "refresh channels — exiting so the operator can reopen");
            rend->request_stop();
            g_stop = true;
            break;
        }
        rend->frame(host);
        ++frames;
        if (headless)
            std::this_thread::sleep_for(std::chrono::duration<double>(period));
        if (!self_test_shot.empty() && frames >= 2) {
            std::string err;
            if (!rend->capture_self_test(self_test_shot, err)) {
                S376_LOG_ERROR("ig", "self-test-shot failed: %s", err.c_str());
                rc = 1;
            } else {
                S376_LOG_INFO("ig", "self-test-shot ok  %s", self_test_shot.c_str());
            }
            rend->request_stop();
            break;
        }
        if (!screenshot_path.empty() && frames >= 8) {
            const double waited =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (!rend->pager_idle() && waited < screenshot_wait)
                continue;
            if (!rend->capture(screenshot_path)) {
                S376_LOG_ERROR("ig", "screenshot failed: %s", screenshot_path.c_str());
                rc = 1;
                g_stop = true;
                rend->request_stop();
                break;
            }
            int sw = channels.front().width, sh = channels.front().height;
            if (channels.size() > 1)
                s376::wrap_strip_size(channels, sw, sh);
            S376_LOG_INFO("ig", "wrote %s (FBO %dx%d, waited %.2fs pager=%s%s)",
                          screenshot_path.c_str(), sw, sh, waited,
                          rend->pager_idle() ? "idle" : "timeout",
                          channels.size() > 1 ? "  strip" : "");
            rend->request_stop();
            break;
        }
        if (frames % 300 == 0 && host.have_eyepoint) {
            S376_LOG_INFO("ig", "%s", s376::diagnostics_dump(cfg, &host, selected_names).c_str());
        }
    }

    if (!self_test_shot.empty() && frames == 0)
        S376_LOG_WARN("ig", "self-test-shot: no GL frames (CPU already passed; hangar GPU for FBO)");
    if (g_stop)
        rend->request_stop();
    rend->end();
    g_stop = true;
    rx.stop();
    if (synth_th.joinable())
        synth_th.join();
    {
        const s376::HostState host = have_shot ? shot_host : state.load();
        S376_LOG_INFO("ig", "stopped after %llu frames\n%s",
                      static_cast<unsigned long long>(frames),
                      s376::diagnostics_dump(cfg, &host, selected_names).c_str());
    }
    s376::log::shutdown();
    return rc;
}
