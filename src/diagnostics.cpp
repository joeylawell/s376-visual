#include "s376/airport.hpp"
#include "s376/accuracy.hpp"
#include "s376/correct.hpp"
#include "s376/diagnostics.hpp"
#include "s376/faa.hpp"
#include "s376/fetch.hpp"
#include "s376/terrain.hpp"
#include "s376/geo.hpp"
#include "s376/host.hpp"
#include "s376/render.hpp"
#include "s376/safety.hpp"
#include "s376/version.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#ifdef S376_HAS_OSG
#include <osg/Version>
#ifndef _WIN32
#include <X11/Xlib.h>
#include <fcntl.h>
#endif
#endif
#ifdef S376_HAS_OSGEARTH
#include <osgEarth/Version>
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#else
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

namespace s376 {
namespace {

const char* st_name(CheckStatus s) {
    switch (s) {
    case CheckStatus::Pass:
        return "PASS";
    case CheckStatus::Warn:
        return "WARN";
    case CheckStatus::Fail:
        return "FAIL";
    case CheckStatus::Skip:
        return "SKIP";
    }
    return "?";
}

void add(DoctorReport& r, Check c) {
    if (c.status == CheckStatus::Fail)
        ++r.fails;
    if (c.status == CheckStatus::Warn)
        ++r.warns;
    r.checks.push_back(std::move(c));
}

std::string lower_icao(std::string id) {
    for (char& c : id)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return id;
}

bool looks_packed(const std::string& dir) {
    if (std::filesystem::exists(dir + "/elev.s376dem") ||
        std::filesystem::exists(dir + "/lod0/elev.s376dem") ||
        std::filesystem::exists(dir + "/imagery.dds"))
        return true;
    return count_cache_tiles(dir) > 0;
}

} // namespace

std::string format_byte_count(std::uint64_t n) {
    std::ostringstream o;
    o << std::fixed << std::setprecision(1);
    if (n < 1024)
        return std::to_string(n) + " B";
    if (n < 1024ull * 1024)
        o << (n / 1024.0) << " KiB";
    else if (n < 1024ull * 1024 * 1024)
        o << (n / (1024.0 * 1024.0)) << " MiB";
    else
        o << (n / (1024.0 * 1024.0 * 1024.0)) << " GiB";
    return o.str();
}

std::vector<AirportCacheSize> scan_airport_caches(const std::string& root,
                                                  const std::string& extra_dir) {
    std::vector<AirportCacheSize> out;
    auto push_unique = [&](const std::string& icao, const std::string& path) {
        for (const auto& e : out) {
            if (e.path == path || e.icao == icao)
                return;
        }
        AirportCacheSize s;
        s.icao = icao;
        s.path = path;
        s.bytes = directory_bytes(path);
        s.tiles = count_cache_tiles(path);
        out.push_back(std::move(s));
    };

    for (const auto& a : all_public_airports()) {
        const std::string low = lower_icao(a.icao);
        std::string path;
        // Lowercase first: packs are written as cache/<icao>. On Windows
        // cache/KSEA and cache/ksea are the same directory, so checking
        // ICAO first would report the invented uppercase spelling.
        const std::string candidates[] = {
            root + "/" + low, root + "/" + a.icao, extra_dir,
        };
        for (const auto& c : candidates) {
            if (!c.empty() && std::filesystem::is_directory(c) &&
                (looks_packed(c) || extra_dir != c)) {
                if (c == extra_dir && !looks_packed(c) &&
                    lower_icao(std::filesystem::path(c).filename().string()) != low &&
                    std::filesystem::path(c).filename().string() != a.icao)
                    continue;
                path = c;
                break;
            }
        }
        if (path.empty())
            path = root + "/" + low;
        push_unique(a.icao, path);
    }

    if (std::filesystem::is_directory(root)) {
        for (const auto& e : std::filesystem::directory_iterator(root)) {
            if (!e.is_directory())
                continue;
            const std::string p = e.path().string();
            if (!looks_packed(p))
                continue;
            std::string name = e.path().filename().string();
            for (char& c : name)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            push_unique(name, p);
        }
    }
    if (!extra_dir.empty() && looks_packed(extra_dir)) {
        std::string name = std::filesystem::path(extra_dir).filename().string();
        for (char& c : name)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (name.empty())
            name = "CACHE";
        push_unique(name, extra_dir);
    }
    return out;
}

std::string format_approach_bench(const std::vector<ApproachBenchSample>& rows, float budget_ms,
                                  const std::string& quality, const std::string& dropped) {
    std::ostringstream o;
    o << std::fixed << std::setprecision(2);
    o << "# s376-visual --bench  quality=" << quality << "  budget=" << budget_ms << " ms\n";
    o << "nm\tgpu_ms\tcpu_ms\n";
    for (const auto& r : rows) {
        o << r.nm << "\t" << r.gpu_ms << "\t" << r.cpu_ms << "\n";
    }
    if (!dropped.empty())
        o << "dropped\t" << dropped << "\n";
    return o.str();
}

std::string format_approach_bench_summary(const std::vector<ApproachBenchSample>& rows,
                                          float budget_ms, const std::string& quality,
                                          const std::string& dropped) {
    float max_gpu = -1.f, min_gpu = -1.f, sum = 0.f;
    int n = 0;
    double max_nm = 0, min_nm = 0;
    for (const auto& r : rows) {
        if (r.gpu_ms < 0.f)
            continue;
        if (n == 0 || r.gpu_ms > max_gpu) {
            max_gpu = r.gpu_ms;
            max_nm = r.nm;
        }
        if (n == 0 || r.gpu_ms < min_gpu) {
            min_gpu = r.gpu_ms;
            min_nm = r.nm;
        }
        sum += r.gpu_ms;
        ++n;
    }
    const char* result = "NO_GL";
    const char* hz = "SKIP";
    if (n > 0) {
        result = (max_gpu <= budget_ms + 0.005f) ? "UNDER" : "OVER";
        hz = (max_gpu <= 16.67f) ? "YES" : "NO";
    }
    std::ostringstream o;
    o << std::fixed << std::setprecision(2);
    o << "SUMMARY  s376 --bench quality=" << quality << " budget=" << budget_ms << "ms";
    if (n > 0) {
        o << " max=" << max_gpu << "ms@" << max_nm << "nm"
          << " avg=" << (sum / static_cast<float>(n)) << "ms"
          << " min=" << min_gpu << "ms@" << min_nm << "nm";
    } else {
        o << " max=n/a avg=n/a min=n/a";
    }
    o << " dropped=" << (dropped.empty() ? "none" : dropped) << " result=" << result
      << " 60Hz=" << hz << "\n";
    return o.str();
}

std::string format_gpu_pass_table(float measured_ms, bool shadows_on) {
    int n = 0;
    const auto* b = quality_gpu_budgets(n);
    const float budgeted = quality_gpu_budget_total_ms(shadows_on);
    const float scale = (measured_ms > 0.f && budgeted > 0.f) ? (measured_ms / budgeted) : 1.f;
    std::ostringstream o;
    o << std::fixed << std::setprecision(2);
    o << "pass\tbudget_ms\tscaled_ms\n";
    o << "base\t6.00\t" << (6.0f * scale) << "\n";
    for (int i = 0; i < n; ++i) {
        if (std::strcmp(b[i].name, "shadows") == 0 && !shadows_on)
            continue;
        o << b[i].name << "\t" << b[i].gpu_ms << "\t" << (b[i].gpu_ms * scale) << "\n";
    }
    o << "total\t" << budgeted << "\t" << (measured_ms >= 0.f ? measured_ms : budgeted) << "\n";
    o << format_gta_stack_lines(shadows_on);
    return o.str();
}

std::string format_gta_stack_lines(bool shadows_on) {
    int n = 0;
    const auto* b = quality_gpu_budgets(n);
    auto find_ms = [&](const char* name) {
        for (int i = 0; i < n; ++i) {
            if (std::strcmp(b[i].name, name) == 0)
                return b[i].gpu_ms;
        }
        return 0.f;
    };
    const float csm = shadows_on ? find_ms("shadows") : 0.f;
    const float bloom = find_ms("bloom") + find_ms("filmic");
    const float ssao = find_ms("ssao");
    const float wet = find_ms("water") + find_ms("runway_pbr");
    const float haze = find_ms("atmosphere");
    const float aa = find_ms("taa") + find_ms("fxaa");
    std::ostringstream o;
    o << std::fixed << std::setprecision(2);
    o << "gta\tcsm_shadows\t" << csm << "\n";
    o << "gta\thdr_bloom\t" << bloom << "\n";
    o << "gta\tssao\t" << ssao << "\n";
    o << "gta\twet_runway\t" << wet << "\n";
    o << "gta\thaze\t" << haze << "\n";
    o << "gta\tstable_aa\t" << aa << "\n";
    return o.str();
}

std::vector<ChannelFrameTime> bench_channel_times(const AppConfig& cfg, bool try_gpu) {
    auto channels = select_channels(cfg, "all");
    if (channels.empty())
        channels = default_config().channels;

    HostState host;
    host.have_eyepoint = true;
    host.have_env = true;
    host.eye.lat_deg = 47.463684;
    host.eye.lon_deg = -122.307751;
    host.eye.alt_ft = 447;
    host.eye.heading_deg = -180.34f;
    host.eye.pitch_deg = 0;
    host.env.tod_hours = 14.5f;
    host.env.visible_range_ft = 60760.f;
    host.light_steps.fill(5);

    std::vector<ChannelFrameTime> out;
    constexpr int kIters = 8000;
    volatile double sink = 0;
    for (const auto& ch : channels) {
        ChannelFrameTime t;
        t.name = ch.name;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < kIters; ++i) {
            auto v = geo::make_channel_view(
                host.eye.lat_deg, host.eye.lon_deg, host.eye.alt_ft * geo::kFtToM,
                host.eye.true_heading_deg(), host.eye.pitch_deg, host.eye.roll_deg,
                ch.heading_offset_deg, ch.pitch_offset_deg, ch.roll_offset_deg, ch.hfov_deg,
                ch.vfov_deg, ch.width, ch.height, ch.eye_fwd_m, ch.eye_right_m, ch.eye_up_m);
            sink += v.eye.x + v.forward.y + v.up.z;
        }
        t.cpu_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() /
            kIters;
        out.push_back(std::move(t));
    }
    (void)sink;

#ifdef S376_HAS_OSG
    bool have_display = false;
#ifndef _WIN32
    if (const char* dpy = std::getenv("DISPLAY")) {
        if (dpy[0]) {
            // XOpenDisplay prints to stderr when DISPLAY is stale; keep doctor quiet.
            const int saved = dup(STDERR_FILENO);
            const int n = open("/dev/null", O_WRONLY);
            if (n >= 0) {
                dup2(n, STDERR_FILENO);
                close(n);
            }
            if (Display* x = XOpenDisplay(dpy)) {
                XCloseDisplay(x);
                have_display = true;
            }
            if (saved >= 0) {
                dup2(saved, STDERR_FILENO);
                close(saved);
            }
        }
    }
#else
    have_display = try_gpu;
#endif
    if (try_gpu && have_display) {
        try {
            AppConfig c = cfg;
            c.render.engine = "osg";
            c.render.vsync = false;
            auto chans = channels;
            for (auto& ch : chans) {
                ch.width = 160;
                ch.height = 90;
                ch.fullscreen = false;
                ch.window_x = 0;
                ch.window_y = 0;
            }
            auto rend = make_osg_renderer();
            if (rend && rend->begin(c, chans)) {
                rend->frame(host);
                const int kFrames = 8;
                const auto g0 = std::chrono::steady_clock::now();
                for (int i = 0; i < kFrames; ++i)
                    rend->frame(host);
                const double composite_ms =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - g0)
                        .count() /
                    kFrames;
                rend->end();
                const double share = composite_ms / std::max<std::size_t>(1, out.size());
                for (auto& t : out)
                    t.gpu_ms = share;
            }
        } catch (...) {
            // GPU bench is best-effort; CPU numbers still stand.
        }
    }
