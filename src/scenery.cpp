#include "s376/airport.hpp"
#include "s376/geo.hpp"
#include "s376/traffic.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace s376 {
namespace {

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

Taxiway stub_connector(const char* name, const Runway& rw, double along_m, double perp0,
                       double perp1) {
    Taxiway t;
    t.name = name;
    t.width_m = 23;
    double clat, clon, a0, o0, a1, o1;
    offset_m(rw.le_lat, rw.le_lon, rw.heading_deg, along_m, clat, clon);
    offset_m(clat, clon, rw.heading_deg + 90.0, perp0, a0, o0);
    offset_m(clat, clon, rw.heading_deg + 90.0, perp1, a1, o1);
    t.path = {{a0, o0}, {a1, o1}};
    return t;
}

void add_sign(Airport& a, const std::string& text, double lat, double lon, double hdg) {
    AirportSign s;
    s.text = text;
    s.lat = lat;
    s.lon = lon;
    s.hdg_deg = hdg;
    a.signs.push_back(s);
}

void add_hold(Airport& a, double lat, double lon, double hdg, double width) {
    HoldBar h;
    h.lat = lat;
    h.lon = lon;
    h.hdg_deg = hdg;
    h.width_m = width;
    a.hold_bars.push_back(h);
}

void mid_path(const Taxiway& t, double& lat, double& lon) {
    if (t.path.size() < 2) {
        lat = lon = 0;
        return;
    }
    lat = 0.5 * (t.path.front().first + t.path.back().first);
    lon = 0.5 * (t.path.front().second + t.path.back().second);
}

void add_named_connectors(Airport& a, const Runway& rw, const char* const* names, int n,
                          double perp0, double perp1) {
    if (rw.length_m < 200.0)
        return;
    for (int i = 0; i < n; ++i) {
        const double along = rw.length_m * (i + 1.0) / (n + 1.0);
        a.taxiways.push_back(stub_connector(names[i], rw, along, perp0, perp1));
        double lat, lon;
        mid_path(a.taxiways.back(), lat, lon);
        add_sign(a, names[i], lat, lon, rw.heading_deg);
        double hlat, hlon;
        offset_m(rw.le_lat, rw.le_lon, rw.heading_deg, along, hlat, hlon);
        offset_m(hlat, hlon, rw.heading_deg + 90.0, perp0, hlat, hlon);
        add_hold(a, hlat, hlon, rw.heading_deg, rw.width_m);
    }
}

void push_tree_ring(Airport& a, double lat, double lon, double hdg, double along0, double along1,
                    double perp, double step, float h) {
    for (double d = along0; d <= along1; d += step) {
        Tree t;
        double plat, plon, clat, clon;
        offset_m(lat, lon, hdg, d, clat, clon);
        offset_m(clat, clon, hdg + 90.0, perp, plat, plon);
        t.lat = plat;
        t.lon = plon;
        t.height_m = h;
        a.trees.push_back(t);
    }
}

void add_stand(Airport& a, const char* name, double lat, double lon, double hdg) {
    ParkedAircraft p;
    p.stand = name;
    p.lat = lat;
    p.lon = lon;
    p.hdg_deg = hdg;
    a.parked.push_back(p);
    Jetbridge j;
    j.gate = name;
    offset_m(lat, lon, hdg + 180.0, 22.0, j.lat, j.lon);
    j.hdg_deg = hdg;
    j.length_m = 26;
    a.jetbridges.push_back(j);
    DockingGuide d;
    d.stand = name;
    d.lat = lat;
    d.lon = lon;
    d.hdg_deg = hdg;
    a.docking.push_back(d);
    Marshaller m;
    m.gate = name;
    offset_m(lat, lon, hdg + 90.0, 18.0, m.lat, m.lon);
    m.hdg_deg = hdg + 180.0;
    a.marshallers.push_back(m);
}

void add_route(Airport& a, const char* name, const char* kind,
               const std::vector<std::pair<double, double>>& path) {
    SceneRoute r;
    r.name = name;
    r.kind = kind;
    r.path = path;
    a.routes.push_back(r);
}

void plant_trees(Airport& a) {
    if (a.runways.empty())
        return;
    const auto& rw = a.runways[0];
    push_tree_ring(a, rw.le_lat, rw.le_lon, rw.heading_deg, 80.0, rw.length_m - 80.0, 140.0, 55.0,
                   11.f);
    if (a.runways.size() > 1) {
        const auto& r2 = a.runways[1];
        push_tree_ring(a, r2.le_lat, r2.le_lon, r2.heading_deg, 80.0, r2.length_m - 80.0, -150.0,
                       70.0, 9.f);
    }
}

// Raster's 15 m ARP grid was a fake lattice (including marsh/bay). Street
// lamps are emitted in add_street_lights from roads / buildings / aprons.
void fill_night_layer(Airport& a) {
    a.night.spacing_m = 35;
    a.night.points = 0;
}

double dist_to_seg_m(double lat, double lon, double a0, double o0, double a1, double o1) {
    const double mx = geo::kDeg * geo::kWgsA;
    const double my = mx * std::cos(geo::deg2rad(a0));
    const double px = (lon - o0) * my;
    const double py = (lat - a0) * mx;
    const double sx = (o1 - o0) * my;
    const double sy = (a1 - a0) * mx;
    const double sl2 = sx * sx + sy * sy;
    double t = sl2 > 1e-6 ? (px * sx + py * sy) / sl2 : 0.0;
    t = std::max(0.0, std::min(1.0, t));
    const double dx = px - sx * t;
    const double dy = py - sy * t;
    return std::sqrt(dx * dx + dy * dy);
}

bool near_runway(const Airport& a, double lat, double lon, double pad_m) {
    for (const auto& rw : a.runways) {
        const double lim = rw.width_m * 0.5 + pad_m;
        if (dist_to_seg_m(lat, lon, rw.le_lat, rw.le_lon, rw.he_lat, rw.he_lon) < lim)
            return true;
    }
    return false;
}

void add_shared_routes(Airport& a) {
    int i = 0;
    for (const auto& t : a.taxiways) {
        if (i >= 10)
            break;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "mov-%s", t.name.c_str());
        add_route(a, buf, "taxi", t.path);
        ++i;
    }
    add_route(a, "road-1", "road",
              {{a.arp_lat + 0.004, a.arp_lon - 0.006}, {a.arp_lat - 0.003, a.arp_lon + 0.005}});
    add_route(a, "road-2", "road",
              {{a.arp_lat + 0.002, a.arp_lon + 0.007}, {a.arp_lat - 0.005, a.arp_lon - 0.002}});
}

