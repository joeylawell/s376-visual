#pragma once

#include "s376/geo.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace s376 {

// Our own packet-112 switch numbers (not a vendor list). Runway 1
// defaults: see data/lights/switches.toml (base 0 + offsets 1..8).
constexpr int kSwApproach = 1;
constexpr int kSwStrobe = 2;
constexpr int kSwRunwayEdge = 3;
constexpr int kSwCenterline = 4;
constexpr int kSwVasi = 5;
constexpr int kSwThreshold = 6; // touchdown zone
constexpr int kSwTaxiEdge = 41;
constexpr int kSwStopBar = 56;
constexpr int kSwTaxiCenterline = 60;

enum class LightKind {
    Edge,
    Centerline,
    Approach,
    Threshold,
    Strobe,
    Vasi,
    TaxiEdge,
    StopBar,
    TaxiCenterline,
    Flasher,
    Reil,
    Rwsl,
    End,
    LeadOn,
    SignGlow,
    Beacon,
    Night
};

// Mesh / taxi bake caps (980 Ti draw). Collision is not these: every
// OSM footprint goes into solids.bin / SolidBvh, independent of LOD.
constexpr std::size_t kMaxOsmTaxiways = 800;
constexpr std::size_t kMaxOsmAprons = 200;
constexpr std::size_t kMaxOsmBuildings = 12000;
// Bake working set: keep every footprint until rank_osm_buildings, then
// write_osm_bin caps the mesh. Queens houses must not evict Manhattan.
constexpr std::size_t kMaxOsmBuildingsBake = 80000;
constexpr std::size_t kMaxOsmRoads = 400;
// Airfield campus (≤ ~2.2 km of ARP): full-detail walls.
constexpr int kMeshBuildingsNear = 800;
// Neighbourhood ring (0.8–16 km): Queens / Brooklyn / Rockaway houses.
// Official + landmark buildings are always meshed and do not count here.
constexpr int kMeshBuildingsFar = 2500;
// Tall OSM footprints 0.8–28 km (Manhattan / Queens cores). Real heights.
constexpr int kMeshBuildingsSkyline = 800;
constexpr float kSkylineHeightM = 40.f;
constexpr double kSkylineRingKm = 28.0;
// OSM `height`, else `building:levels` × this, else the default. Metres AGL.
constexpr float kOsmLevelHeightM = 3.5f;
constexpr float kDefaultBuildingHeightM = 12.f; // ~4 storeys when OSM is silent
// Mesh sit: buildings rest this far above DEM/ARP so they clear pavement.
constexpr float kBuildingSitM = 0.5f;
// Jetbridge deck matches `build_airport_mesh` (strip at ARP + 5 m, half-width 2.2).
constexpr float kJetbridgeHalfWm = 2.2f;
constexpr float kJetbridgeDeckM = 5.5f;
// Always-resident collision budget. Over this, drop OSM houses first —
// never airport structures (terminals, hangars, tower, jetbridges).
constexpr std::size_t kMaxSolidBytes = 256ull * 1024ull * 1024ull;

enum class BuildingKind : std::uint8_t { Osm = 0, Airport = 1, Landmark = 2 };

struct LightPoint {
    double lat = 0;
    double lon = 0;
    double alt_m = 0;
    float r = 1;
    float g = 1;
    float b = 1;
    // Reverse-direction colour for bidirectional HIRL / RCL. r2 < 0 = unused.
    float r2 = -1.f;
    float g2 = 0;
    float b2 = 0;
    float color_hdg_deg = 0; // looking along this heading uses r/g/b
    int switch_num = kSwRunwayEdge;
    LightKind kind = LightKind::Edge;
    int runway_k = 0; // 1..7, 0 = taxi / airport-wide
    int seq = -1;     // sequenced-flasher index, −1 = none
    int papi_box = -1;     // 0..3 inboard→outboard; −1 = not a PAPI unit
    float papi_gs_deg = 3.f;
    float papi_rotate_deg = 0;
    float papi_usable_left_deg = 15.f;
    float papi_usable_right_deg = 15.f;
    float papi_rwy_hdg_deg = 0;
};

