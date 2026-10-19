#include "s376/airport.hpp"
#include "s376/faa.hpp"
#include "s376/landmarks.hpp"
#include "s376/lights.hpp"
#include "s376/traffic.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace s376 {
namespace {

constexpr double kR = geo::kWgsA;

double wrap360(double a) {
    a = std::fmod(a, 360.0);
    return a < 0 ? a + 360.0 : a;
}

void add_tri(AirportMesh& m, const geo::Ecef& a, const geo::Ecef& b, const geo::Ecef& c,
             const geo::Ecef& n, float r, float g, float bl, float mat = 0) {
    MeshVert v;
    v.n = n;
    v.r = r;
    v.g = g;
    v.b = bl;
    v.mat = mat;
    v.p = a;
    m.verts.push_back(v);
    v.p = b;
    m.verts.push_back(v);
    v.p = c;
    m.verts.push_back(v);
}

void strip_quad(AirportMesh& mesh, double lat0, double lon0, double lat1, double lon1,
                double heading_deg, double half_w, double elev0, double elev1, float r, float g,
                float b, float mat = 0) {
    double l0lat, l0lon, r0lat, r0lon, l1lat, l1lon, r1lat, r1lon;
    offset_m(lat0, lon0, heading_deg - 90.0, half_w, l0lat, l0lon);
    offset_m(lat0, lon0, heading_deg + 90.0, half_w, r0lat, r0lon);
    offset_m(lat1, lon1, heading_deg - 90.0, half_w, l1lat, l1lon);
    offset_m(lat1, lon1, heading_deg + 90.0, half_w, r1lat, r1lon);
    const geo::Ecef A = geo::lla_to_ecef(l0lat, l0lon, elev0);
    const geo::Ecef B = geo::lla_to_ecef(r0lat, r0lon, elev0);
    const geo::Ecef C = geo::lla_to_ecef(r1lat, r1lon, elev1);
    const geo::Ecef D = geo::lla_to_ecef(l1lat, l1lon, elev1);
    geo::Ecef e, n, u;
    geo::ecef_to_enu_axes((lat0 + lat1) * 0.5, (lon0 + lon1) * 0.5, e, n, u);
    add_tri(mesh, A, B, C, u, r, g, b, mat);
    add_tri(mesh, A, C, D, u, r, g, b, mat);
}

// Runway-frame rectangle. along/left in metres from (lat,lon) along hdg.
void mark_rect(AirportMesh& mesh, double lat, double lon, double hdg, double elev, double along0,
               double along1, double left, double right, float r, float g, float b) {
    double a0lat, a0lon, a1lat, a1lon;
    offset_m(lat, lon, hdg, along0, a0lat, a0lon);
    offset_m(lat, lon, hdg, along1, a1lat, a1lon);
    const double hw = std::fabs(right - left) * 0.5;
    const double mid = 0.5 * (left + right);
    double m0lat, m0lon, m1lat, m1lon;
    offset_m(a0lat, a0lon, hdg + 90.0, mid, m0lat, m0lon);
    offset_m(a1lat, a1lon, hdg + 90.0, mid, m1lat, m1lon);
    strip_quad(mesh, m0lat, m0lon, m1lat, m1lon, hdg, hw, elev, elev, r, g, b, 1.f);
}

const char* glyph5x7(char c) {
    switch (c) {
    case '0':
        return "01110100011000110001100011000101110";
    case '1':
        return "00100011000010000100001000010001110";
    case '2':
        return "01110100010000100110010001000011111";
    case '3':
        return "01110100010000100110000011000101110";
    case '4':
        return "00010001100101010010111110001000010";
    case '5':
        return "11111100001111000001000011000101110";
    case '6':
        return "01110100001111010001100011000101110";
    case '7':
        return "11111000010001000100010000100001000";
    case '8':
        return "01110100011000101110100011000101110";
    case '9':
        return "01110100011000101111000010000101110";
    case 'L':
        return "10000100001000010000100001000011111";
    case 'R':
        return "11110100011000111110101001001010001";
    case 'C':
        return "01110100011000010000100001000101110";
    default:
        return nullptr;
    }
}

void paint_glyph(AirportMesh& mesh, double lat, double lon, double hdg, double elev, double along,
                 double left, double tall_m, double wide_m, char ch) {
    const char* g = glyph5x7(ch);
    if (!g)
        return;
    const double pw = wide_m / 5.0;
    const double ph = tall_m / 7.0;
    for (int row = 0; row < 7; ++row) {
        for (int col = 0; col < 5; ++col) {
            if (g[row * 5 + col] != '1')
                continue;
            const double a0 = along + (6 - row) * ph;
            const double a1 = a0 + ph * 0.92;
            const double l0 = left + col * pw;
            mark_rect(mesh, lat, lon, hdg, elev, a0, a1, l0, l0 + pw * 0.88, 0.96f, 0.96f, 0.94f);
        }
    }
}

void paint_ident(AirportMesh& mesh, double lat, double lon, double hdg, double elev, double along,
                 const std::string& ident) {
    if (ident.empty())
        return;
    const double tall = 18.0;
    const double wide = 10.0;
    const double gap = 2.5;
    const double total = ident.size() * wide + (ident.size() - 1) * gap;
    double x = -total * 0.5;
    for (char ch : ident) {
        paint_glyph(mesh, lat, lon, hdg, elev, along, x, tall, wide, ch);
        x += wide + gap;
    }
}

void add_faa_markings(AirportMesh& mesh, const Runway& rw) {
    const double elev = std::max(rw.le_elev_m, rw.he_elev_m) + 0.48;
    const double half = rw.width_m * 0.5;
    const double cl_h = 0.5 * kCenterlineWidthIn * 0.0254; // 36 in
    // Edge + centerline once from the LE end.
    mark_rect(mesh, rw.le_lat, rw.le_lon, rw.heading_deg, elev, 0, rw.length_m, half - 1.1,
              half - 0.15, 0.95f, 0.95f, 0.93f);
    mark_rect(mesh, rw.le_lat, rw.le_lon, rw.heading_deg, elev, 0, rw.length_m, -(half - 0.15),
              -(half - 1.1), 0.95f, 0.95f, 0.93f);
    for (double d = 12; d < rw.length_m - 20; d += 60.0)
        mark_rect(mesh, rw.le_lat, rw.le_lon, rw.heading_deg, elev, d,
                  std::min(d + 36.0, rw.length_m - 8), -cl_h, cl_h, 0.96f, 0.96f, 0.94f);

    auto one_end = [&](double lat, double lon, double hdg, const std::string& ident,
                       double disp_ft) {
        const double disp_m = disp_ft * geo::kFtToM;
        if (disp_m > 8.0) {
            // AC 150/5340-1M 2.9: white arrows in the displaced area, not
            // yellow blast-pad chevrons (those are §2.12 unusable pavement).
            const double step = 100.0 * geo::kFtToM;
            for (double d = 20.0 * geo::kFtToM; d < disp_m - 25.0 * geo::kFtToM; d += step) {
                mark_rect(mesh, lat, lon, hdg, elev, d, d + 18.0 * geo::kFtToM, -cl_h, cl_h, 0.97f,
                          0.97f, 0.95f);
                mark_rect(mesh, lat, lon, hdg, elev, d + 14.0 * geo::kFtToM, d + 28.0 * geo::kFtToM,
                          2.0, 8.0, 0.97f, 0.97f, 0.95f);
                mark_rect(mesh, lat, lon, hdg, elev, d + 14.0 * geo::kFtToM, d + 28.0 * geo::kFtToM,
                          -8.0, -2.0, 0.97f, 0.97f, 0.95f);
            }
            offset_m(lat, lon, hdg, disp_m, lat, lon);
            // AC 150/5340-1M 2.10: 10 ft white threshold bar at the DTHR.
            const double bar = kDisplacedThresholdBarFt * geo::kFtToM;
            mark_rect(mesh, lat, lon, hdg, elev, 0.0, bar, -(half - 0.3), half - 0.3, 0.97f, 0.97f,
                      0.95f);
        }
        // Threshold stripes: 16 on a 200 ft runway, 12 on 150 ft (Table 2-1).
        const int nb = threshold_stripe_count(rw.width_m);
        const double bar_w = 5.75 * geo::kFtToM;
        const double stripe0 = (disp_m > 8.0 ? kDisplacedThresholdBarFt * geo::kFtToM : 0.0) + 1.0;
        const double stripe1 = stripe0 + 150.0 * geo::kFtToM;
        const double span = half - 2.0;
        for (int i = 0; i < nb; ++i) {
            const double t = (i + 0.5) / nb;
            const double x = -span + t * 2.0 * span;
            if (std::fabs(x) < 3.0)
                continue; // keep the centerline gap
            mark_rect(mesh, lat, lon, hdg, elev, stripe0, stripe1, x - bar_w * 0.5, x + bar_w * 0.5,
                      0.97f, 0.97f, 0.95f);
        }
        // Designators just past the bars (photo: big "4R").
        paint_ident(mesh, lat, lon, hdg, elev, stripe1 + 10.0, ident);
        // AC 150/5340-1M: aiming point starts 1020 ft, 150×30 ft, 72 ft inner.
        const double aim0 = kAimingPointStartFt * geo::kFtToM;
        const double aim1 = aim0 + kAimingPointLenFt * geo::kFtToM;
        const double aim_in = 0.5 * kAimingPointInnerFt * geo::kFtToM;
        const double aim_out = aim_in + kAimingPointWidthFt * geo::kFtToM;
        if (rw.length_m > aim1 + 80.0) {
            mark_rect(mesh, lat, lon, hdg, elev, aim0, aim1, aim_in, aim_out, 0.96f, 0.96f, 0.94f);
            mark_rect(mesh, lat, lon, hdg, elev, aim0, aim1, -aim_out, -aim_in, 0.96f, 0.96f, 0.94f);
        }
        // TDZ 3-2-2-1-1 at 500/1500/2000/2500/3000 ft (aiming point occupies 1000).
        const double bar_len = 75.0 * geo::kFtToM;
        const double tdz_w = 6.0 * geo::kFtToM;
        const double tdz_gap = 5.0 * geo::kFtToM;
        const double tdz_inner = 36.0 * geo::kFtToM;
        for (int s = 0; s < 5; ++s) {
            const double d = kTdzStationFt[s] * geo::kFtToM;
            if (d + bar_len >= rw.length_m * 0.45)
                break;
            const int nbar = kTdzBarCounts[s];
            for (int side = -1; side <= 1; side += 2) {
                for (int b = 0; b < nbar; ++b) {
                    const double inner = tdz_inner + b * (tdz_w + tdz_gap);
                    const double x0 = side > 0 ? inner : -(inner + tdz_w);
                    const double x1 = side > 0 ? inner + tdz_w : -inner;
                    mark_rect(mesh, lat, lon, hdg, elev, d, d + bar_len, x0, x1, 0.95f, 0.95f,
                              0.93f);
                }
            }
        }
    };
    one_end(rw.le_lat, rw.le_lon, rw.heading_deg, rw.ident_le, rw.le_displaced_ft);
    one_end(rw.he_lat, rw.he_lon, wrap360(rw.heading_deg + 180.0), rw.ident_he, rw.he_displaced_ft);
    // Rubber in the first 900 m of each end only — a full-length 0.10
    // strip read as a black blob at the departure end from 0.6 nm.
    auto rubber = [&](double lat, double lon, double hdg, double elev) {
        const double reach = std::min(900.0, rw.length_m * 0.35);
        double elat, elon;
        offset_m(lat, lon, hdg, reach, elat, elon);
        strip_quad(mesh, lat, lon, elat, elon, hdg, half * 0.32, elev + 0.42, elev + 0.42, 0.26f,
                   0.25f, 0.24f, 0.f);
    };
    rubber(rw.le_lat, rw.le_lon, rw.heading_deg, rw.le_elev_m);
    rubber(rw.he_lat, rw.he_lon, wrap360(rw.heading_deg + 180.0), rw.he_elev_m);
}

double light_alt_m(double lat, double lon, double runway_elev_m) {
    const double ground = session_ground_m(lat, lon, runway_elev_m);
    const float cover = geo::land_coverage(lat, lon);
    if (cover < 0.35f)
        return std::max(ground, 0.25) + 2.4; // ALS pier over water
    // Elevated HIRL / CL / taxi fixtures sit above the asphalt so a 3°
    // short-final does not bury them in the pavement depth buffer.
    return std::max(ground, runway_elev_m) + 0.85;
}

void push_light(Airport& a, double lat, double lon, double alt_m, float r, float g, float b,
                int sw, LightKind kind, int runway_k = 0, int seq = -1, int papi_box = -1,
                float papi_gs = 3.f) {
    LightPoint p;
    p.lat = lat;
    p.lon = lon;
    p.alt_m = alt_m;
    p.r = r;
    p.g = g;
    p.b = b;
    p.switch_num = sw;
    p.kind = kind;
    p.runway_k = runway_k;
    p.seq = seq;
    p.papi_box = papi_box;
    p.papi_gs_deg = papi_gs;
    a.lights.push_back(p);
}

void bar(Airport& a, double lat, double lon, double hdg, double alt_m, double half_span, int n,
         float r, float g, float b, int sw, LightKind kind, int runway_k = 0) {
    if (n < 1)
        return;
    for (int i = 0; i < n; ++i) {
        const double t = n == 1 ? 0.0 : (i / static_cast<double>(n - 1) - 0.5) * 2.0;
        double plat, plon;
        offset_m(lat, lon, hdg + 90.0, t * half_span, plat, plon);
        push_light(a, plat, plon, light_alt_m(plat, plon, alt_m), r, g, b, sw, kind, runway_k);
    }
}

void along_surveyed(const Runway& rw, double d_from_le_m, double& lat, double& lon) {
    const double t = rw.length_m > 1.0 ? d_from_le_m / rw.length_m : 0.0;
    geo::direct_wgs84(rw.le_lat, rw.le_lon, rw.heading_deg, t * rw.length_m, lat, lon);
    // Land exactly on the surveyed HE when we ask for the far end.
    if (t >= 1.0 - 1e-12) {
        lat = rw.he_lat;
        lon = rw.he_lon;
    }
}

void set_bidir(Airport& a, float r, float g, float b, float r2, float g2, float b2, float hdg) {
    auto& L = a.lights.back();
    L.r = r;
    L.g = g;
    L.b = b;
    L.r2 = r2;
    L.g2 = g2;
    L.b2 = b2;
    L.color_hdg_deg = hdg;
}

void set_unidir_last(Airport& a, int n, float hdg) {
    const int N = static_cast<int>(a.lights.size());
    for (int i = 0; i < n && i < N; ++i) {
        auto& L = a.lights[static_cast<std::size_t>(N - 1 - i)];
        L.r2 = 0.f;
        L.g2 = 0.f;
        L.b2 = 0.f;
        L.color_hdg_deg = hdg;
    }
}

void hirL_pair(double rem_ahead_m, double caution_m, float& r, float& g, float& b) {
    if (rem_ahead_m <= caution_m) {
        r = 1.f;
        g = 0.78f;
        b = 0.15f;
    } else {
        r = 1.f;
        g = 1.f;
        b = 0.85f;
    }
}

void rcl_pair(double rem_ahead_m, int i, float& r, float& g, float& b) {
    const double red_m = 1000.0 * geo::kFtToM;
    const double alt_m = 3000.0 * geo::kFtToM;
    if (rem_ahead_m <= red_m) {
        r = 1.f;
        g = 0.18f;
        b = 0.12f;
    } else if (rem_ahead_m <= alt_m && (i % 2) == 0) {
        r = 1.f;
        g = 0.18f;
        b = 0.12f;
    } else {
        r = 1.f;
        g = 1.f;
        b = 0.95f;
    }
}

void lights_for_runway(Airport& a, const Runway& rw, int k) {
    const int sw_app_a = lights::runway_switch(k, lights::kOffApproachA);
    const int sw_strobe_a = lights::runway_switch(k, lights::kOffStrobeA);
    const int sw_edge = lights::runway_switch(k, lights::kOffEdge);
    const int sw_cl = lights::runway_switch(k, lights::kOffCenterline);
    const int sw_vasi = lights::runway_switch(k, lights::kOffVasi);
    const int sw_tdz = lights::runway_switch(k, lights::kOffTdz);
    const int sw_app_b = lights::runway_switch(k, lights::kOffApproachB);
    const int sw_strobe_b = lights::runway_switch(k, lights::kOffStrobeB);
    const double step_edge = 60.0; // HIRL ~200 ft
    const double step_cl = 15.0;   // CAT II/III centerline
    const double caution_m = std::min(2000.0 * geo::kFtToM, rw.length_m * 0.5);
    const int n_edge = std::max(2, static_cast<int>(std::floor(rw.length_m / step_edge)));
    const double le_disp_m = rw.le_displaced_ft * geo::kFtToM;
    const double he_disp_m = rw.he_displaced_ft * geo::kFtToM;
    for (int i = 0; i <= n_edge; ++i) {
        const double d = rw.length_m * i / n_edge;
        float r_he, g_he, b_he, r_le, g_le, b_le;
        hirL_pair(rw.length_m - d, caution_m, r_he, g_he, b_he); // looking toward HE
        hirL_pair(d, caution_m, r_le, g_le, b_le);               // looking toward LE
        // AC 150/5340-30J 2.3.2.1.2: displaced-area edge lights are red
        // toward the approach and keep the runway colour the other way.
        if (le_disp_m > 8.0 && d < le_disp_m - 1.0) {
            r_he = 1.f;
            g_he = 0.12f;
            b_he = 0.10f;
        }
        if (he_disp_m > 8.0 && d > rw.length_m - he_disp_m + 1.0) {
            r_le = 1.f;
            g_le = 0.12f;
            b_le = 0.10f;
        }
        double clat, clon;
        along_surveyed(rw, d, clat, clon);
        const double elev = rw.le_elev_m + (rw.he_elev_m - rw.le_elev_m) * (d / rw.length_m);
        double llat, llon, rlat, rlon;
        offset_m(clat, clon, rw.heading_deg - 90.0, rw.width_m * 0.5, llat, llon);
        offset_m(clat, clon, rw.heading_deg + 90.0, rw.width_m * 0.5, rlat, rlon);
        push_light(a, llat, llon, light_alt_m(llat, llon, elev), r_he, g_he, b_he, sw_edge,
                   LightKind::Edge, k);
        set_bidir(a, r_he, g_he, b_he, r_le, g_le, b_le, static_cast<float>(rw.heading_deg));
        push_light(a, rlat, rlon, light_alt_m(rlat, rlon, elev), r_he, g_he, b_he, sw_edge,
                   LightKind::Edge, k);
        set_bidir(a, r_he, g_he, b_he, r_le, g_le, b_le, static_cast<float>(rw.heading_deg));
    }

    const int n_cl = std::max(2, static_cast<int>(std::floor(rw.length_m / step_cl)));
    for (int i = 0; i <= n_cl; ++i) {
        const double d = rw.length_m * i / n_cl;
        float r_he, g_he, b_he, r_le, g_le, b_le;
        rcl_pair(rw.length_m - d, i, r_he, g_he, b_he);
        rcl_pair(d, i, r_le, g_le, b_le);
        double clat, clon;
        along_surveyed(rw, d, clat, clon);
        const double elev = rw.le_elev_m + (rw.he_elev_m - rw.le_elev_m) * (d / rw.length_m);
        push_light(a, clat, clon, light_alt_m(clat, clon, elev), r_he, g_he, b_he, sw_cl,
                   LightKind::Centerline, k);
        set_bidir(a, r_he, g_he, b_he, r_le, g_le, b_le, static_cast<float>(rw.heading_deg));
    }

    auto landing = [&](bool le, double& lat, double& lon, double& hdg, double& elev, double& disp_m) {
        lat = le ? rw.le_lat : rw.he_lat;
        lon = le ? rw.le_lon : rw.he_lon;
        hdg = le ? rw.heading_deg : rw.heading_he_deg;
        elev = le ? rw.le_elev_m : rw.he_elev_m;
        disp_m = (le ? rw.le_displaced_ft : rw.he_displaced_ft) * geo::kFtToM;
        if (disp_m > 0.5)
            offset_m(lat, lon, hdg, disp_m, lat, lon);
    };

    auto end_bar = [&](bool le, int sw) {
        double lat, lon, hdg, elev, disp_m;
        landing(le, lat, lon, hdg, elev, disp_m);
        const std::string sys = le ? rw.approach_le : rw.approach_he;
        int n_green = 8;
        if (sys == "ALSF-2") {
            // JO 6850.2C Fig 2-1: green lamps on 5 ft centres, 45 ft past each edge.
            const double half = rw.width_m * 0.5 + 45.0 * geo::kFtToM;
            n_green = std::max(9, static_cast<int>(std::lround((2.0 * half) / (5.0 * geo::kFtToM))) + 1);
            bar(a, lat, lon, hdg, elev, half, n_green, 0.15f, 1.f, 0.25f, sw, LightKind::End, k);
        } else {
            bar(a, lat, lon, hdg, elev, rw.width_m * 0.5, n_green, 0.15f, 1.f, 0.25f, sw,
                LightKind::End, k);
        }
        // Green toward the approach (unidirectional). r2=0 = off from behind.
        set_unidir_last(a, n_green, static_cast<float>(hdg));
        // Red end lights face the runway, so a short-final does not see
        // red mixed into the green bar or the opposite-end green bar.
        bar(a, le ? rw.le_lat : rw.he_lat, le ? rw.le_lon : rw.he_lon, hdg, elev, rw.width_m * 0.5, 6,
            1.f, 0.12f, 0.1f, sw, LightKind::End, k);
        set_unidir_last(a, 6, static_cast<float>(wrap360(hdg + 180.0)));
    };
    end_bar(true, sw_tdz);
    end_bar(false, sw_tdz);

    auto tdz_bars = [&](bool le, bool want) {
        if (!want)
            return;
        double lat, lon, hdg, elev, disp_m;
        landing(le, lat, lon, hdg, elev, disp_m);
        // AC 150/5340-30J Fig A-35: two 3-lamp barrettes, inner lamp 36 ft
        // from CL, 5 ft spacing, 30 bars every 100 ft from 100–3000 ft.
        const double inner = 36.0 * geo::kFtToM;
        const double lamp = 5.0 * geo::kFtToM;
        for (int i = 1; i <= 30; ++i) {
            const double d = 100.0 * i * geo::kFtToM;
            double clat, clon;
            offset_m(lat, lon, hdg, d, clat, clon);
            for (int side = -1; side <= 1; side += 2) {
                double blat, blon;
                offset_m(clat, clon, hdg + 90.0 * side, inner + lamp, blat, blon);
                bar(a, blat, blon, hdg, elev, lamp, 3, 1.f, 1.f, 0.85f, sw_tdz, LightKind::Threshold,
                    k);
            }
        }
    };
    tdz_bars(true, rw.tdz_le);
    tdz_bars(false, rw.tdz_he);

    auto papi = [&](bool le) {
        const std::string side = le ? rw.papi_le : rw.papi_he;
        if (side.empty() || side == "NONE")
            return;
        const int nsys = le ? rw.papi_count_le : rw.papi_count_he;
        const double gs = le ? rw.papi_deg_le : rw.papi_deg_he;
        const double tch = le ? rw.vgsi_tch_ft_le : rw.vgsi_tch_ft_he;
        double lat, lon, hdg, elev, disp_m;
        landing(le, lat, lon, hdg, elev, disp_m);
        const bool right = side.find("P4R") != std::string::npos && side.find("P4L") == std::string::npos;
        const double sdir = right ? 1.0 : -1.0;
        const double along0 = lights::papi_distance_ft(tch, gs) * geo::kFtToM;
        const double inboard = 50.0 * geo::kFtToM; // JO 6850.2C §506.a, jet +25/−0
        const double box_sp = 30.0 * geo::kFtToM;  // §506.b
        const float rot0 = static_cast<float>(le ? rw.papi_rotate_le : rw.papi_rotate_he);
        const float ul = static_cast<float>(le ? rw.papi_usable_left_le : rw.papi_usable_left_he);
        const float ur = static_cast<float>(le ? rw.papi_usable_right_le : rw.papi_usable_right_he);
        const int n_place = std::max(1, nsys);
        for (int s = 0; s < n_place; ++s) {
            double plat, plon;
            offset_m(lat, lon, hdg, along0 + s * 30.0, plat, plon);
            const float rot = (s == 0) ? rot0 : 0.f;
            for (int i = 0; i < 4; ++i) {
                double qlat, qlon;
                const double x = sdir * (rw.width_m * 0.5 + inboard + i * box_sp + s * 8.0);
                offset_m(plat, plon, hdg + 90.0, x, qlat, qlon);
                // On-path default: inner red, outer white (overridden by the eye).
                const float cr = i < 2 ? 1.f : 0.15f;
                const float cg = i < 2 ? 0.12f : 1.f;
                // Optical center 2 ft AGL — not light_alt_m+0.9 (5.7 ft),
                // which pushed TCH 5.7 ft above the published figure.
                const double papi_alt = elev + lights::kPapiOpticalHtFt * geo::kFtToM;
                push_light(a, qlat, qlon, papi_alt, cr, cg, 0.15f, sw_vasi, LightKind::Vasi, k, -1,
                           i, static_cast<float>(gs));
                auto& L = a.lights.back();
                L.papi_rotate_deg = rot;
                L.papi_usable_left_deg = ul;
                L.papi_usable_right_deg = ur;
                L.papi_rwy_hdg_deg = static_cast<float>(hdg);
            }
        }
    };
    papi(true);
    papi(false);

    auto reil = [&](bool le, int sw) {
        if (!(le ? rw.reil_le : rw.reil_he))
            return;
        double lat, lon, hdg, elev, disp_m;
        landing(le, lat, lon, hdg, elev, disp_m);
        double llat, llon, rlat, rlon;
        offset_m(lat, lon, hdg + 90.0, rw.width_m * 0.5 + 8.0, rlat, rlon);
        offset_m(lat, lon, hdg - 90.0, rw.width_m * 0.5 + 8.0, llat, llon);
        push_light(a, llat, llon, light_alt_m(llat, llon, elev), 1.f, 1.f, 1.f, sw, LightKind::Reil,
                   k);
        push_light(a, rlat, rlon, light_alt_m(rlat, rlon, elev), 1.f, 1.f, 1.f, sw, LightKind::Reil,
                   k);
    };
    reil(true, sw_strobe_a);
    reil(false, sw_strobe_b);

    auto als = [&](bool le, int sw, int sw_strobe) {
        const std::string sys = le ? rw.approach_le : rw.approach_he;
        if (sys.empty() || sys == "NONE")
            return;
        double lat, lon, hdg, elev, disp_m;
        landing(le, lat, lon, hdg, elev, disp_m);
        const double back = wrap360(hdg + 180.0);
        // JO 6850.2C Fig 2-1: 5 lamps at 40½ in → bar 13.5 ft, half-span 6.75 ft.
        const double cl_half = 2.0 * 40.5 * 0.0254;
        auto bar5 = [&](double dist_ft, LightKind kind, int seq = -1) {
            double plat, plon;
            offset_m(lat, lon, back, dist_ft * geo::kFtToM, plat, plon);
            bar(a, plat, plon, hdg, light_alt_m(plat, plon, elev), cl_half, 5, 1.f, 1.f, 0.92f, sw,
                kind, k);
            if (seq >= 0) {
                // SFL / RAIL ride the strobe circuit, not the steady ALS switch.
                push_light(a, plat, plon, light_alt_m(plat, plon, elev) + 0.3, 1.f, 1.f, 1.f,
                           sw_strobe, LightKind::Flasher, k, seq);
            }
        };
        auto rlls_13l = [&]() {
            // Chart Supplement 13L only: 5 SFL 1200–2000 ft already sit on
            // the ALSF-2 CL; plus a 5-SFL group ~1 NM out, gate 35° south.
            auto d180 = [](double h) {
                double x = std::fabs(h - 180.0);
                return x > 180.0 ? 360.0 - x : x;
            };
            const double plus = wrap360(back + 35.0);
            const double minus = wrap360(back - 35.0);
            const double gate = d180(plus) < d180(minus) ? plus : minus;
            for (int i = 0; i < 5; ++i) {
                double plat, plon;
                offset_m(lat, lon, gate, (6076.0 - 200.0 * i) * geo::kFtToM, plat, plon);
                push_light(a, plat, plon, light_alt_m(plat, plon, elev) + 0.3, 1.f, 1.f, 1.f,
                           sw_strobe, LightKind::Flasher, k, i);
            }
        };
        if (sys == "ALSF-2") {
            // 24 centreline bars 100–2400 ft; flashers on stations 10–24
            // (1000–2400 ft), seq 0 = outermost.
            for (int i = 1; i <= 24; ++i)
                bar5(100.0 * i, LightKind::Approach, (i >= 10) ? (24 - i) : -1);
            // 1000 ft bar: CL 5 already placed; 8 white each side at 5 ft
            // from 15 to 50 ft off CL → 21 lamps, 100 ft wide.
            double xlat, xlon;
            offset_m(lat, lon, back, 1000.0 * geo::kFtToM, xlat, xlon);
            for (int side = -1; side <= 1; side += 2) {
                for (int j = 0; j < 8; ++j) {
                    double blat, blon;
                    offset_m(xlat, xlon, hdg + 90.0 * side, (15.0 + 5.0 * j) * geo::kFtToM, blat,
                             blon);
                    push_light(a, blat, blon, light_alt_m(blat, blon, elev), 1.f, 1.f, 0.92f, sw,
                               LightKind::Approach, k);
                }
            }
            // 500 ft bar: 4 white each side at 5 ft, between CL bar and side row.
            double mlat, mlon;
            offset_m(lat, lon, back, 500.0 * geo::kFtToM, mlat, mlon);
            for (int side = -1; side <= 1; side += 2) {
                for (int j = 0; j < 4; ++j) {
                    double blat, blon;
                    offset_m(mlat, mlon, hdg + 90.0 * side, (15.0 + 5.0 * j) * geo::kFtToM, blat,
                             blon);
                    push_light(a, blat, blon, light_alt_m(blat, blon, elev), 1.f, 1.f, 0.92f, sw,
                               LightKind::Approach, k);
                }
            }
            // Red side rows: 9 stations 100–900 ft, 3 lamps at 5 ft, inner 36 ft.
            for (int i = 1; i <= 9; ++i) {
                double slat, slon;
                offset_m(lat, lon, back, 100.0 * i * geo::kFtToM, slat, slon);
                for (int side = -1; side <= 1; side += 2) {
                    double blat, blon;
                    offset_m(slat, slon, hdg + 90.0 * side, (36.0 + 5.0) * geo::kFtToM, blat, blon);
                    bar(a, blat, blon, hdg, light_alt_m(blat, blon, elev), 5.0 * geo::kFtToM, 3, 1.f,
                        0.15f, 0.1f, sw, LightKind::Approach, k);
                }
            }
            if (le ? rw.rlls_lead_in_le : rw.rlls_lead_in_he)
                rlls_13l();
        } else if (sys == "ALSF-1") {
            for (int i = 1; i <= 24; ++i)
                bar5(100.0 * i, LightKind::Approach, (i >= 10) ? (24 - i) : -1);
            double xlat, xlon;
            offset_m(lat, lon, back, 1000.0 * geo::kFtToM, xlat, xlon);
            bar(a, xlat, xlon, hdg, elev, 50.0 * geo::kFtToM, 21, 1.f, 1.f, 0.92f, sw,
                LightKind::Approach, k);
        } else if (sys == "MALSR") {
            // JO 6850.2C Fig 2-3/2-4: 7 MALS bars 200–1400 ft, 2.5 ft
            // spacing; 66 ft crossbar at 1000 ft; RAIL = 5 single flashers.
            const double mals_half = 5.0 * geo::kFtToM; // 5 lamps at 2.5 ft
            for (int i = 1; i <= 7; ++i) {
                if (i == 5)
                    continue; // 1000 ft is the wide crossbar
                double plat, plon;
                offset_m(lat, lon, back, 200.0 * i * geo::kFtToM, plat, plon);
                bar(a, plat, plon, hdg, light_alt_m(plat, plon, elev), mals_half, 5, 1.f, 1.f, 0.92f,
                    sw, LightKind::Approach, k);
            }
            double xlat, xlon;
            offset_m(lat, lon, back, 1000.0 * geo::kFtToM, xlat, xlon);
            // JO 6850.2C Fig 2-3: 1000 ft bar = CL 5-lamp + two 5-lamp
            // side bars, 66 ft overall — not 27 even lamps.
            bar(a, xlat, xlon, hdg, light_alt_m(xlat, xlon, elev), mals_half, 5, 1.f, 1.f, 0.92f, sw,
                LightKind::Approach, k);
            for (int side = -1; side <= 1; side += 2) {
                double blat, blon;
                offset_m(xlat, xlon, hdg + 90.0 * side, 28.0 * geo::kFtToM, blat, blon);
                bar(a, blat, blon, hdg, light_alt_m(blat, blon, elev), mals_half, 5, 1.f, 1.f, 0.92f,
                    sw, LightKind::Approach, k);
            }
            for (int i = 0; i < 5; ++i) {
                double plat, plon;
                offset_m(lat, lon, back, (1600.0 + 200.0 * i) * geo::kFtToM, plat, plon);
                // seq 0 = outermost so the RAIL runs toward the runway.
                push_light(a, plat, plon, light_alt_m(plat, plon, elev) + 0.3, 1.f, 1.f, 1.f,
                           sw_strobe, LightKind::Flasher, k, 4 - i);
            }
        } else if (sys == "MALSF") {
            for (int i = 1; i <= 7; ++i)
                bar5(200.0 * i, LightKind::Approach);
            for (int i = 0; i < 5; ++i) {
                double plat, plon;
                offset_m(lat, lon, back, (1400.0 + 200.0 * i) * geo::kFtToM, plat, plon);
                push_light(a, plat, plon, light_alt_m(plat, plon, elev) + 0.3, 1.f, 1.f, 1.f,
                           sw_strobe, LightKind::Flasher, k, 4 - i);
            }
        } else if (sys == "MALS") {
            for (int i = 1; i <= 7; ++i)
                bar5(200.0 * i, LightKind::Approach);
        } else if (sys == "SALS") {
            for (int i = 1; i <= 7; ++i)
                bar5(200.0 * i, LightKind::Approach);
        } else if (sys == "CAT-I") {
            for (int i = 1; i <= 10; ++i)
                bar5(300.0 * i, LightKind::Approach);
        } else if (sys == "CAT-II") {
            for (int i = 1; i <= 15; ++i)
                bar5(200.0 * i, LightKind::Approach, (i >= 8) ? (15 - i) : -1);
            double xlat, xlon;
            offset_m(lat, lon, back, 1000.0 * geo::kFtToM, xlat, xlon);
            bar(a, xlat, xlon, hdg, elev, 18.0, 11, 1.f, 1.f, 0.92f, sw, LightKind::Approach, k);
        } else if (sys == "RLLS") {
            // 13R published data is RLLS only — 5 CL flashers sequenced
            // toward the threshold. Do not copy the 13L 35°/1 NM remark
            // or invent 5-lamp steady bars.
            for (int i = 0; i < 5; ++i) {
                double plat, plon;
                offset_m(lat, lon, back, (1200.0 + 200.0 * i) * geo::kFtToM, plat, plon);
                push_light(a, plat, plon, light_alt_m(plat, plon, elev) + 0.3, 1.f, 1.f, 1.f,
                           sw_strobe, LightKind::Flasher, k, 4 - i);
            }
        } else {
            bar5(1400.0, LightKind::Approach);
        }
    };
    als(true, sw_app_a, sw_strobe_a);
    als(false, sw_app_b, sw_strobe_b);
}

void lights_for_taxi(Airport& a) {
    for (const auto& tw : a.taxiways) {
        // OSM ways are pavement. Lighting them (every 30 m, both edges + CL)
        // turned a KJFK bake into a 67k-lamp night lattice and a sky wireframe
        // when Overpass highways were mis-read as taxiways.
        if (tw.from_osm || tw.name == "osm")
            continue;
        if (tw.path.size() < 2)
            continue;
        for (std::size_t s = 1; s < tw.path.size(); ++s) {
            const double a0 = tw.path[s - 1].first, o0 = tw.path[s - 1].second;
            const double a1 = tw.path[s].first, o1 = tw.path[s].second;
            const double dlat = (a1 - a0) * geo::kDeg * geo::kWgsA;
            const double dlon = (o1 - o0) * geo::kDeg * geo::kWgsA * std::cos(geo::deg2rad(a0));
            const double len = std::sqrt(dlat * dlat + dlon * dlon);
            const double hdg = geo::rad2deg(std::atan2(dlon, dlat));
            const int n = std::max(1, static_cast<int>(std::floor(len / 30.0)));
            for (int i = 0; i <= n; ++i) {
                const double t = i / static_cast<double>(n);
                const double lat = a0 + (a1 - a0) * t;
                const double lon = o0 + (o1 - o0) * t;
                double elat, elon, clat = lat, clon = lon;
                // FAA AC 150/5340-30: taxi edge aviation blue, CL aviation green.
                offset_m(lat, lon, hdg + 90.0, tw.width_m * 0.5, elat, elon);
                push_light(a, elat, elon, light_alt_m(elat, elon, a.elev_m), 0.15f, 0.40f, 1.00f,
                           a.sw_taxi_edge, LightKind::TaxiEdge);
                offset_m(lat, lon, hdg - 90.0, tw.width_m * 0.5, elat, elon);
                push_light(a, elat, elon, light_alt_m(elat, elon, a.elev_m), 0.15f, 0.40f, 1.00f,
                           a.sw_taxi_edge, LightKind::TaxiEdge);
                if (i % 2 == 0)
                    push_light(a, clat, clon, light_alt_m(clat, clon, a.elev_m), 0.10f, 1.00f, 0.22f,
                               a.sw_taxi_centerline, LightKind::TaxiCenterline);
            }
            if (s == 1) {
                bar(a, a0, o0, hdg, a.elev_m, tw.width_m * 0.45, 5, 1.f, 0.12f, 0.1f, a.sw_stop_bar,
                    LightKind::StopBar);
            }
        }
    }
}

Runway runway_from_faa_pair(const FaaEnd& le, const FaaEnd& he) {
    Runway r;
    r.ident_le = le.ident;
    r.ident_he = he.ident;
    r.le_lat = le.lat;
    r.le_lon = le.lon;
    r.le_elev_m = le.elev_ft * geo::kFtToM;
    r.he_lat = he.lat;
    r.he_lon = he.lon;
    r.he_elev_m = he.elev_ft * geo::kFtToM;
    r.length_m = geo::inverse_wgs84(le.lat, le.lon, he.lat, he.lon, &r.heading_deg);
    geo::inverse_wgs84(he.lat, he.lon, le.lat, le.lon, &r.heading_he_deg);
    r.width_m = le.width_ft * geo::kFtToM;
    r.approach_le = le.als;
    r.approach_he = he.als;
    r.le_displaced_ft = le.displaced_ft;
    r.he_displaced_ft = he.displaced_ft;
    r.papi_le = le.papi;
    r.papi_he = he.papi;
    r.papi_deg_le = le.papi_deg;
    r.papi_deg_he = he.papi_deg;
    r.vgsi_tch_ft_le = le.vgsi_tch_ft;
    r.vgsi_tch_ft_he = he.vgsi_tch_ft;
    r.ils_tch_ft_le = le.ils_tch_ft;
    r.ils_tch_ft_he = he.ils_tch_ft;
    r.papi_rotate_le = le.papi_rotate_deg;
    r.papi_rotate_he = he.papi_rotate_deg;
    r.papi_usable_left_le = le.papi_usable_left_deg;
    r.papi_usable_right_le = le.papi_usable_right_deg;
    r.papi_usable_left_he = he.papi_usable_left_deg;
    r.papi_usable_right_he = he.papi_usable_right_deg;
    r.papi_count_le = (le.papi.find('+') != std::string::npos) ? 2 : 1;
    r.papi_count_he = (he.papi.find('+') != std::string::npos) ? 2 : 1;
    r.rlls_lead_in_le = le.rlls_lead_in;
    r.rlls_lead_in_he = he.rlls_lead_in;
    r.reil_le = le.reil;
    r.reil_he = he.reil;
    r.tdz_le = le.tdz;
    r.tdz_he = he.tdz;
    r.emas_le = le.emas;
    r.emas_he = he.emas;
    r.emas_le_len_ft = le.emas_len_ft;
    r.emas_he_len_ft = he.emas_len_ft;
    r.emas_wid_ft = std::max(le.emas_wid_ft, he.emas_wid_ft);
    r.source.name = le.source;
    r.source.effective = le.effective;
    r.source.retrieved = le.retrieved;
    return r;
}

Airport airport_from_faa_ref(const FaaAirportRef& faa) {
    Airport a;
    a.icao = faa.icao;
    a.name = faa.name;
    a.attribution = faa.source + " effective " + faa.effective + " retrieved " + faa.retrieved;
    a.arp_lat = faa.arp_lat;
    a.arp_lon = faa.arp_lon;
    a.elev_m = faa.elev_ft * geo::kFtToM;
    a.source.name = faa.source;
    a.source.effective = faa.effective;
    a.source.retrieved = faa.retrieved;
    for (std::size_t i = 0; i + 1 < faa.ends.size(); i += 2)
        a.runways.push_back(runway_from_faa_pair(faa.ends[i], faa.ends[i + 1]));
    return a;
}

Taxiway parallel_taxi(const char* name, const Runway& rw, double perp_m) {
    Taxiway t;
    t.name = name;
    t.width_m = 23;
    double a_lat, a_lon, b_lat, b_lon;
    offset_m(rw.le_lat, rw.le_lon, rw.heading_deg + 90.0, perp_m, a_lat, a_lon);
    offset_m(rw.he_lat, rw.he_lon, rw.heading_deg + 90.0, perp_m, b_lat, b_lon);
    t.path = {{a_lat, a_lon}, {b_lat, b_lon}};
    return t;
}

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

Apron box_apron(const char* name, double lat, double lon, double hdg, double len_m, double wid_m,
                double elev_unused) {
    (void)elev_unused;
    Apron a;
    a.name = name;
    ring_box(a.ring, lat, lon, hdg, len_m, wid_m);
    return a;
}

Building box_building(const char* name, double lat, double lon, double hdg, double len_m,
                      double wid_m, float height_m) {
    Building b;
    b.name = name;
    b.height_m = height_m;
    b.kind = BuildingKind::Airport;
    ring_box(b.ring, lat, lon, hdg, len_m, wid_m);
    return b;
}

} // namespace

