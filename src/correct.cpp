#include "s376/correct.hpp"

#include "s376/geo.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace s376 {
namespace {

double wrap360(double a) {
    while (a < 0.0)
        a += 360.0;
    while (a >= 360.0)
        a -= 360.0;
    return a;
}

void add_item(CorrectionReport& r, const std::string& icao, const char* feature, const char* img,
              const char* official, const char* action) {
    ImageryCorrection c;
    c.icao = icao;
    c.feature = feature;
    c.imagery = img;
    c.official = official;
    c.action = action;
    r.items.push_back(std::move(c));
}

struct Pix {
    int w = 0;
    int h = 0;
    std::vector<std::uint8_t>* rgb = nullptr;
};

void set_px(Pix& im, int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    if (x < 0 || y < 0 || x >= im.w || y >= im.h || !im.rgb)
        return;
    const int di = (y * im.w + x) * 3;
    (*im.rgb)[static_cast<std::size_t>(di)] = r;
    (*im.rgb)[static_cast<std::size_t>(di + 1)] = g;
    (*im.rgb)[static_cast<std::size_t>(di + 2)] = b;
}

void lerp_px(Pix& im, int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b, double t) {
    if (x < 0 || y < 0 || x >= im.w || y >= im.h || !im.rgb)
        return;
    const int di = (y * im.w + x) * 3;
    auto& p = *im.rgb;
    const double u = 1.0 - t;
    p[static_cast<std::size_t>(di)] =
        static_cast<std::uint8_t>(std::lround(u * p[static_cast<std::size_t>(di)] + t * r));
    p[static_cast<std::size_t>(di + 1)] =
        static_cast<std::uint8_t>(std::lround(u * p[static_cast<std::size_t>(di + 1)] + t * g));
    p[static_cast<std::size_t>(di + 2)] =
        static_cast<std::uint8_t>(std::lround(u * p[static_cast<std::size_t>(di + 2)] + t * b));
}

void latlon_to_px(const GeoBox& box, int w, int h, double lat, double lon, int& x, int& y) {
    x = static_cast<int>(std::lround((lon - box.west) / (box.east - box.west) * (w - 1)));
    y = static_cast<int>(std::lround((box.north - lat) / (box.north - box.south) * (h - 1)));
}

void px_to_latlon(const GeoBox& box, int w, int h, int x, int y, double& lat, double& lon) {
    lon = box.west + (box.east - box.west) * x / std::max(1, w - 1);
    lat = box.north - (box.north - box.south) * y / std::max(1, h - 1);
}

void metres_per_deg(double lat, double& mlat, double& mlon) {
    mlat = geo::kDeg * geo::kWgsA;
    mlon = geo::kDeg * geo::kWgsA * std::max(0.2, std::cos(geo::deg2rad(lat)));
}

void runway_st(const Runway& rw, double lat, double lon, double& along, double& across) {
    double mlat = 0, mlon = 0;
    metres_per_deg(0.5 * (rw.le_lat + rw.he_lat), mlat, mlon);
    const double n = (lat - rw.le_lat) * mlat;
    const double e = (lon - rw.le_lon) * mlon;
    const double h = geo::deg2rad(rw.heading_deg);
    const double ch = std::cos(h);
    const double sh = std::sin(h);
    along = n * ch + e * sh;
    across = -n * sh + e * ch;
}

bool on_official_surface(const Runway& rw, double along, double across) {
    const double half = rw.width_m * 0.5;
    if (along >= -0.5 && along <= rw.length_m + 0.5 && std::fabs(across) <= half + 0.6)
        return true;
    auto emas = [&](bool le) {
        const bool on = le ? rw.emas_le : rw.emas_he;
        const double len_ft = le ? rw.emas_le_len_ft : rw.emas_he_len_ft;
        if (!on)
            return false;
        const double len_m = (len_ft >= 1.0 ? len_ft : 200.0) * geo::kFtToM;
        const double wid =
            (rw.emas_wid_ft > 1.0 ? rw.emas_wid_ft * geo::kFtToM : rw.width_m) * 0.5 + 0.6;
        if (le)
            return along >= -len_m - 0.5 && along <= 0.5 && std::fabs(across) <= wid;
        return along >= rw.length_m - 0.5 && along <= rw.length_m + len_m + 0.5 &&
               std::fabs(across) <= wid;
    };
    return emas(true) || emas(false);
}

bool in_stale_band(const Runway& rw, double along, double across) {
    if (on_official_surface(rw, along, across))
        return false;
    const double extra = 10.0;
    const double stub = 350.0;
    if (std::fabs(across) > rw.width_m * 0.5 + extra)
        return false;
    return along >= -stub && along <= rw.length_m + stub;
}

bool looks_paint(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    const int mx = std::max(r, std::max(g, b));
    const int mn = std::min(r, std::min(g, b));
    if (mx >= 175 && (mx - mn) < 48)
        return true; // white / light-grey paint
    if (r >= 150 && g >= 110 && b <= 110 && (r + g) > 2 * b + 40)
        return true; // yellow
    return false;
}

bool looks_pavement(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    const int avg = (static_cast<int>(r) + g + b) / 3;
    const int sat = std::max(r, std::max(g, b)) - std::min(r, std::min(g, b));
    return avg >= 28 && avg <= 175 && sat < 38;
}

void paint_runway_pixels(Pix& im, const GeoBox& box, double lat0, double lon0, double lat1,
                         double lon1, double half_w_m, std::uint8_t r, std::uint8_t g,
                         std::uint8_t b) {
    if (im.w < 2 || im.h < 2 || box.east <= box.west || box.north <= box.south)
        return;
    double mlat = 0, mlon = 0;
    metres_per_deg(0.5 * (lat0 + lat1), mlat, mlon);
    int x0, y0, x1, y1;
    latlon_to_px(box, im.w, im.h, lat0, lon0, x0, y0);
    latlon_to_px(box, im.w, im.h, lat1, lon1, x1, y1);
    const double dx = static_cast<double>(x1 - x0);
    const double dy = static_cast<double>(y1 - y0);
    const int steps = std::max(2, static_cast<int>(std::hypot(dx, dy)));
    const double px_per_m_x = (im.w - 1) / ((box.east - box.west) * mlon);
    const double px_per_m_y = (im.h - 1) / ((box.north - box.south) * mlat);
    const int half_px =
        std::max(1, static_cast<int>(std::lround(half_w_m * 0.5 * (px_per_m_x + px_per_m_y))));
    for (int i = 0; i <= steps; ++i) {
        const double t = i / static_cast<double>(steps);
        const int cx = static_cast<int>(std::lround(x0 + dx * t));
        const int cy = static_cast<int>(std::lround(y0 + dy * t));
        for (int oy = -half_px; oy <= half_px; ++oy) {
            for (int ox = -half_px; ox <= half_px; ++ox)
                set_px(im, cx + ox, cy + oy, r, g, b);
        }
    }
}

void paint_mark_rect(Pix& im, const GeoBox& box, double lat, double lon, double hdg, double along0,
                     double along1, double x0, double x1, std::uint8_t r, std::uint8_t g,
                     std::uint8_t b) {
    if (im.w < 2 || im.h < 2)
        return;
    double mlat = 0, mlon = 0;
    metres_per_deg(lat, mlat, mlon);
    const double px_per_m =
        0.5 * ((im.w - 1) / ((box.east - box.west) * mlon) +
               (im.h - 1) / ((box.north - box.south) * mlat));
    const double along_len = std::fabs(along1 - along0);
    const double across_len = std::fabs(x1 - x0);
    const int n_along = std::max(2, static_cast<int>(std::lround(along_len * std::max(0.4, px_per_m))));
    const int n_across = std::max(2, static_cast<int>(std::lround(across_len * std::max(0.4, px_per_m))));
    for (int i = 0; i <= n_along; ++i) {
        const double a = along0 + (along1 - along0) * i / n_along;
        double alat, alon;
        offset_m(lat, lon, hdg, a, alat, alon);
        for (int j = 0; j <= n_across; ++j) {
            const double x = x0 + (x1 - x0) * j / n_across;
            double plat, plon;
            offset_m(alat, alon, hdg + 90.0, x, plat, plon);
            int px, py;
            latlon_to_px(box, im.w, im.h, plat, plon, px, py);
            set_px(im, px, py, r, g, b);
        }
    }
}

void paint_official_markings(Pix& im, const GeoBox& box, const Runway& rw) {
    const double half = rw.width_m * 0.5;
    constexpr std::uint8_t wr = 247, wg = 247, wb = 242;
    const double cl_h = 0.5 * kCenterlineWidthIn * 0.0254;
    paint_mark_rect(im, box, rw.le_lat, rw.le_lon, rw.heading_deg, 0, rw.length_m, half - 1.1,
                    half - 0.15, wr, wg, wb);
    paint_mark_rect(im, box, rw.le_lat, rw.le_lon, rw.heading_deg, 0, rw.length_m, -(half - 0.15),
                    -(half - 1.1), wr, wg, wb);
    for (double d = 12; d < rw.length_m - 20; d += 60.0)
        paint_mark_rect(im, box, rw.le_lat, rw.le_lon, rw.heading_deg, d,
                        std::min(d + 36.0, rw.length_m - 8), -cl_h, cl_h, wr, wg, wb);

    auto one_end = [&](double lat, double lon, double hdg, double disp_ft) {
        const double disp_m = disp_ft * geo::kFtToM;
        if (disp_m > 8.0) {
            const double step = 100.0 * geo::kFtToM;
            for (double d = 20.0 * geo::kFtToM; d < disp_m - 25.0 * geo::kFtToM; d += step) {
                paint_mark_rect(im, box, lat, lon, hdg, d, d + 18.0 * geo::kFtToM, -cl_h, cl_h, wr,
                                wg, wb);
                paint_mark_rect(im, box, lat, lon, hdg, d + 14.0 * geo::kFtToM,
                                d + 28.0 * geo::kFtToM, 2.0, 8.0, wr, wg, wb);
                paint_mark_rect(im, box, lat, lon, hdg, d + 14.0 * geo::kFtToM,
                                d + 28.0 * geo::kFtToM, -8.0, -2.0, wr, wg, wb);
            }
            offset_m(lat, lon, hdg, disp_m, lat, lon);
            const double bar = kDisplacedThresholdBarFt * geo::kFtToM;
            paint_mark_rect(im, box, lat, lon, hdg, 0.0, bar, -(half - 0.3), half - 0.3, wr, wg, wb);
        }
        const int nb = threshold_stripe_count(rw.width_m);
        const double bar_w = 5.75 * geo::kFtToM;
        const double stripe0 = (disp_m > 8.0 ? kDisplacedThresholdBarFt * geo::kFtToM : 0.0) + 1.0;
        const double stripe1 = stripe0 + 150.0 * geo::kFtToM;
        const double span = half - 2.0;
        for (int i = 0; i < nb; ++i) {
            const double t = (i + 0.5) / nb;
            const double x = -span + t * 2.0 * span;
            if (std::fabs(x) < 3.0)
                continue;
            paint_mark_rect(im, box, lat, lon, hdg, stripe0, stripe1, x - bar_w * 0.5,
                            x + bar_w * 0.5, wr, wg, wb);
        }
        const double aim0 = kAimingPointStartFt * geo::kFtToM;
        const double aim1 = aim0 + kAimingPointLenFt * geo::kFtToM;
        const double aim_in = 0.5 * kAimingPointInnerFt * geo::kFtToM;
        const double aim_out = aim_in + kAimingPointWidthFt * geo::kFtToM;
        if (rw.length_m > aim1 + 80.0) {
            paint_mark_rect(im, box, lat, lon, hdg, aim0, aim1, aim_in, aim_out, wr, wg, wb);
            paint_mark_rect(im, box, lat, lon, hdg, aim0, aim1, -aim_out, -aim_in, wr, wg, wb);
        }
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
                    paint_mark_rect(im, box, lat, lon, hdg, d, d + bar_len, x0, x1, wr, wg, wb);
                }
            }
        }
    };
    one_end(rw.le_lat, rw.le_lon, rw.heading_deg, rw.le_displaced_ft);
    one_end(rw.he_lat, rw.he_lon, wrap360(rw.heading_he_deg != 0.0 ? rw.heading_he_deg
                                                                  : rw.heading_deg + 180.0),
            rw.he_displaced_ft);
}

