#include "s376/landmarks.hpp"

#include "s376/geo.hpp"
#include "s376/missiles.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <istream>
#include <mutex>
#include <ostream>
#include <stdexcept>

namespace s376 {
namespace {

constexpr double kManhattanGroundM = 3.0;

// Public, documented massing. Coordinates and heights from published
// Port Authority / NYC / owner figures (not Raster). Retrieved 2026-10-18.
const Landmark kNyc[] = {
    {"wtc1-1973",
     "1 World Trade Center",
     LandmarkEra::Classic,
     1972,
     2001,
     40.7117,
     -74.0134,
     0.0,
     63.4,
     63.4,
     417.f,
     526.7f,
     140.0,
     "Port Authority roof 1,368 ft / antenna 1,728 ft; NYC WGS84",
     "2026-10-18"},
    {"wtc2-1973",
     "2 World Trade Center",
     LandmarkEra::Classic,
     1973,
     2001,
     40.7108,
     -74.0131,
     0.0,
     63.4,
     63.4,
     415.f,
     0.f,
     140.0,
     "Port Authority roof 1,362 ft; NYC WGS84",
     "2026-10-18"},
    {"wtc3-1981",
     "3 World Trade Center",
     LandmarkEra::Classic,
     1981,
     2001,
     40.7105,
     -74.0114,
     90.0,
     80.0,
     45.0,
     73.5f,
     0.f,
     80.0,
     "Marriott Vista / 3 WTC, 22 storeys; public site plan",
     "2026-10-18"},
    {"wtc4-1977",
     "4 World Trade Center",
     LandmarkEra::Classic,
     1977,
     2001,
     40.7102,
     -74.0120,
     0.0,
     70.0,
     50.0,
     36.f,
     0.f,
     80.0,
     "Southeast Plaza Building, 9 storeys; public site plan",
     "2026-10-18"},
    {"wtc5-1972",
     "5 World Trade Center",
     LandmarkEra::Classic,
     1972,
     2001,
     40.7119,
     -74.0108,
     0.0,
     70.0,
     50.0,
     36.f,
     0.f,
     80.0,
     "Northeast Plaza Building, 9 storeys; public site plan",
     "2026-10-18"},
    {"wtc6-1975",
     "6 World Trade Center",
     LandmarkEra::Classic,
     1975,
     2001,
     40.7126,
     -74.0140,
     0.0,
     65.0,
     50.0,
     32.f,
     0.f,
     80.0,
     "U.S. Customs House, 8 storeys; public site plan",
     "2026-10-18"},
    {"wtc7-1987",
     "7 World Trade Center",
     LandmarkEra::Classic,
     1987,
     2001,
     40.7132,
     -74.0119,
     15.0,
     75.0,
     50.0,
     186.f,
     0.f,
     100.0,
     "Original 7 WTC, 47 storeys / 610 ft; public figures",
     "2026-10-18"},
    {"onewtc",
     "One World Trade Center",
     LandmarkEra::Current,
     2014,
     0,
     40.7127,
     -74.0134,
     0.0,
     61.0,
     61.0,
     417.f,
     541.3f,
     140.0,
     "One WTC architectural 1,776 ft; NYC WGS84",
     "2026-10-18"},
    {"wtc3-2018",
     "3 World Trade Center",
     LandmarkEra::Current,
     2018,
     0,
     40.7107,
     -74.0116,
     0.0,
     55.0,
     45.0,
     329.f,
     0.f,
     90.0,
     "3 WTC, 1,079 ft; public figures",
     "2026-10-18"},
    {"wtc4-2013",
     "4 World Trade Center",
     LandmarkEra::Current,
     2013,
     0,
     40.7104,
     -74.0119,
     0.0,
     50.0,
     50.0,
     298.f,
     0.f,
     90.0,
     "4 WTC, 977 ft; public figures",
     "2026-10-18"},
    {"wtc7-2006",
     "7 World Trade Center",
     LandmarkEra::Current,
     2006,
     0,
     40.7130,
     -74.0118,
     0.0,
     50.0,
     50.0,
     226.f,
     0.f,
     90.0,
     "7 WTC (2006), 743 ft; public figures",
     "2026-10-18"},
    {"432park",
     "432 Park Avenue",
     LandmarkEra::Current,
     2015,
     0,
     40.7614,
     -73.9716,
     0.0,
     28.5,
     28.5,
     425.5f,
     0.f,
     60.0,
     "432 Park, 1,396 ft; public figures",
     "2026-10-18"},
    {"cpt",
     "Central Park Tower",
     LandmarkEra::Current,
     2020,
     0,
     40.7663,
     -73.9808,
     0.0,
     28.0,
     28.0,
     472.4f,
     0.f,
     70.0,
     "Central Park Tower, 1,550 ft; 217 W 57th, NYC WGS84",
     "2026-10-19"},
    {"hudson-10",
     "10 Hudson Yards",
     LandmarkEra::Current,
     2016,
     0,
     40.7528,
     -74.0008,
     0.0,
     45.0,
     40.0,
     267.7f,
     0.f,
     70.0,
     "10 Hudson Yards; public figures",
     "2026-10-18"},
    {"hudson-15",
     "15 Hudson Yards",
     LandmarkEra::Current,
     2019,
     0,
     40.7534,
     -74.0028,
     0.0,
     40.0,
     40.0,
     229.4f,
     0.f,
     70.0,
     "15 Hudson Yards; public figures",
     "2026-10-18"},
    {"hudson-30",
     "30 Hudson Yards",
     LandmarkEra::Current,
     2019,
     0,
     40.7536,
     -74.0009,
     0.0,
     50.0,
     40.0,
     387.1f,
     0.f,
     80.0,
     "30 Hudson Yards, 1,268 ft; public figures",
     "2026-10-18"},
    {"hudson-35",
     "35 Hudson Yards",
     LandmarkEra::Current,
     2019,
     0,
     40.7544,
     -74.0025,
     0.0,
     40.0,
     40.0,
     307.5f,
     0.f,
     70.0,
     "35 Hudson Yards; public figures",
     "2026-10-18"},
    {"hudson-50",
     "50 Hudson Yards",
     LandmarkEra::Current,
     2022,
     0,
     40.7543,
     -73.9996,
     0.0,
     50.0,
     45.0,
     308.2f,
     0.f,
     70.0,
     "50 Hudson Yards; public figures",
     "2026-10-18"},
    {"empire",
     "Empire State Building",
     LandmarkEra::Current,
     1931,
     0,
     40.7484,
     -73.9857,
     0.0,
     57.0,
     57.0,
     381.0f,
     443.2f,
     100.0,
     "ESB roof 1,250 ft / antenna 1,454 ft; NYC WGS84",
     "2026-10-18"},
    {"chrysler",
     "Chrysler Building",
     LandmarkEra::Current,
     1930,
     0,
     40.7516,
     -73.9755,
     0.0,
     30.0,
     30.0,
     282.0f,
     318.9f,
     80.0,
     "Chrysler roof 925 ft / spire 1,046 ft; NYC WGS84",
     "2026-10-18"},
    {"citigroup",
     "Citigroup Center",
     LandmarkEra::Current,
     1977,
     0,
     40.7586,
     -73.9700,
     45.0,
     48.0,
     48.0,
     278.9f,
     0.f,
     70.0,
     "601 Lexington / Citigroup Center 915 ft; public figures",
     "2026-10-18"},
    {"spruce8",
     "8 Spruce Street",
     LandmarkEra::Current,
     2011,
     0,
     40.7107,
     -74.0056,
     0.0,
     34.0,
     34.0,
     265.0f,
     0.f,
     70.0,
     "8 Spruce / New York by Gehry 870 ft; public figures",
     "2026-10-18"},
};

void ring_box(std::vector<std::pair<double, double>>& ring, double lat, double lon, double hdg,
              double len_m, double wid_m) {
    const double hl = len_m * 0.5, hw = wid_m * 0.5;
    const double corners[4][2] = {{-hl, -hw}, {hl, -hw}, {hl, hw}, {-hl, hw}};
    for (int i = 0; i < 4; ++i) {
        double p1lat, p1lon, p2lat, p2lon;
        offset_m(lat, lon, hdg, corners[i][0], p1lat, p1lon);
        offset_m(p1lat, p1lon, hdg + 90.0, corners[i][1], p2lat, p2lon);
        ring.push_back({p2lat, p2lon});
    }
}

Building box_of(const char* name, double lat, double lon, double hdg, double len_m, double wid_m,
                float height_m) {
    Building b;
    b.name = name;
    b.height_m = height_m;
    ring_box(b.ring, lat, lon, hdg, len_m, wid_m);
    return b;
}

double centroid_dist_m(const Building& b, double lat, double lon) {
    if (b.ring.empty())
        return 1e18;
    double clat = 0, clon = 0;
    for (const auto& p : b.ring) {
        clat += p.first;
        clon += p.second;
    }
    clat /= static_cast<double>(b.ring.size());
    clon /= static_cast<double>(b.ring.size());
    return geo::ground_distance_m(clat, clon, lat, lon);
}

void add_tri_uv(AirportMesh& m, const geo::Ecef& a, const geo::Ecef& b, const geo::Ecef& c,
                const geo::Ecef& nrm, float r, float g, float bl, float mat, float u0, float v0,
                float u1, float v1, float u2, float v2) {
    MeshVert v;
    v.n = nrm;
    v.r = r;
    v.g = g;
    v.b = bl;
    v.mat = mat;
    v.p = a;
    v.u = u0;
    v.v = v0;
    m.verts.push_back(v);
    v.p = b;
    v.u = u1;
    v.v = v1;
    m.verts.push_back(v);
    v.p = c;
    v.u = u2;
    v.v = v2;
    m.verts.push_back(v);
}

struct Session {
    SkylineEra era = SkylineEra::Current;
    bool unlocked = false;
    SolidBvh bvh;
    std::mutex mu;
    std::int64_t started_ms = 0;
    bool have_eye = false;
    double lat = 0;
    double lon = 0;
    std::vector<Airport> apts;
    std::shared_ptr<const HeightSource> dem;
};

Session& sess() {
    static Session s;
    static std::once_flag once;
    std::call_once(once, [] {
        s.started_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count();
        fill_solids(s.bvh, SkylineEra::Current, {});
    });
    return s;
}

} // namespace

const Landmark* nyc_landmarks(int* count) {
    if (count)
        *count = static_cast<int>(sizeof(kNyc) / sizeof(kNyc[0]));
    return kNyc;
}

const Landmark* find_landmark(const char* id) {
    if (!id)
        return nullptr;
    int n = 0;
    const Landmark* all = nyc_landmarks(&n);
    for (int i = 0; i < n; ++i) {
        if (std::strcmp(all[i].id, id) == 0)
            return &all[i];
    }
    return nullptr;
}

bool landmark_visible(const Landmark& lm, SkylineEra era) {
    if (lm.era == LandmarkEra::Always)
        return true;
    if (era == SkylineEra::Classic)
        return lm.era == LandmarkEra::Classic;
    return lm.era == LandmarkEra::Current;
}

SkylineEra parse_skyline_era(const std::string& s) {
    if (s.empty() || s == "current")
        return SkylineEra::Current;
    if (s == "classic")
        return SkylineEra::Classic;
    throw std::runtime_error("era must be current|classic, got " + s);
}

const char* skyline_era_name(SkylineEra e) {
    return e == SkylineEra::Classic ? "classic" : "current";
}

const char* landmark_era_name(LandmarkEra e) {
    switch (e) {
    case LandmarkEra::Always:
        return "always";
    case LandmarkEra::Classic:
        return "classic";
    case LandmarkEra::Current:
        return "current";
    }
    return "current";
}

std::string era_unlock_toast(int clicks) {
    if (clicks < 4)
        return {};
    if (clicks >= EraUnlock::kNeed)
        return "Classic skyline unlocked";
    const int left = EraUnlock::kNeed - clicks;
    if (left == 1)
        return "Click 1 more time to unlock Classic NYC skyline (pre-2001)";
    return "Click " + std::to_string(left) + " more times to unlock Classic NYC skyline (pre-2001)";
}

bool EraUnlock::click(std::int64_t now_ms) {
    if (unlocked) {
        toast.clear();
        return false;
    }
    if (clicks <= 0 || now_ms < window_start_ms || now_ms - window_start_ms > kWindowMs) {
        clicks = 1;
        window_start_ms = now_ms;
    } else {
        ++clicks;
    }
    toast = era_unlock_toast(clicks);
    if (clicks >= kNeed) {
        unlocked = true;
        toast = "Classic skyline unlocked";
        return true;
    }
    return false;
}

InstructorHit instructor_hit(float x, float y, bool unlocked) {
    if (x >= 8.f && x <= 280.f && y >= 8.f && y <= 36.f)
        return InstructorHit::Version;
    if (!unlocked)
        return InstructorHit::None;
    if (x >= 8.f && x <= 130.f && y >= 42.f && y <= 70.f)
        return InstructorHit::EraCurrent;
    if (x >= 138.f && x <= 400.f && y >= 42.f && y <= 70.f)
        return InstructorHit::EraClassic;
    if (x >= 8.f && x <= 130.f && y >= 74.f && y <= 102.f)
        return InstructorHit::MissilesOff;
    if (x >= 138.f && x <= 400.f && y >= 74.f && y <= 102.f)
        return InstructorHit::MissilesOn;
    return InstructorHit::None;
}

std::vector<Building> landmark_buildings(SkylineEra era) {
    std::vector<Building> out;
    int n = 0;
    const Landmark* all = nyc_landmarks(&n);
    for (int i = 0; i < n; ++i) {
        const Landmark& lm = all[i];
        if (!landmark_visible(lm, era))
            continue;
        out.push_back(box_of(lm.name, lm.lat, lm.lon, lm.hdg_deg, lm.length_m, lm.width_m,
                             lm.height_m));
        if (lm.tip_m > lm.height_m + 8.f) {
            const std::string tip = std::string(lm.name) + " tip";
            out.push_back(box_of(tip.c_str(), lm.lat, lm.lon, lm.hdg_deg,
                                 std::max(4.0, lm.length_m * 0.08),
                                 std::max(4.0, lm.width_m * 0.08), lm.tip_m));
        }
    }
    return out;
}

void apply_skyline_era(std::vector<Building>& buildings, SkylineEra era) {
    (void)era;
    int n = 0;
    const Landmark* all = nyc_landmarks(&n);
    buildings.erase(std::remove_if(buildings.begin(), buildings.end(),
                                   [&](const Building& b) {
                                       for (int i = 0; i < n; ++i) {
                                           if (centroid_dist_m(b, all[i].lat, all[i].lon) <
                                               all[i].hide_radius_m)
                                               return true;
                                       }
                                       return false;
                                   }),
                    buildings.end());
}

void SolidBvh::clear() {
    recs_.clear();
    verts_.clear();
    cell_off_.clear();
    cell_ids_.clear();
    gx_ = gy_ = 0;
}

void SolidBvh::add_building(const Building& b, double ground_m) {
    if (b.ring.size() < 3)
        return;
    Rec v;
    v.vert0 = static_cast<std::uint32_t>(verts_.size());
    v.nvert = static_cast<std::uint16_t>(std::min<std::size_t>(b.ring.size(), 65535));
    v.west = v.east = static_cast<float>(b.ring.front().second);
    v.south = v.north = static_cast<float>(b.ring.front().first);
    for (std::uint16_t i = 0; i < v.nvert; ++i) {
        const float lat = static_cast<float>(b.ring[i].first);
        const float lon = static_cast<float>(b.ring[i].second);
        verts_.push_back(lat);
        verts_.push_back(lon);
        v.south = std::min(v.south, lat);
        v.north = std::max(v.north, lat);
        v.west = std::min(v.west, lon);
        v.east = std::max(v.east, lon);
    }
    v.height_m = std::max(2.f, b.height_m);
    v.z0_m = static_cast<float>(ground_m);
    v.kind = static_cast<std::uint8_t>(b.kind);
    recs_.push_back(v);
}

void SolidBvh::add_jetbridge(const Jetbridge& j, double ground_m) {
    add_building(jetbridge_building(j), ground_m);
}

void SolidBvh::add_landmark(const Landmark& lm, double ground_m) {
    Building body = box_of(lm.name, lm.lat, lm.lon, lm.hdg_deg, lm.length_m, lm.width_m, lm.height_m);
    body.kind = BuildingKind::Landmark;
    add_building(body, ground_m);
    if (lm.tip_m > lm.height_m + 8.f) {
        Building tip = box_of("tip", lm.lat, lm.lon, lm.hdg_deg, std::max(4.0, lm.length_m * 0.08),
                              std::max(4.0, lm.width_m * 0.08), lm.tip_m);
        tip.kind = BuildingKind::Landmark;
        add_building(tip, ground_m);
    }
}

bool SolidBvh::point_in(const Rec& v, double lat, double lon) const {
    if (lat < v.south || lat > v.north || lon < v.west || lon > v.east)
        return false;
    const int n = v.nvert;
    if (n < 3)
        return false;
    bool c = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        const float yi = verts_[v.vert0 + static_cast<std::uint32_t>(i) * 2];
        const float xi = verts_[v.vert0 + static_cast<std::uint32_t>(i) * 2 + 1];
        const float yj = verts_[v.vert0 + static_cast<std::uint32_t>(j) * 2];
        const float xj = verts_[v.vert0 + static_cast<std::uint32_t>(j) * 2 + 1];
        if (((yi > lat) != (yj > lat)) &&
            (lon < (xj - xi) * (lat - yi) / (yj - yi + 1e-18) + xi))
            c = !c;
    }
    return c;
}

