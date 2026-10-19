#pragma once

#include "s376/fetch.hpp"

#include <string>
#include <vector>

namespace s376 {

// One airport (or metro) fetch/import plan. Lives under data/regions/.
// Never holds imagery — only numbers and source names. --plan only.
struct RegionPlan {
    std::string name;
    std::string icao;
    double south = 0;
    double north = 0;
    double west = 0;
    double east = 0;
    double near_m = 0;
    double near_dem_m = 0;
    double mid_m = 0;
    double far_m = 0;
    double near_km = 8;
    double mid_km = 40;
    double approach_nm = 12;
    double radius_km = 40.0 * 1.852;
    std::string imagery;
    std::string dem = "auto";
    std::string policy; // public | either | raster-local
    std::string source;
    std::vector<std::string> airports;
    bool local_import = false;
};

RegionPlan load_region_plan(const std::string& path);
bool write_region_plan(const std::string& path, const RegionPlan& p);
std::string format_region_plan(const RegionPlan& p);
FetchRequest fetch_request_from_region(const RegionPlan& p, const std::string& out_dir);
RegionPlan region_plan_for_icao(const std::string& icao);
// KJFK 0.3 m near-field (NYS/NYC 2024, ~3 km of each runway). Not the
// in-flight 1 m hangar fetch.
RegionPlan kjfk_nearfield_03m_plan();
std::vector<std::string> raster_airport_icaos();
std::string region_plan_path(const std::string& icao, const std::string& data_root = "data/regions");
bool write_fetch_plan_toml(const std::string& path, const FetchPlan& plan, const FetchRequest& req);

} // namespace s376