int blend_stale_pavement(Pix& im, const GeoBox& box, const Airport& a) {
    if (im.w < 2 || im.h < 2 || a.runways.empty() || !im.rgb)
        return 0;
    double gr = 0, gg = 0, gb = 0;
    int gn = 0;
    for (int y = 0; y < im.h; y += std::max(1, im.h / 64)) {
        for (int x = 0; x < im.w; x += std::max(1, im.w / 64)) {
            double lat, lon;
            px_to_latlon(box, im.w, im.h, x, y, lat, lon);
            bool near = false;
            for (const auto& rw : a.runways) {
                double along = 0, across = 0;
                runway_st(rw, lat, lon, along, across);
                if (std::fabs(across) <= rw.width_m * 0.5 + 80.0 && along >= -400.0 &&
                    along <= rw.length_m + 400.0) {
                    near = true;
                    break;
                }
            }
            if (near)
                continue;
            const int di = (y * im.w + x) * 3;
            gr += (*im.rgb)[static_cast<std::size_t>(di)];
            gg += (*im.rgb)[static_cast<std::size_t>(di + 1)];
            gb += (*im.rgb)[static_cast<std::size_t>(di + 2)];
            ++gn;
        }
    }
    const std::uint8_t ground_r = gn > 0 ? static_cast<std::uint8_t>(gr / gn) : 52;
    const std::uint8_t ground_g = gn > 0 ? static_cast<std::uint8_t>(gg / gn) : 68;
    const std::uint8_t ground_b = gn > 0 ? static_cast<std::uint8_t>(gb / gn) : 46;

    int n = 0;
    for (int y = 0; y < im.h; ++y) {
        for (int x = 0; x < im.w; ++x) {
            double lat, lon;
            px_to_latlon(box, im.w, im.h, x, y, lat, lon);
            bool stale = false;
            for (const auto& rw : a.runways) {
                double along = 0, across = 0;
                runway_st(rw, lat, lon, along, across);
                if (in_stale_band(rw, along, across)) {
                    stale = true;
                    break;
                }
            }
            if (!stale)
                continue;
            const int di = (y * im.w + x) * 3;
            const std::uint8_t r = (*im.rgb)[static_cast<std::size_t>(di)];
            const std::uint8_t g = (*im.rgb)[static_cast<std::size_t>(di + 1)];
            const std::uint8_t b = (*im.rgb)[static_cast<std::size_t>(di + 2)];
            if (looks_paint(r, g, b)) {
                lerp_px(im, x, y, ground_r, ground_g, ground_b, 0.90);
                ++n;
            } else if (looks_pavement(r, g, b)) {
                lerp_px(im, x, y, ground_r, ground_g, ground_b, 0.82);
                ++n;
            }
        }
    }
    return n > 0 ? 1 : 0;
}