struct DataSource {
    std::string name;
    std::string effective; // NASR / AIP 28-day cycle
    std::string retrieved;
};

struct Runway {
    std::string ident_le;
    std::string ident_he;
    double le_lat = 0;
    double le_lon = 0;
    double le_elev_m = 0;
    double he_lat = 0;
    double he_lon = 0;
    double he_elev_m = 0;
    double heading_deg = 0;    // LE true heading (from both ends)
    double heading_he_deg = 0; // HE true heading
    double length_m = 0;
    double width_m = 0;
    std::string approach_le = "MALSR";
    std::string approach_he = "MALSR";
    double le_displaced_ft = 0;
    double he_displaced_ft = 0;
    std::string papi_le = "P4L";
    std::string papi_he = "P4L";
    double papi_deg_le = 3.0;
    double papi_deg_he = 3.0;
    double vgsi_tch_ft_le = 0;
    double vgsi_tch_ft_he = 0;
    double ils_tch_ft_le = 0;
    double ils_tch_ft_he = 0;
    double papi_rotate_le = 0;
    double papi_rotate_he = 0;
    double papi_usable_left_le = 15;
    double papi_usable_right_le = 15;
    double papi_usable_left_he = 15;
    double papi_usable_right_he = 15;
    int papi_count_le = 1;
    int papi_count_he = 1;
    bool rlls_lead_in_le = false;
    bool rlls_lead_in_he = false;
    bool reil_le = false;
    bool reil_he = false;
    bool tdz_le = false;
    bool tdz_he = false;
    bool emas_le = false;
    bool emas_he = false;
    double emas_le_len_ft = 0;
    double emas_he_len_ft = 0;
    double emas_wid_ft = 0;
    DataSource source;
};

struct Taxiway {
    std::string name;
    std::vector<std::pair<double, double>> path; // lat, lon
    double width_m = 23;
    // osm.bin pavement only — FAA edge/CL/stop bars stay on official taxi.
    bool from_osm = false;
};

enum class RoadClass { Motorway, Trunk, Primary, Secondary };

struct Road {
    std::string name;
    RoadClass cls = RoadClass::Secondary;
    std::vector<std::pair<double, double>> path; // lat, lon
    std::vector<double> seg_len_m;
    double length_m = 0;
};

struct Apron {
    std::string name;
    std::vector<std::pair<double, double>> ring; // lat, lon, closed or open
};

struct Building {
    std::string name;
    std::vector<std::pair<double, double>> ring;
    float height_m = 12;
    BuildingKind kind = BuildingKind::Osm;
    std::string type; // OSM building=* or schematic role (hangar/cargo/…)
    float roof_r = -1.f; // <0 = palette; else imagery / public roof colour
    float roof_g = -1.f;
    float roof_b = -1.f;
};

struct AirportSign {
    std::string text;
    double lat = 0;
    double lon = 0;
    double hdg_deg = 0;
};

struct HoldBar {
    double lat = 0;
    double lon = 0;
    double hdg_deg = 0;
    double width_m = 23;
};

struct Tree {
    double lat = 0;
    double lon = 0;
    float height_m = 10;
};

struct ParkedAircraft {
    std::string stand;
    double lat = 0;
    double lon = 0;
    double hdg_deg = 0;
};

struct Jetbridge {
    std::string gate;
    double lat = 0;
    double lon = 0;
    double hdg_deg = 0;
    double length_m = 28;
};

// Schematic terminals / hangars / ATCT / cargo — not OSM houses.
bool is_airport_structure(const Building& b);
// Promote OSM terminal/hangar/tower ways.
void classify_osm_building(Building& b);
void rank_osm_buildings(std::vector<Building>& buildings, double arp_lat, double arp_lon);
// Public PANYNJ / FAA JFK ATCT architectural height (321 ft).
constexpr float kKjfkAtctHeightM = 97.8f;
// Same footprint as the jetbridge mesh strip (length × 4.4 m).
Building jetbridge_building(const Jetbridge& j);
void building_centroid(const Building& b, double& lat, double& lon);

