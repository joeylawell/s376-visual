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
    {
        const double t3 = std::tan(s376::geo::deg2rad(3.0));
        CHECK_NEAR(s376::lights::papi_distance_ft(69.0, 3.0),
                   (69.0 - s376::lights::kPapiOpticalHtFt) / t3, 3.0);
        CHECK_NEAR(s376::lights::papi_distance_ft(74.0, 3.0),
                   (74.0 - s376::lights::kPapiOpticalHtFt) / t3, 3.0);
        const double d69 = s376::lights::papi_distance_ft(69.0, 3.0);
        CHECK_NEAR(s376::lights::kPapiOpticalHtFt + d69 * t3, 69.0, 1.0);
        const double d74 = s376::lights::papi_distance_ft(74.0, 3.0);
        CHECK_NEAR(s376::lights::kPapiOpticalHtFt + d74 * t3, 74.0, 1.0);
    }
    CHECK_NEAR(e13l->papi_rotate_deg, 10.0, 0.1);
    CHECK_NEAR(e13l->papi_usable_left_deg, 0.0, 0.1);
    CHECK_NEAR(s376::faa_end(faa, "22L")->papi_rotate_deg, -4.0, 0.1);
    CHECK_NEAR(e22r->papi_usable_right_deg, 8.0, 0.1);
    CHECK_NEAR(e13r->papi_rotate_deg, -22.0, 0.1);
    CHECK(e13r->papi_usable_left_deg >= 32.0);

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

    auto check_tch = [&](const s376::Runway& rw, bool le, double official_ft, int k) {
        double tlat, tlon, hdg;
        landing(rw, le, tlat, tlon, hdg);
        const double elev = le ? rw.le_elev_m : rw.he_elev_m;
        const double gs = le ? rw.papi_deg_le : rw.papi_deg_he;
        int n = 0;
        for (const auto& L : jfk.lights) {
            if (L.runway_k != k || L.kind != s376::LightKind::Vasi || L.papi_box != 0)
                continue;
            const double al = along_m(tlat, tlon, hdg, L.lat, L.lon);
            if (al < 50.0 || al > 600.0)
                continue;
            const double h_ft = (L.alt_m - elev) * s376::geo::kMToFt;
            const double tch = h_ft + (al * s376::geo::kMToFt) * std::tan(s376::geo::deg2rad(gs));
            CHECK_NEAR(h_ft, s376::lights::kPapiOpticalHtFt, 0.35);
            CHECK_NEAR(tch, official_ft, 1.0);
            ++n;
        }
        CHECK(n >= 1);
    };
    check_tch(*rw04r, true, 69.0, 2);
    check_tch(*rw13l, true, 74.0, 3);

    // 13L: on the extended CL is in coverage; 80 m pilot-left is not.
    // +az is right of CL as seen on the approach (landing-right = hdg+90).
    {
        double plat = 0, plon = 0;
        float hdg = 0, rot = 0, ul = 0, ur = 0;
        for (const auto& L : jfk.lights) {
            if (L.kind == s376::LightKind::Vasi && L.papi_rwy_hdg_deg > 100.f &&
                L.papi_usable_left_deg < 1.f) {
                plat = L.lat;
                plon = L.lon;
                hdg = L.papi_rwy_hdg_deg;
                rot = L.papi_rotate_deg;
                ul = L.papi_usable_left_deg;
                ur = L.papi_usable_right_deg;
                break;
            }
        }
        CHECK(plat != 0.0);
        double tlat, tlon, thdg;
        landing(*rw13l, true, tlat, tlon, thdg);
        double clat, clon;
        s376::offset_m(tlat, tlon, thdg + 180.0, 800.0, clat, clon);
        CHECK(s376::lights::papi_in_coverage(clat, clon, plat, plon, hdg, rot, ul, ur));
        double left_lat, left_lon;
        s376::offset_m(clat, clon, thdg - 90.0, 80.0, left_lat, left_lon);
        CHECK(!s376::lights::papi_in_coverage(left_lat, left_lon, plat, plon, hdg, rot, ul, ur));
        double right_lat, right_lon;
        s376::offset_m(clat, clon, thdg + 90.0, 50.0, right_lat, right_lon);
        CHECK(s376::lights::papi_azimuth_deg(right_lat, right_lon, plat, plon, hdg) > 0.f);
        CHECK(s376::lights::papi_azimuth_deg(left_lat, left_lon, plat, plon, hdg) < 0.f);
    }

    // 22L aimed 4° left; 22R cutoff 8° right; 13R one system aimed 22° left.
    {
        const auto* e22l = s376::faa_end(faa, "22L");
        CHECK(e22l && e22l->papi_rotate_deg < 0.0);
        CHECK_NEAR(e22l->papi_rotate_deg, -4.0, 0.1);
        CHECK_NEAR(e22r->papi_usable_right_deg, 8.0, 0.1);
        CHECK(e22r->papi_usable_left_deg > e22r->papi_usable_right_deg);
        CHECK_NEAR(e13r->papi_rotate_deg, -22.0, 0.1);
        int n13r = 0;
        double plat = 0, plon = 0;
        float hdg = 0, rot = 0, ul = 15, ur = 15;
        double t13, o13, h13;
        landing(*rw13r, true, t13, o13, h13);
        for (const auto& L : jfk.lights) {
            if (L.runway_k != 4 || L.kind != s376::LightKind::Vasi)
                continue;
            const double al = along_m(t13, o13, h13, L.lat, L.lon);
            if (al < 50.0 || al > 600.0)
                continue;
            ++n13r;
            plat = L.lat;
            plon = L.lon;
            hdg = L.papi_rwy_hdg_deg;
            rot = L.papi_rotate_deg;
            ul = L.papi_usable_left_deg;
            ur = L.papi_usable_right_deg;
        }
        CHECK(n13r == 4); // one P4L, not an invented second system
        CHECK_NEAR(rot, -22.0, 0.1);
        CHECK(ul >= 32.f); // usable window must contain the 22° ±10° beam
        double clat, clon, left_lat, left_lon, right_lat, right_lon;
        s376::offset_m(t13, o13, h13 + 180.0, 1200.0, clat, clon);
        s376::offset_m(clat, clon, h13 - 90.0, 1200.0 * std::tan(s376::geo::deg2rad(22.0)), left_lat,
                       left_lon);
        s376::offset_m(clat, clon, h13 + 90.0, 1200.0 * std::tan(s376::geo::deg2rad(22.0)), right_lat,
                       right_lon);
        CHECK(s376::lights::papi_in_coverage(left_lat, left_lon, plat, plon, hdg, rot, ul, ur));
        CHECK(!s376::lights::papi_in_coverage(right_lat, right_lon, plat, plon, hdg, rot, ul, ur));
        // 22R: 8° right of CL is the published cutoff.
        double p22 = 0, o22 = 0;
        float h22 = 0, r22 = 0, ul22 = 0, ur22 = 0;
        for (const auto& L : jfk.lights) {
            if (L.runway_k == 1 && L.kind == s376::LightKind::Vasi && L.papi_usable_right_deg < 10.f) {
                p22 = L.lat;
                o22 = L.lon;
                h22 = L.papi_rwy_hdg_deg;
                r22 = L.papi_rotate_deg;
                ul22 = L.papi_usable_left_deg;
                ur22 = L.papi_usable_right_deg;
                break;
            }
        }
        CHECK(p22 != 0.0);
        double c22, k22, in_lat, in_lon, out_lat, out_lon;
        s376::offset_m(p22, o22, static_cast<double>(h22) + 180.0, 800.0, c22, k22);
        s376::offset_m(c22, k22, static_cast<double>(h22) + 90.0, 800.0 * std::tan(s376::geo::deg2rad(7.0)),
                       in_lat, in_lon);
        s376::offset_m(c22, k22, static_cast<double>(h22) + 90.0, 800.0 * std::tan(s376::geo::deg2rad(10.0)),
                       out_lat, out_lon);
        CHECK(s376::lights::papi_in_coverage(in_lat, in_lon, p22, o22, h22, r22, ul22, ur22));
        CHECK(!s376::lights::papi_in_coverage(out_lat, out_lon, p22, o22, h22, r22, ul22, ur22));
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
        CHECK(n_xb == 15); // centre 5 + two 5-lamp bars, not 27 even lamps
        CHECK_NEAR(max_xb, 33.0 * s376::geo::kFtToM, 2.5);
        int n_cl = 0, n_side = 0;
        for (const auto& L : jfk.lights) {
            if (L.runway_k != 3 || L.kind != s376::LightKind::Approach)
                continue;
            const double al = along_m(tlat, tlon, back, L.lat, L.lon);
            if (std::fabs(al - 1000.0 * s376::geo::kFtToM) >= 8.0)
                continue;
            const double ac = std::fabs(across_m(tlat, tlon, hdg, L.lat, L.lon));
            if (ac < 6.0 * s376::geo::kFtToM)
                ++n_cl;
            else if (ac > 20.0 * s376::geo::kFtToM)
                ++n_side;
        }
        CHECK(n_cl == 5);
        CHECK(n_side == 10);
        CHECK(n_rail_bar == 0);
        CHECK(n_rail_sfl == 5);
        int seq_outer = 99, seq_inner = -1;
        double al_outer = -1, al_inner = 1e9;
        for (const auto& L : jfk.lights) {
            if (L.runway_k != 3 || L.kind != s376::LightKind::Flasher)
                continue;
            const double al = along_m(tlat, tlon, back, L.lat, L.lon);
            if (al < 1500.0 * s376::geo::kFtToM || al > 2500.0 * s376::geo::kFtToM)
                continue;
            if (al > al_outer) {
                al_outer = al;
                seq_outer = L.seq;
            }
            if (al < al_inner) {
                al_inner = al;
                seq_inner = L.seq;
            }
        }
        CHECK(seq_outer == 0); // RAIL sequences toward the runway
        CHECK(seq_inner > seq_outer);
    }

    // 13R published lead-in: 5 CL SFL only — no 13L 35° copy, no steady bars.
    {
        double tlat, tlon, hdg;
        landing(*rw13r, true, tlat, tlon, hdg);
        const double back = s376::geo::wrap360(hdg + 180.0);
        int n_steady = 0, n_sfl = 0, n_nm = 0;
        int seq_outer = 99, seq_inner = -1;
        double al_outer = -1, al_inner = 1e9;
        for (const auto& L : jfk.lights) {
            if (L.runway_k != 4)
                continue;
            const double al = along_m(tlat, tlon, back, L.lat, L.lon);
            const double d = s376::geo::ground_distance_m(tlat, tlon, L.lat, L.lon);
            if (L.kind == s376::LightKind::Approach)
                ++n_steady;
            if (L.kind == s376::LightKind::Flasher) {
                ++n_sfl;
                CHECK(s376::lights::is_runway_switch(L.switch_num, s376::lights::kOffStrobeA) ||
                      s376::lights::is_runway_switch(L.switch_num, s376::lights::kOffStrobeB));
                if (d > 1500.0)
                    ++n_nm;
                if (al > al_outer) {
                    al_outer = al;
                    seq_outer = L.seq;
                }
                if (al < al_inner) {
                    al_inner = al;
                    seq_inner = L.seq;
                }
            }
        }
        CHECK(n_steady == 0);
        CHECK(n_sfl == 5);
        CHECK(n_nm == 0);
        CHECK(seq_outer == 0);
        CHECK(seq_inner > seq_outer);
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

    // AC 150/5340-30J 2.3.2.1.2: 13L displaced-area edge lights red toward the approach.
    {
        int n_red = 0, n_white = 0;
        const double disp = rw13l->le_displaced_ft * s376::geo::kFtToM;
        CHECK_NEAR(rw13l->le_displaced_ft, 907.0, 0.5);
        for (const auto& L : jfk.lights) {
            if (L.runway_k != 3 || L.kind != s376::LightKind::Edge || L.r2 < 0.f)
                continue;
            const double al = along_m(rw13l->le_lat, rw13l->le_lon, rw13l->heading_deg, L.lat, L.lon);
            if (al < 10.0 || al > disp - 10.0)
                continue;
            CHECK(L.r > 0.8f && L.g < 0.35f); // red toward HE / 13L approach
            CHECK(L.g2 > 0.7f);               // opposite direction stays white
            ++n_red;
        }
        for (const auto& L : jfk.lights) {
            if (L.runway_k != 3 || L.kind != s376::LightKind::Edge || L.r2 < 0.f)
                continue;
            const double al = along_m(rw13l->le_lat, rw13l->le_lon, rw13l->heading_deg, L.lat, L.lon);
            if (al > disp + 80.0 && al < disp + 400.0) {
                CHECK(L.g > 0.7f); // white past the landing THR
                ++n_white;
            }
        }
        CHECK(n_red >= 4);
        CHECK(n_white >= 4);
    }

    // Threshold green toward the approach; end red toward the runway. None omni.
    {
        int n_omni = 0, n_green = 0, n_red = 0;
        for (const auto& L : jfk.lights) {
            if (L.kind != s376::LightKind::End)
                continue;
            if (L.r2 < 0.f)
                ++n_omni;
            if (L.g > 0.7f) {
                ++n_green;
                CHECK(L.r2 == 0.f);
            }
            if (L.r > 0.7f && L.g < 0.35f) {
                ++n_red;
                CHECK(L.r2 == 0.f);
            }
        }
        CHECK(n_omni == 0);
        CHECK(n_green + n_red >= 200);

        double tlat, tlon, hdg;
        landing(*rw04r, true, tlat, tlon, hdg);
        double elat, elon;
        s376::offset_m(tlat, tlon, s376::geo::wrap360(hdg + 180.0), 1852.0, elat, elon);
        int near_green = 0, near_red = 0, far_green = 0, far_red = 0;
        for (const auto& L : jfk.lights) {
            if (L.runway_k != 2 || L.kind != s376::LightKind::End || L.r2 < 0.f)
                continue;
            double brg = 0;
            s376::geo::inverse_wgs84(elat, elon, L.lat, L.lon, &brg);
            const double rel = s376::geo::wrap180(brg - L.color_hdg_deg);
            const bool rev = std::fabs(rel) > 90.0;
            const float cr = rev ? L.r2 : L.r;
            const float cg = rev ? L.g2 : L.g;
            const double al = along_m(tlat, tlon, hdg, L.lat, L.lon);
            if (std::fabs(al) < 80.0) {
                if (cg > 0.7f)
                    ++near_green;
                if (cr > 0.7f && cg < 0.35f)
                    ++near_red;
            }
            if (al > rw04r->length_m - 80.0) {
                if (cg > 0.7f)
                    ++far_green;
                if (cr > 0.7f && cg < 0.35f)
                    ++far_red;
            }
        }
        CHECK(near_green >= 8);
        CHECK(near_red == 0);  // no red mixed into the 04R green bar
        CHECK(far_green == 0); // 22L green bar is not visible on the 04R final
        CHECK(far_red >= 4);   // far-end red faces the runway
    }

    // Approach flashers are on the strobe circuit (switchable, not always-on ALS).
    {
        int n_strobe = 0, n_app = 0;
        for (const auto& L : jfk.lights) {
            if (L.kind != s376::LightKind::Flasher)
                continue;
            if (s376::lights::is_runway_switch(L.switch_num, s376::lights::kOffStrobeA) ||
                s376::lights::is_runway_switch(L.switch_num, s376::lights::kOffStrobeB))
                ++n_strobe;
            if (s376::lights::is_runway_switch(L.switch_num, s376::lights::kOffApproachA) ||
                s376::lights::is_runway_switch(L.switch_num, s376::lights::kOffApproachB))
                ++n_app;
        }
        CHECK(n_strobe >= 20);
        CHECK(n_app == 0);
    }

    // AC 150/5340-1M markings: 16 stripes on 200 ft, 1020/72 aiming point,
    // 36 in CL, 10 ft DTHR bar, 3-2-2-1-1 TDZ, white arrows (no yellow).
    {
        CHECK(s376::threshold_stripe_count(200.0 * s376::geo::kFtToM) == 16);
        CHECK(s376::threshold_stripe_count(150.0 * s376::geo::kFtToM) == 12);
        CHECK_NEAR(s376::kAimingPointStartFt, 1020.0, 0.1);
        CHECK_NEAR(s376::kAimingPointInnerFt, 72.0, 0.1);
        CHECK_NEAR(s376::kCenterlineWidthIn, 36.0, 0.1);
        CHECK_NEAR(s376::kDisplacedThresholdBarFt, 10.0, 0.1);
        CHECK(s376::kTdzBarCounts[0] == 3 && s376::kTdzBarCounts[1] == 2 &&
              s376::kTdzBarCounts[2] == 2 && s376::kTdzBarCounts[3] == 1 &&
              s376::kTdzBarCounts[4] == 1);
        CHECK_NEAR(s376::kTdzStationFt[0], 500.0, 0.1);
        const auto mesh = s376::build_airport_mesh(jfk, false, true);
        int n_yellow = 0, n_white_disp = 0, n_bar = 0;
        double tlat, tlon, hdg;
        landing(*rw13l, true, tlat, tlon, hdg);
        const double disp = rw13l->le_displaced_ft * s376::geo::kFtToM;
        for (const auto& v : mesh.verts) {
            double lat, lon, alt;
            s376::geo::ecef_to_lla(v.p, lat, lon, alt);
            const double al_phys =
                along_m(rw13l->le_lat, rw13l->le_lon, rw13l->heading_deg, lat, lon);
            const double ac = std::fabs(across_m(rw13l->le_lat, rw13l->le_lon, rw13l->heading_deg, lat, lon));
            if (al_phys < -5.0 || al_phys > rw13l->length_m + 5.0 || ac > rw13l->width_m)
                continue;
            const bool yellow = v.r > 0.80f && v.g > 0.60f && v.g < 0.88f && v.b < 0.30f;
            if (yellow && al_phys < disp)
                ++n_yellow;
            if (al_phys > 20.0 && al_phys < disp - 10.0 && v.r > 0.90f && v.g > 0.90f)
                ++n_white_disp;
            const double al_thr = along_m(tlat, tlon, hdg, lat, lon);
            if (al_thr > -1.5 && al_thr < 4.5 && ac > 6.0 && v.r > 0.90f && v.g > 0.90f)
                ++n_bar;
        }
        CHECK(n_yellow == 0);
        CHECK(n_white_disp >= 40);
        CHECK(n_bar >= 4);
    }
}