bool is_airport_structure(const Building& b) {
    if (b.kind == BuildingKind::Airport)
        return true;
    if (b.name.empty() || b.name == "osm")
        return false;
    return true;
}

void building_centroid(const Building& b, double& lat, double& lon) {
    lat = lon = 0;
    if (b.ring.empty())
        return;
    for (const auto& p : b.ring) {
        lat += p.first;
        lon += p.second;
    }
    lat /= static_cast<double>(b.ring.size());
    lon /= static_cast<double>(b.ring.size());
}

void building_palette(const Building& b, float& wr, float& wg, float& wb, float& rr, float& rg,
                      float& rb) {
    wr = 0.62f;
    wg = 0.58f;
    wb = 0.52f;
    rr = 0.28f;
    rg = 0.30f;
    rb = 0.32f;
    const std::string key = b.type + " " + b.name;
    auto has = [&](const char* s) { return key.find(s) != std::string::npos; };
    if (has("hangar")) {
        wr = 0.48f;
        wg = 0.50f;
        wb = 0.54f;
        rr = 0.22f;
        rg = 0.23f;
        rb = 0.25f;
    } else if (has("cargo") || has("industrial") || has("warehouse")) {
        wr = 0.55f;
        wg = 0.50f;
        wb = 0.42f;
        rr = 0.34f;
        rg = 0.28f;
        rb = 0.20f;
    } else if (has("tower") || has("control_tower")) {
        wr = 0.72f;
        wg = 0.74f;
        wb = 0.78f;
        rr = 0.40f;
        rg = 0.42f;
        rb = 0.45f;
    } else if (has("residential") || has("house") || has("apartments") || has("detached") ||
               has("terrace") || has("semidetached_house")) {
        wr = 0.58f;
        wg = 0.42f;
        wb = 0.36f;
        rr = 0.32f;
        rg = 0.22f;
        rb = 0.18f;
    } else if (has("commercial") || has("retail") || has("office") || has("hotel")) {
        wr = 0.52f;
        wg = 0.56f;
        wb = 0.60f;
        rr = 0.24f;
        rg = 0.26f;
        rb = 0.28f;
    } else if (has("garage") || has("garages") || has("parking") || has("transportation")) {
        wr = 0.50f;
        wg = 0.51f;
        wb = 0.53f;
        rr = 0.30f;
        rg = 0.30f;
        rb = 0.31f;
    } else if (has("roof") || has("grandstand") || has("stadium")) {
        wr = 0.44f;
        wg = 0.46f;
        wb = 0.48f;
        rr = 0.18f;
        rg = 0.20f;
        rb = 0.22f;
    } else if (has("terminal") || has("T1") || has("T4") || has("T5") || has("T7") || has("T8") ||
               has("ITB") || has("TBIT") || has("main-term") || has("train_station")) {
        wr = 0.78f;
        wg = 0.74f;
        wb = 0.66f;
        rr = 0.22f;
        rg = 0.24f;
        rb = 0.26f;
    }
    float h = 0.f;
    for (unsigned char c : key)
        h = std::fmod(h * 31.f + static_cast<float>(c), 997.f);
    // Wider jitter so a campus of `building=yes` is not one beige slab.
    const float j = (h / 997.f - 0.5f) * 0.14f;
    const float j2 = std::fmod(h * 7.f, 97.f) / 97.f - 0.5f;
    wr = std::max(0.12f, std::min(0.86f, wr + j));
    wg = std::max(0.12f, std::min(0.86f, wg + j * 0.6f + j2 * 0.04f));
    wb = std::max(0.12f, std::min(0.86f, wb + j * 0.4f));
    rr = std::max(0.10f, std::min(0.55f, rr + j * 0.5f));
    rg = std::max(0.10f, std::min(0.55f, rg + j * 0.35f));
    rb = std::max(0.10f, std::min(0.55f, rb + j * 0.25f));
}

