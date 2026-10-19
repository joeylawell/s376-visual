#include "s376/parity.hpp"

#include "s376/faa.hpp"
#include "s376/safety.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace s376 {
namespace {

const ParityItem kItems[] = {
    {"rwy-faa", "Runways from both FAA ends", ParityStatus::Ok, "FAA",
     "KJFK/KEWR Vincenty; FAA wins vs Raster"},
    {"edge", "Runway edge lights", ParityStatus::Ok, "FAA", "60 m, yellow last 600 m"},
    {"cl", "Runway centreline", ParityStatus::Ok, "FAA", "15 m, red last 300 m"},
    {"tdz", "Touchdown-zone lights", ParityStatus::Ok, "FAA", "Charted ends only"},
    {"alsf2", "ALSF-2 + sequenced flashers", ParityStatus::Ok, "FAA", "04R/22L/13L JFK; 04R/22L EWR"},
    {"malsr", "MALSR + RAIL", ParityStatus::Ok, "FAA", "31R JFK; 04L/22R EWR"},
    {"rlls", "RLLS lead-in", ParityStatus::Ok, "FAA", "13R JFK"},
    {"papi", "PAPI all ends", ParityStatus::Ok, "FAA", "Side + 3.00°; 31R P4R; 13R two"},
    {"reil", "REIL strobes", ParityStatus::Partial, "FAA", "KEWR 22R/11/29; JFK none charted"},
    {"taxi-edge", "Blue taxi edge", ParityStatus::Ok, "public", "Along schematic + OSM taxi"},
    {"taxi-cl", "Green taxi centreline", ParityStatus::Ok, "public", ""},
    {"stop", "Stop bars", ParityStatus::Ok, "public", "Hold-short red"},
    {"lead-on", "Lead-on lights", ParityStatus::Partial, "public", "C/U connectors"},
    {"rwsl", "RWSL", ParityStatus::Partial, "public", "Hold crossings; not live host-driven"},
    {"sign-glow", "Sign glow", ParityStatus::Partial, "public", "Yellow points on our signs"},
    {"taxi-2022", "Taxi names C1–C6, U1–U3, no CC", ParityStatus::Partial, "FAA",
     "Schematic connectors; full graph from OSM"},
    {"signs", "Taxi / location signs", ParityStatus::Partial, "public", "At named connectors"},
    {"hold", "Hold bars", ParityStatus::Partial, "public", "At runway-edge of C/U"},
    {"term", "Terminals incl. T5", ParityStatus::Partial, "OSM", "Schematic T1/T4/T5/T7/T8 + OSM"},
    {"jetbridge", "Jetbridges", ParityStatus::Partial, "public", "Stand-in boxes at gates"},
    {"dock", "Docking guidance", ParityStatus::Partial, "public", "Stand-in bars"},
    {"parked", "Parked aircraft", ParityStatus::Partial, "public", "Gate stand-ins; not host CSN"},
    {"tower", "Tower + rotating beacon", ParityStatus::Partial, "public", "ATCT box + beacon point"},
    {"bldgs", "~30k buildings", ParityStatus::Partial, "OSM",
     "solids.bin = every OSM footprint (HAT); mesh nearest 800/2500"},
    {"trees", "Trees", ParityStatus::Partial, "public", "Rings off the pavement"},
    {"routes", "12 moving-model routes", ParityStatus::Partial, "public",
     "Paths stored; host CSN movers still a gap"},
    {"marshaller", "Marshallers", ParityStatus::Partial, "public", "Gate poses; host CSN ignored"},
    {"traffic", "Road traffic", ParityStatus::Ok, "OSM",
     "GPU-batched cars on motorway/trunk/primary/secondary; Van Wyck, Belt, JFK terminals, NJTP"},
    {"summer", "Summer imagery", ParityStatus::Partial, "public", "NYS Latest / NAIP via s376-cache"},
    {"winter", "Winter imagery", ParityStatus::Partial, "public",
     "s376-cache fetch --season winter → cache/<ICAO>/winter"},
    {"night15", "Road-derived street lights", ParityStatus::Ok, "OSM",
     "OSM/public highway class + building density; never water/marsh; no 60 m lattice"},
    {"kewr", "Full KEWR", ParityStatus::Partial, "FAA", "Both-end runways + scenery stand-ins"},
    {"skyline-era", "Classic NYC skyline easter egg", ParityStatus::Ok, "public",
     "Hidden unlock; stylized pre-2001 Lower Manhattan; NYC geofence; session-only"},
    {"wizard", "Operator console (s376-wizard)", ParityStatus::Ok, "public",
     "Raster Wizard replacement; local S376C1 control; Refresh channels clears session"},
};

double read_f64(const std::uint8_t* p, bool be) {
    std::uint64_t u = 0;
    if (be) {
        for (int i = 0; i < 8; ++i)
            u = (u << 8) | p[i];
    } else {
        for (int i = 7; i >= 0; --i)
            u = (u << 8) | p[i];
    }
    double d;
    std::memcpy(&d, &u, 8);
    return d;
}

bool looks_lat(double v) {
    return std::isfinite(v) && std::fabs(v) > 1.0 && std::fabs(v) < 80.0;
}
bool looks_lon(double v) {
    return std::isfinite(v) && std::fabs(v) > 1.0 && std::fabs(v) <= 180.0 && std::fabs(v) > 20.0;
}

} // namespace