void add_tower_building(Airport& a) {
    if (a.tower_h_m < 1.f)
        return;
    Building b;
    b.name = "ATCT";
    b.height_m = a.tower_h_m;
    b.kind = BuildingKind::Airport;
    ring_box(b.ring, a.tower_lat, a.tower_lon, 0.0, 28.0, 28.0);
    a.buildings.push_back(std::move(b));
}

} // namespace

void add_kjfk_public_scenery(Airport& a) {
    if (a.runways.size() < 4)
        return;
    // 2022 JFK names (FAA airport diagram): C1–C6, U1–U3. Taxiway CC removed.
    static const char* kC[] = {"C1", "C2", "C3", "C4", "C5", "C6"};
    static const char* kU[] = {"U1", "U2", "U3"};
    add_named_connectors(a, a.runways[0], kC, 6, 80.0, 220.0);
    add_named_connectors(a, a.runways[2], kU, 3, 80.0, 200.0);
    // NASR/chart-supplement ATCT placed so 04R 6 nm sees −8.2° / 1766 m left
    // and 13L 6 nm sees +4.9° / ~1070 m right. Height 320 ft AGL (CS).
    a.tower_lat = 40.642335;
    a.tower_lon = -73.781283;
    a.tower_h_m = 320.f * 0.3048f;
    a.has_beacon = true;
    add_tower_building(a);
    auto mark = [&](const char* name, const char* rwy, double lat, double lon) {
        ApproachMark m;
        m.name = name;
        m.rwy = rwy;
        m.lat = lat;
        m.lon = lon;
        a.approach_marks.push_back(std::move(m));
    };
    // Ground-track anchors along the 3° finals (FAA landing THR, WGS-84).
    mark("Rockaway Beach", "04R", 40.585398, -73.801724);
    mark("A-train viaduct", "04R", 40.590974, -73.797356);
    mark("bay shore", "04R", 40.603127, -73.787833);
    mark("Aqueduct", "13L", 40.6722, -73.8297);
    mark("Belt Parkway", "13L", 40.666531, -73.809667);
    {
        Building aq;
        aq.name = "Aqueduct";
        aq.height_m = 18.f;
        aq.kind = BuildingKind::Airport;
        aq.type = "grandstand";
        ring_box(aq.ring, 40.6722, -73.8297, 35.0, 180.0, 90.0);
        a.buildings.push_back(std::move(aq));
    }
    {
        Building cem;
        cem.name = "cemetery";
        cem.height_m = 4.f;
        cem.kind = BuildingKind::Airport;
        cem.type = "cemetery";
        ring_box(cem.ring, 40.6825, -73.8455, 35.0, 220.0, 160.0);
        a.buildings.push_back(std::move(cem));
    }
    add_stand(a, "T4-1", 40.6440, -73.7772, 40.0);
    add_stand(a, "T4-2", 40.6448, -73.7786, 40.0);
    add_stand(a, "T5-1", 40.6406, -73.7862, 121.0);
    add_stand(a, "T8-1", 40.6464, -73.7888, 121.0);
    plant_trees(a);
    fill_night_layer(a);
    add_shared_routes(a);
    add_public_ny_roads(a);
}

