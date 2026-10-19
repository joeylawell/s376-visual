#include "s376/imagery.hpp"

#include "s376/airport.hpp"
#include "s376/correct.hpp"
#include "s376/dxt.hpp"
#include "s376/geo.hpp"
#include "s376/log.hpp"
#include "s376/parity.hpp"
#include "s376/safety.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace s376 {
namespace {

const AirportImagery kTable[] = {
    {"KJFK", "John F Kennedy International", "nys", 0.15,
     "NYS ITS 2024 orthos / NYC 2024", "NYS public ortho",
     "orthos.its.ny.gov Latest; ~0.5 ft", ImageryPolicy::Public},
    {"KEWR", "Newark Liberty International", "njogis", 0.30,
     "NJOGIS 2020 30 cm (s3://njogis-imagery/2020 COG)", "NJOGIS public",
     "Statewide 1 ft / 30 cm COG. Not a live ImageServer pull — download COGs, then import-raster.",
     ImageryPolicy::Public},
    {"KBOS", "Boston Logan International", "massgis", 0.15,
     "MassGIS 2023 15 cm", "MassGIS public",
     "tiles.arcgis.com MassGIS orthos2023 WMTS. Download or WMTS; not NAIP.",
     ImageryPolicy::Public},
    {"KIAD", "Washington Dulles International", "loudoun", 0.30,
     "Loudoun COLOR_2025 30 cm", "Loudoun County public",
     "County COLOR_2025 aerial. Download tiles, then import-raster.", ImageryPolicy::Public},
    {"KLAS", "Harry Reid / McCarran", "clark", 0.15,
     "Clark County cached imagery", "Clark County GIS",
     "County cache over the field. Download, then import-raster.", ImageryPolicy::Public},
    {"KLAX", "Los Angeles International", "lacounty", 0.10,
     "LA County 4-inch", "LA County public",
     "4 in (~0.10 m). Download, then import-raster.", ImageryPolicy::Public},
    {"KMCO", "Orlando International", "ocpa", 0.15,
     "OCPA Aerials2025", "Orange County Property Appraiser",
     "Aerials2025. Download, then import-raster.", ImageryPolicy::Public},
    {"KMIA", "Miami International", "miami", 0.15,
     "Miami-Dade MDCImagery", "Miami-Dade public",
     "MDCImagery. Download, then import-raster.", ImageryPolicy::Public},
    {"KSEA", "Seattle-Tacoma International", "kingco", 0.08,
     "King County KingCo_Aerial_2023", "King County GIS (EagleView licensed to KC)",
     "gismaps.kingcounty.gov BaseMaps/KingCo_Aerial_2023. ~3 in west county.",
     ImageryPolicy::Public},
    {"KSFO", "San Francisco International", "sanmateo", 0.15,
     "San Mateo 2022 ImageServer 15 cm", "San Mateo County GIS",
     "gis.smcgov.org SanMateoCounty_Imagery2022 (service may report 0.5 m).",
     ImageryPolicy::Public},
    {"EDDF", "Frankfurt Main", "hessen", 0.20,
     "Hessen DOP20 (DL-DE Zero)", "DL-DE/Zero",
     "Hessen DOP20. Download, then import-raster.", ImageryPolicy::Public},
    {"EGLL", "London Heathrow", "ea-vap", 0.25,
     "EA Vertical Aerial Photography (OGL, coverage to check)", "OGL",
     "environment.data.gov.uk VAP. Confirm the field is inside the layer.",
     ImageryPolicy::Public},
    {"LEBL", "Barcelona El Prat", "icgc", 0.25,
     "ICGC ortofoto 2024 25 cm / IGN PNOA", "ICGC / IGN public",
     "Catalan 25 cm or Spain PNOA. Download, then import-raster.", ImageryPolicy::Public},
    {"SEQM", "Quito Mariscal Sucre", "igm", 0.30,
     "IGM Ecuador WMS", "IGM Ecuador public",
     "IGM WMS. Download, then import-raster.", ImageryPolicy::Public},
    {"SKBO", "Bogotá El Dorado", "ideca", 0.15,
     "IDECA Bogotá 2025", "IDECA public",
     "Bogotá 2025 ortho. Download, then import-raster.", ImageryPolicy::Public},
    {"KRNO", "Reno-Tahoe International", "naip", 0.60,
     "USDA NAIP (~0.6 m) — only free source; roughly matches Raster", "US public domain",
     "NAIP is the free pull. import-raster if the local Raster pad is sharper.",
     ImageryPolicy::Either},
    {"SKRG", "Rionegro / Medellín", "none", 0,
     "(no verified public source)", "—",
     "Use import-raster on the Raster airport folder.", ImageryPolicy::RasterLocal},
    {"VNKT", "Kathmandu Tribhuvan", "s2", 10.0,
     "Sentinel-2 10 m only (worse than Raster)", "CC BY 4.0 + Copernicus",
     "Do not fetch S2 as the near field. import-raster the Raster pad.",
     ImageryPolicy::RasterLocal},
};

std::string upper_copy(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string lower_ext(const std::filesystem::path& p) {
    std::string e = p.extension().string();
    for (char& c : e)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e;
}

bool is_opaque_ext(const std::string& ext) {
    return ext == ".nxt" || ext == ".nxb" || ext == ".nx3" || ext == ".nxs" || ext == ".nxo";
}

bool is_raster_candidate(const std::string& ext) {
    return ext == ".tif" || ext == ".tiff" || ext == ".jp2" || ext == ".j2k" || ext == ".jpg" ||
           ext == ".jpeg" || ext == ".png" || ext == ".img" || ext == ".sid";
}

bool refuse_src_path(const std::string& path, std::string& err) {
    if (path.empty()) {
        err = "import-raster needs a local airport directory";
        return true;
    }
    if (path.find("10.1.95") != std::string::npos || path.find("192.168.173") != std::string::npos) {
        err = "refusing a sim-LAN path";
        return true;
    }
    if (looks_like_s376_repo(path)) {
        err = "path looks like the s376-visual repo, not an airport folder";
        return true;
    }
    return false;
}

ImageRgb resample_box(const GeorefRgb& src, const GeoBox& dest, int w, int h) {
    ImageRgb im;
    im.w = std::max(4, w);
    im.h = std::max(4, h);
    im.rgb.assign(static_cast<std::size_t>(im.w * im.h * 3), 0);
    if (!src.ok() || src.img.w < 1 || src.img.h < 1)
        return im;
    for (int y = 0; y < im.h; ++y) {
        const double lat = dest.north - (dest.north - dest.south) * (y + 0.5) / im.h;
        const double v = (src.north - lat) / std::max(1e-15, src.north - src.south) * src.img.h;
        const int y0 = static_cast<int>(std::floor(v));
        if (y0 < 0 || y0 >= src.img.h)
            continue;
        for (int x = 0; x < im.w; ++x) {
            const double lon = dest.west + (dest.east - dest.west) * (x + 0.5) / im.w;
            const double u = (lon - src.west) / std::max(1e-15, src.east - src.west) * src.img.w;
            const int x0 = static_cast<int>(std::floor(u));
            if (x0 < 0 || x0 >= src.img.w)
                continue;
            const int si = (y0 * src.img.w + x0) * 3;
            const int di = (y * im.w + x) * 3;
            im.rgb[di] = src.img.rgb[si];
            im.rgb[di + 1] = src.img.rgb[si + 1];
            im.rgb[di + 2] = src.img.rgb[si + 2];
        }
    }
    return im;
}

} // namespace

const AirportImagery* airport_imagery_table(int* count) {
    if (count)
        *count = static_cast<int>(sizeof(kTable) / sizeof(kTable[0]));
    return kTable;
}

const AirportImagery* airport_imagery(const std::string& icao) {
    const std::string id = upper_copy(icao);
    int n = 0;
    const auto* t = airport_imagery_table(&n);
    for (int i = 0; i < n; ++i) {
        if (id == t[i].icao)
            return &t[i];
    }
    return nullptr;
}

bool needs_raster_fallback(const std::string& icao) {
    const auto* s = airport_imagery(icao);
    return s && s->policy == ImageryPolicy::RasterLocal;
}

bool imagery_kind_is_live_pull(const std::string& kind) {
    return kind == "nys" || kind == "hro" || kind == "naip" || kind == "s2" || kind == "hillshade" ||
           kind == "sanmateo" || kind == "kingco";
}

std::string imagery_policy_name(ImageryPolicy p) {
    switch (p) {
    case ImageryPolicy::Public:
        return "public";
    case ImageryPolicy::Either:
        return "either";
    case ImageryPolicy::RasterLocal:
        return "raster-local";
    }
    return "public";
}

std::string format_imagery_sources_table() {
    std::ostringstream o;
    o << "ICAO  public source                              m/px   policy        fetch\n";
    int n = 0;
    const auto* t = airport_imagery_table(&n);
    for (int i = 0; i < n; ++i) {
        const char* pull = imagery_kind_is_live_pull(t[i].public_kind) ? t[i].public_kind : "import";
        if (t[i].policy == ImageryPolicy::RasterLocal)
            pull = "import-raster";
        char m[32];
        if (t[i].public_m <= 0)
            std::snprintf(m, sizeof(m), "  —  ");
        else
            std::snprintf(m, sizeof(m), "%5.2f", t[i].public_m);
        o << t[i].icao << "  " << t[i].public_label << "  " << m << "  "
          << imagery_policy_name(t[i].policy) << "  " << pull << "\n";
    }
    return o.str();
}

std::string guess_icao_from_path(const std::string& path) {
    const std::string up = upper_copy(path);
    int n = 0;
    const auto* t = airport_imagery_table(&n);
    std::string best;
    for (int i = 0; i < n; ++i) {
        if (up.find(t[i].icao) != std::string::npos &&
            std::string(t[i].icao).size() > best.size())
            best = t[i].icao;
    }
    static const char* kAlso[] = {"KSEA", "KSFO", "KLAS", "KLAX", "KJFK", "KEWR", "KBOS",
                                  "EGLL", "EDDF", nullptr};
    for (int i = 0; kAlso[i]; ++i) {
        if (up.find(kAlso[i]) != std::string::npos && std::string(kAlso[i]).size() > best.size())
            best = kAlso[i];
    }
    return best;
}

std::string format_import_needed() {
    return "needed: a georeferenced GeoTIFF / JPEG2000 / JPEG+world / PNG+world that GDAL can "
           "open (WGS84 or a projected CRS GDAL understands). Proprietary airport-pack files "
           "are not imported. Export or place standard rasters next to the folder; do not "
           "commit them.";
}

ImportScan scan_import_dir(const std::string& dir) {
    ImportScan s;
    s.path = dir;
    if (refuse_src_path(dir, s.error))
        return s;
    if (!std::filesystem::is_directory(dir)) {
        s.error = "not a directory: " + dir;
        return s;
    }
    s.icao = guess_icao_from_path(dir);
    std::map<std::string, int> opaque;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(
             dir, std::filesystem::directory_options::skip_permission_denied, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec)
            continue;
        if (it->is_directory()) {
            const auto name = it->path().filename().string();
            if (name == ".git" || name == "build" || name == "cache")
                it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file())
            continue;
        ++s.files;
        const auto ext = lower_ext(it->path());
        const auto path = it->path().string();
        if (is_opaque_ext(ext)) {
            ++s.opaque;
            ++opaque[ext];
            continue;
        }
        if (!is_raster_candidate(ext))
            continue;
        double w = 0, so = 0, e = 0, n = 0;
        int ww = 0, hh = 0, bands = 0;
        if (!peek_georef(path, w, so, e, n, ww, hh, bands)) {
            ++s.opaque;
            ++opaque[ext + "?ungref"];
            continue;
        }
        if (bands >= 3) {
            ++s.decodable;
            s.decodable_paths.push_back(path);
        } else if (bands == 1) {
            ++s.dems;
            s.dem_paths.push_back(path);
        } else {
            ++s.opaque;
            ++opaque[ext + "?bands"];
        }
    }
    for (const auto& kv : opaque) {
        s.opaque_ext.push_back(kv.first + " x" + std::to_string(kv.second));
    }
    return s;
}

