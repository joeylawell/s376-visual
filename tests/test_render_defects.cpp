#include "s376/airport.hpp"
#include "s376/config.hpp"
#include "s376/geo.hpp"
#include "s376/lights.hpp"
#include "s376/state.hpp"
#include "s376/xt4.hpp"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

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

void landing(const s376::Runway& rw, bool le, double& lat, double& lon, double& hdg, double& elev) {
    lat = le ? rw.le_lat : rw.he_lat;
    lon = le ? rw.le_lon : rw.he_lon;
    hdg = le ? rw.heading_deg : rw.heading_he_deg;
    elev = le ? rw.le_elev_m : rw.he_elev_m;
    const double disp = (le ? rw.le_displaced_ft : rw.he_displaced_ft) * s376::geo::kFtToM;
    if (disp > 0.5)
        s376::offset_m(lat, lon, hdg, disp, lat, lon);
}

double along_m(double lat0, double lon0, double hdg, double lat, double lon) {
    double brg = 0;
    const double d = s376::geo::inverse_wgs84(lat0, lon0, lat, lon, &brg);
    return d * std::cos(s376::geo::deg2rad(s376::geo::wrap180(brg - hdg)));
}

s376::ChannelConfig front_channel() {
    const auto chans = s376::select_channels(s376::default_config(), "mid_left");
    return chans.empty() ? s376::ChannelConfig{} : chans.front();
}

s376::geo::ChannelView front_view(const s376::ShotPose& pose) {
    const auto ch = front_channel();
    s376::xt4::Dcs eye;
    eye.set_true_pose(pose.lat, pose.lon, pose.alt_ft, pose.hdg_true_deg, pose.pitch_deg,
                      pose.roll_deg);
    return s376::geo::make_channel_view(
        eye.lat_deg, eye.lon_deg, eye.alt_ft * s376::geo::kFtToM, eye.true_heading_deg(),
        eye.pitch_deg, eye.roll_deg, ch.heading_offset_deg, ch.pitch_offset_deg,
        ch.roll_offset_deg, ch.hfov_deg, ch.vfov_deg, ch.width, ch.height, ch.eye_fwd_m,
        ch.eye_right_m, ch.eye_up_m);
}

float lamp_k(const s376::LightPoint& L, const s376::HostState& hs, const s376::ShotPose& pose) {
    int step = 3;
    if (L.switch_num >= 0 && L.switch_num < s376::xt4::kMaxLightSwitches)
        step = hs.light_steps[static_cast<std::size_t>(L.switch_num)];
    const float tod = hs.have_env ? hs.env.tod_hours : 14.5f;
    const s376::geo::Ecef eye =
        s376::geo::lla_to_ecef(pose.lat, pose.lon, pose.alt_ft * s376::geo::kFtToM);
    const s376::geo::Ecef p = s376::geo::lla_to_ecef(L.lat, L.lon, L.alt_m);
    const float dx = static_cast<float>(p.x - eye.x);
    const float dy = static_cast<float>(p.y - eye.y);
    const float dz = static_cast<float>(p.z - eye.z);
    const float range = std::sqrt(dx * dx + dy * dy + dz * dz);
    const float night = s376::lights::night_weight(tod);
    float k = s376::lights::intensity(L.kind, step) * s376::lights::tod_visibility(L.kind, tod);
    k *= s376::lights::range_visibility(L.kind, range);
    k *= s376::lights::day_als_intensity(L.kind, night);
    k *= s376::lights::close_als_intensity(L.kind, range);
    return k;
}

} // namespace