int SolidBvh::cell_at(double lat, double lon) const {
    if (gx_ < 1 || gy_ < 1)
        return -1;
    if (lat < gsouth_ || lat > gnorth_ || lon < gwest_ || lon > geast_)
        return -1;
    const double u = (lon - gwest_) / std::max(1e-12, geast_ - gwest_);
    const double v = (lat - gsouth_) / std::max(1e-12, gnorth_ - gsouth_);
    const int ix = std::max(0, std::min(gx_ - 1, static_cast<int>(u * gx_)));
    const int iy = std::max(0, std::min(gy_ - 1, static_cast<int>(v * gy_)));
    return iy * gx_ + ix;
}

void SolidBvh::build() {
    cell_off_.clear();
    cell_ids_.clear();
    gx_ = gy_ = 0;
    if (recs_.empty())
        return;
    gwest_ = recs_[0].west;
    geast_ = recs_[0].east;
    gsouth_ = recs_[0].south;
    gnorth_ = recs_[0].north;
    for (const auto& r : recs_) {
        gwest_ = std::min(gwest_, static_cast<double>(r.west));
        geast_ = std::max(geast_, static_cast<double>(r.east));
        gsouth_ = std::min(gsouth_, static_cast<double>(r.south));
        gnorth_ = std::max(gnorth_, static_cast<double>(r.north));
    }
    const double pad = 1e-5;
    gwest_ -= pad;
    geast_ += pad;
    gsouth_ -= pad;
    gnorth_ += pad;
    const int side = std::max(8, static_cast<int>(std::sqrt(static_cast<double>(recs_.size()))));
    gx_ = std::min(512, side);
    gy_ = gx_;
    std::vector<std::vector<std::uint32_t>> cells(static_cast<std::size_t>(gx_ * gy_));
    for (std::uint32_t i = 0; i < recs_.size(); ++i) {
        const Rec& r = recs_[i];
        const int x0 = cell_at(r.south, r.west) % gx_;
        const int y0 = cell_at(r.south, r.west) / gx_;
        const int x1 = cell_at(r.north, r.east) % gx_;
        const int y1 = cell_at(r.north, r.east) / gx_;
        const int xa = std::min(x0, x1), xb = std::max(x0, x1);
        const int ya = std::min(y0, y1), yb = std::max(y0, y1);
        for (int y = ya; y <= yb; ++y)
            for (int x = xa; x <= xb; ++x)
                cells[static_cast<std::size_t>(y * gx_ + x)].push_back(i);
    }
    cell_off_.resize(static_cast<std::size_t>(gx_ * gy_ + 1));
    cell_off_[0] = 0;
    for (int c = 0; c < gx_ * gy_; ++c) {
        cell_off_[static_cast<std::size_t>(c + 1)] =
            cell_off_[static_cast<std::size_t>(c)] +
            static_cast<std::uint32_t>(cells[static_cast<std::size_t>(c)].size());
        cell_ids_.insert(cell_ids_.end(), cells[static_cast<std::size_t>(c)].begin(),
                         cells[static_cast<std::size_t>(c)].end());
    }
}