Building jetbridge_building(const Jetbridge& j) {
    Building b;
    b.name = j.gate.empty() ? "jetbridge" : j.gate;
    b.height_m = kJetbridgeDeckM;
    b.kind = BuildingKind::Airport;
    double nlat = 0, nlon = 0;
    offset_m(j.lat, j.lon, j.hdg_deg, j.length_m, nlat, nlon);
    double l0lat, l0lon, r0lat, r0lon, l1lat, l1lon, r1lat, r1lon;
    offset_m(j.lat, j.lon, j.hdg_deg - 90.0, kJetbridgeHalfWm, l0lat, l0lon);
    offset_m(j.lat, j.lon, j.hdg_deg + 90.0, kJetbridgeHalfWm, r0lat, r0lon);
    offset_m(nlat, nlon, j.hdg_deg - 90.0, kJetbridgeHalfWm, l1lat, l1lon);
    offset_m(nlat, nlon, j.hdg_deg + 90.0, kJetbridgeHalfWm, r1lat, r1lon);
    b.ring = {{l0lat, l0lon}, {r0lat, r0lon}, {r1lat, r1lon}, {l1lat, l1lon}};
    return b;
}

void offset_m(double lat, double lon, double heading_deg, double dist_m, double& olat,
              double& olon) {
    geo::direct_wgs84(lat, lon, heading_deg, dist_m, olat, olon);
}

