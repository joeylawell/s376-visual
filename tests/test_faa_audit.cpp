#include "s376/airport.hpp"
#include "s376/faa.hpp"
#include "s376/geo.hpp"
#include "s376/lights.hpp"

#include <cmath>
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

const s376::Runway* rwy(const s376::Airport& a, const char* le) {
    for (const auto& r : a.runways) {
        if (r.ident_le == le)
            return &r;
    }
    return nullptr;
}

void landing(const s376::Runway& rw, bool le, double& lat, double& lon, double& hdg) {
    lat = le ? rw.le_lat : rw.he_lat;
    lon = le ? rw.le_lon : rw.he_lon;
    hdg = le ? rw.heading_deg : rw.heading_he_deg;
    const double disp = (le ? rw.le_displaced_ft : rw.he_displaced_ft) * s376::geo::kFtToM;
    if (disp > 0.5)
        s376::offset_m(lat, lon, hdg, disp, lat, lon);
}

double across_m(double lat0, double lon0, double hdg, double lat, double lon) {
    double brg = 0;
    const double d = s376::geo::inverse_wgs84(lat0, lon0, lat, lon, &brg);
    return d * std::sin(s376::geo::deg2rad(s376::geo::wrap180(brg - hdg)));
}

double along_m(double lat0, double lon0, double hdg, double lat, double lon) {
    double brg = 0;
    const double d = s376::geo::inverse_wgs84(lat0, lon0, lat, lon, &brg);
    return d * std::cos(s376::geo::deg2rad(s376::geo::wrap180(brg - hdg)));
}

} // namespace