int apply_overlay_pix(Pix& im, const GeoBox& box, const Airport& a) {
    if (im.w < 2 || im.h < 2 || !im.rgb ||
        im.rgb->size() < static_cast<std::size_t>(im.w * im.h * 3))
        return 0;
    if (box.east <= box.west || box.north <= box.south)
        return 0;
    int n = blend_stale_pavement(im, box, a);
    for (const auto& rw : a.runways) {
        paint_runway_pixels(im, box, rw.le_lat, rw.le_lon, rw.he_lat, rw.he_lon, rw.width_m * 0.5,
                            42, 42, 44);
        ++n;
        auto emas = [&](bool le) {
            const bool on = le ? rw.emas_le : rw.emas_he;
            const double len_ft = le ? rw.emas_le_len_ft : rw.emas_he_len_ft;
            if (!on)
                return;
            const double lat = le ? rw.le_lat : rw.he_lat;
            const double lon = le ? rw.le_lon : rw.he_lon;
            const double out = le ? wrap360(rw.heading_deg + 180.0) : rw.heading_he_deg;
            const double len_m = (len_ft >= 1.0 ? len_ft : 200.0) * geo::kFtToM;
            double elat, elon;
            offset_m(lat, lon, out, len_m, elat, elon);
            const double wid =
                (rw.emas_wid_ft > 1.0 ? rw.emas_wid_ft * geo::kFtToM : rw.width_m) * 0.5;
            paint_runway_pixels(im, box, lat, lon, elat, elon, wid, 110, 104, 92);
            ++n;
        };
        emas(true);
        emas(false);
        paint_official_markings(im, box, rw);
        ++n;
    }
    return n;
}

} // namespace