const char* light_kind_name(LightKind k) {
    switch (k) {
    case LightKind::Edge:
        return "edge";
    case LightKind::Centerline:
        return "centerline";
    case LightKind::Approach:
        return "approach";
    case LightKind::Threshold:
        return "threshold";
    case LightKind::Strobe:
        return "strobe";
    case LightKind::Vasi:
        return "vasi";
    case LightKind::TaxiEdge:
        return "taxi_edge";
    case LightKind::StopBar:
        return "stop_bar";
    case LightKind::TaxiCenterline:
        return "taxi_centerline";
    case LightKind::Flasher:
        return "flasher";
    case LightKind::Reil:
        return "reil";
    case LightKind::Rwsl:
        return "rwsl";
    case LightKind::End:
        return "end";
    case LightKind::LeadOn:
        return "lead_on";
    case LightKind::SignGlow:
        return "sign_glow";
    case LightKind::Beacon:
        return "beacon";
    case LightKind::Night:
        return "night";
    }
    return "?";
}

void decorate_simple(Airport& a, const char* taxi0, double perp0, const char* taxi1, double perp1,
                     const char* apron, double alat, double alon, double hdg, double alen,
                     double awid, const char* bldg, float bh) {
    if (!a.runways.empty()) {
        a.taxiways.push_back(parallel_taxi(taxi0, a.runways[0], perp0));
        if (taxi1 && a.runways.size() > 1)
            a.taxiways.push_back(parallel_taxi(taxi1, a.runways[1], perp1));
    }
    a.aprons.push_back(box_apron(apron, alat, alon, hdg, alen, awid, a.elev_m));
    a.buildings.push_back(box_building(bldg, alat, alon, hdg, alen * 0.4, 80, bh));
}

Airport ksea_public() {
    Airport a = airport_from_faa_ref(faa_ksea());
    a.taxiways.push_back(parallel_taxi("T", a.runways[0], 90.0));
    a.taxiways.push_back(parallel_taxi("B", a.runways[1], 80.0));
    a.aprons.push_back(box_apron("main", a.arp_lat, a.arp_lon + 0.004, 90.0, 800, 400, a.elev_m));
    a.buildings.push_back(box_building("main-term", a.arp_lat, a.arp_lon + 0.004, 90.0, 420, 90, 22.f));
    generate_lights(a);
    return a;
}

Airport ksfo_public() {
    Airport a = airport_from_faa_ref(faa_ksfo());
    a.taxiways.push_back(parallel_taxi("A", a.runways[0], 80.0));
    a.taxiways.push_back(parallel_taxi("B", a.runways[1], -80.0));
    a.taxiways.push_back(parallel_taxi("C", a.runways[2], 70.0));
    a.aprons.push_back(box_apron("terminal", 37.6165, -122.3860, 30.0, 900, 500, a.elev_m));
    a.buildings.push_back(box_building("ITB", 37.6165, -122.3860, 30.0, 380, 80, 24.f));
    generate_lights(a);
    return a;
}

Airport klas_public() {
    Airport a = airport_from_faa_ref(faa_klas());
    a.taxiways.push_back(parallel_taxi("A", a.runways[0], 80.0));
    a.taxiways.push_back(parallel_taxi("B", a.runways[1], -80.0));
    a.taxiways.push_back(parallel_taxi("S", a.runways[2], 70.0));
    a.aprons.push_back(box_apron("D-gates", 36.0845, -115.1520, 90.0, 1100, 450, a.elev_m));
    a.buildings.push_back(box_building("D", 36.0845, -115.1520, 90.0, 500, 80, 20.f));
    generate_lights(a);
    return a;
}

Airport klax_public() {
    Airport a = airport_from_faa_ref(faa_klax());
    a.taxiways.push_back(parallel_taxi("B", a.runways[0], 80.0));
    a.taxiways.push_back(parallel_taxi("C", a.runways[1], -80.0));
    a.taxiways.push_back(parallel_taxi("S", a.runways[2], 70.0));
    a.aprons.push_back(box_apron("TBIT", 33.9435, -118.4085, 69.0, 1200, 500, a.elev_m));
    a.buildings.push_back(box_building("TBIT", 33.9435, -118.4085, 69.0, 520, 90, 28.f));
    generate_lights(a);
    return a;
}