const ParityItem* scenery_parity_items(int* count) {
    if (count)
        *count = static_cast<int>(sizeof(kItems) / sizeof(kItems[0]));
    return kItems;
}

const char* parity_status_name(ParityStatus s) {
    switch (s) {
    case ParityStatus::Ok:
        return "ok";
    case ParityStatus::Partial:
        return "partial";
    case ParityStatus::Gap:
        return "gap";
    }
    return "?";
}

ParityCounts count_parity(const Airport& a) {
    ParityCounts c;
    c.icao = a.icao;
    c.runways = static_cast<int>(a.runways.size());
    c.lights = static_cast<int>(a.lights.size());
    c.taxiways = static_cast<int>(a.taxiways.size());
    c.buildings = static_cast<int>(a.buildings.size());
    c.signs = static_cast<int>(a.signs.size());
    c.hold_bars = static_cast<int>(a.hold_bars.size());
    c.trees = static_cast<int>(a.trees.size());
    c.parked = static_cast<int>(a.parked.size());
    c.jetbridges = static_cast<int>(a.jetbridges.size());
    c.docking = static_cast<int>(a.docking.size());
    c.routes = static_cast<int>(a.routes.size());
    c.marshallers = static_cast<int>(a.marshallers.size());
    c.night_points = a.night.points;
    c.night_spacing_m = a.night.spacing_m;
    c.has_tower = a.tower_h_m > 1.f;
    c.has_beacon = a.has_beacon;
    int c_n = 0, u_n = 0;
    for (const auto& t : a.taxiways) {
        if (t.name == "CC")
            c.has_cc = true;
        if (t.name.size() >= 2 && t.name[0] == 'C' && t.name[1] >= '1' && t.name[1] <= '6')
            ++c_n;
        if (t.name.size() >= 2 && t.name[0] == 'U' && t.name[1] >= '1' && t.name[1] <= '3')
            ++u_n;
    }
    c.has_c1_c6 = c_n >= 6;
    c.has_u1_u3 = u_n >= 3;
    for (const auto& b : a.buildings) {
        if (b.name == "T5")
            c.has_t5 = true;
    }
    for (const auto& rw : a.runways) {
        if (rw.approach_le == "ALSF-2" || rw.approach_he == "ALSF-2")
            c.alsf2 = true;
        if (rw.approach_le == "MALSR" || rw.approach_he == "MALSR")
            c.malsr = true;
        if (rw.approach_le == "RLLS" || rw.approach_he == "RLLS")
            c.rlls = true;
        if (rw.papi_le != "NONE" || rw.papi_he != "NONE")
            c.papi = true;
        if (rw.reil_le || rw.reil_he)
            c.reil = true;
        if (rw.tdz_le || rw.tdz_he)
            c.tdz = true;
    }
    c.roads = static_cast<int>(a.roads.size());
    for (const auto& r : a.roads) {
        if (r.name.find("Van Wyck") != std::string::npos)
            c.has_van_wyck = true;
        if (r.name.find("Belt") != std::string::npos)
            c.has_belt = true;
        if (r.name.find("Turnpike") != std::string::npos)
            c.has_njtp = true;
        if (r.name.find("JFK Terminal") != std::string::npos)
            c.has_jfk_terminal_rd = true;
    }
    return c;
}

std::string format_parity_counts(const ParityCounts& c) {
    std::ostringstream o;
    o << c.icao << "  rwy=" << c.runways << " lights=" << c.lights << " taxi=" << c.taxiways
      << " bldg=" << c.buildings << " signs=" << c.signs << " hold=" << c.hold_bars
      << " trees=" << c.trees << " parked=" << c.parked << " jet=" << c.jetbridges
      << " dock=" << c.docking << " routes=" << c.routes << " marsh=" << c.marshallers
      << " night=" << c.night_points << "@" << c.night_spacing_m << "m"
      << " tower=" << (c.has_tower ? 1 : 0) << " beacon=" << (c.has_beacon ? 1 : 0)
      << " T5=" << (c.has_t5 ? 1 : 0) << " C1-6=" << (c.has_c1_c6 ? 1 : 0)
      << " U1-3=" << (c.has_u1_u3 ? 1 : 0) << " CC=" << (c.has_cc ? 1 : 0)
      << " roads=" << c.roads << " VanWyck=" << (c.has_van_wyck ? 1 : 0)
      << " Belt=" << (c.has_belt ? 1 : 0) << " NJTP=" << (c.has_njtp ? 1 : 0)
      << " JFKrd=" << (c.has_jfk_terminal_rd ? 1 : 0) << "\n";
    return o.str();
}