void SolidBvh::apply_ground(const HeightSource* dem, double fallback_m) {
    for (auto& r : recs_) {
        double clat = 0, clon = 0;
        for (int i = 0; i < r.nvert; ++i) {
            clat += verts_[r.vert0 + static_cast<std::uint32_t>(i) * 2];
            clon += verts_[r.vert0 + static_cast<std::uint32_t>(i) * 2 + 1];
        }
        clat /= std::max(1, static_cast<int>(r.nvert));
        clon /= std::max(1, static_cast<int>(r.nvert));
        double gft = 0;
        const float sit =
            (r.kind == static_cast<std::uint8_t>(BuildingKind::Airport) &&
             std::fabs(r.height_m - kJetbridgeDeckM) > 0.25f)
                ? kBuildingSitM
                : 0.f;
        if (dem && dem->sample_ft(clat, clon, gft))
            r.z0_m = static_cast<float>(gft * geo::kFtToM) + sit;
        else if (!dem)
            r.z0_m = static_cast<float>(fallback_m) + sit;
    }
}

std::size_t SolidBvh::bytes() const {
    return recs_.size() * sizeof(Rec) + verts_.size() * sizeof(float) +
           cell_ids_.size() * sizeof(std::uint32_t) + cell_off_.size() * sizeof(std::uint32_t);
}