Airport kjfk_public() {
    Airport a = airport_from_faa_ref(faa_kjfk());
    a.taxiways.push_back(parallel_taxi("A", a.runways[0], 80.0));
    // Taxi B sits ~80 m left of 04R (toward 04L). Its green CL is the
    // line left of 04R at night — not a misplaced runway edge.
    a.taxiways.push_back(parallel_taxi("B", a.runways[1], -80.0));
    a.taxiways.push_back(parallel_taxi("Z", a.runways[3], 80.0));
    a.aprons.push_back(box_apron("T4", 40.6445, -73.7780, 40.0, 1000, 450, a.elev_m));
    a.buildings.push_back(box_building("T4", 40.6445, -73.7780, 40.0, 380, 90, 28.f));
    a.buildings.push_back(box_building("T5", 40.6410, -73.7870, 121.0, 300, 80, 26.f));
    a.buildings.push_back(box_building("T8", 40.6468, -73.7895, 121.0, 280, 80, 24.f));
    a.buildings.push_back(box_building("T1", 40.6438, -73.7908, 121.0, 260, 80, 24.f));
    a.buildings.push_back(box_building("T7", 40.6488, -73.7832, 40.0, 240, 75, 22.f));
    a.buildings.push_back(box_building("hangar", 40.6515, -73.7940, 121.0, 180, 70, 16.f));
    a.buildings.push_back(box_building("cargo", 40.6550, -73.7720, 40.0, 200, 80, 14.f));
    add_kjfk_public_scenery(a);
    generate_lights(a);
    return a;
}

Airport kewr_public() {
    Airport a = airport_from_faa_ref(faa_kewr());
    a.taxiways.push_back(parallel_taxi("A", a.runways[0], 80.0));
    a.taxiways.push_back(parallel_taxi("B", a.runways[1], -80.0));
    a.taxiways.push_back(parallel_taxi("S", a.runways[2], 70.0));
    a.aprons.push_back(box_apron("A-gates", 40.6895, -74.1740, 26.0, 900, 420, a.elev_m));
    a.buildings.push_back(box_building("A", 40.6895, -74.1740, 26.0, 360, 80, 22.f));
    a.buildings.push_back(box_building("B", 40.6920, -74.1775, 26.0, 280, 70, 20.f));
    a.buildings.push_back(box_building("C", 40.6955, -74.1680, 95.0, 300, 75, 20.f));
    a.buildings.push_back(box_building("tower", 40.6928, -74.1688, 26.0, 40, 40, 55.f));
    a.buildings.push_back(box_building("hangar", 40.6985, -74.1785, 26.0, 160, 60, 16.f));
    a.tower_lat = 40.6928;
    a.tower_lon = -74.1688;
    a.tower_h_m = 55.f;
    a.has_beacon = true;
    add_kewr_public_scenery(a);
    generate_lights(a);
    return a;
}

Airport airport_from_faa(const std::string& icao) {
    const FaaAirportRef* ref = faa_airport(icao);
    if (!ref)
        throw std::runtime_error("no official FAA/AIP both-end layout for " + icao);
    Airport a = airport_from_faa_ref(*ref);
    generate_lights(a);
    return a;
}

Airport kbos_public() {
    Airport a = airport_from_faa_ref(faa_kbos());
    a.taxiways.push_back(parallel_taxi("N", a.runways[0], 80.0));
    a.taxiways.push_back(parallel_taxi("E", a.runways[1], -80.0));
    a.aprons.push_back(box_apron("A-gates", 42.3668, -71.0205, 50.0, 900, 420, a.elev_m));
    a.buildings.push_back(box_building("A", 42.3668, -71.0205, 50.0, 360, 80, 22.f));
    a.buildings.push_back(box_building("B", 42.3642, -71.0178, 50.0, 240, 70, 18.f));
    a.buildings.push_back(box_building("E", 42.3695, -71.0228, 150.0, 280, 70, 20.f));
    generate_lights(a);
    return a;
}

Airport kiad_public() {
    Airport a = airport_from_faa_ref(faa_kiad());
    decorate_simple(a, "A", 80.0, "B", -80.0, "midfield", a.arp_lat, a.arp_lon, 1.0, 900, 420,
                    "main", 22.f);
    generate_lights(a);
    return a;
}

Airport kmco_public() {
    Airport a = airport_from_faa_ref(faa_kmco());
    decorate_simple(a, "A", 80.0, "B", -80.0, "airside", a.arp_lat, a.arp_lon, 180.0, 1000, 450,
                    "main", 22.f);
    generate_lights(a);
    return a;
}

Airport kmia_public() {
    Airport a = airport_from_faa_ref(faa_kmia());
    decorate_simple(a, "M", 80.0, "N", -80.0, "terminal", a.arp_lat, a.arp_lon, 87.0, 1100, 450,
                    "main", 22.f);
    generate_lights(a);
    return a;
}

Airport krno_public() {
    Airport a = airport_from_faa_ref(faa_krno());
    decorate_simple(a, "A", 80.0, "B", -80.0, "terminal", a.arp_lat, a.arp_lon, 180.0, 700, 350,
                    "main", 18.f);
    generate_lights(a);
    return a;
}

Airport eddf_public() {
    Airport a = airport_from_faa_ref(faa_eddf());
    decorate_simple(a, "N", 80.0, "S", -80.0, "terminal", a.arp_lat, a.arp_lon, 70.0, 1200, 500,
                    "T1", 24.f);
    generate_lights(a);
    return a;
}

Airport egll_public() {
    Airport a = airport_from_faa_ref(faa_egll());
    decorate_simple(a, "A", 80.0, "S", -80.0, "central", a.arp_lat, a.arp_lon, 90.0, 1100, 450,
                    "T5", 24.f);
    generate_lights(a);
    return a;
}

Airport lebl_public() {
    Airport a = airport_from_faa_ref(faa_lebl());
    decorate_simple(a, "T", 80.0, "S", -80.0, "T1", a.arp_lat, a.arp_lon, 66.0, 900, 400, "T1",
                    20.f);
    generate_lights(a);
    return a;
}

Airport seqm_public() {
    Airport a = airport_from_faa_ref(faa_seqm());
    decorate_simple(a, "A", 80.0, nullptr, 0, "terminal", a.arp_lat, a.arp_lon, 180.0, 800, 380,
                    "main", 18.f);
    generate_lights(a);
    return a;
}

Airport skbo_public() {
    Airport a = airport_from_faa_ref(faa_skbo());
    decorate_simple(a, "A", 80.0, "B", -80.0, "terminal", a.arp_lat, a.arp_lon, 127.0, 1000, 420,
                    "T1", 20.f);
    generate_lights(a);
    return a;
}

Airport skrg_public() {
    Airport a = airport_from_faa_ref(faa_skrg());
    decorate_simple(a, "A", 80.0, nullptr, 0, "terminal", a.arp_lat, a.arp_lon, 360.0, 700, 350,
                    "main", 16.f);
    generate_lights(a);
    return a;
}

Airport vnkt_public() {
    Airport a = airport_from_faa_ref(faa_vnkt());
    decorate_simple(a, "A", 70.0, nullptr, 0, "terminal", a.arp_lat, a.arp_lon, 22.0, 600, 300,
                    "main", 16.f);
    generate_lights(a);
    return a;
}

Airport airport_by_icao(const std::string& icao) {
    std::string id = icao;
    for (char& c : id)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (id == "KSEA")
        return ksea_public();
    if (id == "KSFO")
        return ksfo_public();
    if (id == "KLAS")
        return klas_public();
    if (id == "KLAX")
        return klax_public();
    if (id == "KJFK")
        return kjfk_public();
    if (id == "KEWR")
        return kewr_public();
    if (id == "KBOS")
        return kbos_public();
    if (id == "KIAD")
        return kiad_public();
    if (id == "KMCO")
        return kmco_public();
    if (id == "KMIA")
        return kmia_public();
    if (id == "KRNO")
        return krno_public();
    if (id == "EDDF")
        return eddf_public();
    if (id == "EGLL")
        return egll_public();
    if (id == "LEBL")
        return lebl_public();
    if (id == "SEQM")
        return seqm_public();
    if (id == "SKBO")
        return skbo_public();
    if (id == "SKRG")
        return skrg_public();
    if (id == "VNKT")
        return vnkt_public();
    throw std::runtime_error("no built-in layout for " + icao);
}

std::vector<Airport> all_public_airports() {
    return {ksea_public(), ksfo_public(), klas_public(),  klax_public(), kjfk_public(),
            kewr_public(), kbos_public(), kiad_public(),  kmco_public(), kmia_public(),
            krno_public(), eddf_public(), egll_public(),  lebl_public(), seqm_public(),
            skbo_public(), skrg_public(), vnkt_public()};
}

namespace {

bool landing_threshold(const Airport& a, const std::string& ident, double& lat, double& lon,
                       double& elev_m, double& hdg) {
    std::string id = ident;
    for (char& c : id)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (const auto& rw : a.runways) {
        const bool le = rw.ident_le == id;
        const bool he = rw.ident_he == id;
        if (!le && !he)
            continue;
        lat = le ? rw.le_lat : rw.he_lat;
        lon = le ? rw.le_lon : rw.he_lon;
        hdg = le ? rw.heading_deg
                 : (rw.heading_he_deg > 0.0 ? rw.heading_he_deg : wrap360(rw.heading_deg + 180.0));
        const double disp_m = (le ? rw.le_displaced_ft : rw.he_displaced_ft) * geo::kFtToM;
        if (disp_m > 0.5)
            offset_m(lat, lon, hdg, disp_m, lat, lon);
        elev_m = le ? rw.le_elev_m : rw.he_elev_m;
        return true;
    }
    return false;
}

} // namespace

bool runway_shot_pose(const Airport& a, const std::string& ident, double dist_nm, ShotPose& out) {
    double lat0 = 0, lon0 = 0, elev_m = 0, hdg = 0;
    if (!landing_threshold(a, ident, lat0, lon0, elev_m, hdg))
        return false;
    std::string id = ident;
    for (char& c : id)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    const double elev_ft = elev_m * geo::kMToFt;
    out.icao = a.icao;
    out.rwy = id;
    out.dist_nm = dist_nm;
    out.hdg_true_deg = hdg;
    if (dist_nm <= 0.01) {
        // Lined up looking at the designators (threshold bars + 4R ahead).
        offset_m(lat0, lon0, hdg, 12.0, out.lat, out.lon);
        out.alt_ft = elev_ft + 14.0;
        out.pitch_deg = 0;
    } else {
        const double back = wrap360(hdg + 180.0);
        offset_m(lat0, lon0, back, dist_nm * 1852.0, out.lat, out.lon);
        out.alt_ft = elev_ft + 14.0 + dist_nm * 318.0; // 3°
        out.pitch_deg = -3.0;
    }
    return true;
}

bool plan_approach(const std::string& spec, double gs_deg, double start_agl_ft, ApproachPlan& out) {
    if (gs_deg < 0.25 || gs_deg > 10.0)
        return false;
    if (start_agl_ft < 20.0 || start_agl_ft > 20000.0)
        return false;
    std::string s = spec;
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    const auto c1 = s.find(':');
    if (c1 == std::string::npos || c1 == 0 || c1 + 1 >= s.size())
        return false;
    const auto c2 = s.find(':', c1 + 1);
    out.icao = s.substr(0, c1);
    out.rwy = c2 == std::string::npos ? s.substr(c1 + 1) : s.substr(c1 + 1, c2 - c1 - 1);
    if (out.rwy.empty())
        return false;
    const Airport a = airport_by_icao(out.icao);
    if (!landing_threshold(a, out.rwy, out.thr_lat, out.thr_lon, out.thr_elev_m, out.hdg_deg))
        return false;
    out.gs_deg = gs_deg;
    out.start_agl_ft = start_agl_ft;
    const double gs_rad = gs_deg * geo::kDeg;
    out.start_dist_m = (start_agl_ft * geo::kFtToM) / std::tan(gs_rad);
    return out.start_dist_m > 1.0;
}

double approach_time_s(const ApproachPlan& p) {
    const double gs_rad = std::max(0.25, p.gs_deg) * geo::kDeg;
    const double path_m = (p.start_agl_ft * geo::kFtToM) / std::sin(gs_rad);
    const double v = std::max(1.0, p.kts) * 1852.0 / 3600.0;
    return path_m / v;
}

int approach_frame_count(const ApproachPlan& p) {
    const double fps = std::max(1.0, p.fps);
    return std::max(2, static_cast<int>(std::lround(approach_time_s(p) * fps)) + 1);
}

ShotPose approach_sample(const ApproachPlan& p, double t_s) {
    const double T = approach_time_s(p);
    const double u = T <= 0.0 ? 1.0 : std::min(1.0, std::max(0.0, t_s / T));
    const double back = p.start_dist_m * (1.0 - u);
    ShotPose s;
    s.icao = p.icao;
    s.rwy = p.rwy;
    s.dist_nm = back / 1852.0;
    s.hdg_true_deg = p.hdg_deg;
    s.pitch_deg = -p.gs_deg;
    s.roll_deg = 0;
    s.alt_ft = p.thr_elev_m * geo::kMToFt + p.start_agl_ft * (1.0 - u);
    offset_m(p.thr_lat, p.thr_lon, wrap360(p.hdg_deg + 180.0), back, s.lat, s.lon);
    return s;
}

