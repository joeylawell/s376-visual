#pragma once

#include "s376/terrain.hpp"

#include <string>
#include <utility>
#include <vector>

namespace s376 {

struct GeoBox {
    double west = 0;
    double south = 0;
    double east = 0;
    double north = 0;
};

struct LodSpec {
    int level = 0;
    double radius_km = 8;
    int dem_n = 256;
    int img_n = 512;
    bool bc7 = false; // near-field only
    double img_m = 0; // metres / px; 0 = use img_n
    double dem_m = 0;
    bool tiled = false;
};

struct RasterTile {
    GeoBox box;
    int w = 0;
    int h = 0;
    int ix = 0;
    int iy = 0;
};

struct AirportRef {
    std::string icao;
    std::string name;
    double lat = 0;
    double lon = 0;
    double elev_ft = 0;
    bool conus = true; // NAIP vs Sentinel-2
};

struct FetchRequest {
    std::string icao = "KSEA";
    // Outer / far-lod box. 40 nm (74.08 km) so Jamaica Bay, Rockaway,
    // the Atlantic and Queens/Brooklyn stay in lod1/lod2. Disk is free;
    // the pager holds RAM.
    double radius_km = 40.0 * 1.852;
    std::string dem = "auto";      // auto | glo30 | 3dep
    std::string imagery = "auto";  // auto | nys | hro | naip | s2 | sanmateo | kingco | hillshade
    std::string out_dir;
    std::string season = "summer"; // summer | winter (second pack under out/winter)
    bool dry_run = false;
    bool mips = true;
    // Metres / px. 0 = native for the chosen source (NYS 0.15, NAIP 0.6,
    // HRO 0.3, S2 10; 3DEP 1, GLO-30 30). Disk is not a constraint —
    // pull the highest available ortho on the field and approaches.
    double near_m = 0;
    double near_dem_m = 0;
    double mid_m = 0;
    double mid_dem_m = 0;
    double far_m = 0;
    double far_dem_m = 0;
    double near_radius_km = 8.0;  // ARP box at native ortho
    double mid_radius_km = 40.0;
    double approach_nm = 12.0; // native ortho along each final
    int max_export = 2048;     // ImageServer export cap (2048; 4000 500/504s)
    bool plan_only = false;    // --plan: print tile bboxes + coverage, no GET
    int jobs = 1;              // parallel tile GETs (Mint: 16)
    int mem_budget_mb = 0;     // 0 = kFetchRssBoundBytes; --mem-budget caps jobs + RSS
};

struct FetchEstimate {
    std::uint64_t img_tiles = 0;
    std::uint64_t dem_tiles = 0;
    std::uint64_t est_raw_bytes = 0;
    std::uint64_t est_packed_bytes = 0;
};

// USGS 3DEP / NYS ImageServer reject ~4k F32/JPG exports (500/504).
constexpr int kMaxServiceExportPx = 2048;
// Fetch is streaming: one decoded tile in RAM. Peak RSS must stay inside
// this regardless of how many tiles are on disk (KJFK lod0 is hundreds).
constexpr std::uint64_t kFetchRssBoundBytes = 2ull << 30;
constexpr int kFetchRssLogEvery = 16;

// --mem-budget MB overrides the 2 GiB streaming RSS cap. Jobs are
// clamped so a 16 GB Mint box cannot launch 16 full tile decodes.
inline std::uint64_t fetch_rss_bound_bytes(const FetchRequest& r) {
    if (r.mem_budget_mb <= 0)
        return kFetchRssBoundBytes;
    const int mb = r.mem_budget_mb < 256 ? 256 : r.mem_budget_mb;
    return static_cast<std::uint64_t>(mb) << 20;
}
inline int fetch_jobs_for_budget(int jobs, int mem_budget_mb) {
    if (jobs < 1)
        jobs = 1;
    if (jobs > 32)
        jobs = 32;
    if (mem_budget_mb > 0 && mem_budget_mb < 1024)
        return jobs < 2 ? jobs : 2;
    if (mem_budget_mb > 0 && mem_budget_mb < 2048)
        return jobs < 4 ? jobs : 4;
    return jobs;
}

struct PlannedTile {
    int lod = 0;
    int ix = 0;
    int iy = 0; // 0 = south
    GeoBox box;
    int dem_w = 0;
    int dem_h = 0;
    int img_w = 0;
    int img_h = 0;
};

struct FetchPlan {
    std::string icao;
    std::string dem_kind;
    std::string img_kind;
    std::string attribution;
    std::vector<LodSpec> lods;
    std::vector<PlannedTile> tiles; // dense grid per LOD; same bbox for DEM+img
    std::vector<std::string> urls;
    FetchEstimate estimate;
    bool covers_faa = false;
    std::vector<std::string> uncovered;
};

GeoBox box_from_radius(double lat, double lon, double radius_km);
GeoBox union_boxes(const std::vector<GeoBox>& boxes);
std::vector<GeoBox> coverage_boxes(const AirportRef& apt, const FetchRequest& req);
std::vector<LodSpec> default_lods(double radius_km); // synth / offline
std::vector<LodSpec> fetch_lods(const FetchRequest& req);
GeoBox lod_coverage(const AirportRef& apt, const FetchRequest& req, const LodSpec& lod);
std::vector<RasterTile> tile_raster(const GeoBox& box, double m_per_px, int max_px);
int clamp_export_px(int px); // 64…kMaxServiceExportPx
int pixel_span(const GeoBox& b, bool north_south, double m_per_px, int cap);
std::vector<PlannedTile> plan_lod_tiles(const AirportRef& apt, const FetchRequest& req,
                                        const LodSpec& lod);
std::string format_fetch_plan(const FetchPlan& plan, const AirportRef& apt,
                              const FetchRequest& req);
bool cache_tile_ready(const std::string& tile_dir);
bool file_looks_like_raster(const std::string& path);
FetchEstimate estimate_fetch(const FetchRequest& req);
AirportRef lookup_airport_ref(const std::string& icao); // throws if unknown

// Highest published sample spacing for a source. 0 if unknown.
double native_imagery_m(const std::string& kind); // nys|hro|naip|s2|hillshade
double native_dem_m(const std::string& kind);     // 3dep|glo30
// Fill 0 m/px slots from the source native spacing. Disk-unlimited defaults.
FetchRequest resolved_fetch(const FetchRequest& req);

// curl glob-safe: no raw [ ] that curl would treat as a character class.
bool curl_glob_safe(const std::string& url);
std::string http_get_command(const std::string& url, const std::string& dest);
std::string http_post_command(const std::string& url, const std::string& body_file,
                              const std::string& dest);
bool http_post_file(const std::string& url, const std::string& body, const std::string& dest);

std::size_t count_cache_tiles(const std::string& dir);

std::string glo30_tile_url(int lat_deg, int lon_deg);
std::vector<std::string> glo30_urls_for_box(const GeoBox& b);
std::string usgs_export_url(const char* service, const GeoBox& b, int w, int h, const char* format,
                            const char* pixel = nullptr);
// nys | hro | naip. NYS Latest ortho (~0.5–1 ft/px, NYC/LI); HRO via USGSNAIPPlus.
std::string imagery_export_url(const std::string& kind, const GeoBox& b, int w, int h);
bool imagery_covers_nys(double lat, double lon);
std::string s2_tile_url(int z, int x, int y);

bool http_get_file(const std::string& url, const std::string& dest);
FetchPlan plan_fetch(const FetchRequest& req);
// Downloads (unless dry_run) and writes lod*/ + root elev/imagery. Returns bytes written.
std::uint64_t fetch_and_pack(const FetchRequest& req, FetchPlan* plan_out = nullptr);

// Pack an already-loaded region into a LOD pyramid (no network).
std::uint64_t pack_lods(const CacheRegion& src, const std::string& dir, const std::vector<LodSpec>& lods,
                        bool mips = true);

DemGrid resample_dem(const DemGrid& src, const GeoBox& box, int w, int h);
ImageRgb resample_rgb(const ImageRgb& src, int w, int h);

std::uint64_t directory_bytes(const std::string& dir);

// ARP-centered coarse DEM from packed tiles. Holds at most one tile DEM.
DemGrid merge_root_dem_from_cache(const std::string& cache_dir, const GeoBox& box, int n = 256);
void log_fetch_rss(const char* where, std::size_t packed, std::size_t planned);
// Write one tile (overlay + DDS/DEM) and drop the rasters. Used by fetch
// and the RSS bound bench. Pass-by-value so the caller can move in.
void write_streamed_cache_tile(const std::string& tile_dir, DemGrid dem, ImageRgb img,
                               const GeoBox& box, const std::string& icao, bool bc7, bool mips);
// Synth N large tiles the way fetch does (write + drop + merge). Returns
// peak RSS observed during the loop.
std::uint64_t bench_stream_pack_rss(const std::string& dir, int tiles, int dem_n, int img_n);

} // namespace s376