void SolidBvh::trim_osm_to_budget(std::size_t max_bytes) {
    if (bytes() <= max_bytes)
        return;
    std::vector<Rec> keep;
    std::vector<float> verts;
    keep.reserve(recs_.size());
    auto pack = [&](const Rec& r) {
        Rec n = r;
        n.vert0 = static_cast<std::uint32_t>(verts.size());
        for (int i = 0; i < r.nvert; ++i) {
            verts.push_back(verts_[r.vert0 + static_cast<std::uint32_t>(i) * 2]);
            verts.push_back(verts_[r.vert0 + static_cast<std::uint32_t>(i) * 2 + 1]);
        }
        keep.push_back(n);
    };
    for (const auto& r : recs_) {
        if (r.kind != static_cast<std::uint8_t>(BuildingKind::Osm))
            pack(r);
    }
    recs_.swap(keep);
    verts_.swap(verts);
    cell_off_.clear();
    cell_ids_.clear();
    gx_ = gy_ = 0;
}

SolidMem solids_mem(const SolidBvh& bvh) {
    SolidMem m;
    m.volumes = bvh.recs_.size();
    m.bytes = bvh.bytes();
    for (const auto& r : bvh.recs_) {
        if (r.kind == static_cast<std::uint8_t>(BuildingKind::Airport))
            ++m.airport_volumes;
        else if (r.kind == static_cast<std::uint8_t>(BuildingKind::Landmark))
            ++m.landmark_volumes;
        else
            ++m.osm_volumes;
    }
    return m;
}