ShotPose parse_shot_at(const std::string& spec) {
    std::string s = spec;
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    ShotPose p;
    const auto c1 = s.find(':');
    if (c1 == std::string::npos) {
        p.icao = s;
        const Airport a = airport_by_icao(p.icao);
        p.lat = a.arp_lat;
        p.lon = a.arp_lon;
        p.alt_ft = a.elev_m * geo::kMToFt + 50.0;
        p.hdg_true_deg = a.runways.empty() ? 0.0 : a.runways[0].heading_deg;
        p.pitch_deg = 0;
        p.roll_deg = 0;
        return p;
    }
    p.icao = s.substr(0, c1);
    const auto c2 = s.find(':', c1 + 1);
    p.rwy = c2 == std::string::npos ? s.substr(c1 + 1) : s.substr(c1 + 1, c2 - c1 - 1);
    p.dist_nm = 0;
    if (c2 != std::string::npos)
        p.dist_nm = std::stod(s.substr(c2 + 1));
    const Airport a = airport_by_icao(p.icao);
    if (!runway_shot_pose(a, p.rwy, p.dist_nm, p))
        throw std::runtime_error("no runway " + p.rwy + " at " + p.icao);
    return p;
}

void apply_shot_overrides(ShotPose& p, const ShotOverrides& o) {
    if (o.have_hdg)
        p.hdg_true_deg = wrap360(o.hdg_deg);
    if (o.have_alt)
        p.alt_ft = o.alt_ft;
    if (o.have_bank)
        p.roll_deg = o.bank_deg;
    if (o.have_pitch)
        p.pitch_deg = o.pitch_deg;
    // ICAO-only + high alt: stand off opposite heading so the field is ahead
    // (e.g. --at KJFK --alt 12000 --hdg 036).
    if (p.rwy.empty() && o.have_alt && p.alt_ft > 400.0) {
        const Airport a = airport_by_icao(p.icao);
        const double elev_ft = a.elev_m * geo::kMToFt;
        const double agl = p.alt_ft - elev_ft;
        if (agl > 400.0) {
            // Cockpit 12k photos: ocean, then Rockaway, then JFK, lots of sky.
            // ~11 nm stand-off, nearly level (horizon ~mid-low in a 32.5° VFOV).
            const double back_nm = std::min(11.0, agl / 1100.0);
            offset_m(a.arp_lat, a.arp_lon, wrap360(p.hdg_true_deg + 180.0), back_nm * 1852.0, p.lat,
                     p.lon);
            if (!o.have_pitch)
                p.pitch_deg = -std::min(2.0, agl / 8000.0);
        }
    }
}

std::string overpass_aeroway_query(double south, double west, double north, double east) {
    std::ostringstream o;
    o << "[out:json][timeout:40];(way[\"aeroway\"=\"taxiway\"](" << south << "," << west << ","
      << north << "," << east << ");way[\"aeroway\"=\"apron\"](" << south << "," << west << ","
      << north << "," << east << ");way[\"building\"](" << south << "," << west << "," << north
      << "," << east << ");way[\"natural\"=\"water\"](" << south << "," << west << "," << north
      << "," << east << ");way[\"natural\"=\"coastline\"](" << south << "," << west << "," << north
      << "," << east << ");way[\"highway\"=\"motorway\"](" << south << "," << west << "," << north
      << "," << east << ");way[\"highway\"=\"trunk\"](" << south << "," << west << "," << north
      << "," << east << ");way[\"highway\"=\"primary\"](" << south << "," << west << "," << north
      << "," << east << ");way[\"highway\"=\"secondary\"](" << south << "," << west << "," << north
      << "," << east << "););out geom;";
    return o.str();
}

namespace {

std::size_t json_skip_ws(const std::string& s, std::size_t i) {
    while (i < s.size() &&
           (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t'))
        ++i;
    return i;
}

// Brace match that ignores braces inside JSON strings.
std::size_t json_match_object(const std::string& s, std::size_t open) {
    if (open >= s.size() || s[open] != '{')
        return std::string::npos;
    int depth = 0;
    bool in_str = false;
    for (std::size_t i = open; i < s.size(); ++i) {
        const char c = s[i];
        if (in_str) {
            if (c == '\\' && i + 1 < s.size()) {
                ++i;
                continue;
            }
            if (c == '"')
                in_str = false;
            continue;
        }
        if (c == '"') {
            in_str = true;
            continue;
        }
        if (c == '{')
            ++depth;
        else if (c == '}') {
            --depth;
            if (depth == 0)
                return i;
        }
    }
    return std::string::npos;
}

// Overpass `out geom` emits tags AFTER the geometry array. Test fixtures
// and GeoJSON-ish samples put `"tags"` first. Take the object that belongs
// to this way only — never the next element's tags.
std::string osm_way_tags(const std::string& json, std::size_t geom_key, std::size_t geom_end) {
    if (geom_end < json.size()) {
        std::size_t i = json_skip_ws(json, geom_end + 1);
        if (i < json.size() && json[i] == ',') {
            i = json_skip_ws(json, i + 1);
            if (i + 6 <= json.size() && json.compare(i, 6, "\"tags\"") == 0) {
                const auto brace = json.find('{', i);
                if (brace != std::string::npos) {
                    const auto close = json_match_object(json, brace);
                    if (close != std::string::npos)
                        return json.substr(brace, close - brace + 1);
                }
            }
        }
    }
    // Tags-before-geometry (test fixtures). Scope to this way: after its
    // `"type"` and before this `"geometry"`. A file-wide find() stole the
    // previous way's tags and turned aprons/buildings into taxiways.
    const auto typ = json.rfind("\"type\"", geom_key);
    if (typ != std::string::npos) {
        const auto t = json.find("\"tags\"", typ);
        if (t != std::string::npos && t < geom_key) {
            const auto brace = json.find('{', t);
            if (brace != std::string::npos && brace < geom_key) {
                const auto close = json_match_object(json, brace);
                if (close != std::string::npos && close < geom_key)
                    return json.substr(brace, close - brace + 1);
            }
        }
    }
    // Stream slices often start at the tags `{` (nearest brace before
    // geometry) and drop the `"tags"` / `"type"` keys. Accept that object
    // only when it looks like tags, never Overpass `"bounds"`.
    const auto nearest = json.rfind('{', geom_key);
    if (nearest != std::string::npos && nearest < geom_key) {
        const auto close = json_match_object(json, nearest);
        if (close != std::string::npos && close < geom_key) {
            const auto cand = json.substr(nearest, close - nearest + 1);
            if (cand.find("\"building\"") != std::string::npos ||
                cand.find("\"aeroway\"") != std::string::npos ||
                cand.find("\"highway\"") != std::string::npos ||
                cand.find("\"water\"") != std::string::npos ||
                cand.find("coastline") != std::string::npos)
                return cand;
        }
    }
    return {};
}

// Stream slices used to end at the geometry `]`, dropping Overpass tags.
// Returns npos when the way may still continue past the current buffer.
std::size_t osm_include_trailing_tags(const std::string& s, std::size_t geom_end) {
    std::size_t i = json_skip_ws(s, geom_end + 1);
    if (i >= s.size())
        return std::string::npos;
    if (s[i] == '}')
        return geom_end;
    if (s[i] != ',')
        return geom_end;
    i = json_skip_ws(s, i + 1);
    if (i >= s.size())
        return std::string::npos;
    if (i + 6 > s.size())
        return std::string::npos;
    if (s.compare(i, 6, "\"tags\"") != 0)
        return geom_end;
    const auto brace = s.find('{', i);
    if (brace == std::string::npos)
        return std::string::npos;
    const auto close = json_match_object(s, brace);
    if (close == std::string::npos)
        return std::string::npos;
    return close;
}

} // namespace

std::string overpass_aeroway_url(double south, double west, double north, double east) {
    const std::string q = overpass_aeroway_query(south, west, north, east);
    std::ostringstream o;
    o << "https://overpass-api.de/api/interpreter?data=";
    for (unsigned char c : q) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '=' ||
            c == ';' || c == ',' || c == '(' || c == ')' || c == '/' || c == ':') {
            o << c;
        } else {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            o << buf;
        }
    }
    return o.str();
}

int merge_osm_json(Airport& a, const std::string& json) {
    int added = 0;
    std::size_t pos = 0;
    while (true) {
        auto g = json.find("\"geometry\"", pos);
        if (g == std::string::npos)
            break;
        auto arr = json.find('[', g);
        if (arr == std::string::npos)
            break;
        int depth = 0;
        std::size_t end = arr;
        for (; end < json.size(); ++end) {
            if (json[end] == '[')
                ++depth;
            else if (json[end] == ']') {
                --depth;
                if (depth == 0)
                    break;
            }
        }
        std::string kind = "taxiway";
        float bldg_h = kDefaultBuildingHeightM;
        const std::string tags = osm_way_tags(json, g, end);
        if (tags.find("\"building\"") != std::string::npos)
            kind = "building";
        else if (osm_tags_are_road(tags))
            kind = "road";
        else if (tags.find("apron") != std::string::npos)
            kind = "apron";
        else if (tags.find("\"water\"") != std::string::npos ||
                 tags.find("coastline") != std::string::npos)
            kind = "skip";
        auto read_num = [&](const char* key) -> float {
            const std::string k = std::string("\"") + key + "\"";
            auto at = tags.find(k);
            if (at == std::string::npos)
                return 0.f;
            auto colon = tags.find(':', at + k.size());
            if (colon == std::string::npos)
                return 0.f;
            const char* p = tags.c_str() + colon + 1;
            while (*p == ' ' || *p == '\t' || *p == '"')
                ++p;
            return static_cast<float>(std::strtod(p, nullptr));
        };
        const float tagged_h = read_num("height");
        if (tagged_h >= 3.f) {
            bldg_h = tagged_h;
        } else {
            const float levels = read_num("building:levels");
            if (levels >= 1.f)
                bldg_h = levels * kOsmLevelHeightM;
        }
        if (bldg_h < 3.f)
            bldg_h = kDefaultBuildingHeightM;
        const std::string geom = json.substr(arr, end - arr + 1);
        std::vector<std::pair<double, double>> pts;
        std::size_t p = 0;
        while (true) {
            auto latp = geom.find("\"lat\"", p);
            auto lonp = geom.find("\"lon\"", p);
            if (latp == std::string::npos || lonp == std::string::npos)
                break;
            auto lat_colon = geom.find(':', latp);
            auto lon_colon = geom.find(':', lonp);
            if (lat_colon == std::string::npos || lon_colon == std::string::npos)
                break;
            pts.push_back({std::strtod(geom.c_str() + lat_colon + 1, nullptr),
                           std::strtod(geom.c_str() + lon_colon + 1, nullptr)});
            p = std::max(latp, lonp) + 5;
        }
        auto tag_str = [&](const char* key) -> std::string {
            const std::string k = std::string("\"") + key + "\"";
            auto at = tags.find(k);
            if (at == std::string::npos)
                return {};
            auto colon = tags.find(':', at + k.size());
            if (colon == std::string::npos)
                return {};
            auto q1 = tags.find('"', colon);
            if (q1 == std::string::npos)
                return {};
            auto q2 = tags.find('"', q1 + 1);
            if (q2 == std::string::npos)
                return {};
            return tags.substr(q1 + 1, q2 - q1 - 1);
        };
        if (pts.size() >= 2 && kind != "skip") {
            if (kind == "building") {
                Building b;
                b.name = "osm";
                b.type = tag_str("building");
                if (b.type == "yes" || b.type == "no")
                    b.type.clear();
                b.ring = std::move(pts);
                b.height_m = bldg_h;
                a.buildings.push_back(std::move(b));
            } else if (kind == "apron") {
                Apron ap;
                ap.name = "osm-apron";
                ap.ring = std::move(pts);
                a.aprons.push_back(std::move(ap));
            } else if (kind == "road") {
                Road rd;
                rd.name = "osm";
                rd.cls = parse_osm_highway(tags);
                rd.path = std::move(pts);
                finalize_road(rd);
                a.roads.push_back(std::move(rd));
            } else {
                Taxiway t;
                t.name = "osm";
                t.path = std::move(pts);
                t.from_osm = true;
                a.taxiways.push_back(std::move(t));
            }
            ++added;
        }
        pos = end + 1;
    }
    return added;
}

int try_merge_osm_file(Airport& a, const std::string& path) {
    return merge_osm_file_stream(a, path, 2ull * 1024ull * 1024ull);
}

