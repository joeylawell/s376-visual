#include "s376/config.hpp"
#include "s376/log.hpp"
#include "s376/safety.hpp"
#include "s376/synth.hpp"
#include "s376/version.hpp"

#include <iostream>
#include <string>

static void usage() {
    std::cerr
        << "s376-synth " << s376::version()
        << " — fake xT4 host traffic\n"
           "usage: s376-synth [options]\n"
           "  --dest HOST:PORT   default 127.0.0.1:2300 (loopback only unless S376_ALLOW_LAN=1)\n"
           "  --rate HZ          default 20\n"
           "  --encoding ORDER/FMT   le/vax | be/vax | le/ieee | be/ieee\n"
           "  --duration SEC     default 30; negative = until killed\n"
           "  --weather          124/125/126/127/128 weather + type-105 storm cell\n"
           "  -v, --verbose\n";
}

int main(int argc, char** argv) {
    std::string dest = "127.0.0.1:2300";
    s376::SynthConfig cfg;
    double duration = 30;
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto need = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << a << " needs an argument\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "-h" || a == "--help") {
            usage();
            return 0;
        }
        if (a == "--dest")
            dest = need();
        else if (a == "--rate")
            cfg.rate_hz = std::stod(need());
        else if (a == "--duration")
            duration = std::stod(need());
        else if (a == "--encoding") {
            const std::string e = need();
            auto slash = e.find('/');
            cfg.word_order =
                s376::parse_word_order(slash == std::string::npos ? e : e.substr(0, slash));
            cfg.float_fmt =
                s376::parse_float_fmt(slash == std::string::npos ? "vax" : e.substr(slash + 1));
        } else if (a == "--weather")
            cfg.weather = true;
        else if (a == "-v" || a == "--verbose")
            verbose = true;
        else {
            std::cerr << "unknown option " << a << "\n";
            usage();
            return 2;
        }
    }

    s376::log::Options lo;
    lo.verbose = verbose;
    lo.no_file = true;
    s376::log::setup(lo, "synth");
    try {
        const int n = s376::run_synth_sender(dest, cfg, duration, false, false);
        S376_LOG_INFO("synth", "sent %d datagrams to %s", n, dest.c_str());
    } catch (const std::exception& ex) {
        S376_LOG_ERROR("synth", "%s", ex.what());
        s376::log::shutdown();
        return 1;
    }
    s376::log::shutdown();
    return 0;
}