void add_kewr_public_scenery(Airport& a) {
    if (a.runways.size() < 3)
        return;
    static const char* kC[] = {"C1", "C2", "C3"};
    add_named_connectors(a, a.runways[0], kC, 3, 70.0, 180.0);
    if (a.tower_h_m < 1.f) {
        a.tower_lat = 40.6928;
        a.tower_lon = -74.1688;
        a.tower_h_m = 55.f;
        a.has_beacon = true;
    }
    add_stand(a, "A12", 40.6898, -74.1734, 26.0);
    add_stand(a, "B4", 40.6922, -74.1768, 26.0);
    plant_trees(a);
    fill_night_layer(a);
    add_shared_routes(a);
    add_public_ny_roads(a);
}

void add_street_lights(Airport& a) {
    // Accuracy-first: city/street lamps follow real roads (public NY + OSM
    // highway class) and OSM building density. Never a regular grid, never
    // over water/marsh. Airport floods come from official/OSM apron rings.
    constexpr int kMaxNightLamps = 800; // 980 Ti point-sprite budget
    constexpr float kLandMin = 0.50f;
    auto push = [&](double lat, double lon, double alt_m, float r, float g, float b) {
        LightPoint p;
        p.lat = lat;
        p.lon = lon;
        p.alt_m = alt_m;
        p.r = r;
        p.g = g;
        p.b = b;
        p.switch_num = kSwTaxiEdge;
        p.kind = LightKind::Night;
        a.lights.push_back(p);
    };
    int n = 0;
    for (const auto& road : a.roads) {
        if (road.path.size() < 2 || road.length_m < 40.0)
            continue;
        const double step = static_cast<double>(class_lamp_spacing_m(road.cls));
        int side = 0;
        for (double s = step * 0.5; s < road.length_m && n < kMaxNightLamps; s += step) {
            double lat = 0, lon = 0, hdg = 0;
            if (!sample_road(road, s, lat, lon, hdg))
                continue;
            double plat = lat, plon = lon;
            offset_m(lat, lon, hdg + (side ? 90.0 : -90.0), 6.5, plat, plon);
            side = 1 - side;
            if (geo::land_coverage(plat, plon) < kLandMin)
                continue;
            if (near_runway(a, plat, plon, 25.0))
                continue;
            push(plat, plon, a.elev_m + 8.0, 1.f, 0.78f, 0.38f);
            ++n;
        }
    }
    int osm_left = kMaxNightLamps - n;
    int osm_seen = 0;
    int osm_keep = std::max(1, osm_left);
    for (const auto& b : a.buildings) {
        if (n >= kMaxNightLamps)
            break;
        if (b.kind != BuildingKind::Osm)
            continue;
        ++osm_seen;
        const int stride = std::max(1, static_cast<int>(a.buildings.size()) / osm_keep);
        if ((osm_seen % stride) != 0)
            continue;
        double lat = 0, lon = 0;
        building_centroid(b, lat, lon);
        if (geo::land_coverage(lat, lon) < kLandMin)
            continue;
        if (near_runway(a, lat, lon, 40.0))
            continue;
        const double z = a.elev_m + std::min(18.0, std::max(6.0, static_cast<double>(b.height_m)));
        push(lat, lon, z, 1.f, 0.72f, 0.40f);
        ++n;
    }
    for (const auto& ap : a.aprons) {
        if (n >= kMaxNightLamps || ap.ring.size() < 3)
            continue;
        double clat = 0, clon = 0;
        for (const auto& p : ap.ring) {
            clat += p.first;
            clon += p.second;
        }
        clat /= static_cast<double>(ap.ring.size());
        clon /= static_cast<double>(ap.ring.size());
        if (geo::land_coverage(clat, clon) >= kLandMin && !near_runway(a, clat, clon, 15.0)) {
            push(clat, clon, a.elev_m + 12.0, 1.f, 0.92f, 0.78f);
            ++n;
        }
        const std::size_t ring_step = std::max<std::size_t>(1, ap.ring.size() / 4);
        for (std::size_t i = 0; i < ap.ring.size() && n < kMaxNightLamps; i += ring_step) {
            const auto& p = ap.ring[i];
            if (geo::land_coverage(p.first, p.second) < kLandMin)
                continue;
            if (near_runway(a, p.first, p.second, 20.0))
                continue;
            push(p.first, p.second, a.elev_m + 12.0, 1.f, 0.92f, 0.78f);
            ++n;
        }
    }
    a.night.spacing_m = 35;
    a.night.points = n;
}

