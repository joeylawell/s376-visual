#pragma once

#include "s376/airport.hpp"
#include "s376/faa.hpp"
#include "s376/fetch.hpp"
#include "s376/terrain.hpp"

#include <string>
#include <vector>

namespace s376 {

// CI fail limits. Do not loosen these for looks or frame time.
constexpr double kAccEndM = 3.0;
constexpr double kAccHeadingDeg = 0.1;
constexpr double kAccThresholdM = 3.0;
constexpr double kAccImageryM = 1.0;
constexpr double kAccElevM = 1.0;

struct AccFail {
    std::string check;
    std::string detail;
};

struct AccReport {
    std::string icao;
    std::vector<AccFail> fails;
    int checks = 0;
    bool ok() const { return fails.empty(); }
};

// Compare a built airport to its official FAA/AIP reference (ends,
// heading, displaced threshold, ALS type, PAPI, REIL, TDZ, width).
AccReport check_airport_vs_faa(const Airport& a, const FaaAirportRef& faa);

// Pixel↔geodetic round-trip of a north-up box must stay inside `tol_m`.
double geotransform_roundtrip_m(const GeoBox& box, int w, int h, double lat, double lon);
bool imagery_aligned_to_runways(const GeoBox& box, int w, int h, const Airport& a, double tol_m,
                                AccReport* out = nullptr);

// DEM sample vs FAA orthometric elevation (metres).
bool elev_matches_faa(const DemGrid& dem, double lat, double lon, double faa_elev_m, double tol_m,
                      double* got_m = nullptr);

// Highest-res (lod 0) tiles / planned box must contain ARP and every end.
bool box_contains(const GeoBox& b, double lat, double lon, double pad_deg = 0);
bool highest_res_covers_airport(const GeoBox& lod0, const Airport& a, AccReport* out = nullptr);
bool highest_res_covers_tiles(const std::vector<CacheTileRef>& tiles, const Airport& a,
                              AccReport* out = nullptr);

std::string format_acc_report(const AccReport& r);

// Sample the finest cache DEM that covers the point (lod0/tY_X, then root).
bool sample_cache_elev_m(const std::string& cache_dir, double lat, double lon, double& z_m);

} // namespace s376