struct DockingGuide {
    std::string stand;
    double lat = 0;
    double lon = 0;
    double hdg_deg = 0;
};

struct SceneRoute {
    std::string name;
    std::string kind; // taxi | road | air
    std::vector<std::pair<double, double>> path;
};

struct Marshaller {
    std::string gate;
    double lat = 0;
    double lon = 0;
    double hdg_deg = 0;
};

struct NightLayer {
    double spacing_m = 35; // typical lit-highway cobra-head spacing, not a grid
    int points = 0;
};

struct Airport {
    std::string icao;
    std::string name;
    std::string attribution;
    double arp_lat = 0;
    double arp_lon = 0;
    double elev_m = 0;
    DataSource source;
    int sw_edge = kSwRunwayEdge;
    int sw_centerline = kSwCenterline;
    int sw_approach = kSwApproach;
    int sw_threshold = kSwThreshold;
    int sw_taxi_edge = kSwTaxiEdge;
    int sw_stop_bar = kSwStopBar;
    int sw_taxi_centerline = kSwTaxiCenterline;
    std::vector<Runway> runways;
    std::vector<Taxiway> taxiways;
    std::vector<Apron> aprons;
    std::vector<Building> buildings;
    std::string solids_path; // cache/<ICAO>/solids.bin — full OSM collision set
    std::vector<LightPoint> lights;
    std::vector<AirportSign> signs;
    std::vector<HoldBar> hold_bars;
    std::vector<Tree> trees;
    std::vector<ParkedAircraft> parked;
    std::vector<Jetbridge> jetbridges;
    std::vector<DockingGuide> docking;
    std::vector<SceneRoute> routes;
    std::vector<Marshaller> marshallers;
    std::vector<Road> roads;
    // OSM natural=water rings (bake → shoreline.bin). Not meshed.
    std::vector<std::vector<std::pair<double, double>>> water_rings;
    NightLayer night;
    double tower_lat = 0;
    double tower_lon = 0;
    float tower_h_m = 0;
    bool has_beacon = false;
};

struct MeshVert {
    geo::Ecef p;
    geo::Ecef n;
    float r = 0.18f;
    float g = 0.18f;
    float b = 0.19f;
    float mat = 0; // 0 asphalt, 1 marking, 2 concrete, 3 wall, 4 roof, 5 spire
    float u = 0;
    float v = 0;
};

struct AirportMesh {
    std::vector<MeshVert> verts; // triangles, 3 at a time
};

struct ShotPose {
    std::string icao;
    std::string rwy;
    double dist_nm = 0;
    double lat = 0;
    double lon = 0;
    double alt_ft = 0;
    double hdg_true_deg = 0;
    double pitch_deg = 0;
    double roll_deg = 0;
};

struct ShotOverrides {
    bool have_alt = false;
    double alt_ft = 0;
    bool have_bank = false;
    double bank_deg = 0;
    bool have_hdg = false;
    double hdg_deg = 0;
    bool have_pitch = false;
    double pitch_deg = 0;
};

// Official FAA NASR / national AIP AD 2 both-end layouts. Taxi
// connectors are schematic offsets from those runways (stand-in for an
// Overpass aeroway=* download). No RSI / CAE geometry.
Airport ksea_public();
Airport ksfo_public();
Airport klas_public();
Airport klax_public();
Airport kjfk_public();
Airport kewr_public();
Airport kbos_public();
Airport kiad_public();
Airport kmco_public();
Airport kmia_public();
Airport krno_public();
Airport eddf_public();
Airport egll_public();
Airport lebl_public();
Airport seqm_public();
Airport skbo_public();
Airport skrg_public();
Airport vnkt_public();
Airport airport_by_icao(const std::string& icao);
Airport airport_from_faa(const std::string& icao); // official FAA/AIP both ends
std::vector<Airport> all_public_airports();