int merge_osm_file_stream(Airport& a, const std::string& path, std::uint64_t max_bytes,
                          SolidBvh* solids) {
    if (path.empty() || !std::filesystem::exists(path) || !std::filesystem::is_regular_file(path))
        return 0;
    const auto sz = std::filesystem::file_size(path);
    if (max_bytes > 0 && sz > max_bytes)
        return 0;
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return 0;
    int added = 0;
    std::string carry;
    carry.reserve(256 * 1024);
    char buf[64 * 1024];
    while (in) {
        in.read(buf, sizeof(buf));
        const auto n = static_cast<std::size_t>(in.gcount());
        if (n == 0)
            break;
        carry.append(buf, n);
        std::size_t pos = 0;
        while (true) {
            auto g = carry.find("\"geometry\"", pos);
            if (g == std::string::npos) {
                if (carry.size() > 16384)
                    carry.erase(0, carry.size() - 8192);
                break;
            }
            auto arr = carry.find('[', g);
            if (arr == std::string::npos) {
                carry.erase(0, g);
                break;
            }
            int depth = 0;
            std::size_t end = arr;
            bool done = false;
            for (; end < carry.size(); ++end) {
                if (carry[end] == '[')
                    ++depth;
                else if (carry[end] == ']') {
                    --depth;
                    if (depth == 0) {
                        done = true;
                        break;
                    }
                }
            }
            if (!done) {
                carry.erase(0, g);
                break;
            }
            const auto tagged_end = osm_include_trailing_tags(carry, end);
            if (tagged_end == std::string::npos) {
                carry.erase(0, g);
                break;
            }
            end = tagged_end;
            const auto win = carry.rfind('{', g);
            const std::size_t start = win != std::string::npos && win < g ? win : g;
            const auto chunk = carry.substr(start, end - start + 1);
            const bool is_bldg = chunk.find("\"building\"") != std::string::npos;
            const bool is_apron = chunk.find("apron") != std::string::npos;
            const bool is_road = osm_tags_are_road(chunk);
            // Same caps as write_osm_bin — a 400 MB Overpass dump must not
            // grow unbounded Airport vectors at bake time.
            if (is_bldg && a.buildings.size() >= kMaxOsmBuildings && !solids) {
                pos = end + 1;
                continue;
            }
            if (is_road && !is_bldg && a.roads.size() >= kMaxOsmRoads) {
                pos = end + 1;
                continue;
            }
            if (is_apron && !is_bldg && !is_road && a.aprons.size() >= kMaxOsmAprons) {
                pos = end + 1;
                continue;
            }
            if (!is_bldg && !is_apron && !is_road && a.taxiways.size() >= kMaxOsmTaxiways) {
                pos = end + 1;
                continue;
            }
            if (!solids && a.taxiways.size() >= kMaxOsmTaxiways &&
                a.aprons.size() >= kMaxOsmAprons && a.buildings.size() >= kMaxOsmBuildings &&
                a.roads.size() >= kMaxOsmRoads)
                return added;
            const auto b0 = a.buildings.size();
            added += merge_osm_json(a, chunk);
            if (solids) {
                for (std::size_t i = b0; i < a.buildings.size(); ++i)
                    solids->add_building(a.buildings[i], a.elev_m);
                if (a.buildings.size() > kMaxOsmBuildings)
                    a.buildings.resize(kMaxOsmBuildings);
            }
            pos = end + 1;
            if (pos > 128 * 1024) {
                carry.erase(0, pos);
                pos = 0;
            }
        }
    }
    return added;
}

namespace {

void write_u32(std::ostream& o, std::uint32_t v) {
    o.write(reinterpret_cast<const char*>(&v), 4);
}
void write_f32(std::ostream& o, float v) {
    o.write(reinterpret_cast<const char*>(&v), 4);
}
void write_f64(std::ostream& o, double v) {
    o.write(reinterpret_cast<const char*>(&v), 8);
}
bool read_u32(std::istream& i, std::uint32_t& v) {
    i.read(reinterpret_cast<char*>(&v), 4);
    return static_cast<bool>(i);
}
bool read_f32(std::istream& i, float& v) {
    i.read(reinterpret_cast<char*>(&v), 4);
    return static_cast<bool>(i);
}
bool read_f64(std::istream& i, double& v) {
    i.read(reinterpret_cast<char*>(&v), 8);
    return static_cast<bool>(i);
}

void write_poly(std::ostream& o, const std::string& name, float extra,
                const std::vector<std::pair<double, double>>& pts) {
    const auto nlen = static_cast<std::uint32_t>(std::min<std::size_t>(name.size(), 255));
    write_u32(o, nlen);
    o.write(name.data(), static_cast<std::streamsize>(nlen));
    write_f32(o, extra);
    write_u32(o, static_cast<std::uint32_t>(pts.size()));
    for (const auto& p : pts) {
        write_f64(o, p.first);
        write_f64(o, p.second);
    }
}

bool read_poly(std::istream& i, std::string& name, float& extra,
               std::vector<std::pair<double, double>>& pts) {
    std::uint32_t nlen = 0, npts = 0;
    if (!read_u32(i, nlen) || nlen > 1024)
        return false;
    name.assign(nlen, '\0');
    i.read(name.data(), static_cast<std::streamsize>(nlen));
    if (!read_f32(i, extra) || !read_u32(i, npts) || npts > 20000)
        return false;
    pts.resize(npts);
    for (auto& p : pts) {
        if (!read_f64(i, p.first) || !read_f64(i, p.second))
            return false;
    }
    return static_cast<bool>(i);
}

} // namespace

bool write_osm_bin(const Airport& a, const std::string& path) {
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent);
    std::ofstream o(path, std::ios::binary);
    if (!o)
        return false;
    o.write("S376OSM1", 8);
    write_u32(o, 2);
    const auto nt = std::min<std::size_t>(a.taxiways.size(), kMaxOsmTaxiways);
    const auto na = std::min<std::size_t>(a.aprons.size(), kMaxOsmAprons);
    const auto nb = std::min<std::size_t>(a.buildings.size(), kMaxOsmBuildings);
    const auto nr = std::min<std::size_t>(a.roads.size(), kMaxOsmRoads);
    write_u32(o, static_cast<std::uint32_t>(nt));
    write_u32(o, static_cast<std::uint32_t>(na));
    write_u32(o, static_cast<std::uint32_t>(nb));
    for (std::size_t i = 0; i < nt; ++i)
        write_poly(o, a.taxiways[i].name, static_cast<float>(a.taxiways[i].width_m), a.taxiways[i].path);
    for (std::size_t i = 0; i < na; ++i)
        write_poly(o, a.aprons[i].name, 0.f, a.aprons[i].ring);
    for (std::size_t i = 0; i < nb; ++i)
        write_poly(o, a.buildings[i].name, a.buildings[i].height_m, a.buildings[i].ring);
    write_u32(o, static_cast<std::uint32_t>(nr));
    for (std::size_t i = 0; i < nr; ++i) {
        float cls = 3.f;
        if (a.roads[i].cls == RoadClass::Motorway)
            cls = 0.f;
        else if (a.roads[i].cls == RoadClass::Trunk)
            cls = 1.f;
        else if (a.roads[i].cls == RoadClass::Primary)
            cls = 2.f;
        write_poly(o, a.roads[i].name, cls, a.roads[i].path);
    }
    return static_cast<bool>(o);
}

int read_osm_bin(Airport& a, const std::string& path) {
    std::ifstream i(path, std::ios::binary);
    if (!i)
        return 0;
    char mag[8];
    i.read(mag, 8);
    if (!i || std::memcmp(mag, "S376OSM1", 8) != 0)
        return 0;
    std::uint32_t ver = 0, nt = 0, na = 0, nb = 0;
    if (!read_u32(i, ver) || (ver != 1 && ver != 2) || !read_u32(i, nt) || !read_u32(i, na) ||
        !read_u32(i, nb))
        return 0;
    int added = 0;
    for (std::uint32_t n = 0; n < nt; ++n) {
        Taxiway t;
        float w = 23.f;
        if (!read_poly(i, t.name, w, t.path))
            break;
        t.width_m = w;
        t.from_osm = true;
        a.taxiways.push_back(std::move(t));
        ++added;
    }
    for (std::uint32_t n = 0; n < na; ++n) {
        Apron ap;
        float z = 0;
        if (!read_poly(i, ap.name, z, ap.ring))
            break;
        a.aprons.push_back(std::move(ap));
        ++added;
    }
    for (std::uint32_t n = 0; n < nb; ++n) {
        Building b;
        if (!read_poly(i, b.name, b.height_m, b.ring))
            break;
        a.buildings.push_back(std::move(b));
        ++added;
    }
    if (ver >= 2) {
        std::uint32_t nr = 0;
        if (read_u32(i, nr) && nr <= static_cast<std::uint32_t>(kMaxOsmRoads)) {
            for (std::uint32_t n = 0; n < nr; ++n) {
                Road r;
                float cls = 3.f;
                if (!read_poly(i, r.name, cls, r.path))
                    break;
                if (cls < 0.5f)
                    r.cls = RoadClass::Motorway;
                else if (cls < 1.5f)
                    r.cls = RoadClass::Trunk;
                else if (cls < 2.5f)
                    r.cls = RoadClass::Primary;
                else
                    r.cls = RoadClass::Secondary;
                finalize_road(r);
                a.roads.push_back(std::move(r));
                ++added;
            }
        }
    }
    return added;
}

int bake_osm_bin(const std::string& json_path, const std::string& bin_path) {
    Airport tmp;
    SolidBvh solids;
    // Bake tools may stream a large Overpass file; no 2 MiB cap.
    // Mesh vectors stay capped; every building goes into solids.bin.
    const int n = merge_osm_file_stream(tmp, json_path, 0, &solids);
    if (!write_osm_bin(tmp, bin_path))
        return 0;
    solids.build();
    const auto solids_path = std::filesystem::path(bin_path).parent_path() / "solids.bin";
    (void)write_solids_bin(solids, solids_path.string());
    return n;
}

int try_load_osm(Airport& a, const std::string& cache_dir) {
    std::string id = a.icao;
    std::string low = a.icao;
    for (char& c : id)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (char& c : low)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    auto folder_name = [](const std::string& path) {
        auto name = std::filesystem::path(path).parent_path().filename().string();
        for (char& c : name)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return name;
    };
    auto icao_like = [](const std::string& name) {
        if (name.size() != 4)
            return false;
        for (char c : name) {
            if (!std::isalpha(static_cast<unsigned char>(c)))
                return false;
        }
        return true;
    };
    auto pack_owner = [&](const std::string& dir) -> std::string {
        for (const char* fn : {"coverage.toml", "region.toml"}) {
            std::ifstream in(dir + "/" + fn);
            std::string line;
            while (in && std::getline(in, line)) {
                const auto pos = line.find("icao");
                if (pos == std::string::npos)
                    continue;
                const auto q1 = line.find('"', pos);
                if (q1 == std::string::npos)
                    continue;
                const auto q2 = line.find('"', q1 + 1);
                if (q2 == std::string::npos || q2 <= q1 + 1)
                    continue;
                std::string v = line.substr(q1 + 1, q2 - q1 - 1);
                for (char& c : v)
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                if (icao_like(v))
                    return v;
            }
        }
        const auto folder = folder_name(dir + "/osm.bin");
        if (icao_like(folder))
            return folder;
        std::string up = dir;
        for (char& c : up)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        std::string found;
        for (const auto& apt : all_public_airports()) {
            if (up.find(apt.icao) == std::string::npos)
                continue;
            if (!found.empty() && found != apt.icao)
                return std::string();
            found = apt.icao;
        }
        return found;
    };
    std::vector<std::string> paths;
    if (!cache_dir.empty()) {
        const auto p = cache_dir + "/osm.bin";
        // cache/KJFK and /tmp/kjfk-real are KJFK only — never merge into KSEA.
        // Anonymous bake dirs (no ICAO in the path) still load for unit tests.
        const auto owner = pack_owner(cache_dir);
        if (owner.empty() || owner == id)
            paths.push_back(p);
    } else {
        paths.push_back("cache/" + id + "/osm.bin");
        paths.push_back("cache/" + low + "/osm.bin");
    }
    for (const auto& p : paths) {
        if (!std::filesystem::exists(p))
            continue;
        const int n = read_osm_bin(a, p);
        if (n > 0) {
            const auto sp = std::filesystem::path(p).parent_path() / "solids.bin";
            if (std::filesystem::exists(sp))
                a.solids_path = sp.string();
            return n;
        }
    }
    return 0;
}

void generate_lights(Airport& a) {
    a.lights.clear();
    for (std::size_t i = 0; i < a.runways.size(); ++i)
        lights_for_runway(a, a.runways[i], static_cast<int>(i) + 1);
    lights_for_taxi(a);
    add_scenery_lights(a);
}

std::string airport_geojson(const Airport& a) {
    std::ostringstream o;
    o << std::fixed;
    o << "{\"type\":\"FeatureCollection\",\"name\":\"" << a.icao
      << "\",\"attribution\":\"" << a.attribution << "\",\"features\":[";
    bool first = true;
    auto feat = [&](const char* kind, const char* name,
                    const std::vector<std::pair<double, double>>& pts, bool ring) {
        if (pts.size() < 2)
            return;
        if (!first)
            o << ',';
        first = false;
        o << "{\"type\":\"Feature\",\"properties\":{\"aeroway\":\"" << kind << "\",\"name\":\""
          << name << "\"},\"geometry\":{\"type\":\"" << (ring ? "Polygon" : "LineString")
          << "\",\"coordinates\":";
        if (ring)
            o << '[';
        o << '[';
        for (std::size_t i = 0; i < pts.size(); ++i) {
            if (i)
                o << ',';
            o << '[' << pts[i].second << ',' << pts[i].first << ']';
        }
        if (ring && (pts.front().first != pts.back().first || pts.front().second != pts.back().second))
            o << ",[" << pts.front().second << ',' << pts.front().first << ']';
        o << ']';
        if (ring)
            o << ']';
        o << "}}";
    };
    for (const auto& rw : a.runways) {
        const std::string nm = rw.ident_le + "/" + rw.ident_he;
        std::vector<std::pair<double, double>> line = {{rw.le_lat, rw.le_lon}, {rw.he_lat, rw.he_lon}};
        feat("runway", nm.c_str(), line, false);
    }
    for (const auto& tx : a.taxiways)
        feat("taxiway", tx.name.c_str(), tx.path, false);
    for (const auto& ap : a.aprons)
        feat("apron", ap.name.c_str(), ap.ring, true);
    o << "]}";
    return o.str();
}