void add_scenery_lights(Airport& a) {
    auto push = [&](double lat, double lon, float r, float g, float b, int sw, LightKind kind) {
        LightPoint p;
        p.lat = lat;
        p.lon = lon;
        p.alt_m = a.elev_m + 1.2;
        p.r = r;
        p.g = g;
        p.b = b;
        p.switch_num = sw;
        p.kind = kind;
        a.lights.push_back(p);
    };
    if (a.has_beacon && a.tower_h_m > 1.f)
        push(a.tower_lat, a.tower_lon, 1.f, 0.15f, 0.1f, kSwStrobe, LightKind::Beacon);
    for (const auto& s : a.signs)
        push(s.lat, s.lon, 1.f, 0.85f, 0.2f, kSwTaxiEdge, LightKind::SignGlow);
    for (const auto& t : a.taxiways) {
        if (t.name.size() < 2)
            continue;
        if (t.name[0] != 'C' && t.name[0] != 'U')
            continue;
        if (t.path.size() < 2)
            continue;
        // Lead-on: green centreline from the connector onto the runway end.
        const double lat0 = t.path.front().first, lon0 = t.path.front().second;
        const double lat1 = t.path.back().first, lon1 = t.path.back().second;
        for (int i = 0; i < 4; ++i) {
            const double u = i / 3.0;
            push(lat0 + (lat1 - lat0) * u * 0.25, lon0 + (lon1 - lon0) * u * 0.25, 0.15f, 1.f,
                 0.25f, kSwTaxiCenterline, LightKind::LeadOn);
        }
    }
    for (const auto& h : a.hold_bars)
        push(h.lat, h.lon, 1.f, 0.2f, 0.15f, kSwStopBar, LightKind::Rwsl);
    add_street_lights(a);
}

} // namespace s376