CorrectionReport plan_corrections(const Airport& a, const FaaAirportRef& faa) {
    CorrectionReport r;
    r.icao = a.icao;
    r.imagery = check_airport_vs_faa(a, faa);
    r.imagery.icao = a.icao;
    add_item(r, a.icao, "runway-surface",
             "photo / Raster pad may show old paint, an extension, or missing pavement",
             (faa.source + " effective " + faa.effective).c_str(),
             "repaint official runway surface from both surveyed ends");
    for (const auto& e : faa.ends) {
        std::ostringstream off;
        off << e.ident << " physical end " << e.lat << "," << e.lon << "  ALS " << e.als << "  PAPI "
            << e.papi << " " << e.papi_deg << "°";
        add_item(r, a.icao, ("RWY " + e.ident).c_str(),
                 "imagery is texture only; stale markings / old threshold possible",
                 off.str().c_str(),
                 "repaint markings at official landing THR; official ALS/PAPI/REIL/TDZ win");
        if (e.displaced_ft > 0.5) {
            std::ostringstream d;
            d << e.ident << " displaced " << e.displaced_ft << " ft (" << e.source << ")";
            add_item(r, a.icao, ("RWY " + e.ident + " displaced-threshold").c_str(),
                     "photo may show an older painted threshold", d.str().c_str(),
                     "repaint arrows/bars at official THR; blend stale paint in the displaced area");
        }
        if (e.emas) {
            std::ostringstream em;
            em << e.ident << " EMAS " << e.emas_len_ft << "x" << e.emas_wid_ft << " ft";
            add_item(r, a.icao, ("RWY " + e.ident + " EMAS").c_str(),
                     "photo may omit the bed or show a previous length", em.str().c_str(),
                     "paint official EMAS bed beyond the DER");
        }
    }
    add_item(r, a.icao, "stale-pavement",
             "construction, demolished pavement, renamed/removed taxiways in the photo",
             "official runway / taxi geometry only",
             "blend out paint and pavement the official data says is not there");
    return r;
}