bool SolidBvh::roof_msl(double lat, double lon, double* z_m) const {
    if (recs_.empty() || !z_m)
        return false;
    const int c = cell_at(lat, lon);
    if (c < 0)
        return false;
    const std::uint32_t a = cell_off_[static_cast<std::size_t>(c)];
    const std::uint32_t b = cell_off_[static_cast<std::size_t>(c + 1)];
    bool any = false;
    double best = -1e9;
    for (std::uint32_t i = a; i < b; ++i) {
        const Rec& r = recs_[cell_ids_[i]];
        if (point_in(r, lat, lon)) {
            best = std::max(best, static_cast<double>(r.z1_m()));
            any = true;
        }
    }
    if (any)
        *z_m = best;
    return any;
}

SegHit SolidBvh::segment(double lat0, double lon0, double z0_m, double lat1, double lon1,
                         double z1_m) const {
    SegHit h;
    if (recs_.empty())
        return h;
    constexpr int kSteps = 40;
    for (int s = 0; s <= kSteps; ++s) {
        const double t = static_cast<double>(s) / kSteps;
        const double lat = lat0 + (lat1 - lat0) * t;
        const double lon = lon0 + (lon1 - lon0) * t;
        const double z = z0_m + (z1_m - z0_m) * t;
        const int c = cell_at(lat, lon);
        if (c < 0)
            continue;
        const std::uint32_t a = cell_off_[static_cast<std::size_t>(c)];
        const std::uint32_t b = cell_off_[static_cast<std::size_t>(c + 1)];
        for (std::uint32_t i = a; i < b; ++i) {
            const Rec& r = recs_[cell_ids_[i]];
            if (z >= r.z0_m - 0.5 && z <= r.z1_m() + 0.5 && point_in(r, lat, lon)) {
                h.hit = true;
                h.lat = lat;
                h.lon = lon;
                h.z_m = z;
                h.t = t;
                return h;
            }
        }
    }
    return h;
}

namespace {

bool hidden_by_landmark(double lat, double lon) {
    int n = 0;
    const Landmark* all = nyc_landmarks(&n);
    for (int i = 0; i < n; ++i) {
        if (geo::ground_distance_m(lat, lon, all[i].lat, all[i].lon) < all[i].hide_radius_m)
            return true;
    }
    return false;
}

void write_u32s(std::ostream& o, std::uint32_t v) {
    o.write(reinterpret_cast<const char*>(&v), 4);
}
void write_f32s(std::ostream& o, float v) {
    o.write(reinterpret_cast<const char*>(&v), 4);
}
bool read_u32s(std::istream& i, std::uint32_t& v) {
    i.read(reinterpret_cast<char*>(&v), 4);
    return static_cast<bool>(i);
}
bool read_f32s(std::istream& i, float& v) {
    i.read(reinterpret_cast<char*>(&v), 4);
    return static_cast<bool>(i);
}

bool hidden_building(const Building& b) {
    if (b.ring.empty())
        return false;
    double clat = 0, clon = 0;
    for (const auto& p : b.ring) {
        clat += p.first;
        clon += p.second;
    }
    clat /= static_cast<double>(b.ring.size());
    clon /= static_cast<double>(b.ring.size());
    return hidden_by_landmark(clat, clon);
}

} // namespace