#else
    (void)try_gpu;
#endif
    return out;
}

DoctorReport run_doctor(const AppConfig& cfg, const std::string& config_path, bool listen,
                        bool bench) {
    DoctorReport r;
    add(r, {"version", CheckStatus::Pass, std::string("s376-visual ") + version(), ""});
#ifdef _WIN32
    add(r, {"platform", CheckStatus::Pass, "Windows", ""});
#else
    add(r, {"platform", CheckStatus::Pass, "Linux", ""});
#endif

#ifdef S376_HAS_OSGEARTH
    add(r, {"osgearth", CheckStatus::Pass, "osgEarth present at build time", ""});
#elif defined(S376_HAS_OSG)
    add(r,
        {"osgearth", CheckStatus::Warn,
         "OSG " + std::string(osgGetVersion()) + " present; osgEarth not found (procedural globe + local DEM)",
         "build osgEarth from source on Mint 22 (see README) and re-cmake"});
#else
    add(r, {"osgearth", CheckStatus::Warn, "no OSG; renderer is null/headless",
            "apt install libopenscenegraph-dev"});
#endif

#ifdef S376_HAS_GDAL
    add(r, {"gdal", CheckStatus::Pass, "GDAL present — GeoTIFF DEM/imagery path enabled", ""});
#else
    add(r, {"gdal", CheckStatus::Warn, "GDAL not built in; portable .s376dem only",
            "apt install libgdal-dev and re-cmake"});
#endif

    if (!config_path.empty()) {
        std::ifstream in(config_path);
        add(r, {"config", in ? CheckStatus::Pass : CheckStatus::Fail, config_path,
                in ? "" : "copy config/s376-visual.toml"});
    } else {
        add(r, {"config", CheckStatus::Pass, "built-in defaults (3 RSI channels)", ""});
    }

    safety::Endpoint ep;
    std::string err;
    if (!safety::parse_host_port(cfg.host.bind, ep, err)) {
        add(r, {"bind-parse", CheckStatus::Fail, err, "host.bind = \"0.0.0.0:2300\""});
    } else {
        try {
            safety::check_bind(ep.host, ep.port, "doctor bind", cfg.host.allow_sim_lan,
                               cfg.host.allow_lan);
            add(r, {"bind-safety", CheckStatus::Pass, ep.host + ":" + std::to_string(ep.port), ""});
        } catch (const safety::UnsafeEndpoint& ex) {
            add(r, {"bind-safety", CheckStatus::Fail, ex.what(),
                    "use 0.0.0.0 or 127.0.0.1; never bind a sim-LAN address in CI"});
        }
    }

#ifndef _WIN32
    ifaddrs* ifa = nullptr;
    bool on_sim = false;
    std::string sim_addr;
    if (getifaddrs(&ifa) == 0) {
        for (ifaddrs* p = ifa; p; p = p->ifa_next) {
            if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET)
                continue;
            char ip[64] = {};
            inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(p->ifa_addr)->sin_addr, ip,
                      sizeof(ip));
            if (safety::is_forbidden(ip)) {
                on_sim = true;
                sim_addr = ip;
            }
        }
        freeifaddrs(ifa);
    }
    if (on_sim) {
        add(r, {"local-addrs", CheckStatus::Warn,
                "this machine has address " + sim_addr + " on a sim LAN",
                "tests/CI must not send to that net; production IG sets allow_sim_lan"});
    } else {
        add(r, {"local-addrs", CheckStatus::Pass,
                "no 10.1.95.0/24 or 192.168.173.0/24 on this host", ""});
    }
