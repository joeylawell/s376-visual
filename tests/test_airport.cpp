#include "s376/airport.hpp"
#include "s376/fetch.hpp"
#include "s376/geo.hpp"
#include "s376/landmarks.hpp"
#include "s376/lights.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

extern int g_fails;
extern int g_passes;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (cond) {                                                                                \
            ++g_passes;                                                                            \
        } else {                                                                                   \
            ++g_fails;                                                                             \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #cond << "\n";          \
        }                                                                                          \
    } while (0)
#define CHECK_NEAR(a, b, eps)                                                                      \
    do {                                                                                           \
        const double _a = static_cast<double>(a);                                                  \
        const double _b = static_cast<double>(b);                                                  \
        if (std::fabs(_a - _b) <= (eps)) {                                                         \
            ++g_passes;                                                                            \
        } else {                                                                                   \
            ++g_fails;                                                                             \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #a << "=" << _a << " "  \
                      << #b << "=" << _b << " eps=" << (eps) << "\n";                              \
        }                                                                                          \
    } while (0)

namespace {

void check_runway_length(const s376::Runway& rw) {
    double dlat = (rw.he_lat - rw.le_lat) * s376::geo::kDeg * s376::geo::kWgsA;
    double dlon = (rw.he_lon - rw.le_lon) * s376::geo::kDeg * s376::geo::kWgsA *
                  std::cos(s376::geo::deg2rad(rw.le_lat));
    const double len = std::sqrt(dlat * dlat + dlon * dlon);
    CHECK_NEAR(len, rw.length_m, 12.0);
}

void check_lights(const s376::Airport& a, int min_edge, int min_cl) {
    CHECK(!a.lights.empty());
    int n_edge = 0, n_cl = 0, n_app = 0, n_end = 0;
    for (const auto& L : a.lights) {
        if (L.kind == s376::LightKind::Edge) {
            ++n_edge;
            CHECK(s376::lights::is_runway_switch(L.switch_num, s376::lights::kOffEdge));
        } else if (L.kind == s376::LightKind::Centerline) {
            ++n_cl;
            CHECK(s376::lights::is_runway_switch(L.switch_num, s376::lights::kOffCenterline));
        } else if (L.kind == s376::LightKind::Approach) {
            ++n_app;
            CHECK(s376::lights::is_runway_switch(L.switch_num, s376::lights::kOffApproachA) ||
                  s376::lights::is_runway_switch(L.switch_num, s376::lights::kOffApproachB));
        } else if (L.kind == s376::LightKind::End || L.kind == s376::LightKind::Threshold) {
            ++n_end;
        }
    }
    CHECK(n_edge >= min_edge);
    CHECK(n_cl >= min_cl);
    CHECK(n_app > 50);
    CHECK(n_end >= 8);
}

void check_mesh(const s376::Airport& a) {
    auto mesh = s376::build_airport_mesh(a);
    CHECK(mesh.verts.size() >= 18);
    CHECK(mesh.verts.size() % 3 == 0);
}

const s376::Building* find_bldg(const s376::Airport& a, const char* name) {
    for (const auto& b : a.buildings) {
        if (b.name == name)
            return &b;
    }
    return nullptr;
}

void check_named_solid(const s376::SolidBvh& bvh, const s376::Airport& a, const char* name) {
    const s376::Building* b = find_bldg(a, name);
    CHECK(b != nullptr);
    if (!b)
        return;
    CHECK(s376::is_airport_structure(*b));
    double lat = 0, lon = 0;
    s376::building_centroid(*b, lat, lon);
    const double ground = a.elev_m;
    const double want = ground + s376::kBuildingSitM + static_cast<double>(b->height_m);
    double roof = 0;
    CHECK(bvh.roof_msl(lat, lon, &roof));
    CHECK_NEAR(roof, want, 1.5);
    const auto hit = bvh.segment(lat, lon, ground + 1.0, lat, lon, ground + b->height_m * 0.5);
    CHECK(hit.hit);
    const double roof_ft = roof * s376::geo::kMToFt;
    const auto hs = s376::sample_hat_solids(nullptr, &bvh, lat, lon, roof_ft + 80.0);
    CHECK(hs.building);
    CHECK_NEAR(hs.hat_ft, 80.0, 8.0);
}

// Host crash path: eye inside the volume → HAT ≤ 0. Segment from above
// through the roof → intersect.
void check_building_contact(const s376::SolidBvh& bvh, const s376::Airport& a, const char* name) {
    const s376::Building* b = find_bldg(a, name);
    CHECK(b != nullptr);
    if (!b)
        return;
    double lat = 0, lon = 0;
    s376::building_centroid(*b, lat, lon);
    const double ground = a.elev_m;
    const double mid_m = ground + s376::kBuildingSitM + static_cast<double>(b->height_m) * 0.5;
    const double mid_ft = mid_m * s376::geo::kMToFt;
    const double above_ft =
        (ground + s376::kBuildingSitM + static_cast<double>(b->height_m) + 80.0) * s376::geo::kMToFt;
    const auto inside = s376::sample_hat_solids(nullptr, &bvh, lat, lon, mid_ft);
    CHECK(inside.building);
    CHECK(inside.hat_ft <= 0.f);
    const auto pierce =
        s376::sample_hat_solids(nullptr, &bvh, lat, lon, mid_ft, &lat, &lon, &above_ft);
    CHECK(pierce.building);
    CHECK(pierce.intersect);
    CHECK(pierce.hat_ft <= 0.f);
}

void check_real_kjfk_solids_contact() {
    const char* cands[] = {"/tmp/kjfk-real/solids.bin", "cache/KJFK/solids.bin", nullptr};
    std::string path;
    for (int i = 0; cands[i]; ++i) {
        if (std::filesystem::exists(cands[i])) {
            path = cands[i];
            break;
        }
    }
    if (path.empty()) {
        std::cerr << "  (KJFK solids.bin not on disk — official T5 contact still ran)\n";
        return;
    }
    s376::SolidBvh real;
    const int n = s376::read_solids_bin(real, path);
    if (n <= 0) {
        std::cerr << "  (KJFK solids.bin at " << path
                  << " is empty/header-only — official T5 contact still ran)\n";
        return;
    }
    real.build();
    CHECK(real.bytes() > 0);
    // Official T5 centroid (src/airport.cpp box_building). If the OSM pack
    // covers that footprint, HAT ≤ 0 inside the roof; otherwise volumes exist.
    const double t5_lat = 40.6410;
    const double t5_lon = -73.7870;
    double roof = 0;
    if (real.roof_msl(t5_lat, t5_lon, &roof) && roof > 0) {
        const double mid_ft = (roof - 2.0) * s376::geo::kMToFt;
        const double above_ft = (roof + 40.0) * s376::geo::kMToFt;
        const auto inside = s376::sample_hat_solids(nullptr, &real, t5_lat, t5_lon, mid_ft);
        CHECK(inside.building);
        CHECK(inside.hat_ft <= 0.f);
        const auto pierce = s376::sample_hat_solids(nullptr, &real, t5_lat, t5_lon, mid_ft, &t5_lat,
                                                    &t5_lon, &above_ft);
        CHECK(pierce.intersect);
        CHECK(pierce.hat_ft <= 0.f);
        std::cerr << "  real KJFK solids.bin contact at T5  roof=" << roof << " m  " << path
                  << "\n";
    } else {
        std::cerr << "  real KJFK solids.bin loaded (" << n << " recs, " << real.bytes()
                  << " B) but no roof at official T5 — volumes still present\n";
    }
}

void check_jetbridge_solid(const s376::SolidBvh& bvh, const s376::Airport& a) {
    CHECK(!a.jetbridges.empty());
    for (const auto& j : a.jetbridges) {
        const s376::Building jb = s376::jetbridge_building(j);
        double lat = 0, lon = 0;
        s376::building_centroid(jb, lat, lon);
        double roof = 0;
        CHECK(bvh.roof_msl(lat, lon, &roof));
        // May sit under a terminal; host takes the highest roof.
        CHECK(roof + 0.1 >= a.elev_m + s376::kJetbridgeDeckM);
        const auto hit = bvh.segment(lat, lon, a.elev_m + 1.0, lat, lon, a.elev_m + 4.0);
        CHECK(hit.hit);
    }
}

} // namespace