bool write_solids_bin(const SolidBvh& bvh, const std::string& path) {
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent);
    std::ofstream o(path, std::ios::binary);
    if (!o)
        return false;
    o.write("S376SLD1", 8);
    write_u32s(o, 1);
    write_u32s(o, static_cast<std::uint32_t>(bvh.recs_.size()));
    write_u32s(o, static_cast<std::uint32_t>(bvh.verts_.size()));
    for (const auto& r : bvh.recs_) {
        write_f32s(o, r.west);
        write_f32s(o, r.south);
        write_f32s(o, r.east);
        write_f32s(o, r.north);
        write_f32s(o, r.height_m);
        write_f32s(o, r.z0_m);
        write_u32s(o, r.vert0);
        write_u32s(o, r.nvert);
    }
    for (float v : bvh.verts_)
        write_f32s(o, v);
    return static_cast<bool>(o);
}

int read_solids_bin(SolidBvh& bvh, const std::string& path) {
    std::ifstream i(path, std::ios::binary);
    if (!i)
        return 0;
    char mag[8];
    i.read(mag, 8);
    if (!i || std::memcmp(mag, "S376SLD1", 8) != 0)
        return 0;
    std::uint32_t ver = 0, nrec = 0, nvert = 0;
    if (!read_u32s(i, ver) || ver != 1 || !read_u32s(i, nrec) || !read_u32s(i, nvert))
        return 0;
    if (nrec > 2'000'000u || nvert > 16'000'000u)
        return 0;
    std::vector<SolidBvh::Rec> recs(nrec);
    for (auto& r : recs) {
        std::uint32_t nv = 0;
        if (!read_f32s(i, r.west) || !read_f32s(i, r.south) || !read_f32s(i, r.east) ||
            !read_f32s(i, r.north) || !read_f32s(i, r.height_m) || !read_f32s(i, r.z0_m) ||
            !read_u32s(i, r.vert0) || !read_u32s(i, nv))
            return 0;
        r.nvert = static_cast<std::uint16_t>(std::min<std::uint32_t>(nv, 65535));
    }
    std::vector<float> verts(nvert);
    for (auto& v : verts) {
        if (!read_f32s(i, v))
            return 0;
    }
    int added = 0;
    for (auto r : recs) {
        if (r.nvert < 3 || r.vert0 + static_cast<std::uint32_t>(r.nvert) * 2 > nvert)
            continue;
        double clat = 0, clon = 0;
        for (int k = 0; k < r.nvert; ++k) {
            clat += verts[r.vert0 + static_cast<std::uint32_t>(k) * 2];
            clon += verts[r.vert0 + static_cast<std::uint32_t>(k) * 2 + 1];
        }
        clat /= static_cast<double>(r.nvert);
        clon /= static_cast<double>(r.nvert);
        if (hidden_by_landmark(clat, clon))
            continue;
        const std::uint32_t dest = static_cast<std::uint32_t>(bvh.verts_.size());
        for (int k = 0; k < r.nvert; ++k) {
            bvh.verts_.push_back(verts[r.vert0 + static_cast<std::uint32_t>(k) * 2]);
            bvh.verts_.push_back(verts[r.vert0 + static_cast<std::uint32_t>(k) * 2 + 1]);
        }
        r.vert0 = dest;
        bvh.recs_.push_back(r);
        ++added;
    }
    return added;
}

void fill_solids(SolidBvh& bvh, SkylineEra era, const std::vector<Airport>& apts,
                 double default_ground_m, const HeightSource* dem) {
    bvh.clear();
    // Airport structures first — never subject to the OSM house trim.
    for (const auto& a : apts) {
        const double g = a.elev_m != 0.0 ? a.elev_m : default_ground_m;
        for (const auto& b : a.buildings) {
            if (!is_airport_structure(b))
                continue;
            if (hidden_building(b))
                continue;
            bvh.add_building(b, g + kBuildingSitM);
        }
        for (const auto& j : a.jetbridges)
            bvh.add_jetbridge(j, g);
    }
    int n = 0;
    const Landmark* all = nyc_landmarks(&n);
    for (int i = 0; i < n; ++i) {
        if (!landmark_visible(all[i], era))
            continue;
        double g = default_ground_m;
        if (dem) {
            double ft = 0;
            if (dem->sample_ft(all[i].lat, all[i].lon, ft))
                g = ft * geo::kFtToM;
        }
        bvh.add_landmark(all[i], g);
    }
    for (const auto& a : apts) {
        if (!a.solids_path.empty())
            read_solids_bin(bvh, a.solids_path);
        else {
            const double g = a.elev_m != 0.0 ? a.elev_m : default_ground_m;
            for (const auto& b : a.buildings) {
                if (is_airport_structure(b) || hidden_building(b))
                    continue;
                bvh.add_building(b, g);
            }
        }
    }
    bvh.trim_osm_to_budget(kMaxSolidBytes);
    if (dem)
        bvh.apply_ground(dem, default_ground_m);
    bvh.build();
}