void test_render_defects() {
    const auto jfk = s376::kjfk_public();
    const auto* rw04r = rwy(jfk, "04R");
    const auto* rw13l = rwy(jfk, "13L");
    CHECK(rw04r && rw13l);
    CHECK_NEAR(rw04r->heading_deg, 30.768, 0.01);
    // Root cause of 13L-all-dark at --tod 19: cover*12 m land pad buried the
    // ~4 m field. 04R ALS over the bay still cleared the water. Lights must
    // sit above the schematic pad.
    {
        const double field = jfk.elev_m;
        const double pad = s376::geo::local_ground_alt_m(0.75f, 1.0, field);
        CHECK(pad < field);
        CHECK(field + 0.85 > pad + 0.2);
        CHECK(s376::geo::local_ground_alt_m(0.2f, 1.0, field) < field - 1.0);
    }

    // Raster front (mid_left): 0° yaw, 50°×32.5° on 1920×1200.
    {
        const auto ch = front_channel();
        CHECK(ch.name == "mid_left");
        CHECK_NEAR(ch.heading_offset_deg, 0.0, 1e-9);
        CHECK_NEAR(ch.pitch_offset_deg, 0.0, 1e-9);
        CHECK_NEAR(ch.roll_offset_deg, 0.0, 1e-9);
        CHECK_NEAR(ch.hfov_deg, 50.0, 1e-9);
        CHECK_NEAR(ch.vfov_deg, 32.5, 1e-9);
        CHECK(ch.width == 1920 && ch.height == 1200);
        CHECK_NEAR(ch.eye_right_m, 0.0, 1e-9);
    }

    // --at eyepoint: true heading 30.768°, −3° pitch, 3° path height.
    // Front-channel THR must sit within a few pixels of frame centre.
    for (double nm : {3.0, 1.0, 0.5}) {
        s376::ShotPose p;
        CHECK(s376::runway_shot_pose(jfk, "04R", nm, p));
        CHECK_NEAR(p.hdg_true_deg, 30.768, 0.01);
        CHECK_NEAR(p.pitch_deg, -3.0, 1e-9);
        const double want_alt = rw04r->le_elev_m * s376::geo::kMToFt + 14.0 + nm * 318.0;
        CHECK_NEAR(p.alt_ft, want_alt, 2.0);

        s376::xt4::Dcs eye;
        eye.set_true_pose(p.lat, p.lon, p.alt_ft, p.hdg_true_deg, p.pitch_deg, p.roll_deg);
        CHECK_NEAR(eye.heading_deg, -30.768, 0.01);
        CHECK_NEAR(eye.true_heading_deg(), 30.768, 0.01);
        CHECK_NEAR(eye.pitch_deg, -3.0, 1e-3);

        double tlat, tlon, hdg, elev;
        landing(*rw04r, true, tlat, tlon, hdg, elev);
        const auto view = front_view(p);
        CHECK_NEAR(view.heading_deg, 30.768, 0.01);
        CHECK_NEAR(view.hfov_deg, 50.0, 1e-9);
        CHECK_NEAR(view.vfov_deg, 32.5, 1e-9);
        const auto pr = s376::geo::project_lla(view, tlat, tlon, elev, 1920, 1200);
        CHECK(pr.in_frame);
        // 14 ft TCH is ~2 px at 3 nm and ~10 px at 0.5 nm below centre.
        const double pix_tol = nm >= 2.5 ? 8.0 : (nm >= 0.9 ? 10.0 : 12.0);
        CHECK_NEAR(pr.pix_x, 960.0, pix_tol);
        CHECK_NEAR(pr.pix_y, 600.0, pix_tol);
        CHECK_NEAR(pr.ndc_x, 0.0, pix_tol / 960.0);
        CHECK_NEAR(pr.ndc_y, 0.0, pix_tol / 600.0);
    }
    {
        s376::ShotPose p;
        CHECK(s376::runway_shot_pose(jfk, "13L", 3.0, p));
        CHECK_NEAR(p.hdg_true_deg, rw13l->heading_deg, 0.05);
        CHECK_NEAR(p.pitch_deg, -3.0, 1e-9);
        double tlat, tlon, hdg, elev;
        landing(*rw13l, true, tlat, tlon, hdg, elev);
        const auto pr = s376::geo::project_lla(front_view(p), tlat, tlon, elev, 1920, 1200);
        CHECK(pr.in_frame);
        CHECK_NEAR(pr.pix_x, 960.0, 8.0);
        CHECK_NEAR(pr.pix_y, 600.0, 8.0);
    }

    // 04R 1 nm night: both HIRL lines in the front frustum (not a horizon sliver).
    {
        s376::ShotPose p;
        CHECK(s376::runway_shot_pose(jfk, "04R", 1.0, p));
        const auto view = front_view(p);
        s376::HostState hs;
        hs.have_env = true;
        hs.have_eyepoint = true;
        hs.env.tod_hours = 19.f;
        int n_left = 0, n_right = 0;
        double y0 = 1e9, y1 = -1e9, xL = 0, xR = 0;
        for (const auto& L : jfk.lights) {
            if (L.runway_k != 2 || L.kind != s376::LightKind::Edge)
                continue;
            const auto pr = s376::geo::project_lla(view, L.lat, L.lon, L.alt_m, 1920, 1200);
            if (!pr.in_frame || lamp_k(L, hs, p) <= 0.f)
                continue;
            y0 = std::min(y0, pr.pix_y);
            y1 = std::max(y1, pr.pix_y);
            if (pr.pix_x < 960.0) {
                xL += pr.pix_x;
                ++n_left;
            } else {
                xR += pr.pix_x;
                ++n_right;
            }
        }
        CHECK(n_left >= 20);
        CHECK(n_right >= 20);
        CHECK(xL / n_left < 950.0);
        CHECK(xR / n_right > 970.0);
        CHECK(y1 - y0 >= 40.0);
    }

    // 04R 0.5 nm: four PAPI boxes in the front frustum.
    {
        s376::ShotPose p;
        CHECK(s376::runway_shot_pose(jfk, "04R", 0.5, p));
        double tlat, tlon, hdg, elev;
        landing(*rw04r, true, tlat, tlon, hdg, elev);
        const auto view = front_view(p);
        int n = 0, n_red = 0;
        for (const auto& L : jfk.lights) {
            if (L.runway_k != 2 || L.kind != s376::LightKind::Vasi)
                continue;
            const double al = along_m(tlat, tlon, hdg, L.lat, L.lon);
            if (al < 200.0 || al > 600.0)
                continue;
            const auto pr = s376::geo::project_lla(view, L.lat, L.lon, L.alt_m, 1920, 1200);
            CHECK(pr.in_frame);
            const float ang = s376::lights::papi_eye_angle_deg(
                p.lat, p.lon, p.alt_ft * s376::geo::kFtToM, L.lat, L.lon, L.alt_m);
            if (!s376::lights::papi_box_white(ang, L.papi_box, L.papi_gs_deg))
                ++n_red;
            ++n;
        }
        CHECK(n == 4);
        CHECK(n_red >= 2); // ILS 3° is below 69 ft VGSI TCH
    }

    // Lit lamp counts: 04R and 13L at tod 3 / 12 / 19, 1 nm and 0.5 nm.
    {
        auto count_lit = [&](const char* ident, int rwy_k, double nm, float tod, int& n_app,
                             int& n_edge, int& n_tdz, int& n_papi, int& n_end) {
            n_app = n_edge = n_tdz = n_papi = n_end = 0;
            s376::ShotPose pose;
            CHECK(s376::runway_shot_pose(jfk, ident, nm, pose));
            s376::HostState hs;
            hs.have_env = true;
            hs.have_eyepoint = true;
            hs.env.tod_hours = tod;
            hs.env.tod_select = -1;
            const auto view = front_view(pose);
            for (const auto& L : jfk.lights) {
                if (L.runway_k != rwy_k)
                    continue;
                if (L.kind != s376::LightKind::Approach && L.kind != s376::LightKind::Edge &&
                    L.kind != s376::LightKind::Threshold && L.kind != s376::LightKind::Vasi &&
                    L.kind != s376::LightKind::End)
                    continue;
                const auto pr = s376::geo::project_lla(view, L.lat, L.lon, L.alt_m, 1920, 1200);
                if (!pr.in_frame)
                    continue;
                if (lamp_k(L, hs, pose) <= 0.f)
                    continue;
                if (L.kind == s376::LightKind::Vasi &&
                    !s376::lights::papi_in_coverage(pose.lat, pose.lon, L.lat, L.lon,
                                                    L.papi_rwy_hdg_deg, L.papi_rotate_deg,
                                                    L.papi_usable_left_deg, L.papi_usable_right_deg))
                    continue;
                if (L.kind == s376::LightKind::Approach)
                    ++n_app;
                else if (L.kind == s376::LightKind::Edge)
                    ++n_edge;
                else if (L.kind == s376::LightKind::Threshold)
                    ++n_tdz;
                else if (L.kind == s376::LightKind::Vasi)
                    ++n_papi;
                else if (L.kind == s376::LightKind::End)
                    ++n_end;
            }
        };
        for (float tod : {3.f, 12.f, 19.f}) {
            for (double nm : {1.0, 0.5}) {
                int a = 0, e = 0, t = 0, v = 0, n = 0;
                count_lit("04R", 2, nm, tod, a, e, t, v, n);
                CHECK(a >= 20);
                CHECK(e >= 8);
                CHECK(t >= 8);
                CHECK(v >= 2);
                CHECK(n >= 4);
                count_lit("13L", 3, nm, tod, a, e, t, v, n);
                CHECK(a >= 20);
                CHECK(e >= 8);
                CHECK(t >= 8);
                CHECK(v >= 2);
                CHECK(n >= 4);
            }
        }
        // 13L displaced area (and the near third) has lit edge lamps at tod 3.
        {
            s376::ShotPose pose;
            CHECK(s376::runway_shot_pose(jfk, "13L", 0.5, pose));
            s376::HostState hs;
            hs.have_env = true;
            hs.env.tod_hours = 3.f;
            double tlat, tlon, hdg, elev;
            landing(*rw13l, true, tlat, tlon, hdg, elev);
            const auto view = front_view(pose);
            int n_disp = 0;
            for (const auto& L : jfk.lights) {
                if (L.runway_k != 3 || L.kind != s376::LightKind::Edge)
                    continue;
                const double al = along_m(rw13l->le_lat, rw13l->le_lon, rw13l->heading_deg, L.lat,
                                          L.lon);
                if (al < 0.0 || al > rw13l->le_displaced_ft * s376::geo::kFtToM + 200.0)
                    continue;
                const auto pr = s376::geo::project_lla(view, L.lat, L.lon, L.alt_m, 1920, 1200);
                if (!pr.in_frame)
                    continue;
                if (lamp_k(L, hs, pose) > 0.f)
                    ++n_disp;
            }
            CHECK(n_disp >= 4);
        }
    }

    // Yellow taxi hold bars stay on land / on the segment (13L 0.5 nm).
    {
        const auto mesh = s376::build_airport_mesh(jfk, false, true);
        double tlat, tlon, hdg, elev;
        landing(*rw13l, true, tlat, tlon, hdg, elev);
        int n_yellow_water = 0;
        for (const auto& v : mesh.verts) {
            if (v.r < 0.80f || v.g < 0.60f || v.g > 0.88f || v.b > 0.30f)
                continue;
            double lat, lon, alt;
            s376::geo::ecef_to_lla(v.p, lat, lon, alt);
            const double d = s376::geo::ground_distance_m(tlat, tlon, lat, lon);
            if (d > 2500.0)
                continue;
            if (s376::geo::land_coverage(lat, lon) < 0.40f)
                ++n_yellow_water;
        }
        CHECK(n_yellow_water == 0);
    }
}
