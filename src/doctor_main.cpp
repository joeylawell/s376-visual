#include "s376/config.hpp"
#include "s376/diagnostics.hpp"
#include "s376/log.hpp"
#include "s376/version.hpp"

#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
    std::string config_path;
    std::string cache_dir;
    bool bench = true;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if ((a == "-c" || a == "--config") && i + 1 < argc)
            config_path = argv[++i];
        else if (a == "--cache-dir" && i + 1 < argc)
            cache_dir = argv[++i];
        else if (a == "--no-bench")
            bench = false;
        else if (a == "-h" || a == "--help") {
            std::cout << "s376-doctor — local environment checks (never sends off-box)\n"
                         "  -c, --config PATH   TOML config (default config/s376-visual.toml)\n"
                         "  --cache-dir PATH    packed cache (rss + cache-tiles + osm.bin +\n"
                         "                      faa-coverage: ARP and every runway end in lod0)\n"
                         "  --no-bench          skip per-channel frame-time bench\n";
            return 0;
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
    if (!cache_dir.empty())
        cfg.render.cache_dir = cache_dir;
    s376::log::Options lo;
    lo.no_file = true;
    s376::log::setup(lo, "doctor");
    auto r = s376::run_doctor(cfg, config_path, true, bench);
    std::cout << s376::format_report(r);
    s376::log::shutdown();
    return r.fails ? 1 : 0;
}