AirportMesh mesh_landmarks(SkylineEra era, double ground_m) {
    AirportMesh mesh;
    const auto bldgs = landmark_buildings(era);
    for (const auto& b : bldgs) {
        if (b.ring.size() < 3)
            continue;
        double clat = 0, clon = 0;
        for (const auto& p : b.ring) {
            clat += p.first;
            clon += p.second;
        }
        clat /= static_cast<double>(b.ring.size());
        clon /= static_cast<double>(b.ring.size());
        const double z0 = session_ground_m(clat, clon, ground_m);
        const double z1 = z0 + static_cast<double>(b.height_m);
        geo::Ecef e, n, u;
        geo::ecef_to_enu_axes(clat, clon, e, n, u);
        const geo::Ecef roof_c = geo::lla_to_ecef(clat, clon, z1);
        const int ns = static_cast<int>(b.ring.size());
        for (int i = 0; i < ns; ++i) {
            const auto& p0 = b.ring[static_cast<std::size_t>(i)];
            const auto& p1 = b.ring[static_cast<std::size_t>((i + 1) % ns)];
            const geo::Ecef A = geo::lla_to_ecef(p0.first, p0.second, z0);
            const geo::Ecef B = geo::lla_to_ecef(p1.first, p1.second, z0);
            const geo::Ecef C = geo::lla_to_ecef(p1.first, p1.second, z1);
            const geo::Ecef D = geo::lla_to_ecef(p0.first, p0.second, z1);
            geo::Ecef wn{B.x - A.x, B.y - A.y, B.z - A.z};
            geo::Ecef nn{wn.y * u.z - wn.z * u.y, wn.z * u.x - wn.x * u.z, wn.x * u.y - wn.y * u.x};
            const float u0 = static_cast<float>(i) / static_cast<float>(ns);
            const float u1 = static_cast<float>(i + 1) / static_cast<float>(ns);
            const bool spire = b.height_m > 500.f || b.name.find("tip") != std::string::npos;
            const float mat = spire ? 5.f : 3.f;
            add_tri_uv(mesh, A, B, C, nn, 0.62f, 0.64f, 0.68f, mat, u0, 0.f, u1, 0.f, u1, 1.f);
            add_tri_uv(mesh, A, C, D, nn, 0.62f, 0.64f, 0.68f, mat, u0, 0.f, u1, 1.f, u0, 1.f);
            add_tri_uv(mesh, roof_c, D, C, u, 0.22f, 0.23f, 0.25f, 4.f, 0.5f, 0.5f, u0, 1.f, u1,
                       1.f);
        }
    }
    return mesh;
}

std::vector<LightPoint> landmark_warning_lights(SkylineEra era) {
    std::vector<LightPoint> out;
    int n = 0;
    const Landmark* all = nyc_landmarks(&n);
    for (int i = 0; i < n; ++i) {
        const Landmark& lm = all[i];
        if (!landmark_visible(lm, era))
            continue;
        if (lm.height_m < 60.f)
            continue;
        auto add = [&](double lat, double lon, double alt) {
            LightPoint L;
            L.lat = lat;
            L.lon = lon;
            L.alt_m = alt;
            L.r = 1.f;
            L.g = 0.08f;
            L.b = 0.06f;
            L.kind = LightKind::Beacon;
            out.push_back(L);
        };
        const double roof = kManhattanGroundM + lm.height_m + 1.0;
        double c0lat, c0lon, c1lat, c1lon, c2lat, c2lon, c3lat, c3lon;
        offset_m(lm.lat, lm.lon, lm.hdg_deg, lm.length_m * 0.42, c0lat, c0lon);
        offset_m(c0lat, c0lon, lm.hdg_deg + 90.0, lm.width_m * 0.42, c0lat, c0lon);
        offset_m(lm.lat, lm.lon, lm.hdg_deg, lm.length_m * 0.42, c1lat, c1lon);
        offset_m(c1lat, c1lon, lm.hdg_deg - 90.0, lm.width_m * 0.42, c1lat, c1lon);
        offset_m(lm.lat, lm.lon, lm.hdg_deg + 180.0, lm.length_m * 0.42, c2lat, c2lon);
        offset_m(c2lat, c2lon, lm.hdg_deg + 90.0, lm.width_m * 0.42, c2lat, c2lon);
        offset_m(lm.lat, lm.lon, lm.hdg_deg + 180.0, lm.length_m * 0.42, c3lat, c3lon);
        offset_m(c3lat, c3lon, lm.hdg_deg - 90.0, lm.width_m * 0.42, c3lat, c3lon);
        add(c0lat, c0lon, roof);
        add(c1lat, c1lon, roof);
        add(c2lat, c2lon, roof);
        add(c3lat, c3lon, roof);
        if (lm.tip_m > lm.height_m + 8.f)
            add(lm.lat, lm.lon, kManhattanGroundM + lm.tip_m);
    }
    return out;
}