std::string format_scenery_checklist() {
    std::ostringstream o;
    int n = 0;
    const auto* items = scenery_parity_items(&n);
    o << "KJFK/KEWR scenery vs Raster 2023 (public rebuild; FAA wins)\n";
    for (int i = 0; i < n; ++i) {
        o << "  [" << parity_status_name(items[i].status) << "] " << items[i].id << "  "
          << items[i].name << "  (" << items[i].source << ")";
        if (items[i].note[0])
            o << "  — " << items[i].note;
        o << "\n";
    }
    return o.str();
}

bool looks_like_s376_repo(const std::string& path) {
    return std::filesystem::exists(path + "/CMakeLists.txt") &&
           std::filesystem::exists(path + "/include/s376");
}

RasterScan scan_local_raster_tree(const std::string& path) {
    RasterScan s;
    s.path = path;
    if (path.empty() || !std::filesystem::exists(path)) {
        s.warning = "path missing";
        return s;
    }
    try {
        safety::check_data_url("file://" + path);
    } catch (...) {
        // Local path is not a URL; still refuse obvious sim-LAN strings.
    }
    if (path.find("10.1.95") != std::string::npos || path.find("192.168.173") != std::string::npos) {
        s.warning = "refusing a sim-LAN path";
        return s;
    }
    if (looks_like_s376_repo(path))
        s.warning = "path looks like the s376-visual repo, not a Raster tree";

    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(
             path, std::filesystem::directory_options::skip_permission_denied, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec)
            continue;
        if (it->is_directory()) {
            const auto name = it->path().filename().string();
            if (name == ".git" || name == "build" || name == "cache" || name == "downloads")
                it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file())
            continue;
        const auto ext = it->path().extension().string();
        std::string low = ext;
        for (char& c : low)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        ++s.files;
        if (low == ".nxt")
            ++s.nxt;
        else if (low == ".nxb")
            ++s.nxb;
        else if (low == ".nx3")
            ++s.nx3;
        else
            continue;
        // Geographic doubles only. No record-layout comments.
        const auto sz = it->file_size();
        if (sz < 16 || sz > 32ull * 1024ull * 1024ull)
            continue;
        std::ifstream in(it->path(), std::ios::binary);
        std::vector<std::uint8_t> buf(static_cast<std::size_t>(sz));
        in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(sz));
        const auto n = static_cast<std::size_t>(in.gcount());
        for (std::size_t i = 0; i + 16 <= n; i += 8) {
            for (int be = 0; be < 2; ++be) {
                const double a = read_f64(buf.data() + i, be != 0);
                const double b = read_f64(buf.data() + i + 8, be != 0);
                if ((looks_lat(a) && looks_lon(b)) || (looks_lat(b) && looks_lon(a)))
                    ++s.coord_hits;
            }
        }
    }
    return s;
}

std::string format_raster_compare(const ParityCounts& ours, const RasterScan& theirs) {
    std::ostringstream o;
    o << format_parity_counts(ours);
    o << "local Raster tree  files=" << theirs.files << " nxt=" << theirs.nxt
      << " nxb=" << theirs.nxb << " nx3=" << theirs.nx3 << " coord_hits=" << theirs.coord_hits
      << "\n";
    if (!theirs.warning.empty())
        o << "warn  " << theirs.warning << "\n";
    if (theirs.coord_hits > 0 && ours.lights > 0) {
        o << "note  Raster coord hits " << theirs.coord_hits << " vs our lights " << ours.lights
          << " (heuristic; FAA positions win)\n";
    }
    if (theirs.nxt + theirs.nxb + theirs.nx3 == 0)
        o << "gap   no Raster database files at that path\n";
    if (const FaaAirportRef* faa = faa_airport(ours.icao))
        o << format_official_vs_raster(*faa, theirs);
    o << format_scenery_checklist();
    return o.str();
}

std::string format_official_vs_raster(const FaaAirportRef& faa, const RasterScan& theirs) {
    std::ostringstream o;
    o << faa.icao << " official vs Raster  source=" << faa.source << " effective=" << faa.effective
      << "\n";
    o << "  rule  official FAA/AIP wins; Raster coords are flagged, never copied\n";
    for (const auto& e : faa.ends) {
        o << "  official  " << e.ident << "  " << e.lat << "," << e.lon << "  ALS " << e.als
          << "  PAPI " << e.papi << "  disp " << e.displaced_ft << " ft\n";
    }
    if (theirs.coord_hits > 0) {
        o << "  FLAG  Raster tree has " << theirs.coord_hits
          << " geographic doubles. If any disagree with the official ends above, "
             "keep the official value and treat Raster as a local imagery fallback only.\n";
    } else if (!theirs.path.empty() && theirs.warning.empty()) {
        o << "  note  no Raster coordinates decoded at that path\n";
    }
    return o.str();
}

} // namespace s376