void test_faa_audit() {
    const auto& faa = s376::faa_kjfk();
    const auto jfk = s376::kjfk_public();

    // 7. NASR cycle + true headings + 04L/31R TDZ + 13L RLLS.
    {
        std::ifstream ref("data/faa/kjfk_reference.toml");
        if (!ref)
            ref.open("../data/faa/kjfk_reference.toml");
        CHECK(static_cast<bool>(ref));
    }
    CHECK(faa.effective == "2026-10-01");
    CHECK(jfk.source.effective == "2026-10-01");
    const auto* e04r = s376::faa_end(faa, "04R");
    const auto* e13l = s376::faa_end(faa, "13L");
    const auto* e04l = s376::faa_end(faa, "04L");
    const auto* e31r = s376::faa_end(faa, "31R");
    const auto* e22r = s376::faa_end(faa, "22R");
    const auto* e13r = s376::faa_end(faa, "13R");
    const auto* e31l = s376::faa_end(faa, "31L");
    CHECK(e04r && e13l && e04l && e31r && e22r && e13r && e31l);
    CHECK_NEAR(e04r->true_brg_deg, 30.7680, 0.01);
    CHECK_NEAR(e13l->true_brg_deg, 120.7546, 0.01);
    CHECK_NEAR(e04l->true_brg_deg, 30.7583, 0.01);
    CHECK_NEAR(e31r->true_brg_deg, 300.7748, 0.01);
    CHECK(e04l->tdz && e31r->tdz);
    CHECK(e13l->rlls_lead_in && e13l->als == "ALSF-2");
    CHECK(e13r->als == "RLLS");
    CHECK_NEAR(e04r->vgsi_tch_ft, 69.0, 0.1);
    CHECK_NEAR(e13l->vgsi_tch_ft, 74.0, 0.1);
    CHECK_NEAR(e04r->ils_tch_ft, 53.0, 0.1);
    CHECK_NEAR(e13l->ils_tch_ft, 58.0, 0.1);

    const auto* rw04r = rwy(jfk, "04R");
    const auto* rw13l = rwy(jfk, "13L");
    const auto* rw04l = rwy(jfk, "04L");
    const auto* rw13r = rwy(jfk, "13R");
    CHECK(rw04r && rw13l && rw04l && rw13r);
    CHECK(rw04l->tdz_le && rw13l->tdz_he);
    CHECK(rw13l->rlls_lead_in_le);

    // 1. PAPI invert, TCH/RPI siting, inboard ≥50 ft, 13L remarks.
    CHECK(!s376::lights::papi_box_white(3.f, 0, 3.f));
    CHECK(!s376::lights::papi_box_white(3.f, 1, 3.f));
    CHECK(s376::lights::papi_box_white(3.f, 2, 3.f));
    CHECK(s376::lights::papi_box_white(3.f, 3, 3.f));
    CHECK_NEAR(s376::lights::papi_distance_ft(69.0, 3.0), 1317.0, 3.0);
    CHECK_NEAR(s376::lights::papi_distance_ft(74.0, 3.0), 1412.0, 3.0);
    CHECK_NEAR(e13l->papi_rotate_deg, 10.0, 0.1);
    CHECK_NEAR(e13l->papi_usable_left_deg, 0.0, 0.1);
    CHECK_NEAR(s376::faa_end(faa, "22L")->papi_rotate_deg, -4.0, 0.1);
    CHECK_NEAR(e22r->papi_usable_right_deg, 8.0, 0.1);
    CHECK_NEAR(e13r->papi_rotate_deg, -22.0, 0.1);

    auto check_papi = [&](const s376::Runway& rw, bool le, double tch_ft) {
        double tlat, tlon, hdg;
        landing(rw, le, tlat, tlon, hdg);
        const double want_along = s376::lights::papi_distance_ft(tch_ft, 3.0) * s376::geo::kFtToM;
        const double edge = rw.width_m * 0.5;
        int n = 0;
        double min_inboard = 1e9;
        const int want_k = (&rw == rw04r) ? 2 : (&rw == rw13l) ? 3 : 0;
        for (const auto& L : jfk.lights) {
            if (want_k && L.runway_k != want_k)
                continue;
            if (L.kind != s376::LightKind::Vasi || L.papi_box != 0)
                continue;
            const double al = along_m(tlat, tlon, hdg, L.lat, L.lon);
            if (std::fabs(al - want_along) > 40.0)
                continue;
            ++n;
            const double ac = std::fabs(across_m(tlat, tlon, hdg, L.lat, L.lon));
            min_inboard = std::min(min_inboard, ac - edge);
        }
        CHECK(n >= 1);
        CHECK(min_inboard >= 15.24 - 0.15); // 50 ft −0
        CHECK_NEAR(min_inboard, 15.24, 0.3);
    };
    check_papi(*rw04r, true, 69.0);
    check_papi(*rw13l, true, 74.0);

    // 13L unusable left of CL; on CL is still in coverage.
    {
        double plat = 0, plon = 0;
        for (const auto& L : jfk.lights) {
            if (L.kind == s376::LightKind::Vasi && L.papi_rwy_hdg_deg > 100.f &&
                L.papi_usable_left_deg < 1.f) {
                plat = L.lat;
                plon = L.lon;
                CHECK(s376::lights::papi_in_coverage(plat, plon, plat, plon, L.papi_rwy_hdg_deg,
                                                     L.papi_rotate_deg, L.papi_usable_left_deg,
                                                     L.papi_usable_right_deg));
                double left_lat, left_lon;
                s376::offset_m(plat, plon, L.papi_rwy_hdg_deg + 180.0 - 90.0, 80.0, left_lat,
                               left_lon);
                CHECK(!s376::lights::papi_in_coverage(left_lat, left_lon, plat, plon,
                                                      L.papi_rwy_hdg_deg, L.papi_rotate_deg,
                                                      L.papi_usable_left_deg, L.papi_usable_right_deg));
                break;
            }
        }
        CHECK(plat != 0.0);
    }

    // 2. ALSF-2 geometry on 04R.
    {
        double tlat, tlon, hdg;
        landing(*rw04r, true, tlat, tlon, hdg);
        const double back = s376::geo::wrap360(hdg + 180.0);
        int n_cl5 = 0, n_1000 = 0, n_500 = 0, n_red = 0, n_green = 0, n_sfl = 0;
        double max_cl_half = 0, max_1000 = 0, min_red = 1e9, max_green = 0;
        double outer_err = 1e9;
        for (const auto& L : jfk.lights) {
            if (L.runway_k != 2)
                continue;
            const double al = along_m(tlat, tlon, back, L.lat, L.lon);
            const double ac = std::fabs(across_m(tlat, tlon, hdg, L.lat, L.lon));
            if (L.kind == s376::LightKind::Flasher && L.seq >= 0)
                ++n_sfl;
            if (L.kind == s376::LightKind::End && L.g > 0.7f) {
                ++n_green;
                max_green = std::max(max_green, ac);
            }
            if (L.kind != s376::LightKind::Approach)
                continue;
            if (std::fabs(al - 2400.0 * s376::geo::kFtToM) < 3.0 && ac < 3.0)
                outer_err = std::min(outer_err, std::fabs(al - 2400.0 * s376::geo::kFtToM));
            if (std::fabs(al - 100.0 * s376::geo::kFtToM) < 8.0 && ac < 3.0)
                ++n_cl5;
            if (std::fabs(al - 1000.0 * s376::geo::kFtToM) < 8.0) {
                ++n_1000;
                max_1000 = std::max(max_1000, ac);
            }
            if (std::fabs(al - 500.0 * s376::geo::kFtToM) < 8.0 && ac > 3.0 && L.g > 0.7f)
                ++n_500;
            if (al > 20.0 && al < 920.0 * s376::geo::kFtToM && L.r > 0.6f && L.g < 0.3f) {
                ++n_red;
                min_red = std::min(min_red, ac);
            }
            if (ac < 3.0 && al > 50.0 && al < 2500.0 * s376::geo::kFtToM)
                max_cl_half = std::max(max_cl_half, ac);
        }
        CHECK(n_cl5 >= 5);
        CHECK(n_1000 >= 21);
        CHECK_NEAR(max_1000, 50.0 * s376::geo::kFtToM, 1.0);
        CHECK(n_500 >= 8);
        CHECK(n_red >= 9 * 2 * 3);
        CHECK_NEAR(min_red, 36.0 * s376::geo::kFtToM, 0.6);
        CHECK(n_green >= 50);
        CHECK(max_green > (100.0 + 40.0) * s376::geo::kFtToM); // 45 ft past a 200 ft edge
        CHECK(n_sfl >= 15);
        CHECK(outer_err < 0.5);
        CHECK(max_cl_half < 2.2); // 13.5 ft bar, outer lamp 6.75 ft
    }

    // 3. TDZ: two rows, 36 ft, 100 ft, 3000 ft; 04L and 31R present.
    auto check_tdz = [&](const s376::Runway& rw, bool le, int k) {
        double tlat, tlon, hdg;
        landing(rw, le, tlat, tlon, hdg);
        int n = 0, n_left = 0, n_right = 0, n_far = 0;
        double min_inner = 1e9;
        for (const auto& L : jfk.lights) {
            if (L.runway_k != k || L.kind != s376::LightKind::Threshold)
                continue;
            const double al = along_m(tlat, tlon, hdg, L.lat, L.lon);
            if (al < 20.0 || al > 3100.0 * s376::geo::kFtToM)
                continue;
            ++n;
            const double ac = across_m(tlat, tlon, hdg, L.lat, L.lon);
            if (ac < 0)
                ++n_left;
            else
                ++n_right;
            min_inner = std::min(min_inner, std::fabs(ac));
            if (al > 2900.0 * s376::geo::kFtToM)
                ++n_far;
        }
        CHECK(n >= 180); // 30 × 2 × 3
        CHECK(n_left >= 80 && n_right >= 80);
        CHECK_NEAR(min_inner, 36.0 * s376::geo::kFtToM, 0.6);
        CHECK(n_far >= 6);
    };
    check_tdz(*rw04r, true, 2);
    check_tdz(*rw04l, true, 1);
    check_tdz(*rw13l, false, 3);

    // 4. Geodesic offset: far-end CL and displaced thresholds.
    {
        double flat, flon;
        s376::offset_m(rw04r->le_lat, rw04r->le_lon, rw04r->heading_deg, rw04r->length_m, flat, flon);
        CHECK(s376::geo::ground_distance_m(flat, flon, rw04r->he_lat, rw04r->he_lon) < 1.0);
        double dlat, dlon;
        s376::offset_m(e22r->lat, e22r->lon, e22r->true_brg_deg, e22r->displaced_ft * s376::geo::kFtToM,
                       dlat, dlon);
        CHECK(s376::geo::ground_distance_m(dlat, dlon, 40.64243358, -73.76963536) < 1.0);
        s376::offset_m(e13r->lat, e13r->lon, e13r->true_brg_deg, e13r->displaced_ft * s376::geo::kFtToM,
                       dlat, dlon);
        CHECK(s376::geo::ground_distance_m(dlat, dlon, 40.64549422, -73.8103848) < 1.0);
        s376::offset_m(e31l->lat, e31l->lon, e31l->true_brg_deg, e31l->displaced_ft * s376::geo::kFtToM,
                       dlat, dlon);
        CHECK(s376::geo::ground_distance_m(dlat, dlon, 40.63257736, -73.78188547) < 1.0);
    }

    // 5. HIRL caution yellow only toward the approach over the last 2000 ft;
    //    RCL alt red/white 3000–1000, then red.
    {
        int n_near_white = 0, n_far_yel = 0, n_cl_alt = 0, n_cl_red = 0;
        for (const auto& L : jfk.lights) {
            if (L.runway_k != 2)
                continue;
            const double al = along_m(rw04r->le_lat, rw04r->le_lon, rw04r->heading_deg, L.lat, L.lon);
            if (L.kind == s376::LightKind::Edge && L.r2 >= 0.f) {
                if (al < 400.0) {
                    CHECK(L.g > 0.9f);  // white toward HE (04R approach)
                    CHECK(L.g2 < 0.85f); // yellow toward LE
                    ++n_near_white;
                }
                if (al > rw04r->length_m - 400.0) {
                    CHECK(L.g < 0.85f); // yellow toward HE
                    CHECK(L.g2 > 0.9f); // white toward LE
                    ++n_far_yel;
                }
            }
            if (L.kind == s376::LightKind::Centerline && L.r2 >= 0.f) {
                if (al > rw04r->length_m - 280.0)
                    ++n_cl_red;
                if (al > rw04r->length_m - 800.0 && al < rw04r->length_m - 400.0 && L.g < 0.4f)
                    ++n_cl_alt;
            }
        }
        CHECK(n_near_white >= 4);
        CHECK(n_far_yel >= 4);
        CHECK(n_cl_red >= 4);
        CHECK(n_cl_alt >= 2);
    }

    // 6. Fakehost headings / ILS TCH live in tests/test_fakehost.cpp.

    // 7. MALSR 31R: 1000 ft 66 ft crossbar, RAIL flashers only.
    {
        double tlat, tlon, hdg;
        landing(*rw13l, false, tlat, tlon, hdg);
        const double back = s376::geo::wrap360(hdg + 180.0);
        int n_xb = 0, n_rail_bar = 0, n_rail_sfl = 0;
        double max_xb = 0;
        for (const auto& L : jfk.lights) {
            if (L.runway_k != 3)
                continue;
            const double al = along_m(tlat, tlon, back, L.lat, L.lon);
            const double ac = std::fabs(across_m(tlat, tlon, hdg, L.lat, L.lon));
            if (L.kind == s376::LightKind::Approach && std::fabs(al - 1000.0 * s376::geo::kFtToM) < 8.0) {
                ++n_xb;
                max_xb = std::max(max_xb, ac);
            }
            if (al > 1500.0 * s376::geo::kFtToM && al < 2500.0 * s376::geo::kFtToM) {
                if (L.kind == s376::LightKind::Approach)
                    ++n_rail_bar;
                if (L.kind == s376::LightKind::Flasher)
                    ++n_rail_sfl;
            }
        }
        CHECK(n_xb >= 20);
        CHECK_NEAR(max_xb, 33.0 * s376::geo::kFtToM, 2.0);
        CHECK(n_rail_bar == 0);
        CHECK(n_rail_sfl == 5);
    }

    // 13R published lead-in: no invented 500 ft bars out to 6000 ft.
    {
        double tlat, tlon, hdg;
        landing(*rw13r, true, tlat, tlon, hdg);
        const double back = s376::geo::wrap360(hdg + 180.0);
        int n_invented = 0, n_sfl = 0;
        for (const auto& L : jfk.lights) {
            if (L.runway_k != 4)
                continue;
            const double al = along_m(tlat, tlon, back, L.lat, L.lon);
            if (L.kind == s376::LightKind::Approach && al > 2500.0 * s376::geo::kFtToM)
                ++n_invented;
            if (L.kind == s376::LightKind::Flasher)
                ++n_sfl;
        }
        CHECK(n_invented == 0);
        CHECK(n_sfl >= 10); // 5 on CL + 5 at 1 NM
    }

    // 13L RLLS add-on: flashers ~1 NM off the 35° south radial.
    {
        double tlat, tlon, hdg;
        landing(*rw13l, true, tlat, tlon, hdg);
        int n_nm = 0;
        for (const auto& L : jfk.lights) {
            if (L.runway_k != 3 || L.kind != s376::LightKind::Flasher)
                continue;
            const double d = s376::geo::ground_distance_m(tlat, tlon, L.lat, L.lon);
            if (d > 1500.0 && d < 2200.0)
                ++n_nm;
        }
        CHECK(n_nm >= 5);
    }
}
