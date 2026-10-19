#include "s376/fetch.hpp"
#include "s376/accuracy.hpp"
#include "s376/airport.hpp"
#include "s376/correct.hpp"
#include "s376/dxt.hpp"
#include "s376/faa.hpp"
#include "s376/geo.hpp"
#include "s376/imagery.hpp"
#include "s376/log.hpp"
#include "s376/safety.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace s376 {
namespace {

void ensure_dir(const std::string& dir) {
    std::filesystem::create_directories(dir);
}

std::string icao_upper(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

int shell_quote_ok(const std::string& s) {
    return s.find('\'') == std::string::npos && s.find('\n') == std::string::npos;
}

} // namespace

GeoBox box_from_radius(double lat, double lon, double radius_km) {
    const double r_m = radius_km * 1000.0;
    const double dlat = geo::rad2deg(r_m / geo::kWgsA);
    const double cl = std::max(std::cos(geo::deg2rad(lat)), 1e-6);
    const double dlon = geo::rad2deg(r_m / (geo::kWgsA * cl));
    GeoBox b;
    b.south = lat - dlat;
    b.north = lat + dlat;
    b.west = lon - dlon;
    b.east = lon + dlon;
    return b;
}

GeoBox union_boxes(const std::vector<GeoBox>& boxes) {
    GeoBox u;
    if (boxes.empty())
        return u;
    u = boxes.front();
    for (const auto& b : boxes) {
        u.west = std::min(u.west, b.west);
        u.south = std::min(u.south, b.south);
        u.east = std::max(u.east, b.east);
        u.north = std::max(u.north, b.north);
    }
    return u;
}

std::vector<GeoBox> coverage_boxes(const AirportRef& apt, const FetchRequest& req) {
    std::vector<GeoBox> boxes;
    boxes.push_back(box_from_radius(apt.lat, apt.lon, req.near_radius_km));
    try {
        const Airport a = airport_by_icao(apt.icao);
        const double final_m = std::max(0.0, req.approach_nm) * 1852.0;
        for (const auto& rw : a.runways) {
            for (int end = 0; end < 2; ++end) {
                const double lat0 = end == 0 ? rw.le_lat : rw.he_lat;
                const double lon0 = end == 0 ? rw.le_lon : rw.he_lon;
                const double hdg = end == 0 ? rw.heading_deg + 180.0 : rw.heading_deg;
                double lat1 = lat0, lon1 = lon0;
                offset_m(lat0, lon0, hdg, final_m, lat1, lon1);
                constexpr int kSteps = 6;
                for (int i = 0; i <= kSteps; ++i) {
                    const double t = static_cast<double>(i) / kSteps;
                    const double la = lat0 + (lat1 - lat0) * t;
                    const double lo = lon0 + (lon1 - lon0) * t;
                    boxes.push_back(box_from_radius(la, lo, 0.8));
                }
            }
        }
    } catch (const std::exception&) {
        // Unknown ICAO layout: ARP circle only.
    }
    return boxes;
}

std::vector<RasterTile> tile_raster(const GeoBox& box, double m_per_px, int max_px) {
    std::vector<RasterTile> out;
    // iy=0 is the southern row, ix=0 the western column (dense, no holes).
    // Allow native NYS (~0.15 m). Do not clamp up to 0.25 — disk is free.
    const double mpp = std::max(0.05, m_per_px);
    const int cap = std::max(64, max_px);
    const double lat = 0.5 * (box.south + box.north);
    const double m_lat = 111320.0;
    const double m_lon = 111320.0 * std::max(0.2, std::cos(geo::deg2rad(lat)));
    const double width_m = std::max(mpp, (box.east - box.west) * m_lon);
    const double height_m = std::max(mpp, (box.north - box.south) * m_lat);
    const int nx = std::max(1, static_cast<int>(std::ceil(width_m / (mpp * cap))));
    const int ny = std::max(1, static_cast<int>(std::ceil(height_m / (mpp * cap))));
    for (int iy = 0; iy < ny; ++iy) {
        for (int ix = 0; ix < nx; ++ix) {
            RasterTile t;
            t.ix = ix;
            t.iy = iy;
            t.box.west = box.west + (box.east - box.west) * ix / nx;
            t.box.east = box.west + (box.east - box.west) * (ix + 1) / nx;
            t.box.south = box.south + (box.north - box.south) * iy / ny;
            t.box.north = box.south + (box.north - box.south) * (iy + 1) / ny;
            const double tw = (t.box.east - t.box.west) * m_lon;
            const double th = (t.box.north - t.box.south) * m_lat;
            t.w = std::max(8, std::min(cap, static_cast<int>(std::lround(tw / mpp))));
            t.h = std::max(8, std::min(cap, static_cast<int>(std::lround(th / mpp))));
            out.push_back(t);
        }
    }
    return out;
}

int clamp_export_px(int px) {
    if (px <= 0 || px > kMaxServiceExportPx)
        return kMaxServiceExportPx;
    return std::max(64, px);
}

int pixel_span(const GeoBox& b, bool north_south, double m_per_px, int cap) {
    const double lat = 0.5 * (b.south + b.north);
    const double m_lat = 111320.0;
    const double m_lon = 111320.0 * std::max(0.2, std::cos(geo::deg2rad(lat)));
    const double metres =
        north_south ? (b.north - b.south) * m_lat : (b.east - b.west) * m_lon;
    const double mpp = std::max(0.05, m_per_px);
    return std::max(8, std::min(cap, static_cast<int>(std::lround(metres / mpp))));
}

std::vector<PlannedTile> plan_lod_tiles(const AirportRef& apt, const FetchRequest& req,
                                        const LodSpec& lod) {
    const FetchRequest r = resolved_fetch(req);
    const int cap = clamp_export_px(r.max_export);
    const double img_m = lod.img_m > 0 ? lod.img_m : 20.0;
    const double dem_m = lod.dem_m > 0 ? lod.dem_m : 30.0;
    const double split_m = std::min(img_m, dem_m);
    const GeoBox box = lod_coverage(apt, r, lod);
    const auto cells = tile_raster(box, split_m, cap);
    std::vector<PlannedTile> out;
    out.reserve(cells.size());
    for (const auto& c : cells) {
        PlannedTile p;
        p.lod = lod.level;
        p.ix = c.ix;
        p.iy = c.iy;
        p.box = c.box;
        p.dem_w = pixel_span(c.box, false, dem_m, cap);
        p.dem_h = pixel_span(c.box, true, dem_m, cap);
        p.img_w = pixel_span(c.box, false, img_m, cap);
        p.img_h = pixel_span(c.box, true, img_m, cap);
        out.push_back(p);
    }
    return out;
}

bool cache_tile_ready(const std::string& tile_dir) {
    return dem_file_complete(tile_dir + "/elev.s376dem");
}

bool file_looks_like_raster(const std::string& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return false;
    const auto sz = std::filesystem::file_size(path, ec);
    if (ec || sz < 256)
        return false;
    std::ifstream f(path, std::ios::binary);
    unsigned char m[4] = {};
    f.read(reinterpret_cast<char*>(m), 4);
    if (f.gcount() < 3)
        return false;
    if (m[0] == 0xFF && m[1] == 0xD8)
        return true; // JPEG
    if (m[0] == 0x89 && m[1] == 'P' && m[2] == 'N' && m[3] == 'G')
        return true;
    if (m[0] == 'I' && m[1] == 'I' && m[2] == '*')
        return true; // TIFF LE
    if (m[0] == 'M' && m[1] == 'M' && m[3] == '*')
        return true; // TIFF BE
    if (m[0] == '<' || m[0] == '{')
        return false; // HTML / JSON error body
    return sz > 4096;
}

std::string format_fetch_plan(const FetchPlan& plan, const AirportRef& apt, const FetchRequest&) {
    std::ostringstream o;
    o << plan.icao << " dem=" << plan.dem_kind << " img=" << plan.img_kind
      << "  tiles=" << plan.tiles.size() << " (iy=0 south, ix=0 west, dense grid)\n";
    std::vector<std::pair<std::string, std::pair<double, double>>> pts;
    pts.push_back({"ARP", {apt.lat, apt.lon}});
    try {
        const Airport a = airport_by_icao(apt.icao);
        for (const auto& rw : a.runways) {
            pts.push_back({rw.ident_le, {rw.le_lat, rw.le_lon}});
            pts.push_back({rw.ident_he, {rw.he_lat, rw.he_lon}});
        }
    } catch (...) {
    }
    int cur_lod = -1;
    for (const auto& t : plan.tiles) {
        if (t.lod != cur_lod) {
            cur_lod = t.lod;
            o << "lod" << t.lod << "\n";
        }
        o << "  t" << t.iy << "_" << t.ix << "  W " << t.box.west << " S " << t.box.south << " E "
          << t.box.east << " N " << t.box.north << "  dem " << t.dem_w << "x" << t.dem_h << "  img "
          << t.img_w << "x" << t.img_h;
        for (const auto& p : pts) {
            if (box_contains(t.box, p.second.first, p.second.second, 0))
                o << "  " << p.first;
        }
        o << "\n";
    }
    if (plan.covers_faa)
        o << "COVERAGE ok — ARP and every runway end sit in a lod0 tile\n";
    else {
        o << "COVERAGE missing:";
        for (const auto& n : plan.uncovered)
            o << " " << n;
        o << "\n";
    }
    o << "estimate img=" << plan.estimate.img_tiles << " dem=" << plan.estimate.dem_tiles
      << " raw~" << plan.estimate.est_raw_bytes << " packed~" << plan.estimate.est_packed_bytes
      << "\n";
    return o.str();
}

std::vector<LodSpec> default_lods(double radius_km) {
    std::vector<LodSpec> out;
    LodSpec near;
    near.level = 0;
    near.radius_km = std::min(8.0, radius_km);
    near.dem_n = 384;
    near.img_n = 768;
    near.bc7 = true;
    out.push_back(near);
    if (radius_km > 8.5) {
        LodSpec mid;
        mid.level = 1;
        mid.radius_km = std::min(25.0, radius_km);
        mid.dem_n = 256;
        mid.img_n = 512;
        out.push_back(mid);
    }
    if (radius_km > 26.0) {
        LodSpec far;
        far.level = 2;
        far.radius_km = radius_km;
        far.dem_n = 128;
        far.img_n = 256;
        out.push_back(far);
    }
    return out;
}

double native_imagery_m(const std::string& kind) {
    if (kind == "nys")
        return 0.15; // ~0.5 ft Latest ortho
    if (kind == "hro")
        return 0.30;
    if (kind == "naip")
        return 0.60;
    if (kind == "njogis")
        return 0.30;
    if (kind == "massgis" || kind == "sanmateo" || kind == "clark" || kind == "ocpa" ||
        kind == "miami" || kind == "ideca")
        return 0.15;
    if (kind == "loudoun")
        return 0.30;
    if (kind == "lacounty")
        return 0.10;
    if (kind == "kingco")
        return 0.08;
    if (kind == "hessen")
        return 0.20;
    if (kind == "ea-vap" || kind == "icgc")
        return 0.25;
    if (kind == "igm")
        return 0.30;
    if (kind == "s2")
        return 10.0;
    if (kind == "hillshade")
        return 20.0;
    if (kind == "none" || kind == "raster-local")
        return 0;
    return 0.60;
}

double native_dem_m(const std::string& kind) {
    if (kind == "3dep")
        return 1.0;
    if (kind == "glo30")
        return 30.0;
    return 1.0;
}

namespace {

void pick_sources(const AirportRef& apt, const FetchRequest& req, std::string& dem,
                  std::string& img) {
    dem = req.dem;
    img = req.imagery;
    if (dem == "auto")
        dem = apt.conus ? "3dep" : "glo30";
    if (img == "auto") {
        if (const auto* src = airport_imagery(apt.icao)) {
            if (src->policy == ImageryPolicy::RasterLocal)
                img = "raster-local";
            else
                img = src->public_kind;
        } else {
            img = !apt.conus ? "s2" : (imagery_covers_nys(apt.lat, apt.lon) ? "nys" : "naip");
        }
    }
}

} // namespace

FetchRequest resolved_fetch(const FetchRequest& in) {
    FetchRequest r = in;
    const AirportRef apt = lookup_airport_ref(r.icao);
    std::string dem, img;
    pick_sources(apt, r, dem, img);
    const double ni = native_imagery_m(img);
    const double nd = native_dem_m(dem);
    if (r.near_m <= 0)
        r.near_m = ni > 0 ? ni : 1.0;
    if (r.near_dem_m <= 0)
        r.near_dem_m = nd;
    if (r.mid_m <= 0)
        r.mid_m = std::max(ni * 4.0, r.near_m * 4.0);
    if (r.mid_dem_m <= 0)
        r.mid_dem_m = std::max(nd * 2.0, r.near_dem_m * 2.0);
    if (r.far_m <= 0)
        r.far_m = std::max(ni * 16.0, r.near_m * 16.0);
    if (r.far_dem_m <= 0)
        r.far_dem_m = std::max(nd, r.near_dem_m * 8.0);
    if (r.near_radius_km <= 0)
        r.near_radius_km = 8.0;
    if (r.mid_radius_km <= 0)
        r.mid_radius_km = 40.0;
    if (r.approach_nm <= 0)
        r.approach_nm = 12.0;
    if (r.radius_km <= 0)
        r.radius_km = kFarFieldKm;
    r.max_export = clamp_export_px(r.max_export);
    r.dem = dem;
    r.imagery = img;
    return r;
}

std::vector<LodSpec> fetch_lods(const FetchRequest& in) {
    const FetchRequest req = resolved_fetch(in);
    std::vector<LodSpec> out;
    LodSpec near;
    near.level = 0;
    near.radius_km = std::min(req.near_radius_km, req.radius_km);
    near.img_m = req.near_m;
    near.dem_m = req.near_dem_m;
    near.bc7 = true;
    near.tiled = true;
    near.img_n = req.max_export;
    near.dem_n = req.max_export;
    out.push_back(near);
    if (req.radius_km > req.near_radius_km + 0.5) {
        LodSpec mid;
        mid.level = 1;
        mid.radius_km = std::min(req.mid_radius_km, req.radius_km);
        mid.img_m = req.mid_m;
        mid.dem_m = req.mid_dem_m;
        mid.tiled = true;
        mid.img_n = req.max_export;
        mid.dem_n = req.max_export;
        out.push_back(mid);
    }
    if (req.radius_km > req.mid_radius_km + 0.5) {
        LodSpec far;
        far.level = 2;
        far.radius_km = req.radius_km;
        far.img_m = req.far_m;
        far.dem_m = req.far_dem_m;
        far.tiled = true;
        far.img_n = req.max_export;
        far.dem_n = req.max_export;
        out.push_back(far);
    }
    return out;
}

GeoBox lod_coverage(const AirportRef& apt, const FetchRequest& req, const LodSpec& lod) {
    if (lod.level == 0)
        return union_boxes(coverage_boxes(apt, req));
    return box_from_radius(apt.lat, apt.lon, lod.radius_km);
}

FetchEstimate estimate_fetch(const FetchRequest& in) {
    FetchEstimate e;
    const FetchRequest req = resolved_fetch(in);
    const AirportRef apt = lookup_airport_ref(req.icao);
    for (const auto& lod : fetch_lods(req)) {
        const auto tiles = plan_lod_tiles(apt, req, lod);
        e.img_tiles += tiles.size();
        e.dem_tiles += tiles.size();
        for (const auto& t : tiles) {
            e.est_raw_bytes += static_cast<std::uint64_t>(t.img_w) * static_cast<std::uint64_t>(t.img_h) * 3;
            const auto packed = lod.bc7 ? dxt::bc7_size(t.img_w, t.img_h) : dxt::bc1_size(t.img_w, t.img_h);
            e.est_packed_bytes += packed + packed / 3; // mips
            e.est_raw_bytes += static_cast<std::uint64_t>(t.dem_w) * static_cast<std::uint64_t>(t.dem_h) * 4;
        }
    }
    return e;
}

AirportRef lookup_airport_ref(const std::string& icao) {
    const std::string id = icao_upper(icao);
    static const AirportRef kAll[] = {
        {"KSEA", "Seattle-Tacoma International", 47.449833, -122.311833, 432, true},
        {"KSFO", "San Francisco International", 37.618833, -122.3755, 13, true},
        {"KLAS", "Harry Reid / McCarran", 36.080333, -115.1525, 2183, true},
        {"KLAX", "Los Angeles International", 33.9425, -118.4080, 128, true},
        {"KJFK", "John F Kennedy International", 40.639928, -73.778692, 13, true},
        {"KEWR", "Newark Liberty International", 40.6925, -74.168667, 18, true},
        {"KBOS", "Boston Logan International", 42.3630, -71.006333, 19, true},
        {"KIAD", "Washington Dulles International", 38.9475, -77.4600, 312, true},
        {"KMCO", "Orlando International", 28.429333, -81.3090, 96, true},
        {"KMIA", "Miami International", 25.795333, -80.290167, 9, true},
        {"KRNO", "Reno-Tahoe International", 39.499167, -119.768167, 4415, true},
        {"EGLL", "London Heathrow", 51.4700, -0.4543, 83, false},
        {"EDDF", "Frankfurt Main", 50.0379, 8.5622, 364, false},
        {"LEBL", "Barcelona El Prat", 41.2971, 2.0785, 12, false},
        {"SEQM", "Quito Mariscal Sucre", -0.1253, -78.3578, 7801, false},
        {"SKBO", "Bogotá El Dorado", 4.7016, -74.1469, 8360, false},
        {"SKRG", "Rionegro / Medellín", 6.1645, -75.4231, 7025, false},
        {"VNKT", "Kathmandu Tribhuvan", 27.6966, 85.3591, 4390, false},
    };
    for (const auto& a : kAll) {
        if (a.icao == id)
            return a;
    }
    throw std::runtime_error("unknown ICAO '" + icao + "' (add it to lookup_airport_ref)");
}

std::string glo30_tile_url(int lat_deg, int lon_deg) {
    // 1° COG on the public AWS Open Data bucket (no credentials).
    char ns[16], ew[16];
    const int alat = std::min(90, std::abs(lat_deg));
    const int alon = std::min(180, std::abs(lon_deg));
    std::snprintf(ns, sizeof(ns), "%c%02d", lat_deg >= 0 ? 'N' : 'S', alat);
    std::snprintf(ew, sizeof(ew), "%c%03d", lon_deg >= 0 ? 'E' : 'W', alon);
    std::ostringstream o;
    o << "https://copernicus-dem-30m.s3.eu-central-1.amazonaws.com/"
      << "Copernicus_DSM_COG_10_" << ns << "_00_" << ew << "_00_DEM/"
      << "Copernicus_DSM_COG_10_" << ns << "_00_" << ew << "_00_DEM.tif";
    return o.str();
}

std::vector<std::string> glo30_urls_for_box(const GeoBox& b) {
    std::vector<std::string> u;
    const int lat0 = static_cast<int>(std::floor(b.south));
    const int lat1 = static_cast<int>(std::floor(b.north - 1e-9));
    const int lon0 = static_cast<int>(std::floor(b.west));
    const int lon1 = static_cast<int>(std::floor(b.east - 1e-9));
    for (int la = lat0; la <= lat1; ++la) {
        for (int lo = lon0; lo <= lon1; ++lo)
            u.push_back(glo30_tile_url(la, lo));
    }
    return u;
}

std::string usgs_export_url(const char* service, const GeoBox& b, int w, int h, const char* format,
                            const char* pixel) {
    std::ostringstream o;
    o << "https://elevation.nationalmap.gov/arcgis/rest/services/" << service
      << "/ImageServer/exportImage?bbox=" << b.west << "," << b.south << "," << b.east << ","
      << b.north << "&bboxSR=4326&imageSR=4326&size=" << w << "," << h << "&format=" << format
      << "&f=image";
    if (pixel)
        o << "&pixelType=" << pixel << "&interpolation=RSP_BilinearInterpolation";
    return o.str();
}

std::string imagery_export_url(const std::string& kind, const GeoBox& b, int w, int h) {
    std::ostringstream o;
    if (kind == "nys") {
        // NYS GIS Program Office Latest ortho (public, ~0.5–1 ft/px over NYC/LI).
        o << "https://orthos.its.ny.gov/arcgis/rest/services/wms/Latest/MapServer/export?bbox="
          << b.west << "," << b.south << "," << b.east << "," << b.north
          << "&bboxSR=4326&imageSR=4326&size=" << w << "," << h
          << "&format=jpg&f=image&transparent=false";
    } else if (kind == "hro") {
        o << "https://imagery.nationalmap.gov/arcgis/rest/services/USGSNAIPPlus/"
             "ImageServer/exportImage?bbox="
          << b.west << "," << b.south << "," << b.east << "," << b.north
          << "&bboxSR=4326&imageSR=4326&size=" << w << "," << h << "&format=tiff&f=image";
    } else if (kind == "sanmateo") {
        o << "https://gis.smcgov.org/image/rest/services/SanMateoCounty_Imagery2022/"
             "ImageServer/exportImage?bbox="
          << b.west << "," << b.south << "," << b.east << "," << b.north
          << "&bboxSR=4326&imageSR=4326&size=" << w << "," << h << "&format=jpg&f=image";
    } else if (kind == "kingco") {
        o << "https://gismaps.kingcounty.gov/arcgis/rest/services/BaseMaps/KingCo_Aerial_2023/"
             "MapServer/export?bbox="
          << b.west << "," << b.south << "," << b.east << "," << b.north
          << "&bboxSR=4326&imageSR=4326&size=" << w << "," << h
          << "&format=jpg&f=image&transparent=false";
    } else if (kind == "naip") {
        o << "https://imagery.nationalmap.gov/arcgis/rest/services/USGSNAIPImagery/"
             "ImageServer/exportImage?bbox="
          << b.west << "," << b.south << "," << b.east << "," << b.north
          << "&bboxSR=4326&imageSR=4326&size=" << w << "," << h << "&format=tiff&f=image";
    }
    return o.str();
}

bool imagery_covers_nys(double lat, double lon) {
    return lat >= 40.45 && lat <= 45.05 && lon >= -79.85 && lon <= -71.75;
}

std::string s2_tile_url(int z, int x, int y) {
    std::ostringstream o;
    o << "https://tiles.maps.eox.at/wmts/1.0.0/s2cloudless-2023_3857/default/g/" << z << "/" << y
      << "/" << x << ".jpg";
    return o.str();
}

const char* curl_bin() {
#ifdef _WIN32
    return "curl.exe";
#else
    return "curl";
#endif
}

bool curl_glob_safe(const std::string& url) {
    return url.find('[') == std::string::npos && url.find(']') == std::string::npos;
}

std::string http_get_command(const std::string& url, const std::string& dest) {
    if (!shell_quote_ok(url) || !shell_quote_ok(dest))
        throw std::runtime_error("refusing curl path with a quote");
    std::ostringstream cmd;
    // -g / --globoff: Overpass QL has [out:json] etc. curl treats [ ] as
    // a glob character class ("bad range in URL") unless globbing is off.
    cmd << curl_bin() << " -g -fsSL --max-time 180 --retry 0 -A "
        << "'s376-visual/0.1 (+https://github.com/joeylawell/s376-visual)' -o '" << dest << "' '"
        << url << "'";
    return cmd.str();
}

std::string http_post_command(const std::string& url, const std::string& body_file,
                              const std::string& dest) {
    if (!shell_quote_ok(url) || !shell_quote_ok(dest) || !shell_quote_ok(body_file))
        throw std::runtime_error("refusing curl path with a quote");
    std::ostringstream cmd;
    cmd << curl_bin() << " -g -fsSL --max-time 180 --retry 0 -A "
        << "'s376-visual/0.1 (+https://github.com/joeylawell/s376-visual)' --data-urlencode 'data@"
        << body_file << "' -o '" << dest << "' '" << url << "'";
    return cmd.str();
}

bool http_get_file(const std::string& url, const std::string& dest) {
    safety::check_data_url(url);
    if (file_looks_like_raster(dest)) {
        S376_LOG_INFO("cache", "resume skip %s", dest.c_str());
        return true;
    }
    ensure_dir(std::filesystem::path(dest).parent_path().string());
    std::error_code ec;
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> jitter(0, 400);
    constexpr int kTries = 8;
    for (int attempt = 0; attempt < kTries; ++attempt) {
        std::filesystem::remove(dest, ec);
        const std::string cmd = http_get_command(url, dest);
        if (attempt == 0)
            S376_LOG_INFO("cache", "GET %s", url.c_str());
        else
            S376_LOG_INFO("cache", "retry %d/%d %s", attempt + 1, kTries, dest.c_str());
        const int rc = std::system(cmd.c_str());
        if (rc == 0 && file_looks_like_raster(dest))
            return true;
        std::filesystem::remove(dest, ec);
        if (attempt + 1 == kTries)
            break;
        const int backoff_ms = (1 << std::min(attempt, 6)) * 1000 + jitter(rng);
        std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
    }
    return false;
}

bool looks_like_overpass_json(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    char head[256] = {};
    in.read(head, 255);
    const std::string s(head);
    if (s.find("<html") != std::string::npos || s.find("Not Acceptable") != std::string::npos)
        return false;
    return s.find('{') != std::string::npos &&
           (s.find("elements") != std::string::npos || s.find("geometry") != std::string::npos ||
            s.find("version") != std::string::npos);
}

bool http_post_file(const std::string& url, const std::string& body, const std::string& dest) {
    safety::check_data_url(url);
    if (std::filesystem::exists(dest) && std::filesystem::file_size(dest) > 1024 &&
        looks_like_overpass_json(dest)) {
        S376_LOG_INFO("cache", "resume skip %s", dest.c_str());
        return true;
    }
    ensure_dir(std::filesystem::path(dest).parent_path().string());
    const std::string body_file = dest + ".post";
    {
        std::ofstream o(body_file);
        o << body;
    }
    const std::string cmd = http_post_command(url, body_file, dest);
    S376_LOG_INFO("cache", "POST %s  %zu bytes", url.c_str(), body.size());
    const int rc = std::system(cmd.c_str());
    std::error_code ec;
    std::filesystem::remove(body_file, ec);
    if (rc != 0)
        return false;
    if (!std::filesystem::exists(dest) || std::filesystem::file_size(dest) < 64)
        return false;
    if (!looks_like_overpass_json(dest)) {
        std::filesystem::remove(dest, ec);
        return false;
    }
    return true;
}

std::size_t count_cache_tiles(const std::string& dir) {
    if (!std::filesystem::exists(dir))
        return 0;
    std::size_t n = 0;
    for (const auto& e : std::filesystem::recursive_directory_iterator(dir)) {
        if (!e.is_regular_file())
            continue;
        const auto name = e.path().filename().string();
        if (name == "elev.s376dem")
            ++n;
    }
    return n;
}

FetchPlan plan_fetch(const FetchRequest& in) {
    const FetchRequest req = resolved_fetch(in);
    const AirportRef apt = lookup_airport_ref(req.icao);
    FetchPlan p;
    p.icao = apt.icao;
    p.lods = fetch_lods(req);
    p.estimate = estimate_fetch(req);
    const std::string dem = req.dem;
    const std::string img = req.imagery;
    p.dem_kind = dem;
    p.img_kind = img;

    std::ostringstream attr;
    attr << "Packed by s376-cache for " << apt.icao << " (" << apt.name << ").\n";
    if (dem == "glo30") {
        attr << "Elevation: Copernicus GLO-30 (© European Union, Copernicus Programme; "
                "licence https://spacedata.copernicus.eu).\n";
        p.urls = glo30_urls_for_box(box_from_radius(apt.lat, apt.lon, req.radius_km));
    } else if (dem == "3dep") {
        attr << "Elevation: USGS 3DEP (public domain, US government work).\n";
        for (const auto& lod : p.lods) {
            const auto tiles = plan_lod_tiles(apt, req, lod);
            p.tiles.insert(p.tiles.end(), tiles.begin(), tiles.end());
            for (const auto& t : tiles)
                p.urls.push_back(
                    usgs_export_url("3DEPElevation", t.box, t.dem_w, t.dem_h, "tiff", "F32"));
        }
        auto g = glo30_urls_for_box(box_from_radius(apt.lat, apt.lon, req.radius_km));
        p.urls.insert(p.urls.end(), g.begin(), g.end());
    } else {
        throw std::runtime_error("dem must be auto|glo30|3dep");
    }
    if (img == "naip" || img == "nys" || img == "hro" || img == "sanmateo" || img == "kingco") {
        if (img == "nys")
            attr << "Imagery: NYS GIS Program Office ortho (public; ~0.5–1 ft/px, NYC/LI).\n";
        else if (img == "hro")
            attr << "Imagery: USGS NAIP Plus / HRO (US public domain) over NAIP.\n";
        else if (img == "sanmateo")
            attr << "Imagery: San Mateo County 2022 ImageServer (county GIS).\n";
        else if (img == "kingco")
            attr << "Imagery: King County KingCo_Aerial_2023 (county GIS).\n";
        else
            attr << "Imagery: USDA NAIP via USGS National Map ImageServer (US public domain).\n";
        if (p.tiles.empty()) {
            for (const auto& lod : p.lods) {
                const auto tiles = plan_lod_tiles(apt, req, lod);
                p.tiles.insert(p.tiles.end(), tiles.begin(), tiles.end());
            }
        }
        for (const auto& t : p.tiles)
            p.urls.push_back(imagery_export_url(img, t.box, t.img_w, t.img_h));
    } else if (img == "s2") {
        attr << "Imagery: EOX Sentinel-2 cloudless 2023 (CC BY 4.0, Copernicus Sentinel data).\n";
        p.urls.push_back(s2_tile_url(12, 0, 0));
    } else if (img == "hillshade") {
        attr << "Imagery: local hillshade (no download).\n";
    } else if (img == "raster-local") {
        attr << "Imagery: no public source as sharp as Raster. "
                "s376-cache import-raster <airport-dir> --out cache/"
             << apt.icao << "\n";
    } else if (const auto* src = airport_imagery(apt.icao)) {
        attr << "Imagery: " << src->public_label << " (" << src->license << "). "
             << "Not a live ImageServer pull. Download the public rasters, then "
                "s376-cache import-raster DIR --out cache/"
             << apt.icao << "\n";
    } else {
        throw std::runtime_error(
            "imagery must be auto|nys|hro|naip|s2|hillshade|sanmateo|kingco or a catalog kind");
    }
    attr << "Do not commit the downloaded rasters. See docs/SOURCES.md.\n";
    p.attribution = attr.str();
    if (p.tiles.empty()) {
        for (const auto& lod : p.lods) {
            const auto tiles = plan_lod_tiles(apt, req, lod);
            p.tiles.insert(p.tiles.end(), tiles.begin(), tiles.end());
        }
    }
    try {
        const Airport a = airport_by_icao(apt.icao);
        std::vector<CacheTileRef> planned;
        for (const auto& t : p.tiles) {
            if (t.lod != 0)
                continue;
            CacheTileRef r;
            r.lod = 0;
            r.west = t.box.west;
            r.south = t.box.south;
            r.east = t.box.east;
            r.north = t.box.north;
            planned.push_back(r);
        }
        AccReport cov;
        cov.icao = apt.icao;
        p.covers_faa = highest_res_covers_tiles(planned, a, &cov);
        for (const auto& f : cov.fails) {
            auto name = f.detail;
            const auto sp = name.find(' ');
            p.uncovered.push_back(sp == std::string::npos ? name : name.substr(0, sp));
        }
    } catch (...) {
        p.covers_faa = true; // no layout: ARP circle only
    }
    return p;
}

DemGrid resample_dem(const DemGrid& src, const GeoBox& box, int w, int h) {
    DemGrid d;
    d.w = std::max(8, w);
    d.h = std::max(8, h);
    d.west = box.west;
    d.south = box.south;
    d.east = box.east;
    d.north = box.north;
    d.z_m.assign(static_cast<std::size_t>(d.w * d.h), 0.f);
    for (int y = 0; y < d.h; ++y) {
        const double lat = d.north - (d.north - d.south) * y / (d.h - 1);
        for (int x = 0; x < d.w; ++x) {
            const double lon = d.west + (d.east - d.west) * x / (d.w - 1);
            double z = 0;
            if (!src.sample_m(lat, lon, z))
                z = 0;
            d.z_m[static_cast<std::size_t>(y * d.w + x)] = static_cast<float>(z);
        }
    }
    return d;
}

ImageRgb resample_rgb(const ImageRgb& src, int w, int h) {
    ImageRgb im;
    im.w = std::max(4, w);
    im.h = std::max(4, h);
    im.rgb.assign(static_cast<std::size_t>(im.w * im.h * 3), 0);
    if (src.rgb.empty() || src.w < 1 || src.h < 1)
        return im;
    for (int y = 0; y < im.h; ++y) {
        const double v = (y + 0.5) * src.h / im.h;
        const int y0 = std::min(src.h - 1, std::max(0, static_cast<int>(v)));
        for (int x = 0; x < im.w; ++x) {
            const double u = (x + 0.5) * src.w / im.w;
            const int x0 = std::min(src.w - 1, std::max(0, static_cast<int>(u)));
            const int si = (y0 * src.w + x0) * 3;
            const int di = (y * im.w + x) * 3;
            im.rgb[di] = src.rgb[si];
            im.rgb[di + 1] = src.rgb[si + 1];
            im.rgb[di + 2] = src.rgb[si + 2];
        }
    }
    return im;
}

std::uint64_t directory_bytes(const std::string& dir) {
    if (!std::filesystem::exists(dir))
        return 0;
    std::uint64_t n = 0;
    for (const auto& e : std::filesystem::recursive_directory_iterator(dir)) {
        if (e.is_regular_file())
            n += e.file_size();
    }
    return n;
}

namespace {

void write_lod_dir(const std::string& dir, const DemGrid& dem, const ImageRgb& img, bool bc7,
                   bool mips) {
    ensure_dir(dir);
    dem.save(dir + "/elev.s376dem");
    if (img.rgb.empty())
        return;
    if (bc7)
        dxt::write_dds_bc7(dir + "/imagery.dds", img.rgb.data(), img.w, img.h, mips);
    else
        dxt::write_dds_bc1(dir + "/imagery.dds", img.rgb.data(), img.w, img.h, mips);
}

} // namespace

namespace {

void splat_dem(DemGrid& dst, const DemGrid& src, std::vector<char>& hit, bool overwrite) {
    if (src.empty() || dst.empty())
        return;
    for (int y = 0; y < dst.h; ++y) {
        const double la = dst.north - (dst.north - dst.south) * y / std::max(1, dst.h - 1);
        for (int x = 0; x < dst.w; ++x) {
            const double lo = dst.west + (dst.east - dst.west) * x / std::max(1, dst.w - 1);
            double z = 0;
            if (!src.sample_m(la, lo, z))
                continue;
            const std::size_t i = static_cast<std::size_t>(y * dst.w + x);
            if (hit[i] && !overwrite)
                continue;
            dst.z_m[i] = static_cast<float>(z);
            hit[i] = 1;
        }
    }
}

} // namespace

void write_streamed_cache_tile(const std::string& tile_dir, DemGrid dem, ImageRgb img,
                               const GeoBox& box, const std::string& icao, bool bc7, bool mips) {
    if (img.rgb.empty() && !dem.empty())
        img = ImageRgb::hillshade(dem);
    apply_correction_overlay_icao(img, box, icao);
    write_lod_dir(tile_dir, dem, img, bc7, mips);
}

void log_fetch_rss(const char* where, std::size_t packed, std::size_t planned) {
    const double now_mib = static_cast<double>(current_rss_bytes()) / (1024.0 * 1024.0);
    const double peak_mib = static_cast<double>(peak_rss_bytes()) / (1024.0 * 1024.0);
    S376_LOG_INFO("cache", "rss %s  tiles=%zu/%zu  now=%.0f MiB  peak=%.0f MiB",
                  where ? where : "fetch", packed, planned, now_mib, peak_mib);
}

DemGrid merge_root_dem_from_cache(const std::string& cache_dir, const GeoBox& box, int n) {
    DemGrid merged;
    merged.w = std::max(8, n);
    merged.h = merged.w;
    merged.west = box.west;
    merged.south = box.south;
    merged.east = box.east;
    merged.north = box.north;
    merged.z_m.assign(static_cast<std::size_t>(merged.w * merged.h), 0.f);
    std::vector<char> hit(static_cast<std::size_t>(merged.w * merged.h), 0);
    auto tiles = scan_cache_tiles(cache_dir);
    std::sort(tiles.begin(), tiles.end(), [](const CacheTileRef& a, const CacheTileRef& b) {
        if (a.lod != b.lod)
            return a.lod < b.lod;
        return a.dem_w * a.dem_h > b.dem_w * b.dem_h;
    });
    for (const auto& t : tiles) {
        try {
            // One subsampled tile at a time — never the full 2048² stack.
            const DemGrid d = DemGrid::load_subsampled(t.path + "/elev.s376dem", 96);
            splat_dem(merged, d, hit, false);
        } catch (...) {
        }
    }
    return merged;
}

std::uint64_t bench_stream_pack_rss(const std::string& dir, int tiles, int dem_n, int img_n) {
    ensure_dir(dir);
    const int n = std::max(1, tiles);
    const int dn = std::max(8, dem_n);
    const int in = std::max(8, img_n);
    std::uint64_t peak = current_rss_bytes();
    for (int i = 0; i < n; ++i) {
        const int ix = i % 8;
        const int iy = i / 8;
        GeoBox box;
        box.west = -73.80 + ix * 0.01;
        box.east = box.west + 0.01;
        box.south = 40.60 + iy * 0.01;
        box.north = box.south + 0.01;
        DemGrid dem;
        dem.w = dn;
        dem.h = dn;
        dem.west = box.west;
        dem.south = box.south;
        dem.east = box.east;
        dem.north = box.north;
        dem.z_m.assign(static_cast<std::size_t>(dn * dn), 12.f);
        ImageRgb img;
        img.w = in;
        img.h = in;
        img.rgb.assign(static_cast<std::size_t>(in * in * 3), 80);
        const std::string tdir = dir + "/lod0/t" + std::to_string(iy) + "_" + std::to_string(ix);
        write_streamed_cache_tile(tdir, std::move(dem), std::move(img), box, "", false, false);
        peak = std::max(peak, current_rss_bytes());
        if ((i + 1) % kFetchRssLogEvery == 0 || i + 1 == n)
            log_fetch_rss("bench", static_cast<std::size_t>(i + 1), static_cast<std::size_t>(n));
    }
    const GeoBox uni{-73.80, 40.60, -73.80 + 8 * 0.01, 40.60 + ((n + 7) / 8) * 0.01};
    DemGrid root = merge_root_dem_from_cache(dir, uni, 64);
    root.save(dir + "/elev.s376dem");
    peak = std::max(peak, current_rss_bytes());
    return peak;
}

std::uint64_t pack_lods(const CacheRegion& src, const std::string& dir, const std::vector<LodSpec>& lods,
                        bool mips) {
    ensure_dir(dir);
    const double clat = (src.dem.north + src.dem.south) * 0.5;
    const double clon = (src.dem.east + src.dem.west) * 0.5;
    ImageRgb base = src.imagery.rgb.empty() ? ImageRgb::hillshade(src.dem) : src.imagery;
    int painted = 0;
    for (const auto& lod : lods) {
        const GeoBox box = box_from_radius(clat, clon, lod.radius_km);
        DemGrid dem = resample_dem(src.dem, box, lod.dem_n, lod.dem_n);
        ImageRgb img = resample_rgb(base, lod.img_n, lod.img_n);
        if (img.rgb.empty())
            img = ImageRgb::hillshade(dem);
        painted += apply_correction_overlay_icao(img, box, src.name);
        write_lod_dir(dir + "/lod" + std::to_string(lod.level), dem, img, lod.bc7, mips);
    }
    if (!lods.empty()) {
        // Finest LOD also at the root so HAT / older loaders keep working.
        const auto& lod0 = lods.front();
        const GeoBox box = box_from_radius(clat, clon, lod0.radius_km);
        CacheRegion root;
        root.name = src.name;
        root.dem = resample_dem(src.dem, box, lod0.dem_n, lod0.dem_n);
        root.imagery = resample_rgb(base, lod0.img_n, lod0.img_n);
        if (root.imagery.rgb.empty())
            root.imagery = ImageRgb::hillshade(root.dem);
        painted += apply_correction_overlay_icao(root.imagery, box, src.name);
        write_cache(root, dir);
        if (lod0.bc7)
            dxt::write_dds_bc7(dir + "/imagery.dds", root.imagery.rgb.data(), root.imagery.w,
                               root.imagery.h, mips);
        else
            dxt::write_dds_bc1(dir + "/imagery.dds", root.imagery.rgb.data(), root.imagery.w,
                               root.imagery.h, mips);
        write_airport_correction_report(dir, src.name, painted);
    }
    return directory_bytes(dir);
}

std::uint64_t fetch_and_pack(const FetchRequest& in, FetchPlan* plan_out) {
    const FetchRequest req = resolved_fetch(in);
    FetchPlan plan = plan_fetch(req);
    if (plan_out)
        *plan_out = plan;
    const AirportRef apt = lookup_airport_ref(req.icao);
    std::string out = req.out_dir.empty() ? ("cache/" + apt.icao) : req.out_dir;
    if (req.season == "winter") {
        if (out.size() < 7 || out.substr(out.size() - 7) != "/winter")
            out += "/winter";
    }
    if (req.dry_run || req.plan_only)
        return 0;
    if (plan.img_kind == "raster-local" || !imagery_kind_is_live_pull(plan.img_kind)) {
        std::ostringstream o;
        o << apt.icao << " imagery '" << plan.img_kind
          << "' is not a live public pull as sharp as Raster.\n";
        if (const auto* src = airport_imagery(apt.icao))
            o << src->public_label << " — " << src->notes << "\n";
        o << "s376-cache import-raster <airport-dir> --out cache/" << apt.icao
          << "\n"
          << format_import_needed();
        throw std::runtime_error(o.str());
    }

    ensure_dir(out);
    std::ofstream attr(out + "/ATTRIBUTION.txt");
    attr << plan.attribution;

    const std::string raw = out + "/raw";
    ensure_dir(raw);
    limit_gdal_cache_mb(64);
    ImageRgb imagery;
    log_fetch_rss("start", 0, plan.tiles.size());

    auto try_get = [&](const std::string& url, const std::string& dest) -> bool {
        try {
            return http_get_file(url, dest);
        } catch (const std::exception& ex) {
            S376_LOG_WARN("cache", "%s", ex.what());
            return false;
        }
    };

    const bool tiled_img = plan.img_kind == "naip" || plan.img_kind == "nys" ||
                           plan.img_kind == "hro" || plan.img_kind == "sanmateo" ||
                           plan.img_kind == "kingco";

    std::atomic<std::size_t> packed_tiles{0};
    std::atomic<std::size_t> missing{0};
    std::mutex imagery_mu;
    auto maybe_rss = [&]() {
        const auto n = packed_tiles.load();
        if (n == 1 || n == plan.tiles.size() ||
            n % static_cast<std::size_t>(kFetchRssLogEvery) == 0)
            log_fetch_rss("tile", n, plan.tiles.size());
        const auto bound = fetch_rss_bound_bytes(req);
        if (peak_rss_bytes() > bound)
            S376_LOG_WARN("cache", "rss peak over --mem-budget (%.0f / %.0f MiB) — streaming one tile",
                          static_cast<double>(peak_rss_bytes()) / (1024.0 * 1024.0),
                          static_cast<double>(bound) / (1024.0 * 1024.0));
    };
    if (plan.dem_kind == "3dep" || tiled_img) {
        const int jobs = fetch_jobs_for_budget(req.jobs, req.mem_budget_mb);
        const auto rss_bound = fetch_rss_bound_bytes(req);
        S376_LOG_INFO("cache", "fetch workers  %d  tiles=%zu  mem-budget %.0f MiB", jobs,
                      plan.tiles.size(), static_cast<double>(rss_bound) / (1024.0 * 1024.0));
        auto one_tile = [&](const PlannedTile& t) {
            const std::string tdir = out + "/lod" + std::to_string(t.lod) + "/t" +
                                     std::to_string(t.iy) + "_" + std::to_string(t.ix);
            if (cache_tile_ready(tdir)) {
                S376_LOG_INFO("cache", "resume skip %s", tdir.c_str());
                packed_tiles.fetch_add(1);
                maybe_rss();
                return;
            }
            DemGrid tile_dem;
            ImageRgb tile_img;
            if (plan.dem_kind == "3dep") {
                const std::string dest = raw + "/l" + std::to_string(t.lod) + "-d-" +
                                         std::to_string(t.iy) + "-" + std::to_string(t.ix) + ".tif";
                const std::string url =
                    usgs_export_url("3DEPElevation", t.box, t.dem_w, t.dem_h, "tiff", "F32");
                if (try_get(url, dest) && gdal_available()) {
                    try {
                        tile_dem = DemGrid::load_geotiff(dest);
                    } catch (const std::exception& ex) {
                        S376_LOG_WARN("cache", "3DEP tile: %s", ex.what());
                    }
                }
            }
            if (tiled_img) {
                const std::string dest = raw + "/l" + std::to_string(t.lod) + "-i-" +
                                         std::to_string(t.iy) + "-" + std::to_string(t.ix) + ".tif";
                const std::string url = imagery_export_url(plan.img_kind, t.box, t.img_w, t.img_h);
                if (try_get(url, dest) && gdal_available()) {
                    try {
                        tile_img = ImageRgb::load_jpeg_or_png(dest);
                    } catch (const std::exception& ex) {
                        S376_LOG_WARN("cache", "NAIP tile: %s", ex.what());
                    }
                }
            }
            if (tile_dem.empty()) {
                missing.fetch_add(1);
                return;
            }
            if (!tile_img.rgb.empty()) {
                std::lock_guard<std::mutex> g(imagery_mu);
                if (imagery.rgb.empty())
                    imagery = resample_rgb(tile_img, 256, 256);
            }
            write_streamed_cache_tile(tdir, std::move(tile_dem), std::move(tile_img), t.box,
                                      apt.icao, t.lod == 0, req.mips);
            packed_tiles.fetch_add(1);
            maybe_rss();
        };
        if (jobs <= 1) {
            for (const auto& t : plan.tiles)
                one_tile(t);
        } else {
            std::atomic<std::size_t> next{0};
            std::vector<std::thread> pool;
            pool.reserve(static_cast<std::size_t>(jobs));
            for (int w = 0; w < jobs; ++w) {
                pool.emplace_back([&]() {
                    for (;;) {
                        const std::size_t i = next.fetch_add(1);
                        if (i >= plan.tiles.size())
                            break;
                        one_tile(plan.tiles[i]);
                    }
                });
            }
            for (auto& th : pool)
                th.join();
        }
    }

    if (tiled_img) {
        const GeoBox b = box_from_radius(apt.lat, apt.lon, std::min(8.0, req.radius_km));
        const std::string dest = raw + "/naip.jpg";
        const std::string url =
            "https://imagery.nationalmap.gov/arcgis/rest/services/USGSNAIPImagery/"
            "ImageServer/exportImage?bbox=" +
            std::to_string(b.west) + "," + std::to_string(b.south) + "," + std::to_string(b.east) +
            "," + std::to_string(b.north) +
            "&bboxSR=4326&imageSR=4326&size=768,768&format=jpg&f=image";
        if (try_get(url, dest) && gdal_available()) {
            try {
                imagery = ImageRgb::load_jpeg_or_png(dest);
            } catch (const std::exception& ex) {
                S376_LOG_WARN("cache", "NAIP read: %s", ex.what());
            }
        }
    } else if (plan.img_kind == "s2") {
        // One z=11 tile covering the ARP is enough for a first pack; stitch later.
        const double lat = geo::deg2rad(apt.lat);
        const int z = 11;
        const int n = 1 << z;
        const int x = static_cast<int>(std::floor((apt.lon + 180.0) / 360.0 * n));
        const int y = static_cast<int>(std::floor(
            (1.0 - std::log(std::tan(lat) + 1.0 / std::cos(lat)) / geo::kPi) / 2.0 * n));
        const std::string dest = raw + "/s2.jpg";
        if (try_get(s2_tile_url(z, x, y), dest) && gdal_available()) {
            try {
                imagery = ImageRgb::load_jpeg_or_png(dest);
            } catch (const std::exception& ex) {
                S376_LOG_WARN("cache", "S2 read: %s", ex.what());
            }
        }
    }

    if (missing.load() > 0 || (!plan.tiles.empty() && packed_tiles.load() < plan.tiles.size())) {
        std::ofstream covf(out + "/coverage.toml");
        covf << "icao = \"" << apt.icao << "\"\ncovers_faa = false\n";
        covf << "planned = " << plan.tiles.size() << "\nready = " << packed_tiles.load() << "\n";
        covf << "missing = " << missing.load() << "\n";
        S376_LOG_WARN("cache",
                      "%s incomplete  ready=%zu planned=%zu missing=%zu — raw kept; re-run the "
                      "same fetch to resume",
                      apt.icao.c_str(), packed_tiles.load(), plan.tiles.size(), missing.load());
        throw std::runtime_error(apt.icao +
                                 " fetch incomplete (missing tiles). Re-run the same command; "
                                 "completed tiles are skipped. Not writing a success pack.");
    }

    CacheRegion region;
    region.name = apt.icao;
    const GeoBox root_box = box_from_radius(apt.lat, apt.lon, req.radius_km);
    // Stream packed tiles (one subsampled DEM at a time). Never hold the
    // full 2048² stack — that is what OOM-killed the 4 GB KJFK fetch.
    region.dem = merge_root_dem_from_cache(out, root_box, 256);
    bool have_dem = false;
    for (float z : region.dem.z_m) {
        if (z != 0.f) {
            have_dem = true;
            break;
        }
    }
    if (!have_dem && (plan.dem_kind == "glo30" || plan.dem_kind == "3dep")) {
        std::vector<char> hit(static_cast<std::size_t>(region.dem.w * region.dem.h), 0);
        int gi = 0;
        for (const auto& url : glo30_urls_for_box(root_box)) {
            const auto dest = raw + "/glo30-" + std::to_string(gi++) + ".tif";
            if (!try_get(url, dest) || !gdal_available())
                continue;
            try {
                DemGrid g = DemGrid::load_geotiff(dest);
                splat_dem(region.dem, g, hit, false);
                have_dem = true;
            } catch (const std::exception& ex) {
                S376_LOG_WARN("cache", "GLO-30 read: %s", ex.what());
            }
        }
    }
    if (!have_dem) {
        S376_LOG_WARN("cache", "no DEM downloaded; packing synth pad for %s", apt.icao.c_str());
        region.dem = DemGrid::synth_ksea();
        region.dem.west = root_box.west;
        region.dem.east = root_box.east;
        region.dem.south = root_box.south;
        region.dem.north = root_box.north;
    }
    region.imagery = imagery.rgb.empty() ? ImageRgb::hillshade(region.dem) : imagery;
    // Coarse root + lodN/elev for HAT / fallback. High-res tiles already
    // live in lodN/tY_X from the ImageServer loop.
    const auto bytes = pack_lods(region, out, default_lods(req.radius_km), req.mips);
    const auto packed = directory_bytes(out);
    S376_LOG_INFO("cache", "packed %s  %llu bytes  %zu tiles  dem=%s img=%s", apt.icao.c_str(),
                  static_cast<unsigned long long>(packed), packed_tiles.load(), plan.dem_kind.c_str(),
                  plan.img_kind.c_str());
    log_fetch_rss("pack", packed_tiles.load(), plan.tiles.size());
    (void)bytes;

    try {
        const Airport layout = airport_by_icao(apt.icao);
        AccReport cov;
        cov.icao = apt.icao;
        const auto tiles = scan_cache_tiles(out);
        bool ok = false;
        if (!tiles.empty()) {
            ok = highest_res_covers_tiles(tiles, layout, &cov);
        } else if (!plan.lods.empty()) {
            ok = highest_res_covers_airport(lod_coverage(apt, req, plan.lods.front()), layout, &cov);
        }
        {
            std::ofstream covf(out + "/coverage.toml");
            const GeoBox lod0 =
                plan.lods.empty() ? box_from_radius(apt.lat, apt.lon, req.near_radius_km)
                                  : lod_coverage(apt, req, plan.lods.front());
            covf << "icao = \"" << apt.icao << "\"\n";
            covf << "arp_lat = " << apt.lat << "\n";
            covf << "arp_lon = " << apt.lon << "\n";
            covf << "lod0_west = " << lod0.west << "\n";
            covf << "lod0_south = " << lod0.south << "\n";
            covf << "lod0_east = " << lod0.east << "\n";
            covf << "lod0_north = " << lod0.north << "\n";
            covf << "covers_faa = " << (ok ? "true" : "false") << "\n";
            covf << "lod0_tiles = " << tiles.size() << "\n";
        }
        if (!ok) {
            S376_LOG_WARN("cache", "%s", format_acc_report(cov).c_str());
            throw std::runtime_error(
                apt.icao + " lod0 does not cover ARP and every runway end (see coverage.toml). "
                           "Re-fetch; do not keep a SW-shifted pack.");
        }
        if (const FaaAirportRef* faa = faa_airport(apt.icao)) {
            for (const auto& e : faa->ends) {
                double z = 0;
                if (sample_cache_elev_m(out, e.lat, e.lon, z) &&
                    std::fabs(z - e.elev_ft * geo::kFtToM) > 15.0) {
                    S376_LOG_WARN("cache",
                                  "%s %s DEM %.1f m vs FAA %.1f ft (%.1f m)",
                                  apt.icao.c_str(), e.ident.c_str(), z, e.elev_ft,
                                  e.elev_ft * geo::kFtToM);
                }
            }
        }
        std::error_code ec;
        std::filesystem::remove_all(raw, ec);
    } catch (const std::runtime_error& ex) {
        if (std::string(ex.what()).find("no built-in layout") != std::string::npos)
            return packed;
        throw;
    }
    return packed;
}

} // namespace s376