void test_airport() {
    auto a = s376::ksea_public();
    CHECK(a.icao == "KSEA");
    CHECK(a.runways.size() == 3);
    CHECK(a.runways[0].ident_le == "16L");
    CHECK(a.runways[0].ident_he == "34R");
    CHECK_NEAR(a.runways[0].le_lat, 47.463799, 1e-6);
    CHECK_NEAR(a.runways[0].le_lon, -122.307999, 1e-6);
    CHECK_NEAR(a.runways[0].length_m, 11901.0 * s376::geo::kFtToM, 4.0);
    CHECK_NEAR(a.runways[0].width_m, 150.0 * s376::geo::kFtToM, 0.2);
    CHECK(a.runways[0].approach_le == "ALSF-2");
    CHECK(!a.aprons.empty());

    check_runway_length(a.runways[0]);
    CHECK(a.runways[0].he_lat < a.runways[0].le_lat);

    bool near_16l = false;
    for (const auto& L : a.lights) {
        const double d = std::fabs(L.lat - 47.463799) + std::fabs(L.lon + 122.307999);
        if (d < 0.002)
            near_16l = true;
    }
    CHECK(near_16l);
    check_lights(a, 100, 200);
    check_mesh(a);

    const char* kn = s376::light_kind_name(s376::LightKind::Approach);
    CHECK(kn[0] == 'a');

    auto sfo = s376::ksfo_public();
    CHECK(sfo.icao == "KSFO");
    CHECK(sfo.runways.size() == 4);
    CHECK(sfo.runways[0].ident_le == "10L");
    CHECK(sfo.runways[0].ident_he == "28R");
    CHECK(sfo.runways[0].approach_he == "ALSF-2");
    CHECK(sfo.runways[0].approach_le == "NONE");
    CHECK(sfo.runways[1].ident_le == "10R");
    CHECK(sfo.runways[2].ident_le == "01L");
    CHECK(sfo.runways[3].ident_le == "01R");
    CHECK_NEAR(sfo.arp_lat, 37.6188, 1e-4);
    CHECK_NEAR(sfo.runways[0].length_m, 11870.0 * s376::geo::kFtToM, 4.0);
    CHECK_NEAR(sfo.runways[0].width_m, 200.0 * s376::geo::kFtToM, 0.2);
    CHECK(sfo.taxiways.size() >= 3);
    CHECK(!sfo.aprons.empty());
    check_runway_length(sfo.runways[0]);
    check_runway_length(sfo.runways[2]);
    check_lights(sfo, 200, 400);
    check_mesh(sfo);

    auto las = s376::klas_public();
    CHECK(las.icao == "KLAS");
    CHECK(las.runways.size() == 4);
    CHECK(las.runways[0].ident_le == "08L");
    CHECK(las.runways[0].ident_he == "26R");
    CHECK(las.runways[0].approach_he == "MALS");
    CHECK(las.runways[1].ident_le == "08R");
    CHECK(las.runways[1].approach_he == "MALSF");
    CHECK(las.runways[2].ident_le == "01L");
    CHECK(las.runways[3].ident_le == "01R");
    CHECK_NEAR(las.arp_lat, 36.080333, 1e-4);
    CHECK_NEAR(las.elev_m, 2183.0 * s376::geo::kFtToM, 0.5);
    CHECK_NEAR(las.runways[0].length_m, 14835.0 * s376::geo::kFtToM, 20.0);
    CHECK(las.taxiways.size() >= 3);
    CHECK(!las.aprons.empty());
    check_runway_length(las.runways[0]);
    check_lights(las, 200, 400);
    check_mesh(las);

    CHECK(s376::airport_by_icao("ksfo").icao == "KSFO");
    CHECK(s376::airport_by_icao("KLAS").runways.size() == 4);

    auto lax = s376::klax_public();
    CHECK(lax.icao == "KLAX");
    CHECK(lax.runways.size() == 4);
    CHECK(lax.runways[0].ident_le == "06L" && lax.runways[0].ident_he == "24R");
    CHECK(lax.runways[0].approach_he == "ALSF-2");
    CHECK(lax.runways[3].ident_le == "07R");
    CHECK_NEAR(lax.arp_lat, 33.9425, 1e-4);
    CHECK_NEAR(lax.runways[2].length_m, 12923.0 * s376::geo::kFtToM, 20.0);
    CHECK(lax.taxiways.size() >= 3);
    check_runway_length(lax.runways[0]);
    check_lights(lax, 200, 400);
    check_mesh(lax);

    auto jfk = s376::kjfk_public();
    CHECK(jfk.icao == "KJFK");
    CHECK(jfk.runways.size() == 4);
    CHECK(jfk.runways[0].ident_le == "04L" && jfk.runways[0].ident_he == "22R");
    CHECK(jfk.runways[0].approach_le == "NONE");
    CHECK(jfk.runways[0].approach_he == "NONE");
    CHECK(jfk.runways[1].approach_le == "ALSF-2");
    CHECK(jfk.runways[1].approach_he == "ALSF-2");
    CHECK(jfk.runways[2].ident_le == "13L" && jfk.runways[2].ident_he == "31R");
    CHECK(jfk.runways[2].approach_le == "ALSF-2");
    CHECK(jfk.runways[2].approach_he == "MALSR");
    CHECK_NEAR(jfk.runways[2].width_m, 200.0 * s376::geo::kFtToM, 0.2);
    CHECK(jfk.runways[3].ident_le == "13R");
    CHECK(jfk.runways[3].approach_le == "RLLS");
    CHECK(jfk.runways[3].approach_he == "NONE");
    CHECK_NEAR(jfk.runways[0].he_displaced_ft, 3424.0, 0.5);
    CHECK_NEAR(jfk.runways[3].le_displaced_ft, 2044.0, 0.5);
    CHECK_NEAR(jfk.runways[3].he_displaced_ft, 3264.0, 0.5);
    CHECK(jfk.runways[1].emas_le && jfk.runways[1].emas_he);
    CHECK(jfk.runways[2].papi_he == "P4R");
    CHECK(jfk.runways[3].papi_count_le == 1);
    CHECK_NEAR(jfk.arp_lat, 40.639928, 1e-5);
    CHECK_NEAR(jfk.runways[3].length_m, 14511.0 * s376::geo::kFtToM, 8.0);
    CHECK(!jfk.source.name.empty());
    CHECK(jfk.source.effective == "2026-10-01");
    CHECK(jfk.runways[0].tdz_le);  // 04L TDZL
    CHECK(jfk.runways[2].tdz_he);  // 31R TDZL
    CHECK(jfk.runways[2].rlls_lead_in_le);
    CHECK_NEAR(jfk.runways[1].vgsi_tch_ft_le, 69.0, 0.1);
    CHECK_NEAR(jfk.runways[2].vgsi_tch_ft_le, 74.0, 0.1);
    CHECK_NEAR(jfk.runways[1].heading_deg, 30.768, 0.05);
    bool saw_c1 = false, saw_u1 = false, saw_cc = false, saw_t5 = false;
    for (const auto& t : jfk.taxiways) {
        if (t.name == "C1")
            saw_c1 = true;
        if (t.name == "U1")
            saw_u1 = true;
        if (t.name == "CC")
            saw_cc = true;
    }
    float t5_h = 0;
    for (const auto& b : jfk.buildings)
        if (b.name == "T5") {
            saw_t5 = true;
            t5_h = b.height_m;
        }
    CHECK(saw_c1 && saw_u1 && !saw_cc && saw_t5);
    CHECK(t5_h >= 24.f); // readable at 3 nm; schematic T5 is not a shed
    CHECK(jfk.has_beacon && jfk.tower_h_m > 10.f);
    CHECK(jfk.roads.size() >= 4);
    CHECK(jfk.routes.size() >= 12);
    CHECK(jfk.night.spacing_m >= 25 && jfk.night.spacing_m <= 50);
    CHECK(jfk.night.points > 20 && jfk.night.points <= 800);
    check_runway_length(jfk.runways[0]);
    check_runway_length(jfk.runways[3]);
    check_lights(jfk, 200, 400);
    check_mesh(jfk);
    {
        const auto& rw = jfk.runways[1]; // 04R ALSF-2
        int n_als = 0, n_before = 0, n_side = 0, n_papi = 0, n_tdz = 0;
        double max_back_m = 0;
        for (const auto& L : jfk.lights) {
            if (L.runway_k != 2)
                continue;
            if (L.kind == s376::LightKind::Vasi) {
                ++n_papi;
                CHECK(L.papi_box >= 0 && L.papi_box <= 3);
                CHECK_NEAR(L.papi_gs_deg, 3.0, 0.05);
            }
            if (L.kind == s376::LightKind::Threshold)
                ++n_tdz;
            if (L.kind != s376::LightKind::Approach && L.kind != s376::LightKind::Flasher)
                continue;
            ++n_als;
            double hdg = 0;
            const double d = s376::geo::inverse_wgs84(rw.le_lat, rw.le_lon, L.lat, L.lon, &hdg);
            double dh = hdg - (rw.heading_deg + 180.0);
            while (dh > 180.0)
                dh -= 360.0;
            while (dh < -180.0)
                dh += 360.0;
            if (std::fabs(dh) < 30.0 && d > 20.0) {
                ++n_before;
                max_back_m = std::max(max_back_m, d);
                const double across = d * std::sin(s376::geo::deg2rad(dh));
                if (std::fabs(across) > 12.0 && L.r > 0.6f && L.g < 0.3f)
                    ++n_side;
            }
        }
        CHECK(n_als > 120);
        CHECK(n_before > 80);
        CHECK(max_back_m > 700.0); // 2400 ft ≈ 732 m
        CHECK(n_side >= 20);
        CHECK(n_papi >= 4);
        CHECK(n_tdz >= 30);
        int n_edge = 0, n_cl = 0, n_edge_l = 0, n_edge_r = 0;
        for (const auto& L : jfk.lights) {
            if (L.runway_k != 2)
                continue;
            double hdg = 0;
            const double d = s376::geo::inverse_wgs84(rw.le_lat, rw.le_lon, L.lat, L.lon, &hdg);
            double along = d * std::cos(s376::geo::deg2rad(hdg - rw.heading_deg));
            double across = d * std::sin(s376::geo::deg2rad(hdg - rw.heading_deg));
            if (along < -20.0 || along > rw.length_m + 20.0)
                continue;
            if (L.kind == s376::LightKind::Edge) {
                ++n_edge;
                if (across < -rw.width_m * 0.15)
                    ++n_edge_l;
                if (across > rw.width_m * 0.15)
                    ++n_edge_r;
            }
            if (L.kind == s376::LightKind::Centerline && std::fabs(across) < 8.0)
                ++n_cl;
        }
        CHECK(n_edge >= 80); // HIRL both sides, ~60 m
        CHECK(n_edge_l >= 30 && n_edge_r >= 30);
        CHECK(n_cl >= 140); // CAT III CL ~15 m on ~2560 m 04R
    }
    {
        // Street lamps follow public NY roads; never a 60 m ARP lattice
        // and never Jamaica Bay / marsh south of the field.
        int n_night = 0, n_grid = 0, n_bay = 0, n_wet = 0;
        for (const auto& L : jfk.lights) {
            if (L.kind != s376::LightKind::Night)
                continue;
            ++n_night;
            if (s376::geo::land_coverage(L.lat, L.lon) < 0.50f)
                ++n_wet;
            if (L.lat > 40.588 && L.lat < 40.625 && L.lon > -73.812 && L.lon < -73.742)
                ++n_bay;
            const double n_m = (L.lat - jfk.arp_lat) * s376::geo::kDeg * s376::geo::kWgsA;
            const double e_m = (L.lon - jfk.arp_lon) * s376::geo::kDeg * s376::geo::kWgsA *
                               std::cos(s376::geo::deg2rad(jfk.arp_lat));
            const double ns = std::fabs(std::fmod(n_m, 60.0));
            const double es = std::fabs(std::fmod(e_m, 60.0));
            if ((ns < 4.0 || ns > 56.0) && (es < 4.0 || es > 56.0))
                ++n_grid;
        }
        CHECK(n_night == jfk.night.points);
        CHECK(n_night > 20);
        CHECK(n_wet == 0);
        CHECK(n_bay == 0);
        CHECK(n_grid < n_night / 4); // not a regular ENU lattice
        bool taxi_b = false;
        const auto& rw04r = jfk.runways[1];
        for (const auto& t : jfk.taxiways) {
            if (t.name != "B" || t.path.size() < 2)
                continue;
            taxi_b = true;
            const double mlat = 0.5 * (t.path.front().first + t.path.back().first);
            const double mlon = 0.5 * (t.path.front().second + t.path.back().second);
            double hdg = 0;
            const double d =
                s376::geo::inverse_wgs84(rw04r.le_lat, rw04r.le_lon, mlat, mlon, &hdg);
            const double across = d * std::sin(s376::geo::deg2rad(hdg - rw04r.heading_deg));
            CHECK(across < -50.0 && across > -110.0); // 04L-side of 04R
        }
        CHECK(taxi_b);
        int n_blue_edge = 0, n_green_cl = 0, n_wrong_edge = 0;
        for (const auto& L : jfk.lights) {
            if (L.kind == s376::LightKind::TaxiEdge) {
                if (L.b > 0.70f && L.g < 0.55f)
                    ++n_blue_edge;
                if (L.g > 0.70f && L.b < 0.45f)
                    ++n_wrong_edge;
            }
            if (L.kind == s376::LightKind::TaxiCenterline && L.g > 0.70f && L.b < 0.45f)
                ++n_green_cl;
        }
        CHECK(n_blue_edge > 40);
        CHECK(n_green_cl > 20);
        CHECK(n_wrong_edge == 0);
    }

    s376::SolidBvh jfk_solids;
    s376::fill_solids(jfk_solids, s376::SkylineEra::Current, {jfk});
    for (const char* name : {"T1", "T4", "T5", "T7", "T8", "hangar", "cargo", "ATCT"})
        check_named_solid(jfk_solids, jfk, name);
    check_building_contact(jfk_solids, jfk, "T5");
    check_real_kjfk_solids_contact();
    check_jetbridge_solid(jfk_solids, jfk);

    auto ewr = s376::kewr_public();
    CHECK(ewr.icao == "KEWR");
    CHECK(ewr.runways.size() == 3);
    CHECK(ewr.runways[0].ident_le == "04L" && ewr.runways[0].ident_he == "22R");
    CHECK(ewr.runways[0].approach_le == "MALSR" && ewr.runways[0].approach_he == "MALSR");
    CHECK(ewr.runways[1].approach_le == "ALSF-2" && ewr.runways[1].approach_he == "ALSF-2");
    CHECK(ewr.runways[2].ident_le == "11" && ewr.runways[2].ident_he == "29");
    CHECK(ewr.runways[2].reil_le && ewr.runways[2].reil_he);
    CHECK(ewr.runways[2].emas_le && ewr.runways[2].emas_he);
    CHECK_NEAR(ewr.runways[0].le_displaced_ft, 2540.6, 0.2);
    CHECK_NEAR(ewr.runways[0].he_displaced_ft, 1440.9, 0.2);
    CHECK_NEAR(ewr.runways[1].papi_deg_le, 2.95, 0.01);
    CHECK(s376::airport_by_icao("kewr").icao == "KEWR");
    check_runway_length(ewr.runways[0]);
    check_lights(ewr, 150, 300);
    check_mesh(ewr);

    s376::SolidBvh ewr_solids;
    s376::fill_solids(ewr_solids, s376::SkylineEra::Current, {ewr});
    for (const char* name : {"A", "B", "C", "tower", "hangar"})
        check_named_solid(ewr_solids, ewr, name);
    check_jetbridge_solid(ewr_solids, ewr);

    {
        s376::SolidBvh both;
        s376::fill_solids(both, s376::SkylineEra::Current, {jfk, ewr});
        const auto mem = s376::solids_mem(both);
        CHECK(mem.airport_volumes >= 15);
        CHECK(mem.bytes < 2ull * 1024ull * 1024ull);
        CHECK(mem.bytes <= s376::kMaxSolidBytes);
        // Schematic KJFK+KEWR (no osm.bin in CI). Houses would add tens of
        // MiB via solids.bin and are trimmed first if the 256 MiB cap hits.
        s376::SolidBvh houses;
        for (int i = 0; i < 1000; ++i) {
            s376::Building h;
            h.name = "osm";
            h.height_m = 12.f;
            const double lat = 40.60 + (i / 40) * 0.002;
            const double lon = -73.80 + (i % 40) * 0.002;
            h.ring = {{lat, lon}, {lat, lon + 0.00008}, {lat + 0.00008, lon + 0.00008},
                      {lat + 0.00008, lon}};
            houses.add_building(h, 4.0);
        }
        houses.build();
        const double per = static_cast<double>(houses.bytes()) / 1000.0;
        const std::size_t est_jfk = static_cast<std::size_t>(per * 548000.0) + mem.bytes;
        const std::size_t est_ewr = static_cast<std::size_t>(per * 80000.0);
        CHECK(est_jfk + est_ewr < s376::kMaxSolidBytes);
        std::cerr << "KJFK+KEWR collision  airport=" << mem.airport_volumes
                  << "  schematic=" << mem.bytes << " B  house≈" << static_cast<int>(per)
                  << " B  full-est KJFK548k+KEWR80k=" << (est_jfk + est_ewr) / 1024 << " KiB\n";
    }

    bool threw = false;
    try {
        (void)s376::airport_by_icao("XXXX");
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);

    auto bos = s376::kbos_public();
    CHECK(bos.icao == "KBOS");
    CHECK(bos.runways.size() == 6);
    CHECK(bos.runways[1].ident_le == "04R" && bos.runways[1].ident_he == "22L");
    CHECK(bos.runways[1].approach_le == "ALSF-2");
    CHECK(bos.runways[3].ident_le == "15R" && bos.runways[3].approach_le == "MALSR");
    CHECK(bos.runways[3].approach_he == "ALSF-2");
    CHECK(bos.runways[5].ident_le == "14");
    CHECK_NEAR(bos.arp_lat, 42.3630, 1e-4);
    CHECK_NEAR(bos.runways[1].length_m, 10006.0 * s376::geo::kFtToM, 4.0);
    CHECK(bos.buildings.size() >= 2);
    check_runway_length(bos.runways[1]);
    check_lights(bos, 200, 400);
    check_mesh(bos);
    CHECK(s376::airport_by_icao("kbos").icao == "KBOS");

    const auto all = s376::all_public_airports();
    CHECK(all.size() == 18);
    CHECK(all[0].icao == "KSEA" && all[1].icao == "KSFO" && all[2].icao == "KLAS");
    CHECK(all[3].icao == "KLAX" && all[4].icao == "KJFK" && all[5].icao == "KEWR");
    CHECK(all[6].icao == "KBOS");
    CHECK(all[10].icao == "KRNO" && all[16].icao == "SKRG" && all[17].icao == "VNKT");
    CHECK(s376::airport_by_icao("egll").icao == "EGLL");
    CHECK(s376::airport_by_icao("SKRG").runways.size() == 1);

    {
        auto p0 = s376::parse_shot_at("KJFK:04R");
        CHECK(p0.icao == "KJFK" && p0.rwy == "04R");
        CHECK_NEAR(p0.dist_nm, 0.0, 1e-9);
        CHECK_NEAR(p0.hdg_true_deg, 31.0, 0.6);
        CHECK_NEAR(p0.pitch_deg, 0.0, 1e-9);
        auto p3 = s376::parse_shot_at("kjfk:04r:3");
        CHECK_NEAR(p3.dist_nm, 3.0, 1e-9);
        CHECK_NEAR(p3.pitch_deg, -3.0, 1e-6);
        CHECK(p3.alt_ft > p0.alt_ft + 800.0);
        auto lined = s376::parse_shot_at("KBOS:04R:0");
        CHECK(lined.icao == "KBOS" && lined.rwy == "04R");
        CHECK_NEAR(lined.hdg_true_deg, 20.0, 1.5);
        auto icao = s376::parse_shot_at("KJFK");
        CHECK(icao.icao == "KJFK" && icao.rwy.empty());
        CHECK_NEAR(icao.lat, 40.6399, 1e-3);
        CHECK_NEAR(icao.lon, -73.7787, 1e-3);
        auto bank = s376::parse_shot_at("KJFK:04R:8");
        s376::ShotOverrides o;
        o.have_alt = true;
        o.alt_ft = 3000;
        o.have_bank = true;
        o.bank_deg = 30;
        s376::apply_shot_overrides(bank, o);
        CHECK_NEAR(bank.alt_ft, 3000.0, 1e-6);
        CHECK_NEAR(bank.roll_deg, 30.0, 1e-6);
        CHECK_NEAR(bank.dist_nm, 8.0, 1e-9);
        CHECK_NEAR(bank.pitch_deg, -3.0, 1e-6);
        auto high = s376::parse_shot_at("KJFK");
        s376::ShotOverrides h;
        h.have_alt = true;
        h.alt_ft = 12000;
        h.have_hdg = true;
        h.hdg_deg = 36;
        s376::apply_shot_overrides(high, h);
        CHECK_NEAR(high.alt_ft, 12000.0, 1e-6);
        CHECK_NEAR(high.hdg_true_deg, 36.0, 1e-6);
        CHECK(high.lat < 40.58); // south of Rockaway, ocean in the foreground
        CHECK(high.pitch_deg <= 0.0 && high.pitch_deg > -3.0); // nearly level, lots of sky
        CHECK(jfk.buildings.size() >= 6);
        bool bad = false;
        try {
            (void)s376::parse_shot_at("XXXX");
        } catch (const std::exception&) {
            bad = true;
        }
        CHECK(bad);
    }

    {
        s376::ApproachPlan ap;
        CHECK(s376::plan_approach("KJFK:04R", 3.0, 2000.0, ap));
        CHECK(ap.icao == "KJFK" && ap.rwy == "04R");
        CHECK_NEAR(ap.gs_deg, 3.0, 1e-12);
        CHECK_NEAR(ap.start_agl_ft, 2000.0, 1e-12);
        CHECK_NEAR(ap.hdg_deg, 31.0, 0.6);
        CHECK_NEAR(ap.start_dist_m, (2000.0 * s376::geo::kFtToM) / std::tan(3.0 * s376::geo::kDeg),
                   1.0);
        ap.kts = 140;
        ap.fps = 30;
        const double T = s376::approach_time_s(ap);
        CHECK(T > 150.0 && T < 175.0);
        const int nf = s376::approach_frame_count(ap);
        CHECK(nf > 4800 && nf < 5000);
        const auto a0 = s376::approach_sample(ap, 0);
        CHECK_NEAR(a0.pitch_deg, -3.0, 1e-9);
        CHECK_NEAR(a0.alt_ft, ap.thr_elev_m * s376::geo::kMToFt + 2000.0, 0.5);
        CHECK_NEAR(a0.dist_nm, ap.start_dist_m / 1852.0, 1e-6);
        CHECK_NEAR(a0.hdg_true_deg, ap.hdg_deg, 1e-6);
        const auto aT = s376::approach_sample(ap, T);
        CHECK_NEAR(aT.dist_nm, 0.0, 1e-6);
        CHECK_NEAR(aT.alt_ft, ap.thr_elev_m * s376::geo::kMToFt, 0.5);
        CHECK_NEAR(aT.pitch_deg, -3.0, 1e-9);
        CHECK_NEAR(aT.lat, ap.thr_lat, 1e-5);
        CHECK_NEAR(aT.lon, ap.thr_lon, 1e-5);
        const auto mid = s376::approach_sample(ap, T * 0.5);
        CHECK_NEAR(mid.alt_ft, (a0.alt_ft + aT.alt_ft) * 0.5, 2.0);
        CHECK_NEAR(mid.dist_nm, a0.dist_nm * 0.5, 0.02);
        CHECK(!s376::plan_approach("KJFK", 3.0, 2000.0, ap));
        CHECK(!s376::plan_approach("KJFK:04R", 0.1, 2000.0, ap));
        CHECK(!s376::plan_approach("KJFK:04R", 3.0, 5.0, ap));
        s376::ApproachPlan ign;
        CHECK(s376::plan_approach("kjfk:04r:8", 3.0, 2000.0, ign));
        CHECK_NEAR(ign.start_agl_ft, 2000.0, 1e-12);
        CHECK(std::fabs(ign.start_dist_m - 8.0 * 1852.0) > 100.0);
    }

    {
        const auto marked = s376::build_airport_mesh(jfk, true, true);
        const auto bare = s376::build_airport_mesh(jfk, false, false);
        CHECK(marked.verts.size() > bare.verts.size());
        CHECK(marked.verts.size() % 3 == 0);
        float saw_mark = 0;
        for (const auto& v : marked.verts)
            saw_mark = std::max(saw_mark, v.mat);
        CHECK(saw_mark >= 1.f);
    }

    const auto url = s376::overpass_aeroway_url(37.60, -122.40, 37.64, -122.35);
    CHECK(url.find("overpass-api.de") != std::string::npos);
    CHECK(url.find("taxiway") != std::string::npos);
    CHECK(url.find("apron") != std::string::npos);
    CHECK(s376::curl_glob_safe(url));
    CHECK(url.find('[') == std::string::npos);
    CHECK(url.find(']') == std::string::npos);
    CHECK(url.find("%5B") != std::string::npos);
    const auto q = s376::overpass_aeroway_query(37.60, -122.40, 37.64, -122.35);
    CHECK(q.find("[out:json]") != std::string::npos);
    CHECK(q.find("building") != std::string::npos);
    CHECK(q.find("natural") != std::string::npos);
    CHECK(q.find("water") != std::string::npos);
    CHECK(q.find("highway") != std::string::npos);
    CHECK(q.find("motorway") != std::string::npos);
    CHECK(!s376::curl_glob_safe(q));
    const auto cmd = s376::http_get_command(url, "/tmp/s376-osm-test.json");
    CHECK(cmd.find(" -g ") != std::string::npos);
    CHECK(cmd.find("--retry") != std::string::npos);
    const auto post = s376::http_post_command("https://overpass-api.de/api/interpreter",
                                              "/tmp/s376-osm-test.post", "/tmp/s376-osm-test.json");
    CHECK(post.find(" -g ") != std::string::npos);
    CHECK(post.find("--data-urlencode") != std::string::npos);

    s376::Airport osm = s376::ksfo_public();
    const std::size_t taxis0 = osm.taxiways.size();
    const std::size_t aprons0 = osm.aprons.size();
    const char* sample =
        "{\"elements\":["
        "{\"type\":\"way\",\"tags\":{\"aeroway\":\"taxiway\"},\"geometry\":["
        "{\"lat\":37.620,\"lon\":-122.380},{\"lat\":37.621,\"lon\":-122.379}]},"
        "{\"type\":\"way\",\"tags\":{\"aeroway\":\"apron\"},\"geometry\":["
        "{\"lat\":37.616,\"lon\":-122.386},{\"lat\":37.617,\"lon\":-122.386},"
        "{\"lat\":37.617,\"lon\":-122.385},{\"lat\":37.616,\"lon\":-122.385}]},"
        "{\"type\":\"way\",\"tags\":{\"building\":\"yes\",\"height\":\"18\"},\"geometry\":["
        "{\"lat\":37.618,\"lon\":-122.384},{\"lat\":37.6185,\"lon\":-122.384},"
        "{\"lat\":37.6185,\"lon\":-122.383},{\"lat\":37.618,\"lon\":-122.383}]}"
        "]}";
    CHECK(s376::merge_osm_json(osm, sample) == 3);
    const char* hwy =
        "{\"type\":\"way\",\"tags\":{\"highway\":\"motorway\",\"name\":\"Van Wyck\"},\"geometry\":["
        "{\"lat\":40.66,\"lon\":-73.80},{\"lat\":40.65,\"lon\":-73.795}]}";
    CHECK(s376::merge_osm_json(osm, hwy) == 1);
    CHECK(!osm.roads.empty());
    CHECK(osm.roads.back().cls == s376::RoadClass::Motorway);
    // Overpass `out geom` puts `"tags"` AFTER geometry. A 442 MB KJFK dump
    // used to classify every highway/building as a taxiway (800-cap, 0
    // buildings, 67k taxi lamps).
    {
        const char* overpass =
            "{\n  \"elements\": [\n"
            "{\n  \"type\": \"way\",\n  \"id\": 1,\n"
            "  \"bounds\": { \"minlat\": 40.64, \"minlon\": -73.78, \"maxlat\": 40.65, "
            "\"maxlon\": -73.77 },\n"
            "  \"geometry\": [\n    { \"lat\": 40.641, \"lon\": -73.778 },\n"
            "    { \"lat\": 40.642, \"lon\": -73.777 }\n  ],\n"
            "  \"tags\": { \"aeroway\": \"taxiway\", \"ref\": \"B\" }\n},\n"
            "{\n  \"type\": \"way\",\n  \"id\": 2,\n"
            "  \"geometry\": [\n    { \"lat\": 40.644, \"lon\": -73.780 },\n"
            "    { \"lat\": 40.6445, \"lon\": -73.780 },\n"
            "    { \"lat\": 40.6445, \"lon\": -73.779 },\n"
            "    { \"lat\": 40.644, \"lon\": -73.779 }\n  ],\n"
            "  \"tags\": { \"building\": \"terminal\", \"height\": \"22\" }\n},\n"
            "{\n  \"type\": \"way\",\n  \"id\": 3,\n"
            "  \"geometry\": [\n    { \"lat\": 40.66, \"lon\": -73.80 },\n"
            "    { \"lat\": 40.65, \"lon\": -73.795 }\n  ],\n"
            "  \"tags\": { \"highway\": \"primary\", \"name\": \"Rockaway Boulevard\" }\n}\n"
            "]}";
        s376::Airport op;
        op.icao = "KJFK";
        CHECK(s376::merge_osm_json(op, overpass) == 3);
        CHECK(op.taxiways.size() == 1);
        CHECK(op.taxiways[0].from_osm);
        CHECK(op.buildings.size() == 1);
        CHECK(op.buildings[0].type == "terminal");
        CHECK_NEAR(op.buildings[0].height_m, 22.0, 0.1);
        CHECK(op.roads.size() == 1);
        CHECK(op.roads[0].cls == s376::RoadClass::Primary);
        const auto tmp_op = (std::filesystem::temp_directory_path() / "s376-osm-overpass").string();
        std::filesystem::remove_all(tmp_op);
        std::filesystem::create_directories(tmp_op);
        {
            std::ofstream out(tmp_op + "/osm.json");
            out << overpass;
        }
        s376::Airport streamed;
        streamed.icao = "KJFK";
        CHECK(s376::merge_osm_file_stream(streamed, tmp_op + "/osm.json", 0) == 3);
        CHECK(streamed.taxiways.size() == 1);
        CHECK(streamed.buildings.size() == 1);
        CHECK(streamed.roads.size() == 1);
        CHECK(s376::bake_osm_bin(tmp_op + "/osm.json", tmp_op + "/osm.bin") == 3);
        s376::Airport baked = s376::kjfk_public();
        const auto lights0 = baked.lights.size();
        CHECK(s376::try_load_osm(baked, tmp_op) == 3);
        CHECK(baked.buildings.size() >= 8); // official terminals + OSM
        s376::generate_lights(baked);
        // OSM taxiways must not multiply FAA edge/CL lamps.
        CHECK(baked.lights.size() < lights0 + 80);
        int n_osm_taxi = 0;
        for (const auto& tw : baked.taxiways) {
            if (tw.from_osm)
                ++n_osm_taxi;
        }
        CHECK(n_osm_taxi == 1);
        std::filesystem::remove_all(tmp_op);
    }
    {
        // Whole-airfield OSM apron must not become one beige fan.
        s376::Airport huge = s376::kjfk_public();
        const auto before = s376::build_airport_mesh(huge).verts.size();
        s376::Apron slab;
        slab.name = "osm-apron";
        slab.ring = {{40.62, -73.82}, {40.62, -73.75}, {40.66, -73.75}, {40.66, -73.82}};
        huge.aprons.push_back(slab);
        s376::Apron pad;
        pad.name = "osm-apron";
        pad.ring = {{40.644, -73.780}, {40.644, -73.778}, {40.646, -73.778}, {40.646, -73.780}};
        huge.aprons.push_back(pad);
        const auto after = s376::build_airport_mesh(huge).verts.size();
        CHECK(after == before); // OSM aprons are imagery, not a mesh fan
    }
    CHECK(osm.taxiways.size() == taxis0 + 1);
    CHECK(osm.aprons.size() == aprons0 + 1);
    CHECK(!osm.buildings.empty());
    CHECK_NEAR(osm.buildings.back().height_m, 18.0, 0.1);
    CHECK(osm.buildings.back().type.empty()); // building=yes is generic
    const char* hangar =
        "{\"type\":\"way\",\"tags\":{\"building\":\"hangar\",\"height\":\"14\"},\"geometry\":["
        "{\"lat\":37.619,\"lon\":-122.382},{\"lat\":37.6195,\"lon\":-122.382},"
        "{\"lat\":37.6195,\"lon\":-122.381},{\"lat\":37.619,\"lon\":-122.381}]}";
    CHECK(s376::merge_osm_json(osm, hangar) == 1);
    CHECK(osm.buildings.back().type == "hangar");
    CHECK(osm.taxiways.back().path.size() == 2);
    CHECK(osm.aprons.back().ring.size() == 4);
    auto osm_mesh = s376::build_airport_mesh(osm);
    CHECK(osm_mesh.verts.size() > s376::build_airport_mesh(s376::ksfo_public()).verts.size());

    const auto tmp = (std::filesystem::temp_directory_path() / "s376-osm-test").string();
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp);
    {
        std::ofstream out(tmp + "/osm.json");
        out << sample;
    }
    s376::Airport from_file;
    from_file.icao = "KSFO";
    CHECK(s376::try_merge_osm_file(from_file, tmp + "/osm.json") == 3);
    CHECK(s376::try_merge_osm_file(from_file, tmp + "/missing.json") == 0);
    CHECK(s376::merge_osm_file_stream(from_file, tmp + "/osm.json", 0) == 3);
    // Runtime path never opens osm.json — only osm.bin.
    CHECK(s376::try_load_osm(from_file, tmp) == 0);
    CHECK(s376::bake_osm_bin(tmp + "/osm.json", tmp + "/osm.bin") == 3);
    CHECK(std::filesystem::exists(tmp + "/osm.bin"));
    CHECK(std::filesystem::exists(tmp + "/solids.bin"));
    s376::Airport from_bin;
    from_bin.icao = "KSFO";
    CHECK(s376::try_load_osm(from_bin, tmp) == 3);
    CHECK(!from_bin.solids_path.empty());
    CHECK(from_bin.taxiways.size() == 1);
    CHECK(from_bin.aprons.size() == 1);
    CHECK(from_bin.buildings.size() == 1);
    CHECK_NEAR(from_bin.buildings.back().height_m, 18.0, 0.1);
    {
        s376::SolidBvh solids;
        s376::fill_solids(solids, s376::SkylineEra::Current, {from_bin});
        double roof = 0;
        CHECK(solids.roof_msl(37.61825, -122.3835, &roof));
        CHECK_NEAR(roof, 18.0, 1.0);
    }
    {
        const char* levels =
            "{\"type\":\"way\",\"tags\":{\"building\":\"yes\",\"building:levels\":\"6\"},"
            "\"geometry\":[{\"lat\":37.630,\"lon\":-122.390},{\"lat\":37.6305,\"lon\":-122.390},"
            "{\"lat\":37.6305,\"lon\":-122.389},{\"lat\":37.630,\"lon\":-122.389}]}";
        s376::Airport lv;
        CHECK(s376::merge_osm_json(lv, levels) == 1);
        CHECK_NEAR(lv.buildings.back().height_m, 6.0 * s376::kOsmLevelHeightM, 0.05);
    }
    {
        s376::Airport capped;
        capped.icao = "KSFO";
        capped.buildings.resize(s376::kMaxOsmBuildings);
        for (auto& b : capped.buildings) {
            b.name = "pad";
            b.height_m = 8.f;
            b.ring = {{0.0, 0.0}, {0.0, 0.001}, {0.001, 0.001}, {0.001, 0.0}};
        }
        s376::SolidBvh extra;
        CHECK(s376::merge_osm_file_stream(capped, tmp + "/osm.json", 0, &extra) >= 1);
        CHECK(capped.buildings.size() == s376::kMaxOsmBuildings);
        extra.build();
        double roof = 0;
        CHECK(extra.roof_msl(37.61825, -122.3835, &roof));
        CHECK_NEAR(roof, 18.0, 1.0);
    }
    {
        const auto jfk = tmp + "/KJFK";
        std::filesystem::create_directories(jfk);
        std::filesystem::copy_file(tmp + "/osm.bin", jfk + "/osm.bin",
                                   std::filesystem::copy_options::overwrite_existing);
        s376::Airport sea;
        sea.icao = "KSEA";
        CHECK(s376::try_load_osm(sea, jfk) == 0);
        s376::Airport jf;
        jf.icao = "KJFK";
        CHECK(s376::try_load_osm(jf, jfk) == 3);
    }
    {
        // /tmp/kjfk-real: folder is not a 4-letter ICAO. coverage.toml
        // owns KJFK — never merge that osm.bin into KSEA / KEWR.
        const auto real = tmp + "/kjfk-real";
        std::filesystem::create_directories(real);
        std::filesystem::copy_file(tmp + "/osm.bin", real + "/osm.bin",
                                   std::filesystem::copy_options::overwrite_existing);
        {
            std::ofstream out(real + "/coverage.toml");
            out << "icao = \"KJFK\"\ncovers_faa = true\n";
        }
        s376::Airport sea;
        sea.icao = "KSEA";
        CHECK(s376::try_load_osm(sea, real) == 0);
        s376::Airport ewr;
        ewr.icao = "KEWR";
        CHECK(s376::try_load_osm(ewr, real) == 0);
        s376::Airport jf;
        jf.icao = "KJFK";
        CHECK(s376::try_load_osm(jf, real) == 3);
    }
    {
        // cache/KJFK-near: folder is not 4 letters; unique ICAO substring.
        const auto near = tmp + "/KJFK-near";
        std::filesystem::create_directories(near);
        std::filesystem::copy_file(tmp + "/osm.bin", near + "/osm.bin",
                                   std::filesystem::copy_options::overwrite_existing);
        s376::Airport sea;
        sea.icao = "KSEA";
        CHECK(s376::try_load_osm(sea, near) == 0);
        s376::Airport jf;
        jf.icao = "KJFK";
        CHECK(s376::try_load_osm(jf, near) == 3);
    }
    {
        const auto huge = tmp + "/huge";
        std::filesystem::create_directories(huge);
        {
            std::ofstream out(huge + "/osm.json");
            out << std::string(3 * 1024 * 1024, 'x');
            out << sample;
        }
        s376::Airport ignore;
        ignore.icao = "KSFO";
        CHECK(s376::try_merge_osm_file(ignore, huge + "/osm.json") == 0);
        CHECK(s376::try_load_osm(ignore, huge) == 0);
        CHECK(ignore.taxiways.empty());
    }

    const auto gj = s376::airport_geojson(s376::ksea_public());
    CHECK(gj.find("FeatureCollection") != std::string::npos);
    CHECK(gj.find("16L/34R") != std::string::npos);
    CHECK(gj.find("\"aeroway\":\"taxiway\"") != std::string::npos);
    CHECK(s376::write_airport_geojson(s376::ksea_public(), tmp + "/osm.geojson") == 1);
    CHECK(std::filesystem::exists(tmp + "/osm.geojson"));
    std::filesystem::remove_all(tmp);
}