struct ApproachPlan {
    std::string icao;
    std::string rwy;
    double gs_deg = 3.0;
    double start_agl_ft = 2000;
    double kts = 140;
    double fps = 30;
    double thr_lat = 0;
    double thr_lon = 0;
    double thr_elev_m = 0;
    double hdg_deg = 0;
    double start_dist_m = 0; // along-track before the landing threshold
};

// `KJFK:04R` (optional `:nm` ignored). Eyepoint AGL at t=0 is start_agl_ft.
// Straight geometric glidepath: AGL = 0 at the landing threshold.
bool plan_approach(const std::string& spec, double gs_deg, double start_agl_ft, ApproachPlan& out);
double approach_time_s(const ApproachPlan& p); // start → threshold along the path
int approach_frame_count(const ApproachPlan& p); // includes the touchdown frame
ShotPose approach_sample(const ApproachPlan& p, double t_s);

// `KJFK`, `KJFK:04R`, or `KJFK:04R:3` (nm before the threshold on a 3° path).
ShotPose parse_shot_at(const std::string& spec);
void apply_shot_overrides(ShotPose& p, const ShotOverrides& o);
bool runway_shot_pose(const Airport& a, const std::string& ident, double dist_nm, ShotPose& out);

// Merge Overpass `out geom` JSON (aeroway=taxiway|apron). Returns ways added.
int merge_osm_json(Airport& a, const std::string& json);
// Raw Overpass QL (may contain [ ]). Never pass this string to curl unquoted.
std::string overpass_aeroway_query(double south, double west, double north, double east);
// GET URL with [ ] { } percent-encoded so curl globbing cannot expand it.
std::string overpass_aeroway_url(double south, double west, double north, double east);
// KJFK / Queens / Manhattan / Rockaway Overpass tiles (ODbL). Name + SWNE.
struct OsmTile {
    const char* name;
    double south, west, north, east;
};
int osm_metro_tile_count(const std::string& icao);
const OsmTile* osm_metro_tiles(const std::string& icao);
// Load that JSON from a file (0 if missing). For bake tools only — the
// renderer must not open a 400 MB Overpass dump.
int try_merge_osm_file(Airport& a, const std::string& path);
// Stream-parse a (possibly huge) Overpass file. Never slurp the whole file.
int merge_osm_file_stream(Airport& a, const std::string& path, std::uint64_t max_bytes = 0,
                          class SolidBvh* solids = nullptr);
// Compact runtime sidecar. The IG loads only this, never osm.json.
bool write_osm_bin(const Airport& a, const std::string& path);
int read_osm_bin(Airport& a, const std::string& path);
// Bake osm.json → osm.bin (mesh-capped) + solids.bin (every building).
int bake_osm_bin(const std::string& json_path, const std::string& bin_path);
// Runtime: osm.bin only. Does not open osm.json.
int try_load_osm(Airport& a, const std::string& cache_dir);
void prefer_osm_airport_massing(Airport& a);
void apply_kjfk_roof_colours(Airport& a);

void generate_lights(Airport& a);
void add_kjfk_public_scenery(Airport& a); // 2022 taxi names, T5, tower, trees…
void add_kewr_public_scenery(Airport& a);
void add_scenery_lights(Airport& a);
// City/street lamps from a.roads + OSM building density + apron floods.
// Never a regular grid. Never over water/marsh (land_coverage floor).
void add_street_lights(Airport& a);
AirportMesh build_airport_mesh(const Airport& a, bool buildings = true, bool markings = true);

// GeoJSON FeatureCollection (runways / taxi / aprons) for an osgEarth
// feature_geom layer. Coordinates are lon,lat. Not an RSI extract.
std::string airport_geojson(const Airport& a);
int write_airport_geojson(const Airport& a, const std::string& path);

// Offset a geodetic point by metres along a true heading (local WGS84).
void offset_m(double lat, double lon, double heading_deg, double dist_m, double& olat,
              double& olon);

const char* light_kind_name(LightKind k);

} // namespace s376