std::string format_import_scan(const ImportScan& s) {
    std::ostringstream o;
    o << "import-raster  dir=" << s.path;
    if (!s.icao.empty())
        o << "  icao=" << s.icao;
    o << "\n  files=" << s.files << "  decodable=" << s.decodable << "  dems=" << s.dems
      << "  opaque=" << s.opaque << "\n";
    if (!s.opaque_ext.empty()) {
        o << "  opaque:";
        for (const auto& e : s.opaque_ext)
            o << " " << e;
        o << "\n";
    }
    if (!s.error.empty())
        o << "  error: " << s.error << "\n";
    return o.str();
}

std::uint64_t import_raster(const ImportRequest& req, ImportScan* scan_out) {
    ImportScan scan = scan_import_dir(req.src_dir);
    if (scan_out)
        *scan_out = scan;
    if (!scan.error.empty())
        throw std::runtime_error(scan.error);
    const std::string icao = upper_copy(req.icao.empty() ? scan.icao : req.icao);
    if (icao.size() != 4)
        throw std::runtime_error("could not guess ICAO from the folder; pass --icao");
    scan.icao = icao;
    if (scan_out)
        *scan_out = scan;

    if (scan.decodable == 0) {
        std::ostringstream o;
        o << "import-raster: no georeferenced imagery GDAL can read in " << req.src_dir << "\n";
        o << format_import_scan(scan);
        o << format_import_needed() << "\n";
        throw std::runtime_error(o.str());
    }
    if (!gdal_available())
        throw std::runtime_error("GDAL was not built in (install libgdal-dev and re-cmake)");

    std::string out = req.out_dir.empty() ? ("cache/" + icao) : req.out_dir;
    if (req.dry_run)
        return 0;

    const auto src_abs = std::filesystem::weakly_canonical(req.src_dir);
    const auto out_abs = std::filesystem::weakly_canonical(out);
    std::error_code ec;
    const auto rel = std::filesystem::relative(out_abs, src_abs, ec);
    if (!ec && !rel.empty() && rel.generic_string().find("..") == std::string::npos)
        throw std::runtime_error("refusing to write the cache inside the source airport folder");

    std::vector<GeorefRgb> imgs;
    imgs.reserve(scan.decodable_paths.size());
    GeoBox uni;
    bool any = false;
    for (const auto& p : scan.decodable_paths) {
        try {
            GeorefRgb g = load_georef_rgb(p);
            if (!g.ok())
                continue;
            imgs.push_back(std::move(g));
            const GeoBox b{imgs.back().west, imgs.back().south, imgs.back().east, imgs.back().north};
            if (!any) {
                uni = b;
                any = true;
            } else {
                uni.west = std::min(uni.west, b.west);
                uni.south = std::min(uni.south, b.south);
                uni.east = std::max(uni.east, b.east);
                uni.north = std::max(uni.north, b.north);
            }
        } catch (const std::exception& ex) {
            S376_LOG_WARN("cache", "import skip %s: %s", p.c_str(), ex.what());
        }
    }
    if (!any)
        throw std::runtime_error("import-raster: rasters listed as decodable failed to load\n" +
                                 format_import_needed());

    AirportRef apt;
    try {
        apt = lookup_airport_ref(icao);
    } catch (...) {
        apt.icao = icao;
        apt.lat = 0.5 * (uni.south + uni.north);
        apt.lon = 0.5 * (uni.west + uni.east);
    }

    DemGrid dem;
    if (!scan.dem_paths.empty()) {
        try {
            dem = DemGrid::load_geotiff(scan.dem_paths.front());
        } catch (const std::exception& ex) {
            S376_LOG_WARN("cache", "import DEM: %s", ex.what());
        }
    }
    if (dem.empty()) {
        dem.w = 256;
        dem.h = 256;
        dem.west = uni.west;
        dem.south = uni.south;
        dem.east = uni.east;
        dem.north = uni.north;
        const float z = static_cast<float>(apt.elev_ft * geo::kFtToM);
        dem.z_m.assign(256 * 256, z);
    }

    const double lat = 0.5 * (uni.south + uni.north);
    const double m_lon = 111320.0 * std::max(0.2, std::cos(geo::deg2rad(lat)));
    double native_m = 1.0;
    for (const auto& g : imgs) {
        const double m = (g.east - g.west) * m_lon / std::max(1, g.img.w);
        native_m = std::min(native_m, std::max(0.05, m));
    }

    std::filesystem::create_directories(out);
    {
        std::ofstream attr(out + "/ATTRIBUTION.txt");
        attr << "Packed by s376-cache import-raster for " << icao << ".\n";
        attr << "Source: operator-supplied local rasters (read-only). Not committed.\n";
        if (const auto* src = airport_imagery(icao))
            attr << "Public alternative: " << src->public_label << " (" << src->license << ").\n";
        attr << "Do not commit this cache or the source folder.\n";
    }

    FetchRequest fr;
    fr.icao = icao;
    fr.max_export = kMaxServiceExportPx;
    const auto cells = tile_raster(uni, native_m, clamp_export_px(fr.max_export));
    std::size_t packed_tiles = 0;
    for (const auto& c : cells) {
        GeorefRgb scratch;
        scratch.west = c.box.west;
        scratch.south = c.box.south;
        scratch.east = c.box.east;
        scratch.north = c.box.north;
        scratch.img.w = c.w;
        scratch.img.h = c.h;
        scratch.img.rgb.assign(static_cast<std::size_t>(c.w * c.h * 3), 0);
        for (const auto& src : imgs) {
            ImageRgb piece = resample_box(src, c.box, c.w, c.h);
            for (std::size_t i = 0; i < scratch.img.rgb.size(); i += 3) {
                if (piece.rgb[i] || piece.rgb[i + 1] || piece.rgb[i + 2]) {
                    scratch.img.rgb[i] = piece.rgb[i];
                    scratch.img.rgb[i + 1] = piece.rgb[i + 1];
                    scratch.img.rgb[i + 2] = piece.rgb[i + 2];
                }
            }
        }
        DemGrid td = resample_dem(dem, c.box, std::min(c.w, 512), std::min(c.h, 512));
        apply_correction_overlay_icao(scratch.img, c.box, icao);
        const std::string tdir =
            out + "/lod0/t" + std::to_string(c.iy) + "_" + std::to_string(c.ix);
        std::filesystem::create_directories(tdir);
        td.save(tdir + "/elev.s376dem");
        dxt::write_dds_bc7(tdir + "/imagery.dds", scratch.img.rgb.data(), scratch.img.w,
                           scratch.img.h, true);
        ++packed_tiles;
    }

    CacheRegion region;
    region.name = icao;
    region.dem = resample_dem(dem, uni, 256, 256);
    GeorefRgb all;
    all.west = uni.west;
    all.south = uni.south;
    all.east = uni.east;
    all.north = uni.north;
    // Root drape is a coarse mosaic for HAT / older loaders.
    ImageRgb root_img;
    root_img.w = 768;
    root_img.h = 768;
    root_img.rgb.assign(768 * 768 * 3, 0);
    for (const auto& src : imgs) {
        ImageRgb piece = resample_box(src, uni, 768, 768);
        for (std::size_t i = 0; i < root_img.rgb.size(); i += 3) {
            if (piece.rgb[i] || piece.rgb[i + 1] || piece.rgb[i + 2]) {
                root_img.rgb[i] = piece.rgb[i];
                root_img.rgb[i + 1] = piece.rgb[i + 1];
                root_img.rgb[i + 2] = piece.rgb[i + 2];
            }
        }
    }
    region.imagery = root_img.rgb.empty() ? ImageRgb::hillshade(region.dem) : root_img;
    const auto bytes = pack_lods(region, out, default_lods(25), true);
    S376_LOG_INFO("cache", "import-raster %s  %zu tiles  %llu bytes  (local, not committed)",
                  icao.c_str(), packed_tiles, static_cast<unsigned long long>(bytes));
    (void)ec;
    return directory_bytes(out);
}

} // namespace s376