#endif

    if (listen) {
        try {
            HostConfig hc = cfg.host;
            // Doctor always binds loopback so it cannot be a LAN probe.
            hc.bind = "127.0.0.1:0";
            hc.allow_sim_lan = false;
            hc.allow_lan = false;
            HostSocket sock;
            // Port 0 is awkward with our parser; use 2300 if free, else skip.
            hc.bind = "127.0.0.1:2300";
            try {
                sock.open(hc);
                add(r, {"udp-2300", CheckStatus::Pass, "bound 127.0.0.1:2300", ""});
                sock.close();
            } catch (const std::exception& ex) {
                add(r, {"udp-2300", CheckStatus::Warn, ex.what(),
                        "another process owns 2300; stop it or pick another port"});
            }
        } catch (const std::exception& ex) {
            add(r, {"udp-2300", CheckStatus::Fail, ex.what(), ""});
        }
    }

    if (const char* e = std::getenv("S376_ALLOW_SIM_LAN")) {
        if (std::string(e) == "1")
            add(r, {"sim-lan-env", CheckStatus::Warn, "S376_ALLOW_SIM_LAN=1 is set",
                    "unset it in tests and CI"});
    } else {
        add(r, {"sim-lan-env", CheckStatus::Pass, "S376_ALLOW_SIM_LAN unset", ""});
    }

    const auto caches = scan_airport_caches("cache", cfg.render.cache_dir);
    auto cache_for = [&](const std::string& icao) -> const AirportCacheSize* {
        for (const auto& s : caches) {
            if (s.icao == icao && s.bytes > 0)
                return &s;
        }
        return nullptr;
    };

    for (const auto* faa : faa_all()) {
        try {
            const Airport a = airport_by_icao(faa->icao);
            const AccReport acc = check_airport_vs_faa(a, *faa);
            std::string name = "faa-" + lower_icao(faa->icao);
            const std::string auth = official_authority(*faa);
            if (acc.ok()) {
                add(r, {name, CheckStatus::Pass,
                        std::to_string(acc.checks) + " checks vs " + auth + " " + faa->effective,
                        ""});
            } else {
                add(r, {name, CheckStatus::Fail, format_acc_report(acc),
                        "rebuild from " + auth + " both ends; see docs/ACCURACY.md"});
            }
            if (official_source_stale(*faa)) {
                add(r, {"aip-stale-" + lower_icao(faa->icao), CheckStatus::Warn,
                        auth + " AIP AD 2 effective " + faa->effective +
                            " is older than 24 months (retrieved " + faa->retrieved + ")",
                        "replace with the current AIRAC AD 2 from " + auth +
                            "; doctor warns until the table is fetchable"});
            }
        } catch (const std::exception& ex) {
            add(r, {"faa-" + lower_icao(faa->icao), CheckStatus::Fail, ex.what(),
                    "add airport_from_faa for this ICAO"});
        }
        try {
            const Airport a = airport_by_icao(faa->icao);
            const CorrectionReport cr = plan_corrections(a, *faa);
            const GeoBox* boxp = nullptr;
            GeoBox lod0{};
            int iw = 0, ih = 0;
            if (const AirportCacheSize* packed = cache_for(faa->icao)) {
                const auto tiles = scan_cache_tiles(packed->path);
                bool any = false;
                for (const auto& t : tiles) {
                    if (t.lod != 0)
                        continue;
                    if (!any) {
                        lod0 = {t.west, t.south, t.east, t.north};
                        any = true;
                    } else {
                        lod0.west = std::min(lod0.west, t.west);
                        lod0.south = std::min(lod0.south, t.south);
                        lod0.east = std::max(lod0.east, t.east);
                        lod0.north = std::max(lod0.north, t.north);
                    }
                    iw = std::max(iw, t.dem_w);
                    ih = std::max(ih, t.dem_h);
                }
                if (any) {
                    boxp = &lod0;
                    if (iw < 2 || ih < 2) {
                        iw = 4000;
                        ih = 4000;
                    }
                }
            }
            const AccReport img = check_imagery_vs_official(a, *faa, boxp, iw, ih);
            std::string name = "imagery-official-" + lower_icao(faa->icao);
            std::ostringstream detail;
            detail << cr.items.size() << " planned corrections; official wins";
            if (boxp)
                detail << "; lod0 geotransform vs official";
            if (img.ok() && cr.ok()) {
                add(r, {name, CheckStatus::Pass, detail.str(), ""});
            } else {
                add(r, {name, CheckStatus::Fail,
                        format_correction_report(cr) + format_acc_report(img),
                        "correct imagery to FAA/AIP; official data always wins"});
            }
        } catch (const std::exception& ex) {
            add(r, {"imagery-official-" + lower_icao(faa->icao), CheckStatus::Fail, ex.what(),
                    "rebuild from official both-end geometry"});
        }
    }

    if (caches.empty()) {
        add(r, {"cache-size", CheckStatus::Skip, "no airport cache dirs",
                "s376-cache fetch KSEA --out cache/ksea"});
    } else {
        for (const auto& s : caches) {
            const std::string name = "cache-" + lower_icao(s.icao);
            if (s.bytes == 0) {
                add(r, {name, CheckStatus::Skip, s.path + " not packed",
                        "s376-cache fetch " + s.icao + " --out " + s.path});
            } else {
                add(r, {name, CheckStatus::Pass,
                        format_byte_count(s.bytes) + " on disk (" + s.path + ")  " +
                            std::to_string(s.tiles) + " tiles",
                        ""});
            }
        }
    }

    if (bench) {
        const auto mesh_t0 = std::chrono::steady_clock::now();
        std::size_t verts = 0, lights = 0;
        for (const auto& a : all_public_airports()) {
            verts += build_airport_mesh(a).verts.size();
            lights += a.lights.size();
        }
        const double mesh_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - mesh_t0)
                .count();
        {
            std::ostringstream d;
            d << std::fixed << std::setprecision(2) << mesh_ms << " ms  " << verts << " verts  "
              << lights << " lights";
            add(r, {"mesh-build", CheckStatus::Pass, d.str(), ""});
        }
        const auto times = bench_channel_times(cfg, true);
        for (const auto& t : times) {
            std::ostringstream d;
            d << std::fixed << std::setprecision(2) << (t.cpu_ms * 1000.0) << " us cpu/view";
            if (t.gpu_ms >= 0)
                d << ", " << std::setprecision(3) << t.gpu_ms << " ms/frame gpu (composite share)";
            else
                d << ", gpu skipped (no usable DISPLAY)";
            add(r, {"frame-" + t.name, CheckStatus::Pass, d.str(), ""});
        }
    }

    {
        const auto rss = peak_rss_bytes();
        const auto cur = current_rss_bytes();
        std::ostringstream d;
        d << "peak " << format_byte_count(rss) << "  now " << format_byte_count(cur)
          << "  budget " << cfg.render.ram_mb << " MiB";
        const auto budget = static_cast<std::uint64_t>(std::max(256, cfg.render.ram_mb)) << 20;
        add(r, {"rss", rss > budget ? CheckStatus::Warn : CheckStatus::Pass, d.str(),
                rss > budget ? "page tiles / bake osm.bin; gtx980ti ram_mb=2048" : ""});
        if (!cfg.render.cache_dir.empty()) {
            const auto tiles = scan_cache_tiles(cfg.render.cache_dir);
            std::uint64_t naive = 0;
            for (const auto& t : tiles)
                naive += static_cast<std::uint64_t>(t.dem_w) * static_cast<std::uint64_t>(t.dem_h) *
                         48ull;
            std::ostringstream td;
            td << tiles.size() << " tiles  naive full-res mesh "
               << format_byte_count(naive) << "  paged mesh "
               << format_byte_count(static_cast<std::uint64_t>(tiles.size()) *
                                    estimate_tile_cpu_bytes(CacheTileRef{}, cfg.render.mesh_n));
            if (!tiles.empty()) {
                const CacheTileRef* sample = &tiles.front();
                for (const auto& t : tiles)
                    if (t.lod >= sample->lod)
                        sample = &t;
                try {
                    auto mesh = load_cache_mesh(sample->path, cfg.render.mesh_n);
                    td << "  sample " << mesh.dem.w << "x" << mesh.dem.h;
                } catch (...) {
                }
            }
            add(r, {"cache-tiles", CheckStatus::Pass, td.str(), ""});
            {
                std::string id;
                auto folder = std::filesystem::path(cfg.render.cache_dir).filename().string();
                for (char c : folder)
                    id.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
                try {
                    const Airport a = airport_by_icao(id);
                    AccReport cov;
                    cov.icao = id;
                    bool any_lod0 = false;
                    for (const auto& t : tiles) {
                        if (t.lod == 0)
                            any_lod0 = true;
                    }
                    if (!any_lod0) {
                        add(r, {"faa-coverage", CheckStatus::Skip, "no lod0 tiles in " + cfg.render.cache_dir,
                                "s376-cache fetch " + id + " — ARP and every end must sit in lod0"});
                    } else if (highest_res_covers_tiles(tiles, a, &cov)) {
                        add(r, {"faa-coverage", CheckStatus::Pass,
                                id + " ARP + " + std::to_string(a.runways.size() * 2) +
                                    " ends inside lod0",
                                ""});
                    } else {
                        add(r, {"faa-coverage", CheckStatus::Fail, format_acc_report(cov),
                                "delete the SW-shifted pack and re-fetch; see HANDOFF.md"});
                    }
                    if (any_lod0) {
                        GeoBox lod0;
                        bool any = false;
                        for (const auto& t : tiles) {
                            if (t.lod != 0)
                                continue;
                            if (!any) {
                                lod0 = {t.west, t.south, t.east, t.north};
                                any = true;
                            } else {
                                lod0.west = std::min(lod0.west, t.west);
                                lod0.south = std::min(lod0.south, t.south);
                                lod0.east = std::max(lod0.east, t.east);
                                lod0.north = std::max(lod0.north, t.north);
                            }
                        }
                        AccReport img;
                        img.icao = id;
                        if (any && imagery_aligned_to_runways(lod0, 4000, 4000, a, kAccImageryM, &img))
                            add(r, {"imagery-align", CheckStatus::Pass,
                                    id + " north-up geotransform < 1 m", ""});
                        else if (any)
                            add(r, {"imagery-align", CheckStatus::Fail, format_acc_report(img),
                                    "north-up export; do not warp imagery off runway geometry"});
                    }
                    if (const FaaAirportRef* faa = faa_airport(id)) {
                        int n = 0, bad = 0;
                        std::ostringstream ed;
                        for (const auto& e : faa->ends) {
                            double z = 0;
                            if (!sample_cache_elev_m(cfg.render.cache_dir, e.lat, e.lon, z))
                                continue;
                            ++n;
                            const double faa_m = e.elev_ft * geo::kFtToM;
                            if (std::fabs(z - faa_m) > kAccElevM) {
                                ++bad;
                                ed << e.ident << " DEM " << z << " m vs " << official_authority(*faa)
                                   << " " << e.elev_ft << " ft (" << faa_m << " m); ";
                            }
                        }
                        if (n == 0)
                            add(r, {"faa-elev", CheckStatus::Skip,
                                    "no cache DEM covers the " + official_authority(*faa) + " ends",
                                    "re-fetch so lod0 tiles include every official end"});
                        else if (bad)
                            add(r, {"faa-elev", CheckStatus::Fail, ed.str(),
                                    "1 m vs " + official_authority(*faa) +
                                        " orthometric; sample lod0 tiles, not the coarse root"});
                        else
                            add(r, {"faa-elev", CheckStatus::Pass,
                                    std::to_string(n) + " runway ends within 1 m of " +
                                        official_authority(*faa),
                                    ""});
                    }
                } catch (const std::exception&) {
                    add(r, {"faa-coverage", CheckStatus::Skip, "cache dir is not a known ICAO", ""});
                }
            }
            const auto json = cfg.render.cache_dir + "/osm.json";
            const auto bin = cfg.render.cache_dir + "/osm.bin";
            if (std::filesystem::exists(json) && !std::filesystem::exists(bin)) {
                std::ostringstream od;
                od << "osm.json " << format_byte_count(std::filesystem::file_size(json))
                   << " not loaded; bake osm.bin";
                add(r, {"osm-bin", CheckStatus::Warn, od.str(),
                        "s376-cache bake-osm " + cfg.render.cache_dir});
            } else if (std::filesystem::exists(bin)) {
                add(r, {"osm-bin", CheckStatus::Pass,
                        "osm.bin " + format_byte_count(std::filesystem::file_size(bin)), ""});
            }
        }
    }

    return r;
}

