#pragma once

#include "s376/airport.hpp"
#include "s376/terrain.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace s376 {

// Sim-wide skyline. Default is the accurate current city. Classic is a
// hidden, session-only easter egg (version × 7 within 15 s): stylized
// pre-2001 Lower Manhattan. Accuracy is not required for Classic (see
// docs/ACCURACY.md). Once on, it stays on until the channels reset
// (host close/reopen, or refresh_channels). Reposition and leaving NYC
// do not clear it; Classic only *draws* inside the NYC geofence.
enum class SkylineEra {
    Current, // post-2001 / present
    Classic  // pre-2001
};

// Which documented standing period a landmark belongs to.
enum class LandmarkEra {
    Always,  // stood in both periods
    Current, // completed after 2001; hidden in Classic
    Classic  // gone (or replaced) after 2001; hidden in Current
};

struct Landmark {
    const char* id = "";
    const char* name = "";
    LandmarkEra era = LandmarkEra::Current;
    int standing_from = 0; // year completed / opened
    int standing_to = 0;   // 0 = still standing
    double lat = 0;
    double lon = 0;
    double hdg_deg = 0;
    double length_m = 40;
    double width_m = 40;
    float height_m = 100; // architectural roof / occupied top
    float tip_m = 0;      // antenna / spire; 0 = none
    double hide_radius_m = 80; // drop nearby OSM footprints (we own the massing)
    const char* source = "";
    const char* retrieved = "";
};

// Instructor / settings overlay hit-test (window px, origin bottom-left).
enum class InstructorHit { None, Version, EraCurrent, EraClassic, MissilesOff, MissilesOn };

struct EraUnlock {
    static constexpr int kNeed = 7;
    static constexpr int kWindowMs = 15000;
    int clicks = 0;
    std::int64_t window_start_ms = 0;
    bool unlocked = false;
    std::string toast;

    // Click the build / version label. Returns true when unlock flips on.
    bool click(std::int64_t now_ms);
};

struct SegHit {
    bool hit = false;
    double lat = 0;
    double lon = 0;
    double z_m = 0;
    double t = 0; // 0..1 along the segment
};

struct SolidMem {
    std::size_t volumes = 0;
    std::size_t bytes = 0;
    std::size_t airport_volumes = 0; // terminals, hangars, tower, jetbridges
    std::size_t osm_volumes = 0;     // houses / other OSM
    std::size_t landmark_volumes = 0;
};

// Always-resident compact index: packed footprints + heights in a
// uniform grid. Independent of mesh LOD, render cap, and tile paging.
class SolidBvh {
  public:
    void clear();
    void add_building(const Building& b, double ground_m);
    void add_landmark(const Landmark& lm, double ground_m);
    void add_jetbridge(const Jetbridge& j, double ground_m);
    void build();
    // Drop OSM houses until `bytes()` fits. Never drops airport / landmarks.
    void trim_osm_to_budget(std::size_t max_bytes = kMaxSolidBytes);
    // Re-base each prism on DEM ground (centroid). Height AGL is kept.
    void apply_ground(const HeightSource* dem, double fallback_m);

    // Highest roof MSL (metres) whose footprint contains the point.
    bool roof_msl(double lat, double lon, double* z_m) const;
    SegHit segment(double lat0, double lon0, double z0_m, double lat1, double lon1,
                   double z1_m) const;
    std::size_t volume_count() const { return recs_.size(); }
    std::size_t bytes() const;

  private:
    struct Rec {
        float west = 0, south = 0, east = 0, north = 0;
        float height_m = 0;
        float z0_m = 0;
        std::uint32_t vert0 = 0;
        std::uint16_t nvert = 0;
        std::uint8_t kind = 0; // 0 OSM house, 1 airport structure, 2 landmark
        float z1_m() const { return z0_m + height_m; }
    };
    std::vector<Rec> recs_;
    std::vector<float> verts_; // lat, lon pairs
    double gwest_ = 0, gsouth_ = 0, geast_ = 0, gnorth_ = 0;
    int gx_ = 0, gy_ = 0;
    std::vector<std::uint32_t> cell_off_;
    std::vector<std::uint32_t> cell_ids_;