std::string format_correction_report(const CorrectionReport& r) {
    std::ostringstream o;
    o << r.icao << " imagery corrections  " << r.items.size() << " planned  painted=" << r.painted
      << "\n";
    o << format_acc_report(r.imagery);
    for (const auto& c : r.items) {
        o << "  CORRECT  " << c.feature << "\n";
        o << "    imagery   " << c.imagery << "\n";
        o << "    official  " << c.official << "\n";
        o << "    action    " << c.action << "\n";
    }
    return o.str();
}

bool write_correction_report(const std::string& path, const CorrectionReport& r) {
    try {
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        std::ofstream f(path);
        if (!f)
            return false;
        f << format_correction_report(r);
        return static_cast<bool>(f);
    } catch (...) {
        return false;
    }
}

int apply_correction_overlay(RgbImage& im, const GeoBox& box, const Airport& a) {
    Pix p{im.w, im.h, &im.rgb};
    return apply_overlay_pix(p, box, a);
}

int apply_correction_overlay(ImageRgb& im, const GeoBox& box, const Airport& a) {
    Pix p{im.w, im.h, &im.rgb};
    return apply_overlay_pix(p, box, a);
}

int apply_correction_overlay_icao(ImageRgb& im, const GeoBox& box, const std::string& icao) {
    try {
        return apply_correction_overlay(im, box, airport_by_icao(icao));
    } catch (...) {
        return 0;
    }
}

bool write_airport_correction_report(const std::string& dir, const std::string& icao, int painted) {
    try {
        const Airport a = airport_by_icao(icao);
        const FaaAirportRef* faa = faa_airport(a.icao);
        if (!faa)
            return false;
        CorrectionReport r = plan_corrections(a, *faa);
        r.painted = painted;
        return write_correction_report(dir + "/corrections.txt", r);
    } catch (...) {
        return false;
    }
}

AccReport check_imagery_vs_official(const Airport& a, const FaaAirportRef& faa, const GeoBox* box,
                                    int w, int h) {
    AccReport r = check_airport_vs_faa(a, faa);
    r.icao = a.icao;
    if (box && w > 1 && h > 1)
        imagery_aligned_to_runways(*box, w, h, a, kAccImageryM, &r);
    return r;
}

} // namespace s376
