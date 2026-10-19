#include "s376/airport.hpp"
#include "s376/landmarks.hpp"
#include "s376/lights.hpp"
#include "s376/tod.hpp"

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
                      << #b << "=" << _b << "\n";                                                  \
        }                                                                                          \
    } while (0)

void test_lights() {
    using namespace s376::lights;
    CHECK(runway_base(1) == 0);
    CHECK(runway_base(2) == 8);
    CHECK(runway_base(3) == 16);
    CHECK(runway_base(4) == 24);
    CHECK(runway_base(5) == 32);
    CHECK(runway_base(6) == 84);
    CHECK(runway_base(7) == 92);
    CHECK(runway_switch(1, kOffApproachA) == 1);
    CHECK(runway_switch(1, kOffStrobeA) == 2);
    CHECK(runway_switch(1, kOffEdge) == 3);
    CHECK(runway_switch(1, kOffCenterline) == 4);
    CHECK(runway_switch(1, kOffVasi) == 5);
    CHECK(runway_switch(1, kOffTdz) == 6);
    CHECK(runway_switch(1, kOffApproachB) == 7);
    CHECK(runway_switch(1, kOffStrobeB) == 8);
    CHECK(runway_switch(2, kOffEdge) == 11);
    CHECK(kSwTaxiEdge == 41 && kSwStopBar == 56 && kSwTaxiCenterline == 60);
    CHECK(s376::kSwRunwayEdge == 3);
    CHECK(s376::kSwCenterline == 4);
    CHECK(s376::kSwApproach == 1);
    CHECK(s376::kSwThreshold == 6);

    CHECK_NEAR(intensity(s376::LightKind::Approach, 0), 0.0, 1e-6);
    CHECK_NEAR(intensity(s376::LightKind::Approach, 1), 0.27, 1e-6);
    CHECK_NEAR(intensity(s376::LightKind::Threshold, 3), 0.50, 1e-6);
    CHECK_NEAR(intensity(s376::LightKind::Approach, 5), 1.0, 1e-6);
    CHECK_NEAR(intensity(s376::LightKind::Strobe, 1), 0.70, 1e-6);
    CHECK_NEAR(intensity(s376::LightKind::Strobe, 5), 1.0, 1e-6);
    CHECK_NEAR(intensity(s376::LightKind::Edge, 1), 0.35, 1e-6);
    CHECK_NEAR(intensity(s376::LightKind::Edge, 4), 0.92, 1e-6);
    CHECK_NEAR(intensity(s376::LightKind::LeadOn, 5), 1.0, 1e-6);
    CHECK_NEAR(intensity(s376::LightKind::SignGlow, 3), intensity(s376::LightKind::Edge, 3), 1e-6);
    CHECK_NEAR(intensity(s376::LightKind::Night, 1), intensity(s376::LightKind::Edge, 1), 1e-6);
    CHECK(s376::lights::type_range_scale(s376::LightKind::Beacon) > 0.9f);
    CHECK(s376::lights::type_range_scale(s376::LightKind::Night) < 0.5f);

    CHECK_NEAR(size_mult_hat(0.f), 12.0, 1e-4);
    CHECK_NEAR(size_mult_hat(500.f), 1.0, 1e-4);
    CHECK(size_mult_hat(250.f) > 1.0 && size_mult_hat(250.f) < 12.0);
    CHECK_NEAR(base_size_px(12.f), 2.8, 1e-4);
    CHECK_NEAR(base_size_px(0.f), kNightFarSizePx, 1e-4);
    CHECK_NEAR(base_size_px(3.f), kNightFarSizePx, 1e-4);
    CHECK(base_size_px(14.5f) < 3.0f);
    CHECK(night_weight(14.5f) < 0.02f);
    CHECK_NEAR(night_weight(3.f), 1.0, 1e-4);
    CHECK(night_weight(19.f) > 0.2f && night_weight(19.f) < 0.5f);
    CHECK_NEAR(point_size_px(12.f, 0.f), 2.8 * 12.0, 1e-3);
    CHECK_NEAR(point_size_px(12.f, 0.f, 0.f), 2.8 * 12.0, 1e-3);
    // Distant lights clamp to the Raster 2.5–2.8 px floor and never grow
    // with range (22L ALS / taxi on a 04R short-final horizon).
    CHECK_NEAR(point_size_px(12.f, 0.f, kSizeNearM), 2.8, 1e-3);
    CHECK_NEAR(point_size_px(12.f, 0.f, 5000.f), 2.8, 1e-3);
    CHECK_NEAR(point_size_px(12.f, 204.f, 4000.f), base_size_px(12.f), 1e-3);
    CHECK(point_size_px(14.5f, 204.f, 350.f) < point_size_px(14.5f, 204.f, 50.f));
    CHECK(point_size_px(14.5f, 204.f, 4000.f) < 3.2f);
    CHECK(range_visibility(s376::LightKind::Approach, 0.f) > 0.95f);
    CHECK(range_visibility(s376::LightKind::Approach, 1200.f) > 0.25f);
    CHECK(range_visibility(s376::LightKind::Vasi, 5000.f) >
          range_visibility(s376::LightKind::TaxiEdge, 5000.f));
    CHECK(range_visibility(s376::LightKind::TaxiEdge, 20000.f) < 0.15f);
    CHECK_NEAR(range_visibility(s376::LightKind::Approach, max_range_m() + 10.f), 0.0, 1e-6);
    CHECK(tod_visibility(s376::LightKind::Edge, 14.5f) < 0.08f);
    CHECK(tod_visibility(s376::LightKind::Edge, 14.5f) > 0.02f);
    CHECK(tod_visibility(s376::LightKind::Night, 14.5f) <
          tod_visibility(s376::LightKind::Vasi, 14.5f));
    CHECK(tod_visibility(s376::LightKind::Approach, 14.5f) > 0.50f);
    CHECK(tod_visibility(s376::LightKind::Vasi, 14.5f) > 0.80f);
    CHECK_NEAR(type_size_boost(s376::LightKind::Vasi), 2.3, 1e-4);
    CHECK_NEAR(type_size_boost(s376::LightKind::Approach), 1.0, 1e-4);
    CHECK_NEAR(type_size_boost(s376::LightKind::Edge), 1.28, 1e-4);
    CHECK_NEAR(type_size_boost(s376::LightKind::Centerline), 1.18, 1e-4);
    CHECK_NEAR(type_size_boost(s376::LightKind::Threshold), 1.18, 1e-4);
    CHECK(tod_visibility(s376::LightKind::Threshold, 14.5f) > 0.35f);
    CHECK(is_als_family(s376::LightKind::Approach));
    CHECK(is_als_family(s376::LightKind::Flasher));
    CHECK(!is_als_family(s376::LightKind::Vasi));
    CHECK(!is_als_family(s376::LightKind::Edge));
    CHECK_NEAR(day_als_intensity(s376::LightKind::Approach, 1.f), 1.0, 1e-4);
    CHECK_NEAR(day_als_intensity(s376::LightKind::Approach, 0.f), s376::lights::kApproachTdz[2],
               1e-4);
    CHECK(day_als_intensity(s376::LightKind::Approach, 0.f) > s376::lights::kApproachTdz[0] + 0.1f);
    CHECK_NEAR(day_als_intensity(s376::LightKind::Edge, 0.f), 1.0, 1e-4);
    CHECK(day_als_size_px(s376::LightKind::Approach, 0.f, 8.f) <= 3.2f);
    CHECK(day_als_size_px(s376::LightKind::Approach, 0.f, 8.f) >= 2.4f);
    CHECK_NEAR(day_als_size_px(s376::LightKind::Approach, 1.f, 8.f), 8.0, 1e-4);
    CHECK_NEAR(day_als_size_px(s376::LightKind::Vasi, 0.f, 6.4f), 6.4, 1e-4);
    CHECK(close_als_size_px(s376::LightKind::Approach, 150.f, 8.f) < 4.0f);
    CHECK(close_als_size_px(s376::LightKind::Approach, 150.f, 8.f) >= 2.2f);
    CHECK_NEAR(close_als_size_px(s376::LightKind::Approach, 2000.f, 8.f), 8.0, 1e-4);
    CHECK(close_als_intensity(s376::LightKind::Approach, 150.f) < 0.55f);
    CHECK_NEAR(close_als_intensity(s376::LightKind::Approach, 2000.f), 1.0, 1e-4);
    CHECK_NEAR(close_als_intensity(s376::LightKind::Edge, 150.f), 1.0, 1e-4);
    CHECK(close_als_size_px(s376::LightKind::End, 200.f, 8.f) <= 5.0f);
    // JO 6850.2C: inboard (box 0) is HIGHEST. On path: 2 inner red, 2 outer white.
    CHECK(!papi_box_white(3.0f, 0, 3.0f) && !papi_box_white(3.0f, 1, 3.0f));
    CHECK(papi_box_white(3.0f, 2, 3.0f) && papi_box_white(3.0f, 3, 3.0f));
    CHECK(papi_box_white(4.0f, 3, 3.0f));
    CHECK(!papi_box_white(2.0f, 0, 3.0f));
    CHECK(!papi_box_white(3.0f, 0, 3.0f)); // inboard stays red on path (HG4 +35′)
    CHECK(papi_box_white(3.6f, 0, 3.0f));  // above the highest unit
    {
        // Eye 1 km south of a PAPI, 52.4 m above it → atan(52.4/1000) ≈ 3°.
        const float ang = papi_eye_angle_deg(40.63, -73.78, 52.4, 40.639, -73.78, 0.0);
        CHECK_NEAR(ang, 3.0, 0.15);
        // +az = right of CL on the approach. Eye 30 m landing-right of a
        // 400 m approach point must be positive; landing-left negative.
        {
            const double plat = 40.639, plon = -73.78, hdg = 31.0;
            double clat, clon, rlat, rlon, llat, llon;
            s376::offset_m(plat, plon, hdg + 180.0, 400.0, clat, clon);
            s376::offset_m(clat, clon, hdg + 90.0, 30.0, rlat, rlon);
            s376::offset_m(clat, clon, hdg - 90.0, 30.0, llat, llon);
            CHECK(papi_azimuth_deg(rlat, rlon, plat, plon, static_cast<float>(hdg)) > 2.f);
            CHECK(papi_azimuth_deg(llat, llon, plat, plon, static_cast<float>(hdg)) < -2.f);
            CHECK_NEAR(papi_azimuth_deg(clat, clon, plat, plon, static_cast<float>(hdg)), 0.0, 0.5);
        }
        CHECK(!papi_box_white(ang, 0, 3.0f) && !papi_box_white(ang, 1, 3.0f));
        CHECK(papi_box_white(ang, 2, 3.0f) && papi_box_white(ang, 3, 3.0f));
    }
    {
        // 0.6 nm on 04R: four PAPI boxes must project left of the runway
        // at the touchdown, stay distinct (~2.5 px), and read 2W/2R.
        auto jfk = s376::kjfk_public();
        s376::ShotPose pose;
        CHECK(s376::runway_shot_pose(jfk, "04R", 0.6, pose));
        const auto& rw = jfk.runways[1]; // 04R / 22L
        const double tlat = rw.le_lat, tlon = rw.le_lon, telev = rw.le_elev_m;
        const float hat = static_cast<float>(pose.alt_ft - telev * s376::geo::kMToFt);
        const auto view = s376::geo::make_channel_view(
            pose.lat, pose.lon, pose.alt_ft * s376::geo::kFtToM, pose.hdg_true_deg, pose.pitch_deg,
            pose.roll_deg, 0, 0, 0, 50, 32.5, 1920, 1200);
        struct Box {
            int i = -1;
            s376::geo::ProjectedPoint p;
            bool white = false;
            float size = 0;
        };
        Box boxes[4];
        int n_horizon = 0;
        float horizon_sz = 0;
        for (const auto& L : jfk.lights) {
            if (L.runway_k != 2)
                continue;
            if (L.kind == s376::LightKind::Approach || L.kind == s376::LightKind::End) {
                const double dthr = s376::geo::ground_distance_m(tlat, tlon, L.lat, L.lon);
                if (dthr > 2000.0) {
                    const auto hp = s376::geo::project_lla(view, L.lat, L.lon, L.alt_m, 1920, 1200);
                    if (hp.in_frame && std::fabs(hp.ndc_y) < 0.20) {
                        ++n_horizon;
                        horizon_sz = point_size_px(14.5f, hat, static_cast<float>(hp.depth_m));
                    }
                }
            }
            if (L.kind != s376::LightKind::Vasi || L.papi_box < 0 || L.papi_box > 3)
                continue;
            if (s376::geo::ground_distance_m(tlat, tlon, L.lat, L.lon) > 500.0)
                continue; // 04R PAPI is ~(69-2)/tan(3°) ft past the THR
            const auto pr = s376::geo::project_lla(view, L.lat, L.lon, L.alt_m, 1920, 1200);
            const float ang = papi_eye_angle_deg(pose.lat, pose.lon, pose.alt_ft * s376::geo::kFtToM,
                                                 L.lat, L.lon, L.alt_m);
            Box& b = boxes[L.papi_box];
            b.i = L.papi_box;
            b.p = pr;
            b.white = papi_box_white(ang, L.papi_box, L.papi_gs_deg);
            b.size = point_size_px(14.5f, hat, static_cast<float>(pr.depth_m));
        }
        int n_ok = 0;
        for (int i = 0; i < 4; ++i) {
            CHECK(boxes[i].i == i);
            CHECK(boxes[i].p.in_frame);
            CHECK(boxes[i].p.ndc_x < -0.01); // left of centerline
            CHECK(boxes[i].p.ndc_y < 0.15);  // touchdown, not the horizon row
            const float vis = std::min(8.f, std::max(5.8f, boxes[i].size * type_size_boost(s376::LightKind::Vasi)));
            CHECK(vis > 5.5f && vis < 8.5f);
            ++n_ok;
        }
        CHECK(n_ok == 4);
        // ILS-style 3° to the THR is below the 69 ft VGSI TCH, so the
        // inboard pair stays red. On the published visual GP it's 2R2W.
        CHECK(!boxes[0].white && !boxes[1].white);
        CHECK(!papi_box_white(3.0f, 0, 3.0f) && papi_box_white(3.0f, 3, 3.0f));
        // Inboard → outboard is leftward (more negative x) on a P4L.
        CHECK(boxes[3].p.pix_x < boxes[2].p.pix_x);
        CHECK(boxes[2].p.pix_x < boxes[1].p.pix_x);
        CHECK(boxes[1].p.pix_x < boxes[0].p.pix_x);
        const double gap = boxes[0].p.pix_x - boxes[1].p.pix_x;
        CHECK(gap > boxes[0].size * 1.6);
        // 22L ALS / end lights sit on the horizon as small points, not 20 px domes.
        CHECK(n_horizon >= 8);
        CHECK(horizon_sz < 3.2f);
        CHECK(horizon_sz < point_size_px(14.5f, hat, 0.f) * 0.4f);
        CHECK(point_size_px(3.f, hat, 4000.f) > 5.0f);
        CHECK(point_size_px(3.f, hat, 4000.f) < 6.5f);
        // Left-horizon 'pier/bridge' is 04L edge (official, runway_k=1),
        // not NYC landmarks (~308° / 20 km, outside the 50° front).
        int n_04l_edge = 0;
        for (const auto& L : jfk.lights) {
            if (L.runway_k != 1 || L.kind != s376::LightKind::Edge)
                continue;
            const auto hp = s376::geo::project_lla(view, L.lat, L.lon, L.alt_m, 1920, 1200);
            if (hp.in_frame && hp.pix_x < 700.0 && std::fabs(hp.ndc_y) < 0.30)
                ++n_04l_edge;
        }
        CHECK(n_04l_edge >= 20);
        int n_04r_edge = 0, n_04r_cl = 0, n_04r_tdz = 0;
        int n_taxi_green_left = 0;
        for (const auto& L : jfk.lights) {
            const auto hp = s376::geo::project_lla(view, L.lat, L.lon, L.alt_m, 1920, 1200);
            if (L.runway_k == 2 && hp.in_frame && hp.ndc_y < 0.35) {
                if (L.kind == s376::LightKind::Edge)
                    ++n_04r_edge;
                if (L.kind == s376::LightKind::Centerline)
                    ++n_04r_cl;
                if (L.kind == s376::LightKind::Threshold)
                    ++n_04r_tdz;
            }
            if (L.kind == s376::LightKind::TaxiCenterline && L.g > 0.70f && hp.in_frame &&
                hp.ndc_x < -0.02 && hp.ndc_y < 0.40)
                ++n_taxi_green_left;
        }
        CHECK(n_04r_edge >= 16);
        CHECK(n_04r_cl >= 20);
        CHECK(n_04r_tdz >= 12);
        // Green line left of 04R is taxi B centerline (aviation green).
        CHECK(n_taxi_green_left >= 8);
        int n_lm = 0;
        int nlm = 0;
        const s376::Landmark* lms = s376::nyc_landmarks(&nlm);
        for (int i = 0; i < nlm; ++i) {
            const auto hp =
                s376::geo::project_lla(view, lms[i].lat, lms[i].lon, lms[i].height_m, 1920, 1200);
            if (hp.in_frame)
                ++n_lm;
        }
        CHECK(n_lm == 0);
    }
    CHECK_NEAR(tod_visibility(s376::LightKind::Edge, 0.5f), 1.0, 1e-3);
    CHECK_NEAR(kMaxRangeFt, 250000.0, 1e-3);
    CHECK(max_range_m() > 70000.f);

    CHECK(beacon_on(0.00) && beacon_on(0.09) && !beacon_on(0.11) && !beacon_on(1.0));
    CHECK(beacon_on(1.50) && !beacon_on(1.61));
    CHECK(strobe_on(0.00) && strobe_on(0.06) && !strobe_on(0.07));
    CHECK(sfl_on(0.0, 0) && !sfl_on(0.0, 1));
    CHECK(sfl_on(1.0 / 30.0, 1)); // whole sequence 2 Hz → slot 1 at 1/30 s
    CHECK(sfl_on(0.5, 0) && !sfl_on(0.5, 1));

    auto tod = s376::default_tod_table();
    CHECK(tod.samples.size() >= 4);
    auto noon = s376::sample_tod(tod, 12.f);
    CHECK(noon.stars < 0.05f);
    CHECK(noon.diffuse[0] > 0.8f);
    auto night = s376::sample_tod(tod, 0.5f);
    CHECK(night.stars > 0.8f);
    CHECK(night.ambient[0] < noon.ambient[0]);

    std::string path = "data/env/tod.toml";
    {
        std::ifstream probe(path);
        if (!probe)
            path = "../data/env/tod.toml";
    }
    s376::TodTable file;
    std::string err;
    CHECK(s376::load_tod_table(path, file, err));
    CHECK(file.samples.size() >= 4);
    auto noon2 = s376::sample_tod(file, 12.f);
    CHECK_NEAR(noon2.diffuse[0], 1.0, 0.05);
}