HatSample sample_hat_solids(const HeightSource* src, const SolidBvh* solids, double lat_deg,
                            double lon_deg, double alt_ft, const double* prev_lat,
                            const double* prev_lon, const double* prev_alt_ft) {
    HatSample s = sample_hat(src, lat_deg, lon_deg, alt_ft);
    if (solids) {
        double roof_m = 0;
        if (solids->roof_msl(lat_deg, lon_deg, &roof_m)) {
            const float roof_ft = static_cast<float>(roof_m * geo::kMToFt);
            if (roof_ft > s.elev_ft) {
                s.elev_ft = roof_ft;
                s.hat_ft = static_cast<float>(alt_ft - roof_ft);
                s.covered = true;
                s.building = true;
            }
        }
        if (prev_lat && prev_lon && prev_alt_ft) {
            const SegHit hit =
                solids->segment(*prev_lat, *prev_lon, *prev_alt_ft * geo::kFtToM, lat_deg, lon_deg,
                                alt_ft * geo::kFtToM);
            if (hit.hit) {
                const float zft = static_cast<float>(hit.z_m * geo::kMToFt);
                s.elev_ft = std::max(s.elev_ft, zft);
                s.hat_ft = std::min(s.hat_ft, static_cast<float>(alt_ft - s.elev_ft));
                if (s.hat_ft > 0.f)
                    s.hat_ft = 0.f;
                s.covered = true;
                s.building = true;
                s.intersect = true;
            }
        }
    }
    return s;
}

SkylineEra session_era() {
    std::lock_guard<std::mutex> g(sess().mu);
    return sess().era;
}

bool session_era_unlocked() {
    std::lock_guard<std::mutex> g(sess().mu);
    return sess().unlocked;
}

SkylineEra visible_skyline_era(SkylineEra chosen, double lat, double lon) {
    if (chosen == SkylineEra::Classic && in_nyc_geofence(lat, lon))
        return SkylineEra::Classic;
    return SkylineEra::Current;
}

SkylineEra active_skyline_era() {
    std::lock_guard<std::mutex> g(sess().mu);
    if (!sess().have_eye)
        return sess().era == SkylineEra::Classic ? SkylineEra::Classic : SkylineEra::Current;
    return visible_skyline_era(sess().era, sess().lat, sess().lon);
}

void set_session_era(SkylineEra era, bool unlocked) {
    std::lock_guard<std::mutex> g(sess().mu);
    sess().unlocked = unlocked;
    sess().era = era;
    const SkylineEra vis =
        sess().have_eye ? visible_skyline_era(era, sess().lat, sess().lon) : era;
    fill_solids(sess().bvh, vis, sess().apts, kManhattanGroundM, sess().dem.get());
}

void refresh_channels() {
    {
        std::lock_guard<std::mutex> g(sess().mu);
        sess().era = SkylineEra::Current;
        sess().unlocked = false;
        fill_solids(sess().bvh, SkylineEra::Current, sess().apts, kManhattanGroundM,
                    sess().dem.get());
    }
    reset_missiles();
}

const SolidBvh& session_solids() {
    return sess().bvh;
}

void rebuild_session_solids(const std::vector<Airport>& apts, const HeightSource* dem) {
    std::lock_guard<std::mutex> g(sess().mu);
    sess().apts = apts;
    const HeightSource* use = sess().dem ? sess().dem.get() : dem;
    const SkylineEra vis =
        sess().have_eye ? visible_skyline_era(sess().era, sess().lat, sess().lon) : sess().era;
    fill_solids(sess().bvh, vis, sess().apts, kManhattanGroundM, use);
}

void session_apply_ground(std::shared_ptr<const HeightSource> dem) {
    std::lock_guard<std::mutex> g(sess().mu);
    sess().dem = std::move(dem);
    sess().bvh.apply_ground(sess().dem.get(), kManhattanGroundM);
}

double session_ground_m(double lat, double lon, double fallback_m) {
    std::shared_ptr<const HeightSource> dem;
    {
        std::lock_guard<std::mutex> g(sess().mu);
        dem = sess().dem;
    }
    double ft = 0;
    if (dem && dem->sample_ft(lat, lon, ft))
        return ft * geo::kFtToM;
    return fallback_m;
}

std::int64_t session_started_ms() {
    return sess().started_ms;
}

bool in_nyc_geofence(double lat, double lon) {
    return lat >= kNycSouth && lat <= kNycNorth && lon >= kNycWest && lon <= kNycEast;
}

void tick_session_view(double lat, double lon) {
    std::lock_guard<std::mutex> g(sess().mu);
    const SkylineEra before =
        sess().have_eye ? visible_skyline_era(sess().era, sess().lat, sess().lon) : sess().era;
    sess().have_eye = true;
    sess().lat = lat;
    sess().lon = lon;
    const SkylineEra after = visible_skyline_era(sess().era, lat, lon);
    if (after != before)
        fill_solids(sess().bvh, after, sess().apts, kManhattanGroundM, sess().dem.get());
}

HatSample sample_session_hat(const HeightSource* src, double lat_deg, double lon_deg,
                             double alt_ft, const double* prev_lat, const double* prev_lon,
                             const double* prev_alt_ft) {
    std::lock_guard<std::mutex> g(sess().mu);
    return sample_hat_solids(src, &sess().bvh, lat_deg, lon_deg, alt_ft, prev_lat, prev_lon,
                             prev_alt_ft);
}

} // namespace s376