    bool point_in(const Rec& v, double lat, double lon) const;
    int cell_at(double lat, double lon) const;

    friend bool write_solids_bin(const SolidBvh& bvh, const std::string& path);
    friend int read_solids_bin(SolidBvh& bvh, const std::string& path);
    friend SolidMem solids_mem(const SolidBvh& bvh);
};

bool write_solids_bin(const SolidBvh& bvh, const std::string& path);
int read_solids_bin(SolidBvh& bvh, const std::string& path); // append, no build

const Landmark* nyc_landmarks(int* count);
const Landmark* find_landmark(const char* id);
bool landmark_visible(const Landmark& lm, SkylineEra era);
SkylineEra parse_skyline_era(const std::string& s); // throws
const char* skyline_era_name(SkylineEra e);
const char* landmark_era_name(LandmarkEra e);

// Android-style countdown. Empty before the 4th click.
// 4 → "Click 3 more times to unlock Classic NYC skyline (pre-2001)"
// 5 → "Click 2 more times..."
// 6 → "Click 1 more time..."
// 7+ → "Classic skyline unlocked"
std::string era_unlock_toast(int clicks);

InstructorHit instructor_hit(float x, float y, bool unlocked);

std::vector<Building> landmark_buildings(SkylineEra era);
// Drop OSM (or other) footprints that sit on a catalog landmark so the
// era-tagged massing wins.
void apply_skyline_era(std::vector<Building>& buildings, SkylineEra era);

// Full-height landmark mesh (no 80 m OSM cap) + UVs for the facade shader.
AirportMesh mesh_landmarks(SkylineEra era, double ground_m = 3.0);
// FAA-style red obstruction lamps on roofs / the 1 WTC antenna.
std::vector<LightPoint> landmark_warning_lights(SkylineEra era);

void fill_solids(SolidBvh& bvh, SkylineEra era, const std::vector<Airport>& apts,
                 double default_ground_m = 3.0, const HeightSource* dem = nullptr);
SolidMem solids_mem(const SolidBvh& bvh);

// HAT/HOT with DEM + active-era solids. Optional previous eye for a
// segment test (tunneling through a building is a hit → HAT ≤ 0).
HatSample sample_hat_solids(const HeightSource* src, const SolidBvh* solids, double lat_deg,
                            double lon_deg, double alt_ft, const double* prev_lat = nullptr,
                            const double* prev_lon = nullptr, const double* prev_alt_ft = nullptr);

// Process session. Always Current at start. Not restored after reset.
SkylineEra session_era();          // chosen setting (may be Classic off-NYC)
SkylineEra active_skyline_era();   // what to draw / collide: Classic only in NYC
bool session_era_unlocked();
void set_session_era(SkylineEra era, bool unlocked);
// Host "refresh channels" / close+reopen: back to Current, lock the egg.
void refresh_channels();
const SolidBvh& session_solids();
void rebuild_session_solids(const std::vector<Airport>& apts, const HeightSource* dem = nullptr);
void session_apply_ground(std::shared_ptr<const HeightSource> dem);
// DEM at the point when the host has called set_heights; else fallback.
double session_ground_m(double lat, double lon, double fallback_m);
std::int64_t session_started_ms();

// NY region box (data/regions/ny.toml). Classic *renders* only inside this.
constexpr double kNycSouth = 40.40;
constexpr double kNycNorth = 40.98;
constexpr double kNycWest = -74.45;
constexpr double kNycEast = -73.45;
bool in_nyc_geofence(double lat, double lon);
SkylineEra visible_skyline_era(SkylineEra chosen, double lat, double lon);

// Track eyepoint for the geofence. Does not clear Classic.
void tick_session_view(double lat, double lon);
HatSample sample_session_hat(const HeightSource* src, double lat_deg, double lon_deg,
                             double alt_ft, const double* prev_lat = nullptr,
                             const double* prev_lon = nullptr,
                             const double* prev_alt_ft = nullptr);

} // namespace s376