std::string format_report(const DoctorReport& r) {
    std::ostringstream o;
    o << "s376-visual doctor\n";
    for (const auto& c : r.checks) {
        o << "  [" << st_name(c.status) << "] " << c.name << " — " << c.detail;
        if (!c.fix.empty() && c.status != CheckStatus::Pass)
            o << "\n           fix: " << c.fix;
        o << "\n";
    }
    o << r.fails << " fail, " << r.warns << " warn\n";
    return o.str();
}

std::string diagnostics_dump(const AppConfig& cfg, const HostState* host,
                             const std::vector<std::string>& selected) {
    std::ostringstream o;
    o << "s376-visual " << version() << "\n";
    o << dump_config(cfg, selected);
#ifdef S376_HAS_OSGEARTH
    o << "renderer=osgearth\n";
#elif defined(S376_HAS_OSG)
    o << "renderer=osg " << osgGetVersion() << "\n";
#else
    o << "renderer=null\n";
#endif
    if (host) {
        o << "datagrams=" << host->datagrams << " eyepoints=" << host->eyepoints << "\n";
        o << "encoding=" << xt4::word_order_name(host->word_order) << "/"
          << xt4::float_fmt_name(host->float_fmt) << "\n";
        if (host->have_eyepoint) {
            o << "eye lat=" << host->eye.lat_deg << " lon=" << host->eye.lon_deg
              << " alt_ft=" << host->eye.alt_ft << " hdg=" << host->eye.true_heading_deg()
              << " pitch=" << host->eye.pitch_deg << " roll=" << host->eye.roll_deg << "\n";
        }
        if (host->have_env) {
            o << "env tod=" << host->env.tod_hours << " vis_ft=" << host->env.visible_range_ft
              << " date=" << host->env.year << "-" << host->env.month << "-" << host->env.day
              << " precip=" << host->env.precip_type << "/" << host->env.precip_intensity
              << " wind=" << host->env.wind_speed_mph << "@" << host->env.wind_dir_deg
              << " lightning=" << host->env.lightning_trigger << "\n";
        }
        if (host->have_fog) {
            o << "fog select=" << host->fog.select << " rvr_ft=" << host->fog.rvr_ft
              << " height_ft=" << host->fog.height_ft << "\n";
        }
        if (host->have_ownship) {
            o << "ownship gear=" << host->ownship.gear << " flaps=" << host->ownship.flaps << "\n";
        }
        if (host->have_cloud) {
            o << "cloud";
            const char* names[3] = {" lower", " upper", " top"};
            for (int i = 0; i < 3; ++i) {
                const auto& L = host->clouds[static_cast<std::size_t>(i)];
                o << names[i] << "=" << L.select << "/" << L.ceiling_ft << "-" << L.top_ft;
            }
            o << "\n";
        }
        for (int i = 0; i < xt4::kMaxActiveStorms; ++i) {
            const auto& c = host->storms[static_cast<std::size_t>(i)];
            if (!c.cfg.active)
                continue;
            o << "storm" << i << " sel=" << c.cfg.select << " csn=" << c.cfg.csn
              << " precip=" << c.cfg.precip_type << "/" << c.cfg.precip_intensity
              << " lightning=" << c.cfg.lightning;
            if (c.have_pose)
                o << " lat=" << c.pose.lat_deg << " lon=" << c.pose.lon_deg;
            o << "\n";
        }
        o << "peer=" << host->last_peer << "\n";
    }
    return o.str();
}

} // namespace s376
