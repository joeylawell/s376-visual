#pragma once

#include "s376/airport.hpp"
#include "s376/faa.hpp"

#include <string>
#include <vector>

namespace s376 {

// Public-repo inventory of what Raster's 2023 KJFK_KEWR pad showed.
// Status is ours vs that list, rebuilt from FAA / OSM / public data.
// FAA wins when the two disagree (see docs/ACCURACY.md).

enum class ParityStatus { Ok, Partial, Gap };

struct ParityItem {
    const char* id;
    const char* name;
    ParityStatus status;
    const char* source; // FAA | OSM | public | host
    const char* note;
};

struct ParityCounts {
    std::string icao;
    int runways = 0;
    int lights = 0;
    int taxiways = 0;
    int buildings = 0;
    int signs = 0;
    int hold_bars = 0;
    int trees = 0;
    int parked = 0;
    int jetbridges = 0;
    int docking = 0;
    int routes = 0;
    int marshallers = 0;
    int night_points = 0;
    double night_spacing_m = 35;
    bool has_tower = false;
    bool has_beacon = false;
    bool has_t5 = false;
    bool has_c1_c6 = false;
    bool has_u1_u3 = false;
    bool has_cc = false; // 2022: must be false
    bool alsf2 = false;
    bool malsr = false;
    bool rlls = false;
    bool papi = false;
    bool reil = false;
    bool tdz = false;
    int roads = 0;
    bool has_van_wyck = false;
    bool has_belt = false;
    bool has_njtp = false;
    bool has_jfk_terminal_rd = false;
};

struct RasterScan {
    std::string path;
    int files = 0;
    int nxt = 0;
    int nxb = 0;
    int nx3 = 0;
    int coord_hits = 0;
    std::string warning;
};

const ParityItem* scenery_parity_items(int* count);
const char* parity_status_name(ParityStatus s);
ParityCounts count_parity(const Airport& a);
std::string format_parity_counts(const ParityCounts& c);
std::string format_scenery_checklist();

// Read a user-supplied local Raster tree at runtime. Looks for geographic
// doubles only — no vendor record layout is stored or printed. Never copies
// files into the repo.
RasterScan scan_local_raster_tree(const std::string& path);
std::string format_raster_compare(const ParityCounts& ours, const RasterScan& theirs);
// Official FAA/AIP ends vs a local Raster tree. Official wins; the
// checker flags that Raster coordinates must not replace them.
std::string format_official_vs_raster(const FaaAirportRef& faa, const RasterScan& theirs);

bool looks_like_s376_repo(const std::string& path);

} // namespace s376
