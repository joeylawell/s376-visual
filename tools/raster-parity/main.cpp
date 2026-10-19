#include "s376/airport.hpp"
#include "s376/faa.hpp"
#include "s376/parity.hpp"
#include "s376/safety.hpp"
#include "s376/version.hpp"

#include <iostream>
#include <string>

// Local-only comparer. Reads a user-supplied Raster tree at runtime.
// Never copies, embeds, or writes Raster files. FAA positions win.

static void usage() {
    std::cerr << "s376-raster-parity " << s376::version()
              << " — compare our official FAA/AIP layout to a local Raster tree\n"
                 "usage:\n"
                 "  s376-raster-parity --ours [--icao ICAO]\n"
                 "  s376-raster-parity PATH [--icao ICAO]\n"
                 "\n"
                 "PATH is on the caller's disk. Nothing from it is committed.\n"
                 "Never point this at 10.1.95.0/24 or 192.168.173.0/24.\n";
}

int main(int argc, char** argv) {
    std::string path;
    std::string icao = "KJFK";
    bool ours_only = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            usage();
            return 0;
        }
        if (a == "--ours")
            ours_only = true;
        else if (a == "--icao" && i + 1 < argc)
            icao = argv[++i];
        else if (a == "--version") {
            std::cout << s376::version() << "\n";
            return 0;
        } else if (a[0] != '-')
            path = a;
        else {
            usage();
            return 2;
        }
    }
    if (!path.empty()) {
        if (path.find("10.1.95") != std::string::npos ||
            path.find("192.168.173") != std::string::npos) {
            std::cerr << "refusing a sim-LAN path\n";
            return 2;
        }
    }

    s376::Airport a;
    try {
        a = s376::airport_by_icao(icao);
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << "\n";
        return 2;
    }
    const auto ours = s376::count_parity(a);
    if (ours_only || path.empty()) {
        std::cout << s376::format_parity_counts(ours);
        if (const s376::FaaAirportRef* faa = s376::faa_airport(icao)) {
            s376::RasterScan empty;
            std::cout << s376::format_official_vs_raster(*faa, empty);
        }
        std::cout << s376::format_scenery_checklist();
        if (path.empty() && !ours_only)
            std::cout << "pass a local Raster tree to compare file/coord counts\n";
        return 0;
    }
    const auto scan = s376::scan_local_raster_tree(path);
    std::cout << s376::format_raster_compare(ours, scan);
    return 0;
}
