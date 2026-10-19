#include "s376/traffic.hpp"

#include "s376/geo.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>

namespace s376 {
namespace {

double wrap_s(double s, double len) {
    if (len <= 1.0)
        return 0;
    s = std::fmod(s, len);
    if (s < 0)
        s += len;
    return s;
}

std::uint32_t rnd(std::uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return s;
}

float rndf(std::uint32_t& s) {
    return static_cast<float>(rnd(s) >> 8) * (1.f / 16777216.f);
}

void push_road(Airport& a, const char* name, RoadClass cls,
               std::initializer_list<std::pair<double, double>> pts) {
    Road r;
    r.name = name;
    r.cls = cls;
    r.path.assign(pts);
    finalize_road(r);
    if (r.length_m > 80.0)
        a.roads.push_back(std::move(r));
}

void add_box(TrafficMesh& m, bool lamps, double lat, double lon, double alt, double hdg,
             double len, double wid, double h, float r, float g, float b, float mat) {
    geo::Ecef e, n, u;
    geo::ecef_to_enu_axes(lat, lon, e, n, u);
    const double rad = geo::deg2rad(hdg);
    const double cf = std::cos(rad), sf = std::sin(rad);
    // Body x forward, y right, z up (ENU mix of north/east).
    const geo::Ecef fwd{n.x * cf + e.x * sf, n.y * cf + e.y * sf, n.z * cf + e.z * sf};
    const geo::Ecef right{e.x * cf - n.x * sf, e.y * cf - n.y * sf, e.z * cf - n.z * sf};
    const geo::Ecef c = geo::lla_to_ecef(lat, lon, alt + h * 0.5);
    const double hl = len * 0.5, hw = wid * 0.5, hh = h * 0.5;
    geo::Ecef p[8];
    int k = 0;
    for (int iz = -1; iz <= 1; iz += 2)
        for (int iy = -1; iy <= 1; iy += 2)
            for (int ix = -1; ix <= 1; ix += 2) {
                p[k].x = c.x + fwd.x * ix * hl + right.x * iy * hw + u.x * iz * hh;
                p[k].y = c.y + fwd.y * ix * hl + right.y * iy * hw + u.y * iz * hh;
                p[k].z = c.z + fwd.z * ix * hl + right.z * iy * hw + u.z * iz * hh;
                ++k;
            }
    const int faces[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1},
                             {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
    auto* dst = lamps ? &m.lamps : &m.body;
    auto tri = [&](const geo::Ecef& A, const geo::Ecef& B, const geo::Ecef& C, const geo::Ecef& nn) {
        MeshVert v;
        v.n = nn;
        v.r = r;
        v.g = g;
        v.b = b;
        v.mat = mat;
        v.p = A;
        dst->push_back(v);
        v.p = B;
        dst->push_back(v);
        v.p = C;
        dst->push_back(v);
    };
    for (const auto& f : faces) {
        const geo::Ecef& A = p[f[0]];
        const geo::Ecef& B = p[f[1]];
        const geo::Ecef& C = p[f[2]];
        geo::Ecef ab{B.x - A.x, B.y - A.y, B.z - A.z};
        geo::Ecef ac{C.x - A.x, C.y - A.y, C.z - A.z};
        geo::Ecef nn{ab.y * ac.z - ab.z * ac.y, ab.z * ac.x - ab.x * ac.z, ab.x * ac.y - ab.y * ac.x};
        tri(A, B, C, nn);
        tri(A, C, p[f[3]], nn);
    }
}

} // namespace

const char* road_class_name(RoadClass c) {
    switch (c) {
    case RoadClass::Motorway:
        return "motorway";
    case RoadClass::Trunk:
        return "trunk";
    case RoadClass::Primary:
        return "primary";
    case RoadClass::Secondary:
        return "secondary";
    }
    return "secondary";
}

bool osm_tags_are_road(const std::string& tags) {
    if (tags.find("\"highway\"") == std::string::npos)
        return false;
    if (tags.find("motorway_link") != std::string::npos ||
        tags.find("trunk_link") != std::string::npos ||
        tags.find("primary_link") != std::string::npos ||
        tags.find("secondary_link") != std::string::npos)
        return false;
    return tags.find("motorway") != std::string::npos || tags.find("trunk") != std::string::npos ||
           tags.find("primary") != std::string::npos || tags.find("secondary") != std::string::npos;
}

RoadClass parse_osm_highway(const std::string& tags) {
    if (tags.find("motorway") != std::string::npos)
        return RoadClass::Motorway;
    if (tags.find("trunk") != std::string::npos)
        return RoadClass::Trunk;
    if (tags.find("primary") != std::string::npos)
        return RoadClass::Primary;
    return RoadClass::Secondary;
}

float class_speed_mps(RoadClass c) {
    switch (c) {
    case RoadClass::Motorway:
        return 28.f;
    case RoadClass::Trunk:
        return 24.f;
    case RoadClass::Primary:
        return 18.f;
    case RoadClass::Secondary:
        return 14.f;
    }
    return 14.f;
}

float class_weight(RoadClass c) {
    switch (c) {
    case RoadClass::Motorway:
        return 8.f;
    case RoadClass::Trunk:
        return 6.f;
    case RoadClass::Primary:
        return 4.f;
    case RoadClass::Secondary:
        return 2.f;
    }
    return 2.f;
}

float class_lamp_spacing_m(RoadClass c) {
    switch (c) {
    case RoadClass::Motorway:
        return 45.f;
    case RoadClass::Trunk:
        return 40.f;
    case RoadClass::Primary:
        return 35.f;
    case RoadClass::Secondary:
        return 30.f;
    }
    return 35.f;
}

float traffic_density(float tod_hours) {
    float h = tod_hours;
    if (!std::isfinite(h))
        h = 12.f;
    h = std::fmod(h, 24.f);
    if (h < 0)
        h += 24.f;
    if (h >= 6.f && h < 9.f)
        return 1.f;
    if (h >= 15.f && h < 19.f)
        return 1.f;
    if (h >= 9.f && h < 15.f)
        return 0.55f;
    if (h >= 19.f && h < 22.f)
        return 0.40f;
    if (h >= 5.f && h < 6.f)
        return 0.35f;
    return 0.22f;
}

bool traffic_night(float tod_hours) {
    float h = tod_hours;
    if (!std::isfinite(h))
        h = 12.f;
    h = std::fmod(h, 24.f);
    if (h < 0)
        h += 24.f;
    return h < 6.8f || h > 18.2f;
}

void finalize_road(Road& r) {
    r.seg_len_m.clear();
    r.length_m = 0;
    if (r.path.size() < 2)
        return;
    r.seg_len_m.resize(r.path.size() - 1);
    for (std::size_t i = 0; i + 1 < r.path.size(); ++i) {
        const double d =
            geo::ground_distance_m(r.path[i].first, r.path[i].second, r.path[i + 1].first,
                                   r.path[i + 1].second);
        r.seg_len_m[i] = std::max(d, 0.5);
        r.length_m += r.seg_len_m[i];
    }
}

bool sample_road(const Road& r, double s_m, double& lat, double& lon, double& hdg) {
    if (r.path.size() < 2 || r.length_m < 1.0 || r.seg_len_m.size() + 1 != r.path.size())
        return false;
    s_m = wrap_s(s_m, r.length_m);
    double acc = 0;
    for (std::size_t i = 0; i < r.seg_len_m.size(); ++i) {
        const double len = r.seg_len_m[i];
        if (s_m > acc + len && i + 1 < r.seg_len_m.size()) {
            acc += len;
            continue;
        }
        const double u = std::min(1.0, std::max(0.0, (s_m - acc) / len));
        lat = r.path[i].first + (r.path[i + 1].first - r.path[i].first) * u;
        lon = r.path[i].second + (r.path[i + 1].second - r.path[i].second) * u;
        double h = 0;
        geo::inverse_wgs84(r.path[i].first, r.path[i].second, r.path[i + 1].first,
                           r.path[i + 1].second, &h);
        hdg = h;
        return true;
    }
    lat = r.path.back().first;
    lon = r.path.back().second;
    hdg = 0;
    return true;
}

void add_public_ny_roads(Airport& a) {
    if (a.icao == "KJFK") {
        push_road(a, "Van Wyck", RoadClass::Motorway,
                  {{40.7142, -73.8178},
                   {40.7020, -73.8120},
                   {40.6885, -73.8068},
                   {40.6750, -73.8032},
                   {40.6640, -73.8010},
                   {40.6548, -73.7962},
                   {40.6482, -73.7918},
                   {40.6435, -73.7892}});
        push_road(a, "Belt Parkway", RoadClass::Trunk,
                  {{40.6618, -73.7365},
                   {40.6565, -73.7488},
                   {40.6512, -73.7605},
                   {40.6468, -73.7720},
                   {40.6438, -73.7825},
                   {40.6412, -73.7915},
                   {40.6408, -73.8040},
                   {40.6435, -73.8180},
                   {40.6488, -73.8320}});
        push_road(a, "JFK Terminal", RoadClass::Secondary,
                  {{40.6458, -73.7752},
                   {40.6470, -73.7795},
                   {40.6465, -73.7848},
                   {40.6448, -73.7882},
                   {40.6422, -73.7875},
                   {40.6410, -73.7838},
                   {40.6425, -73.7788},
                   {40.6448, -73.7758}});
        push_road(a, "JFK Access", RoadClass::Primary,
                  {{40.6475, -73.7915}, {40.6460, -73.7860}, {40.6452, -73.7805}, {40.6448, -73.7768}});
    } else if (a.icao == "KEWR") {
        push_road(a, "NJ Turnpike", RoadClass::Motorway,
                  {{40.7220, -74.1695},
                   {40.7105, -74.1648},
                   {40.7000, -74.1605},
                   {40.6905, -74.1568},
                   {40.6820, -74.1538},
                   {40.6705, -74.1515},
                   {40.6600, -74.1502}});
        push_road(a, "US-1/9", RoadClass::Trunk,
                  {{40.7080, -74.1865}, {40.6985, -74.1820}, {40.6900, -74.1785}, {40.6825, -74.1758}});
        push_road(a, "EWR Terminal", RoadClass::Secondary,
                  {{40.6895, -74.1740}, {40.6910, -74.1765}, {40.6935, -74.1775}, {40.6950, -74.1740}});
    }
}

void collect_roads(TrafficWorld& w, const Airport& a) {
    for (const auto& r : a.roads) {
        if (r.path.size() < 2)
            continue;
        Road c = r;
        if (c.length_m < 1.0)
            finalize_road(c);
        if (c.length_m > 80.0)
            w.roads.push_back(std::move(c));
    }
}

void seed_traffic(TrafficWorld& w, int max_cars, std::uint32_t rng) {
    w.cars.clear();
    if (w.roads.empty() || max_cars <= 0)
        return;
    max_cars = std::min(max_cars, kMaxTrafficCars);
    double weight_sum = 0;
    for (const auto& r : w.roads)
        weight_sum += class_weight(r.cls) * (r.length_m / 1000.0);
    if (weight_sum < 1e-6)
        return;
    std::uint32_t s = rng ? rng : 1;
    const float palette[][3] = {{0.72f, 0.18f, 0.16f}, {0.18f, 0.28f, 0.55f}, {0.88f, 0.88f, 0.90f},
                                {0.15f, 0.15f, 0.16f}, {0.55f, 0.56f, 0.52f}, {0.20f, 0.45f, 0.28f},
                                {0.75f, 0.55f, 0.12f}, {0.35f, 0.38f, 0.42f}};
    int left = max_cars;
    for (std::size_t i = 0; i < w.roads.size() && left > 0; ++i) {
        const auto& r = w.roads[i];
        const double share = class_weight(r.cls) * (r.length_m / 1000.0) / weight_sum;
        int n = std::max(1, static_cast<int>(std::round(share * max_cars)));
        n = std::min(n, left);
        for (int k = 0; k < n; ++k) {
            TrafficCar c;
            c.road = static_cast<int>(i);
            c.s_m = rndf(s) * static_cast<float>(r.length_m);
            c.dir = (rnd(s) & 1u) ? 1 : -1;
            c.speed_mps = class_speed_mps(r.cls) * (0.85f + 0.30f * rndf(s));
            const auto& col = palette[rnd(s) % 8];
            c.r = col[0];
            c.g = col[1];
            c.b = col[2];
            w.cars.push_back(c);
        }
        left -= n;
    }
}

TrafficWorld build_traffic(const std::vector<Airport>& apts, int max_cars) {
    TrafficWorld w;
    for (const auto& a : apts)
        collect_roads(w, a);
    seed_traffic(w, max_cars, 0xA5A5u);
    return w;
}

int step_traffic(TrafficWorld& w, double dt, float tod_hours, double eye_lat, double eye_lon,
                 double eye_alt_m) {
    if (dt < 0)
        dt = 0;
    if (dt > 0.25)
        dt = 0.25;
    const float dens = traffic_density(tod_hours);
    const int want = static_cast<int>(std::round(static_cast<float>(w.cars.size()) * dens));
    w.visible = 0;
    const bool have_eye = std::isfinite(eye_lat) && std::isfinite(eye_lon);
    for (std::size_t i = 0; i < w.cars.size(); ++i) {
        auto& c = w.cars[i];
        if (c.road < 0 || static_cast<std::size_t>(c.road) >= w.roads.size()) {
            c.visible = false;
            continue;
        }
        const auto& r = w.roads[static_cast<std::size_t>(c.road)];
        c.s_m = wrap_s(c.s_m + static_cast<double>(c.dir) * c.speed_mps * dt, r.length_m);
        const bool in_pool = static_cast<int>(i) < want;
        if (!in_pool) {
            c.visible = false;
            continue;
        }
        double lat = 0, lon = 0, hdg = 0;
        if (!sample_road(r, c.s_m, lat, lon, hdg)) {
            c.visible = false;
            continue;
        }
        if (have_eye) {
            const double d = geo::ground_distance_m(eye_lat, eye_lon, lat, lon);
            const double dz = eye_alt_m;
            if (std::sqrt(d * d + dz * dz) > kTrafficCullM) {
                c.visible = false;
                continue;
            }
        }
        c.visible = true;
        ++w.visible;
    }
    return w.visible;
}

void build_traffic_mesh(const TrafficWorld& w, float tod_hours, TrafficMesh& out) {
    out.body.clear();
    out.lamps.clear();
    const bool night = traffic_night(tod_hours);
    out.body.reserve(static_cast<std::size_t>(w.visible) * 36);
    if (night)
        out.lamps.reserve(static_cast<std::size_t>(w.visible) * 24);
    for (const auto& c : w.cars) {
        if (!c.visible || c.road < 0 || static_cast<std::size_t>(c.road) >= w.roads.size())
            continue;
        const auto& r = w.roads[static_cast<std::size_t>(c.road)];
        double lat = 0, lon = 0, hdg = 0;
        if (!sample_road(r, c.s_m, lat, lon, hdg))
            continue;
        if (c.dir < 0)
            hdg = geo::wrap360(hdg + 180.0);
        const double lane = 3.1;
        double clat = lat, clon = lon;
        offset_m(lat, lon, hdg + (c.dir > 0 ? 90.0 : -90.0), lane, clat, clon);
        const double z = 0.35;
        add_box(out, false, clat, clon, z, hdg, 4.3, 1.75, 1.25, c.r, c.g, c.b, 3.f);
        add_box(out, false, clat, clon, z + 1.05, hdg, 2.0, 1.55, 0.85, c.r * 0.7f, c.g * 0.7f,
                c.b * 0.75f, 3.f);
        if (!night)
            continue;
        double flat, flon, rlat, rlon;
        offset_m(clat, clon, hdg, 2.05, flat, flon);
        offset_m(clat, clon, hdg, -2.05, rlat, rlon);
        add_box(out, true, flat, flon, z + 0.55, hdg, 0.18, 1.35, 0.16, 1.f, 0.96f, 0.82f, 1.f);
        add_box(out, true, rlat, rlon, z + 0.55, hdg, 0.16, 1.25, 0.14, 0.95f, 0.12f, 0.08f, 1.f);
    }
}

} // namespace s376
