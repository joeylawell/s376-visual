#pragma once

#include "s376/fetch.hpp"
#include "s376/terrain.hpp"

#include <string>
#include <vector>

namespace s376 {

// Per-airport public imagery. Raster-local is a fallback on the owner's
// machine only — never a committed product. Pick whichever is sharper.

enum class ImageryPolicy {
    Public,       // free source is at least as sharp as Raster
    Either,       // public roughly matches Raster (KRNO / NAIP)
    RasterLocal,  // no verified public source as sharp as Raster
};

struct AirportImagery {
    const char* icao = "";
    const char* name = "";
    const char* public_kind = ""; // nys|njogis|massgis|…|naip|s2|none
    double public_m = 0;          // native metres / px; 0 = none
    const char* public_label = "";
    const char* license = "";
    const char* notes = "";
    ImageryPolicy policy = ImageryPolicy::Public;
};

const AirportImagery* airport_imagery(const std::string& icao);
const AirportImagery* airport_imagery_table(int* count);
bool needs_raster_fallback(const std::string& icao);
bool imagery_kind_is_live_pull(const std::string& kind);
std::string imagery_policy_name(ImageryPolicy p);
std::string format_imagery_sources_table();
std::string guess_icao_from_path(const std::string& path);

struct ImportScan {
    std::string path;
    std::string icao;
    int files = 0;
    int decodable = 0;
    int dems = 0;
    int opaque = 0;
    std::vector<std::string> opaque_ext; // e.g. ".nxt x142"
    std::vector<std::string> decodable_paths;
    std::vector<std::string> dem_paths;
    std::string error;
};

struct ImportRequest {
    std::string src_dir;
    std::string out_dir;
    std::string icao; // empty = guess from path
    bool dry_run = false;
};

// Read-only scan of an operator-supplied airport folder. Does not copy
// files. Does not document vendor record layouts.
ImportScan scan_import_dir(const std::string& dir);
std::string format_import_scan(const ImportScan& s);
std::string format_import_needed(); // what we can decode

// Convert georeferenced rasters GDAL can read into our cache. Throws if
// nothing decodable is on disk. Never writes into src_dir.
std::uint64_t import_raster(const ImportRequest& req, ImportScan* scan_out = nullptr);

} // namespace s376
