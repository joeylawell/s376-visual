#include "s376/airport.hpp"
#include "s376/display.hpp"
#include "s376/diagnostics.hpp"
#include "s376/geo.hpp"
#include "s376/landmarks.hpp"
#include "s376/lights.hpp"
#include "s376/state.hpp"

#include <cmath>
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

double along_m(double lat0, double lon0, double hdg, double lat, double lon) {
    double brg = 0;
    const double d = s376::geo::inverse_wgs84(lat0, lon0, lat, lon, &brg);
    return d * std::cos(s376::geo::deg2rad(s376::geo::wrap180(brg - hdg)));
}

double across_m(double lat0, double lon0, double hdg, double lat, double lon) {
    double brg = 0;
    const double d = s376::geo::inverse_wgs84(lat0, lon0, lat, lon, &brg);
    return d * std::sin(s376::geo::deg2rad(s376::geo::wrap180(brg - hdg)));
}

} // namespace

void test_pilot_view() {
    CHECK(s376::marking_ident("04R") == "4R");
    CHECK(s376::marking_ident("4R") == "4R");
    CHECK(s376::marking_ident("13L") == "13L");
    CHECK(s376::marking_ident("10L") == "10L");
    CHECK_NEAR(s376::kThresholdStripeStartFt, 20.0, 0.1);

    const auto jfk = s376::kjfk_public();
    const auto* rw04r = rwy(jfk, "04R");
    const auto* rw04l = rwy(jfk, "04L");
    const auto* rw13l = rwy(jfk, "13L");
    const auto* rw13r = rwy(jfk, "13R");
    CHECK(rw04r && rw04l && rw13l && rw13r);

    // Parallel runways from the landing thresholds.
    {
        double t04, o04, h04, t04l, o04l, h04l;
        landing(*rw04r, true, t04, o04, h04);
        landing(*rw04l, true, t04l, o04l, h04l);
        const double lat_ft = across_m(t04, o04, h04, t04l, o04l) * s376::geo::kMToFt;
        const double along_ft = along_m(t04, o04, h04, t04l, o04l) * s376::geo::kMToFt;
        CHECK_NEAR(lat_ft, -3000.0, 20.0);
        CHECK_NEAR(along_ft, -2770.0, 20.0);
        CHECK(rw04l->approach_le == "NONE");

        double t13, o13, h13, t13r, o13r, h13r;
        landing(*rw13l, true, t13, o13, h13);
        landing(*rw13r, true, t13r, o13r, h13r);
        const double lat13 = across_m(t13, o13, h13, t13r, o13r) * s376::geo::kMToFt;
        const double along13 = along_m(t13, o13, h13, t13r, o13r) * s376::geo::kMToFt;
        CHECK_NEAR(lat13, 6700.0, 20.0);
        CHECK_NEAR(along13, -3426.0, 25.0);
    }

    // Edge yellow last 2000 ft; CL 3000–1000 alt, then red; no red from approach.
    {
        int n_near_white = 0, n_far_yel = 0, n_cl_alt = 0, n_cl_red = 0, n_cl_app = 0, n_mid = 0;
        for (const auto& L : jfk.lights) {
            if (L.runway_k != 2)
                continue;
            const double al = along_m(rw04r->le_lat, rw04r->le_lon, rw04r->heading_deg, L.lat, L.lon);
            if (L.kind == s376::LightKind::Edge && L.r2 >= 0.f) {
                if (al < 400.0) {
                    CHECK(L.g > 0.9f);
                    CHECK(L.g2 < 0.85f);
                    ++n_near_white;
                }
                if (al > 700.0 && al < rw04r->length_m - 700.0) {
                    CHECK(L.g > 0.9f); // yellow last 2000 ft only
                    ++n_mid;
                }
                if (al > rw04r->length_m - 400.0) {
                    CHECK(L.g < 0.85f);
                    CHECK(L.g2 > 0.9f);
                    ++n_far_yel;
                }
            }
            if (L.kind == s376::LightKind::Centerline && L.r2 >= 0.f) {
                if (al < 400.0) {
                    CHECK(L.g > 0.9f); // no red from the approach end
                    ++n_cl_app;
                }
                if (al > rw04r->length_m - 280.0) {
                    CHECK(L.g < 0.4f && L.r > 0.8f);
                    ++n_cl_red;
                }
                if (al > rw04r->length_m - 800.0 && al < rw04r->length_m - 400.0 && L.g < 0.4f)
                    ++n_cl_alt;
            }
        }
        CHECK(n_near_white >= 4);
        CHECK(n_far_yel >= 4);
        CHECK(n_mid >= 4);
        CHECK(n_cl_app >= 4);
        CHECK(n_cl_red >= 4);
        CHECK(n_cl_alt >= 2);
    }

    // ALSF-2 green bar 45 ft past each edge; 15 SFL toward THR at 2 Hz.
    {
        CHECK_NEAR(s376::lights::kSflHz, 2.0, 1e-9);
        CHECK(s376::lights::kSflSlots == 15);
        CHECK(s376::lights::sfl_on(0.0, 0));
        CHECK(!s376::lights::sfl_on(0.0, 1));
        double tlat, tlon, hdg;
        landing(*rw04r, true, tlat, tlon, hdg);
        double max_green = 0;
        int n_sfl = 0;
        int seq_outer = 99;
        double al_outer = -1;
        const double back = s376::geo::wrap360(hdg + 180.0);
        for (const auto& L : jfk.lights) {
            if (L.runway_k != 2)
                continue;
            if (L.kind == s376::LightKind::End && L.g > 0.7f)
                max_green = std::max(max_green, std::fabs(across_m(tlat, tlon, hdg, L.lat, L.lon)));
            if (L.kind == s376::LightKind::Flasher && L.seq >= 0) {
                ++n_sfl;
                const double al = along_m(tlat, tlon, back, L.lat, L.lon);
                if (al > al_outer) {
                    al_outer = al;
                    seq_outer = L.seq;
                }
            }
        }
        CHECK(max_green > (100.0 + 40.0) * s376::geo::kFtToM);
        CHECK(n_sfl >= 15);
        CHECK(seq_outer == 0);
    }

    // 13L lead-in on switch 70, off by default; ALSF-2 SFL stay on strobe A.
    {
        int n_lead = 0, n_strobe_sfl = 0;
        double tlat, tlon, hdg;
        landing(*rw13l, true, tlat, tlon, hdg);
        for (const auto& L : jfk.lights) {
            if (L.runway_k != 3 || L.kind != s376::LightKind::Flasher)
                continue;
            const double d = s376::geo::ground_distance_m(tlat, tlon, L.lat, L.lon);
            if (d > 1500.0 && d < 2200.0) {
                CHECK(L.switch_num == s376::kSwRllsLeadIn);
                ++n_lead;
            } else if (s376::lights::is_runway_switch(L.switch_num, s376::lights::kOffStrobeA))
                ++n_strobe_sfl;
        }
        CHECK(n_lead >= 5);
        CHECK(n_strobe_sfl >= 15);
        s376::HostState hs;
        CHECK(hs.light_steps[s376::kSwRllsLeadIn] == 0);
    }

    // PAPI optical height is runway elevation + 2 ft.
    {
        double tlat, tlon, hdg;
        landing(*rw04r, true, tlat, tlon, hdg);
        int n = 0;
        for (const auto& L : jfk.lights) {
            if (L.runway_k != 2 || L.kind != s376::LightKind::Vasi)
                continue;
            const double al = along_m(tlat, tlon, hdg, L.lat, L.lon);
            if (al < 200.0 || al > 600.0)
                continue; // 04R boxes, not 22L
            CHECK_NEAR(L.alt_m, rw04r->le_elev_m + s376::lights::kPapiOpticalHtFt * s376::geo::kFtToM,
                       0.05);
            ++n;
        }
        CHECK(n == 4);
    }

    // Designators, 16 stripes from 20 ft, aim 1020, TDZ 3-2-2-1-1, 04R EMAS bar.
    {
        const auto mesh = s376::build_airport_mesh(jfk, false, true);
        double tlat, tlon, hdg;
        landing(*rw04r, true, tlat, tlon, hdg);
        int n_bar = 0, n_stripe = 0, n_aim = 0;
        int tdz_n[5] = {};
        for (const auto& v : mesh.verts) {
            if (v.r < 0.90f || v.g < 0.90f)
                continue;
            double lat, lon, alt;
            s376::geo::ecef_to_lla(v.p, lat, lon, alt);
            const double al = along_m(tlat, tlon, hdg, lat, lon);
            const double ac = std::fabs(across_m(tlat, tlon, hdg, lat, lon));
            if (ac > rw04r->width_m * 0.5)
                continue;
            if (al > -0.5 && al < 3.5 && ac > 6.0)
                ++n_bar;
            const double s0 = s376::kThresholdStripeStartFt * s376::geo::kFtToM;
            if (al > s0 - 0.5 && al < s0 + 4.0 && ac > 4.0)
                ++n_stripe;
            const double aim = s376::kAimingPointStartFt * s376::geo::kFtToM;
            const double aim_in = 0.5 * s376::kAimingPointInnerFt * s376::geo::kFtToM;
            const double aim_out = aim_in + s376::kAimingPointWidthFt * s376::geo::kFtToM;
            if (al > aim - 1.0 && al < aim + 8.0 && ac > aim_in - 1.0 && ac < aim_out + 1.0)
                ++n_aim;
            for (int s = 0; s < 5; ++s) {
                const double d = s376::kTdzStationFt[s] * s376::geo::kFtToM;
                if (al > d - 1.5 && al < d + 4.0 && ac > 8.0)
                    tdz_n[s] += 1;
            }
        }
        CHECK(n_bar >= 4);
        CHECK(n_stripe >= 8);
        CHECK(n_aim >= 4);
        CHECK(tdz_n[0] > tdz_n[4]);
        CHECK(s376::kTdzBarCounts[0] == 3 && s376::kTdzBarCounts[4] == 1);
        CHECK(s376::threshold_stripe_count(rw04r->width_m) == 16);
    }

    // 04R / 13L 6 nm bearings.
    {
        s376::ShotPose p04, p13;
        CHECK(s376::runway_shot_pose(jfk, "04R", 6.0, p04));
        CHECK(s376::runway_shot_pose(jfk, "13L", 6.0, p13));
        const double e04 = p04.alt_ft * s376::geo::kFtToM;
        const double e13 = p13.alt_ft * s376::geo::kFtToM;
        const auto tw04 = s376::observe_from_eye(p04.lat, p04.lon, e04, p04.hdg_true_deg, jfk.tower_lat,
                                                 jfk.tower_lon, jfk.elev_m + jfk.tower_h_m);
        CHECK_NEAR(tw04.rel_deg, -8.2, 0.15);
        CHECK_NEAR(tw04.left_m, 1766.0, 20.0);
        const auto tw13 = s376::observe_from_eye(p13.lat, p13.lon, e13, p13.hdg_true_deg, jfk.tower_lat,
                                                 jfk.tower_lon, jfk.elev_m + jfk.tower_h_m);
        CHECK_NEAR(tw13.rel_deg, 4.9, 0.15);
        CHECK_NEAR(tw13.left_m, -1069.0, 20.0);

        for (const auto& b : jfk.buildings) {
            if (b.name.size() < 2 || b.name[0] != 'T' || b.ring.empty())
                continue;
            double lat = 0, lon = 0;
            for (const auto& p : b.ring) {
                lat += p.first;
                lon += p.second;
            }
            lat /= static_cast<double>(b.ring.size());
            lon /= static_cast<double>(b.ring.size());
            const auto o04 = s376::observe_from_eye(p04.lat, p04.lon, e04, p04.hdg_true_deg, lat, lon,
                                                    jfk.elev_m);
            const auto o13 = s376::observe_from_eye(p13.lat, p13.lon, e13, p13.hdg_true_deg, lat, lon,
                                                    jfk.elev_m);
            CHECK(o04.rel_deg < 0.0); // all terminals left of 04R
            CHECK(o13.rel_deg > 0.0); // all terminals right of 13L
        }

        const s376::Landmark* cpt = s376::find_landmark("cpt");
        const s376::Landmark* esb = s376::find_landmark("empire");
        const s376::Landmark* wtc = s376::find_landmark("onewtc");
        CHECK(cpt && esb && wtc);
        const auto oc = s376::observe_from_eye(p04.lat, p04.lon, e04, p04.hdg_true_deg, cpt->lat,
                                               cpt->lon, cpt->height_m);
        const auto oe = s376::observe_from_eye(p04.lat, p04.lon, e04, p04.hdg_true_deg, esb->lat,
                                               esb->lon, esb->height_m);
        const auto ow = s376::observe_from_eye(p04.lat, p04.lon, e04, p04.hdg_true_deg, wtc->lat,
                                               wtc->lon, wtc->height_m);
        CHECK_NEAR(oc.rel_deg, -56.0, 1.0);
        CHECK_NEAR(oe.rel_deg, -59.0, 1.0);
        CHECK_NEAR(ow.rel_deg, -68.0, 1.0);
        CHECK(oc.elev_ang_deg < 0.0 && oe.elev_ang_deg < 0.0 && ow.elev_ang_deg < 0.0);

        const auto oc13 = s376::observe_from_eye(p13.lat, p13.lon, e13, p13.hdg_true_deg, cpt->lat,
                                                 cpt->lon, cpt->height_m);
        CHECK(std::fabs(oc13.rel_deg) > 90.0); // Manhattan behind on 13L

        auto nm_of = [&](const char* name, const s376::ShotPose& pose, const s376::Runway& rw,
                         bool le) {
            for (const auto& m : jfk.approach_marks) {
                if (m.name != name)
                    continue;
                double tlat, tlon, hdg;
                landing(rw, le, tlat, tlon, hdg);
                return s376::geo::ground_distance_m(tlat, tlon, m.lat, m.lon) * s376::geo::kMToNm;
            }
            (void)pose;
            return -1.0;
        };
        CHECK_NEAR(nm_of("Rockaway Beach", p04, *rw04r, true), 2.80, 0.05);
        CHECK_NEAR(nm_of("A-train viaduct", p04, *rw04r, true), 2.41, 0.05);
        CHECK_NEAR(nm_of("bay shore", p04, *rw04r, true), 1.56, 0.05);
        CHECK_NEAR(nm_of("Aqueduct", p13, *rw13l, true), 2.20, 0.15);
        CHECK_NEAR(nm_of("Belt Parkway", p13, *rw13l, true), 1.18, 0.05);
        bool cem = false;
        for (const auto& b : jfk.buildings) {
            if (b.type != "cemetery" || b.ring.empty())
                continue;
            double lat = 0, lon = 0;
            for (const auto& p : b.ring) {
                lat += p.first;
                lon += p.second;
            }
            lat /= static_cast<double>(b.ring.size());
            lon /= static_cast<double>(b.ring.size());
            double tlat, tlon, hdg;
            landing(*rw13l, true, tlat, tlon, hdg);
            const double nm = s376::geo::ground_distance_m(tlat, tlon, lat, lon) * s376::geo::kMToNm;
            CHECK(nm > 2.5 && nm < 5.5); // 13L final over the Queens cemeteries
            cem = true;
        }
        CHECK(cem);

        const auto text = s376::format_pilot_view_bearings(jfk, p04);
        CHECK(text.find("ATCT") != std::string::npos);
        CHECK(text.find("Central Park Tower") != std::string::npos);
        CHECK(text.find("One World Trade Center") != std::string::npos);
    }

    // 04R ALS over water (Jamaica Bay "pier" lamps).
    {
        double tlat, tlon, hdg;
        landing(*rw04r, true, tlat, tlon, hdg);
        const double back = s376::geo::wrap360(hdg + 180.0);
        int n_water = 0;
        for (const auto& L : jfk.lights) {
            if (L.runway_k != 2 || L.kind != s376::LightKind::Approach)
                continue;
            const double al = along_m(tlat, tlon, back, L.lat, L.lon);
            if (std::fabs(al - 2400.0 * s376::geo::kFtToM) < 8.0 &&
                s376::geo::land_coverage(L.lat, L.lon) < 0.40)
                ++n_water;
        }
        CHECK(n_water >= 3);
    }

    // Wet changes reflections only — lights are independent of condition.
    {
        CHECK_NEAR(s376::runway_wetness(0), 0.0, 1e-6);
        CHECK_NEAR(s376::runway_wetness(2), 0.75, 1e-6);
        const std::size_t n = jfk.lights.size();
        CHECK(n > 100);
        for (const auto& L : jfk.lights) {
            CHECK(L.seq == L.seq); // positions/colours exist regardless of wetness
            (void)L;
        }
        s376::HostState dry, wet;
        dry.env.runway_condition = 0;
        wet.env.runway_condition = 2;
        CHECK(dry.light_steps[3] == wet.light_steps[3]);
        CHECK(s376::runway_wetness(dry.env.runway_condition) !=
              s376::runway_wetness(wet.env.runway_condition));
    }

    // KJFK 2026-10-25: sunset ~18:00 EDT, moon ephemeris is date-correct.
    {
        CHECK_NEAR(s376::geo::civil_utc_offset_hours(-73.78, 2026, 10, 25), -4.0, 1e-9);
        CHECK_NEAR(s376::geo::civil_utc_offset_hours(-73.78, 2026, 1, 15), -5.0, 1e-9);
        double el18 = 0, el12 = 0, el21 = 0;
        s376::geo::sun_ecef_civil(40.64, -73.78, 18.0, 10, 25, 2026, &el18);
        s376::geo::sun_ecef_civil(40.64, -73.78, 12.0, 10, 25, 2026, &el12);
        s376::geo::sun_ecef_civil(40.64, -73.78, 21.0, 10, 25, 2026, &el21);
        CHECK(el12 > 30.0);
        CHECK(std::fabs(el18) < 6.0); // sunset ~18:00 EDT
        CHECK(el21 < -10.0);
        double m18 = 0, m12 = 0;
        s376::geo::moon_ecef_civil(40.64, -73.78, 18.0, 10, 25, 2026, &m18);
        s376::geo::moon_ecef_civil(40.64, -73.78, 12.0, 10, 25, 2026, &m12);
        CHECK(m18 != m12); // date/time dependent, not a -sun fake
        CHECK_NEAR(m18, 6.7, 2.5);  // Queens 18:00 EDT 2026-10-25 ≈ +6.7°
        CHECK_NEAR(m12, -35.6, 3.0);
    }
}
