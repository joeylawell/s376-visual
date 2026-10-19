#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace s376 {

// Row-major geodetic height field. Row 0 is north. Units: metres MSL.
struct DemGrid {
    int w = 0;
    int h = 0;
    double west = 0;  // lon
    double south = 0; // lat
    double east = 0;
    double north = 0;
    std::vector<float> z_m;

    bool empty() const { return w <= 1 || h <= 1 || z_m.size() != static_cast<std::size_t>(w * h); }
    bool in_bounds(double lat, double lon) const;
    bool sample_m(double lat, double lon, double& z_m_out) const;
    bool sample_ft(double lat, double lon, double& z_ft_out) const;

    static DemGrid synth_ksea(int w = 128, int h = 128);
    static DemGrid load(const std::string& path); // .s376dem
    // Header + bbox only (z_m empty). For tile indexes — does not allocate w*h.
    static DemGrid peek(const std::string& path);
    // Load a mesh-sized grid (max_n per side) without keeping the full raster.
    static DemGrid load_subsampled(const std::string& path, int max_n);
    // One bilinear sample without loading the full raster (header + 4 floats).
    static bool sample_file_m(const std::string& path, double lat, double lon, double& z_m_out);
    void save(const std::string& path) const;

    static DemGrid load_geotiff(const std::string& path); // throws if no GDAL / bad file
    void save_geotiff(const std::string& path) const;
};

struct ImageRgb {
    int w = 0;
    int h = 0;
    std::vector<std::uint8_t> rgb; // w*h*3

    static ImageRgb hillshade(const DemGrid& dem);
    static ImageRgb load_jpeg_or_png(const std::string& path); // OSG/GDAL later; unused if empty
    // Georeferenced RGB GeoTIFF for osgEarth. Bounds come from `georef`.
    void save_geotiff(const std::string& path, const DemGrid& georef) const;
};

struct GeorefRgb {
    ImageRgb img;
    double west = 0;
    double south = 0;
    double east = 0;
    double north = 0;
    bool ok() const { return !img.rgb.empty() && east > west && north > south; }
};

GeorefRgb load_georef_rgb(const std::string& path);
bool peek_georef(const std::string& path, double& west, double& south, double& east, double& north,
                 int& w, int& h, int& bands);

class HeightSource {
  public:
    virtual ~HeightSource() = default;
    // true if a local DEM covers the point. On miss, elev_msl_ft is unchanged.
    virtual bool sample_ft(double lat, double lon, double& elev_msl_ft) const = 0;
};

class DemHeightSource : public HeightSource {
  public:
    explicit DemHeightSource(std::shared_ptr<DemGrid> dem) : dem_(std::move(dem)) {}
    bool sample_ft(double lat, double lon, double& elev_msl_ft) const override;
    std::shared_ptr<DemGrid> grid() const { return dem_; }

  private:
    std::shared_ptr<DemGrid> dem_;
};

struct CacheRegion {
    std::string dir;
    std::string name = "region";
    DemGrid dem;
    ImageRgb imagery;
    std::string imagery_dds; // path, may be empty
};

CacheRegion generate_ksea_cache();
void write_cache(const CacheRegion& r, const std::string& dir);
CacheRegion load_cache(const std::string& dir);
// Like load_cache, but DEM is subsampled for a mesh (never 4000² verts).
CacheRegion load_cache_mesh(const std::string& dir, int max_mesh_n = 96);
bool gdal_available();

struct CacheTileRef {
    std::string path;
    int lod = 0;
    double west = 0, south = 0, east = 0, north = 0;
    int dem_w = 0, dem_h = 0;
    std::uint64_t dds_bytes = 0;
    std::uint64_t dem_bytes = 0;
};

// Far-field contract: lod1/lod2 must stay resident out to this disk so
// a 2.99 nm 04R shot still has Jamaica Bay / Rockaway / Atlantic / Queens
// behind the airport instead of empty sky. 40 nm = 74.08 km.
constexpr double kFarFieldNm = 40.0;
constexpr double kFarFieldKm = 40.0 * 1.852;

struct TilePageWant {
    const CacheTileRef* t = nullptr;
    double km = 0;
    double score = 0;
    bool reserve_coarse = false;
};

// Score tiles for the pager. Reserved coarse (lod≥1 inside 40 nm) must
// page before extra lod0 so a 2 GB RAM budget cannot starve the far field.
void score_tiles_for_view(const std::vector<CacheTileRef>& index, double elat, double elon,
                          double tlat, double tlon, double hdg_true, bool have_eye,
                          bool coarse_only, std::vector<TilePageWant>& want);

// Public-source KJFK subset in Joey's hangar layout (ATTRIBUTION.txt,
// coverage.toml, elev.*, imagery.*, lod0/lod1/lod2, region.toml).
bool write_kjfk_farfield_subset(const std::string& dir);
// Public land_coverage plus borough/beach/marsh classes for the synth pack.
// Not surveyed imagery — only used when no ortho is on disk.
void kjfk_subset_land_rgb(double lat_deg, double lon_deg, std::uint8_t& r, std::uint8_t& g,
                          std::uint8_t& b, bool& water);

// Index lodN/ and lodN/tY_X without loading rasters.
std::vector<CacheTileRef> scan_cache_tiles(const std::string& dir);
double tile_center_lat(const CacheTileRef& t);
double tile_center_lon(const CacheTileRef& t);
// CPU bytes if we mesh at max_n and keep the compressed DDS off-heap after upload.
std::uint64_t estimate_tile_cpu_bytes(const CacheTileRef& t, int max_mesh_n = 96);
// cache/<ICAO> or cache/<icao> when `configured` is empty. Empty if none exist.
std::string resolve_cache_dir(const std::string& configured, const std::string& icao);
bool cache_dir_looks_packed(const std::string& dir);
// Header + payload present (not a fetch-in-progress truncated write).
bool dem_file_complete(const std::string& path);

// Process RSS: Linux VmRSS / VmHWM, Windows WorkingSet / PeakWorkingSet.
// 0 if unavailable.
std::uint64_t current_rss_bytes();
std::uint64_t peak_rss_bytes();
// Cap GDAL's block cache so a long fetch cannot grow it with tile count.
void limit_gdal_cache_mb(int mb);

// HAT = eyepoint MSL − terrain MSL. HOT is the terrain elevation.
// If src is null or the point is off the DEM, elev=0 and hat=alt (stand-in).
struct HatSample {
    bool covered = false;
    float hat_ft = 0;
    float elev_ft = 0;
    bool building = false;  // HOT is a roof, not bare DEM
    bool intersect = false; // segment through a solid (tunnel / crash)
};
HatSample sample_hat(const HeightSource* src, double lat_deg, double lon_deg, double alt_ft);

std::shared_ptr<DemGrid> load_or_synth_dem(const std::string& cache_dir);

} // namespace s376