int write_airport_geojson(const Airport& a, const std::string& path) {
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent);
    std::ofstream out(path);
    if (!out)
        return 0;
    out << airport_geojson(a);
    return out ? 1 : 0;
}

bool on_official_pavement(const Airport& a, double lat, double lon) {
    for (const auto& rw : a.runways) {
        double hdg = 0;
        const double len = geo::inverse_wgs84(rw.le_lat, rw.le_lon, lat, lon, &hdg);
        double dh = hdg - rw.heading_deg;
        while (dh > 180.0)
            dh -= 360.0;
        while (dh < -180.0)
            dh += 360.0;
        const double along = len * std::cos(geo::deg2rad(dh));
        const double across = len * std::sin(geo::deg2rad(dh));
        if (along > -80.0 && along < rw.length_m + 80.0 &&
            std::fabs(across) < rw.width_m * 0.5 + 8.0)
            return true;
    }
    return false;
}

AirportMesh build_airport_mesh(const Airport& a, bool buildings, bool markings) {
    AirportMesh mesh;
    for (const auto& rw : a.runways) {
        strip_quad(mesh, rw.le_lat, rw.le_lon, rw.he_lat, rw.he_lon, rw.heading_deg,
                   rw.width_m * 0.5, rw.le_elev_m + 0.65, rw.he_elev_m + 0.65, 0.42f, 0.42f, 0.44f,
                   0.f);
        auto emas = [&](bool le) {
            const bool on = le ? rw.emas_le : rw.emas_he;
            const double len_ft = le ? rw.emas_le_len_ft : rw.emas_he_len_ft;
            if (!on)
                return;
            const double use_len_ft = len_ft >= 1.0 ? len_ft : 200.0;
            const double lat = le ? rw.le_lat : rw.he_lat;
            const double lon = le ? rw.le_lon : rw.he_lon;
            const double elev = le ? rw.le_elev_m : rw.he_elev_m;
            const double out = le ? wrap360(rw.heading_deg + 180.0) : rw.heading_he_deg;
            const double len_m = use_len_ft * geo::kFtToM;
            const double wid_m =
                (rw.emas_wid_ft > 1.0 ? rw.emas_wid_ft : rw.width_m * geo::kMToFt) * geo::kFtToM;
            double elat, elon;
            offset_m(lat, lon, out, len_m, elat, elon);
            strip_quad(mesh, lat, lon, elat, elon, out, wid_m * 0.5, elev + 0.38, elev + 0.38, 0.42f,
                       0.40f, 0.36f, 2.f);
        };
        emas(true);
        emas(false);
        if (markings)
            add_faa_markings(mesh, rw);
    }
    for (const auto& ap : a.aprons) {
        if (ap.ring.size() < 3)
            continue;
        // OSM apron rings are concave ramps; a centroid fan becomes slivers
        // and slabs over the official field. Imagery already shows the ramp.
        if (ap.name == "osm-apron" || ap.name.rfind("osm", 0) == 0)
            continue;
        double clat = 0, clon = 0;
        for (const auto& p : ap.ring) {
            clat += p.first;
            clon += p.second;
        }
        clat /= static_cast<double>(ap.ring.size());
        clon /= static_cast<double>(ap.ring.size());
        const geo::Ecef C = geo::lla_to_ecef(clat, clon, a.elev_m + 0.55);
        geo::Ecef e, n, u;
        geo::ecef_to_enu_axes(clat, clon, e, n, u);
        for (std::size_t i = 0; i < ap.ring.size(); ++i) {
            const auto& p0 = ap.ring[i];
            const auto& p1 = ap.ring[(i + 1) % ap.ring.size()];
            add_tri(mesh, C, geo::lla_to_ecef(p0.first, p0.second, a.elev_m + 0.55),
                    geo::lla_to_ecef(p1.first, p1.second, a.elev_m + 0.55), u, 0.40f, 0.39f, 0.38f,
                    2.f);
        }
    }
    for (const auto& tx : a.taxiways) {
        if (tx.path.size() < 2)
            continue;
        for (std::size_t i = 0; i + 1 < tx.path.size(); ++i) {
            const double lat0 = tx.path[i].first, lon0 = tx.path[i].second;
            const double lat1 = tx.path[i + 1].first, lon1 = tx.path[i + 1].second;
            const double dlat = (lat1 - lat0) * geo::kDeg * kR;
            const double dlon =
                (lon1 - lon0) * geo::kDeg * kR * std::cos(geo::deg2rad((lat0 + lat1) * 0.5));
            const double hdg = wrap360(geo::rad2deg(std::atan2(dlon, dlat)));
            strip_quad(mesh, lat0, lon0, lat1, lon1, hdg, tx.width_m * 0.5, a.elev_m + 0.58,
                       a.elev_m + 0.58, 0.38f, 0.38f, 0.36f, 2.f);
            if (markings) {
                mark_rect(mesh, lat0, lon0, hdg, a.elev_m + 0.46, 78.0, 79.2, -tx.width_m * 0.45,
                          tx.width_m * 0.45, 0.92f, 0.78f, 0.12f);
                mark_rect(mesh, lat0, lon0, hdg, a.elev_m + 0.46, 80.4, 81.6, -tx.width_m * 0.45,
                          tx.width_m * 0.45, 0.92f, 0.78f, 0.12f);
            }
        }
    }
    auto bldg_km = [&](std::size_t k, double& clat, double& clon) {
        clat = 0;
        clon = 0;
        if (a.buildings[k].ring.empty())
            return 1e9;
        for (const auto& p : a.buildings[k].ring) {
            clat += p.first;
            clon += p.second;
        }
        clat /= static_cast<double>(a.buildings[k].ring.size());
        clon /= static_cast<double>(a.buildings[k].ring.size());
        const double dlat = (clat - a.arp_lat) * 111.32;
        const double dlon = (clon - a.arp_lon) * 111.32 *
                            std::max(0.2, std::cos(geo::deg2rad(a.arp_lat)));
        return std::sqrt(dlat * dlat + dlon * dlon);
    };
    std::vector<std::size_t> order;
    order.reserve(a.buildings.size());
    for (std::size_t i = 0; i < a.buildings.size(); ++i)
        order.push_back(i);
    std::sort(order.begin(), order.end(), [&](std::size_t i, std::size_t j) {
        double ci = 0, coi = 0, cj = 0, coj = 0;
        return bldg_km(i, ci, coi) < bldg_km(j, cj, coj);
    });
    int n_campus = 0, n_city = 0;
    auto emit = [&](const Building& b, double clat, double clon, bool far) {
        if (on_official_pavement(a, clat, clon))
            return false;
        float h = b.height_m;
        if (b.kind == BuildingKind::Osm)
            h = std::min(h, 80.f);
        h = std::max(2.f, h);
        const double z0 = session_ground_m(clat, clon, a.elev_m) + kBuildingSitM;
        const double z1 = z0 + static_cast<double>(h);
        geo::Ecef e, n, u;
        geo::ecef_to_enu_axes(clat, clon, e, n, u);
        const geo::Ecef roof_c = geo::lla_to_ecef(clat, clon, z1);
        const std::size_t step = far ? std::max<std::size_t>(1, b.ring.size() / 4) : 1;
        for (std::size_t i = 0; i < b.ring.size(); i += step) {
            const auto& p0 = b.ring[i];
            const auto& p1 = b.ring[(i + step) % b.ring.size()];
            const geo::Ecef A = geo::lla_to_ecef(p0.first, p0.second, z0);
            const geo::Ecef B = geo::lla_to_ecef(p1.first, p1.second, z0);
            const geo::Ecef C = geo::lla_to_ecef(p1.first, p1.second, z1);
            const geo::Ecef D = geo::lla_to_ecef(p0.first, p0.second, z1);
            geo::Ecef wn{B.x - A.x, B.y - A.y, B.z - A.z};
            geo::Ecef nn{wn.y * u.z - wn.z * u.y, wn.z * u.x - wn.x * u.z, wn.x * u.y - wn.y * u.x};
            float wr = 0.62f, wg = 0.58f, wb = 0.52f, rr = 0.28f, rg = 0.30f, rb = 0.32f;
            building_palette(b, wr, wg, wb, rr, rg, rb);
            add_tri(mesh, A, B, C, nn, wr, wg, wb, 3.f);
            add_tri(mesh, A, C, D, nn, wr, wg, wb, 3.f);
            add_tri(mesh, roof_c, D, C, u, rr, rg, rb, 4.f);
        }
        return true;
    };
    if (buildings) {
        // Official / landmark always (terminals, hangars — few).
        for (std::size_t idx : order) {
            const auto& b = a.buildings[idx];
            if (b.kind == BuildingKind::Osm || b.ring.size() < 3)
                continue;
            double clat = 0, clon = 0;
            (void)bldg_km(idx, clat, clon);
            emit(b, clat, clon, false);
        }
        // OSM campus around the ARP, then Queens/Brooklyn/Rockaway ring.
        // Sorting only by ARP used to fill the 2500 cap with airfield sheds.
        for (std::size_t idx : order) {
            const auto& b = a.buildings[idx];
            if (b.kind != BuildingKind::Osm || b.ring.size() < 3)
                continue;
            double clat = 0, clon = 0;
            const double km = bldg_km(idx, clat, clon);
            if (km <= 2.2) {
                if (n_campus >= kMeshBuildingsNear)
                    continue;
                if (emit(b, clat, clon, false))
                    ++n_campus;
            } else if (km >= 0.8 && km <= 16.0) {
                if (n_city >= kMeshBuildingsFar)
                    continue;
                if (emit(b, clat, clon, true))
                    ++n_city;
            }
        }
    }
    for (const auto& t : a.trees) {
        if (on_official_pavement(a, t.lat, t.lon))
            continue;
        strip_quad(mesh, t.lat, t.lon, t.lat + 0.00004, t.lon, 0.0, 1.6, a.elev_m, a.elev_m + t.height_m,
                   0.18f, 0.32f, 0.14f, 4.f);
    }
    // ALS over water already sits at official light_alt_m. Crossed 0.11 m
    // pier legs merged into a black pier/bridge on a 0.6 nm 04R short-final
    // (04L ALS seen edge-on) — do not emit that mesh.
    for (const auto& p : a.parked) {
        double nlat, nlon;
        offset_m(p.lat, p.lon, p.hdg_deg, 18.0, nlat, nlon);
        strip_quad(mesh, p.lat, p.lon, nlat, nlon, p.hdg_deg, 7.0, a.elev_m + 2.5, a.elev_m + 2.5,
                   0.72f, 0.73f, 0.76f, 3.f);
    }
    for (const auto& j : a.jetbridges) {
        const Building jb = jetbridge_building(j);
        if (jb.ring.size() < 3)
            continue;
        double clat = 0, clon = 0;
        building_centroid(jb, clat, clon);
        const double z0 = session_ground_m(clat, clon, a.elev_m);
        const double z1 = z0 + static_cast<double>(jb.height_m);
        geo::Ecef e, n, u;
        geo::ecef_to_enu_axes(clat, clon, e, n, u);
        const geo::Ecef roof_c = geo::lla_to_ecef(clat, clon, z1);
        const int ns = static_cast<int>(jb.ring.size());
        for (int i = 0; i < ns; ++i) {
            const auto& p0 = jb.ring[static_cast<std::size_t>(i)];
            const auto& p1 = jb.ring[static_cast<std::size_t>((i + 1) % ns)];
            const geo::Ecef A = geo::lla_to_ecef(p0.first, p0.second, z0);
            const geo::Ecef B = geo::lla_to_ecef(p1.first, p1.second, z0);
            const geo::Ecef C = geo::lla_to_ecef(p1.first, p1.second, z1);
            const geo::Ecef D = geo::lla_to_ecef(p0.first, p0.second, z1);
            geo::Ecef wn{B.x - A.x, B.y - A.y, B.z - A.z};
            geo::Ecef nn{wn.y * u.z - wn.z * u.y, wn.z * u.x - wn.x * u.z, wn.x * u.y - wn.y * u.x};
            add_tri(mesh, A, B, C, nn, 0.55f, 0.56f, 0.58f, 3.f);
            add_tri(mesh, A, C, D, nn, 0.55f, 0.56f, 0.58f, 3.f);
            add_tri(mesh, roof_c, D, C, u, 0.40f, 0.41f, 0.43f, 4.f);
        }
    }
    return mesh;
}

} // namespace s376
