#include "s376/geo.hpp"

#include <cmath>
#include <iostream>

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
                      << #b << "=" << _b << "\n";                                                  \
        }                                                                                          \
    } while (0)

void test_geo() {
    auto p = s376::geo::lla_to_ecef(0, 0, 0);
    CHECK_NEAR(p.x, s376::geo::kWgsA, 1.0);
    CHECK_NEAR(p.y, 0, 1.0);
    CHECK_NEAR(p.z, 0, 1.0);

    auto q = s376::geo::lla_to_ecef(90, 0, 0);
    CHECK_NEAR(q.x, 0, 2.0);
    CHECK_NEAR(q.z, s376::geo::kWgsB, 2.0);

    s376::geo::Enu f, r, d;
    s376::geo::body_to_enu(0, 0, 0, f, r, d);
    CHECK_NEAR(f.n, 1, 1e-9);
    CHECK_NEAR(f.e, 0, 1e-9);
    CHECK_NEAR(r.e, 1, 1e-9);

    s376::geo::body_to_enu(90, 0, 0, f, r, d);
    CHECK_NEAR(f.e, 1, 1e-9);
    CHECK_NEAR(f.n, 0, 1e-9);

    CHECK(s376::geo::day_of_year(2026, 1, 1) == 1);
    CHECK(s376::geo::day_of_year(2026, 6, 21) == 172);

    double el = 0;
    auto s = s376::geo::sun_ecef(0, 0, 12.0, 3, 21, &el);
    CHECK(el > 70); // noon at equator around equinox
    const double mag = std::sqrt(s.x * s.x + s.y * s.y + s.z * s.z);
    CHECK_NEAR(mag, 1.0, 1e-9);
    {
        // Astronomy at KJFK June --tod 19 is still day (the defect). Visual
        // TOD maps 19 → dusk and 3 → night regardless of date/lon.
        double jfk_el = 0;
        s376::geo::sun_ecef(40.64, -73.78, 19.0, 6, 21, &jfk_el);
        CHECK(jfk_el > 20.0);
        auto vis = s376::geo::sun_from_elev_az(40.64, -73.78, -1.5, 285.0);
        const double vmag = std::sqrt(vis.x * vis.x + vis.y * vis.y + vis.z * vis.z);
        CHECK_NEAR(vmag, 1.0, 1e-9);
        s376::geo::Ecef ee, nn, uu;
        s376::geo::ecef_to_enu_axes(40.64, -73.78, ee, nn, uu);
        const double vis_el = vis.x * uu.x + vis.y * uu.y + vis.z * uu.z;
        CHECK(vis_el < 0.0);
    }

    // Soft US coasts: airports are land, nearby ocean / bays are water.
    CHECK(s376::geo::land_coverage(40.6399, -73.7787) > 0.55); // KJFK
    CHECK(s376::geo::land_coverage(40.620, -73.840) < 0.40);   // Jamaica Bay
    CHECK(s376::geo::land_coverage(40.620, -73.774) < 0.40);   // 04R short final over the bay
    CHECK(s376::geo::land_coverage(40.500, -73.800) < 0.25);   // Atlantic south of Rockaway
    CHECK(s376::geo::land_coverage(40.565, -73.850) > 0.45);   // Rockaway barrier
    CHECK(s376::geo::land_coverage(42.3656, -71.0096) > 0.55); // KBOS
    CHECK(s376::geo::land_coverage(37.6188, -122.3754) > 0.50); // KSFO pad
    CHECK(s376::geo::land_coverage(47.4502, -122.3088) > 0.50); // KSEA
    CHECK(s376::geo::land_coverage(0.0, -30.0) < 0.15);         // mid-Atlantic
    // Schematic pad must sit below the KJFK field (~4 m), not cover*12 m.
    CHECK(s376::geo::local_ground_alt_m(0.80f, 1.0, 4.0) < 3.6);
    CHECK(s376::geo::local_ground_alt_m(0.80f, 1.0, 4.0) > 2.5);
    CHECK(s376::geo::local_ground_alt_m(0.10f, 1.0, 4.0) < 0.5);

    CHECK_NEAR(s376::geo::dms_to_deg(40, 38, 23.741), 40.639928055, 1e-9);
    CHECK_NEAR(s376::geo::dms_to_deg(73, 46, 43.292, true), -73.778692222, 1e-9);
    CHECK_NEAR(s376::geo::wrap360(-10.0), 350.0, 1e-9);

    double hdg = 0;
    const double eq = s376::geo::inverse_wgs84(0.0, 0.0, 0.0, 1.0, &hdg);
    CHECK_NEAR(eq, 111319.5, 2.0); // 1° of longitude on the equator
    CHECK_NEAR(hdg, 90.0, 0.01);
    CHECK_NEAR(s376::geo::ground_distance_m(0.0, 0.0, 1.0, 0.0), 110574.4, 2.0);
    {
        // Vincenty direct is the inverse of inverse_wgs84.
        double lat2 = 0, lon2 = 0, h2 = 0;
        s376::geo::direct_wgs84(0.0, 0.0, 90.0, 111319.5, lat2, lon2, &h2);
        CHECK_NEAR(lat2, 0.0, 1e-6);
        CHECK_NEAR(lon2, 1.0, 1e-5);
        double lat0 = 40.62542827, lon0 = -73.77034558; // 04R LE
        double lat1 = 40.64523697, lon1 = -73.75486186; // 04R HE
        double hdg = 0;
        const double dist = s376::geo::inverse_wgs84(lat0, lon0, lat1, lon1, &hdg);
        double glat = 0, glon = 0;
        s376::geo::direct_wgs84(lat0, lon0, hdg, dist, glat, glon);
        CHECK(s376::geo::ground_distance_m(glat, glon, lat1, lon1) < 0.05);
        CHECK_NEAR(hdg, 30.768, 0.01);
        CHECK_NEAR(s376::geo::wrap180(-190.0), 170.0, 1e-9);
        double back_lat = 0, back_lon = 0, bh = 0;
        s376::geo::direct_wgs84(lat0, lon0, hdg, -100.0, back_lat, back_lon);
        CHECK_NEAR(s376::geo::inverse_wgs84(lat0, lon0, back_lat, back_lon, &bh), 100.0, 0.05);
        CHECK_NEAR(s376::geo::wrap180(bh - (hdg + 180.0)), 0.0, 0.05);
    }

    auto e0 = s376::geo::lla_to_ecef(40.639928, -73.778692, 4.0);
    double rlat = 0, rlon = 0, ralt = 0;
    s376::geo::ecef_to_lla(e0, rlat, rlon, ralt);
    CHECK_NEAR(rlat, 40.639928, 1e-8);
    CHECK_NEAR(rlon, -73.778692, 1e-8);
    CHECK_NEAR(ralt, 4.0, 0.02);

    const double n96 = s376::geo::geoid_undulation_m(40.64, -73.78, s376::geo::GeoidModel::Egm96);
    const double n08 = s376::geo::geoid_undulation_m(40.64, -73.78, s376::geo::GeoidModel::Egm2008);
    CHECK_NEAR(n96, -32.05, 0.6);
    CHECK_NEAR(n08, -31.88, 0.6);
    CHECK(std::fabs(n96 - n08) > 0.05);
    const double ortho = 13.0 * s376::geo::kFtToM;
    const double ellip = s376::geo::orthometric_to_ellipsoid_m(ortho, 40.64, -73.78);
    CHECK_NEAR(ellip, ortho + n96, 1e-9);
    CHECK_NEAR(s376::geo::ellipsoid_to_orthometric_m(ellip, 40.64, -73.78), ortho, 1e-9);

    auto utm = s376::geo::wgs84_to_utm(40.639928, -73.778692);
    CHECK(utm.zone == 18 && utm.north);
    double ulat = 0, ulon = 0;
    s376::geo::utm_to_wgs84(utm, ulat, ulon);
    const double utm_err = s376::geo::ground_distance_m(40.639928, -73.778692, ulat, ulon);
    CHECK(utm_err < 1.0);

    {
        // Point on the optical axis at 1 km → NDC origin, in frame.
        const auto v = s376::geo::make_channel_view(40.62, -73.78, 100.0, 31.0, 0, 0, 0, 0, 0, 50,
                                                    32.5, 1920, 1200);
        s376::geo::Ecef tgt{v.eye.x + v.forward.x * 1000.0, v.eye.y + v.forward.y * 1000.0,
                            v.eye.z + v.forward.z * 1000.0};
        const auto p = s376::geo::project_channel(v, tgt, 1920, 1200);
        CHECK(p.in_front && p.in_frame);
        CHECK_NEAR(p.ndc_x, 0.0, 1e-6);
        CHECK_NEAR(p.ndc_y, 0.0, 1e-6);
        CHECK_NEAR(p.pix_x, 960.0, 0.5);
        CHECK_NEAR(p.pix_y, 600.0, 0.5);
        CHECK_NEAR(p.depth_m, 1000.0, 0.5);
        // Behind the camera is not in frame.
        s376::geo::Ecef back{v.eye.x - v.forward.x * 100.0, v.eye.y - v.forward.y * 100.0,
                             v.eye.z - v.forward.z * 100.0};
        CHECK(!s376::geo::project_channel(v, back, 1920, 1200).in_frame);
    }
}
