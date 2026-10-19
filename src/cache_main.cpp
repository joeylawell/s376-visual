#include "s376/airport.hpp"
#include "s376/diagnostics.hpp"
#include "s376/dxt.hpp"
#include "s376/fetch.hpp"
#include "s376/imagery.hpp"
#include "s376/log.hpp"
#include "s376/region.hpp"
#include "s376/safety.hpp"
#include "s376/terrain.hpp"
#include "s376/version.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

void usage() {
    std::cerr
        << "s376-cache " << s376::version()
        << " — offline DEM / imagery packer (no RSI tiles)\n"
           "usage:\n"
           "  s376-cache fetch ICAO [--radius-km N] [--out DIR] [--dem auto|glo30|3dep]\n"
           "                       [--imagery auto|nys|hro|naip|s2|sanmateo|kingco|hillshade]\n"
           "                       [--dry-run] [--plan]\n"
           "                       [--near-m M] [--near-dem-m M] [--mid-m M] [--mid-dem-m M]\n"
           "                       [--far-m M] [--far-dem-m M]\n"
           "                       [--near-km N] [--mid-km N] [--approach-nm N] [--max-export N]\n"
           "                       [--jobs N] [--mem-budget MB] [--season summer|winter]\n"
           "  s376-cache synth-ksea [--out DIR] [--size N] [--lods]\n"
           "  s376-cache synth-kjfk-farfield [--out DIR]\n"
           "  s376-cache from-geotiff DEM.tif [--imagery FILE] [--out DIR] [--lods]\n"
           "  s376-cache import-raster DIR [--out cache/ICAO] [--icao ICAO] [--dry-run]\n"
           "  s376-cache pack-lods --from DIR --out DIR\n"
           "  s376-cache osm ICAO [--out DIR] [--radius-km N] [--dry-run]\n"
           "  s376-cache bake-osm DIR   stream-parse DIR/osm.json → DIR/osm.bin\n"
           "  s376-cache geojson ICAO [--out DIR]\n"
           "  s376-cache region ICAO [--out FILE]   print / write data/regions plan (--plan only)\n"
           "  s376-cache info DIR\n"
           "\n"
           "fetch downloads Copernicus GLO-30 (or USGS 3DEP) plus NYS / NAIP / S2\n"
           "into BC1/BC7 tiles with mipmaps. Metres/px default to 0 = native for\n"
           "the source (NYS 0.15, HRO 0.30, NAIP 0.60, S2 10; 3DEP 1, GLO-30 30)\n"
           "on the field and 12 nm finals; mid/far step up 4× / 16×. ImageServer\n"
           "exports cap at 2048 px (3DEP/NYS 500/504 on ~4k). Re-run fetch to\n"
           "resume: completed lodN/tY_X tiles are skipped. --plan prints every\n"
           "tile bbox and FAA coverage with no download. Disk is not a constraint\n"
           "(pack may exceed Raster's 857 GB). The 980 Ti RAM/VRAM pager still is.\n"
           "Downloaded rasters stay under cache/ (gitignored).\n"
           "osm pulls Overpass aeroway=taxiway|apron + building=* (ODbL) into DIR/osm.json.\n"
           "geojson writes a FeatureCollection for the osgEarth airport layer.\n"
           "import-raster reads Joey's own Raster airport folder (local, read-only)\n"
           "and writes our cache. It only imports georeferenced GeoTIFF/JPEG2000/\n"
           "JPEG+world/PNG+world. Proprietary pack files are skipped and the tool\n"
           "fails clearly if nothing decodable is on disk. Never commit the source\n"
           "or the packed cache. Never points at 10.1.95.0/24 or 192.168.173.0/24.\n";
}

bool looks_like_hostport(const std::string& s) {
    return s.find(':') != std::string::npos && s.find('/') == std::string::npos &&
           s.find('\\') == std::string::npos;
}

void refuse_lan_path(const std::string& label, const std::string& s) {
    if (!looks_like_hostport(s))
        return;
    s376::safety::Endpoint ep;
    std::string err;
    if (!s376::safety::parse_host_port(s, ep, err))
        return;
    (void)s376::safety::check_destination(ep.host, ep.port, label, false, false);
}

} // namespace

int main(int argc, char** argv) {
    s376::log::Options lo;
    lo.no_file = true;
    s376::log::setup(lo, "cache");

    if (argc < 2) {
        usage();
        s376::log::shutdown();
        return 2;
    }
    const std::string cmd = argv[1];
    if (cmd == "-h" || cmd == "--help") {
        usage();
        s376::log::shutdown();
        return 0;
    }
    if (cmd == "--version") {
        std::cout << s376::version() << "\n";
        s376::log::shutdown();
        return 0;
    }

    std::string out = "cache/ksea";
    std::string dem_path;
    std::string imagery_path;
    std::string from_dir;
    std::string icao;
    std::string dem_kind = "auto";
    std::string img_kind = "auto";
    int size = 128;
    double radius_km = s376::kFarFieldKm;
    double near_m = 0;
    double near_dem_m = 0;
    double mid_m = 0;
    double mid_dem_m = 0;
    double far_m = 0;
    double far_dem_m = 0;
    double near_km = 8.0;
    double mid_km = 40.0;
    double approach_nm = 12.0;
    int max_export = 2048;
    int jobs = 1;
    int mem_budget_mb = 0;
    bool lods = false;
    bool dry = false;
    bool plan_only = false;
    std::string season = "summer";
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        auto need = [&](const char* flag) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << flag << " needs an argument\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--out")
            out = need("--out");
        else if (a == "--from")
            from_dir = need("--from");
        else if (a == "--size")
            size = std::stoi(need("--size"));
        else if (a == "--radius-km")
            radius_km = std::stod(need("--radius-km"));
        else if (a == "--near-m")
            near_m = std::stod(need("--near-m"));
        else if (a == "--near-dem-m")
            near_dem_m = std::stod(need("--near-dem-m"));
        else if (a == "--mid-m")
            mid_m = std::stod(need("--mid-m"));
        else if (a == "--mid-dem-m")
            mid_dem_m = std::stod(need("--mid-dem-m"));
        else if (a == "--far-m")
            far_m = std::stod(need("--far-m"));
        else if (a == "--far-dem-m")
            far_dem_m = std::stod(need("--far-dem-m"));
        else if (a == "--near-km")
            near_km = std::stod(need("--near-km"));
        else if (a == "--mid-km")
            mid_km = std::stod(need("--mid-km"));
        else if (a == "--approach-nm")
            approach_nm = std::stod(need("--approach-nm"));
        else if (a == "--max-export")
            max_export = std::stoi(need("--max-export"));
        else if (a == "--jobs" || a == "-j")
            jobs = std::stoi(need(a.c_str()));
        else if (a == "--mem-budget")
            mem_budget_mb = std::stoi(need(a.c_str()));
        else if (a == "--dem")
            dem_kind = need("--dem");
        else if (a == "--imagery")
            img_kind = need("--imagery");
        else if (a == "--lods")
            lods = true;
        else if (a == "--dry-run")
            dry = true;
        else if (a == "--plan")
            plan_only = true;
        else if (a == "--season")
            season = need("--season");
        else if (a == "-h" || a == "--help") {
            usage();
            s376::log::shutdown();
            return 0;
        } else if (a == "--icao")
            icao = need("--icao");
        else if (a[0] != '-' && icao.empty() &&
                   (cmd == "fetch" || cmd == "osm" || cmd == "bake-osm" || cmd == "geojson" ||
                    cmd == "info" || cmd == "from-geotiff" || cmd == "pack-lods" ||
                    cmd == "import-raster" || cmd == "region")) {
            if (cmd == "fetch" || cmd == "osm" || cmd == "geojson" || cmd == "region")
                icao = a;
            else if (cmd == "import-raster")
                from_dir = a;
            else
                dem_path = a;
        } else {
            std::cerr << "unknown option " << a << "\n";
            usage();
            s376::log::shutdown();
            return 2;
        }
    }

    try {
        refuse_lan_path("cache dest", out);
        if (cmd == "fetch") {
            if (icao.empty()) {
                std::cerr << "fetch needs an ICAO (KSEA, KSFO, KLAS, …)\n";
                s376::log::shutdown();
                return 2;
            }
            s376::FetchRequest req;
            req.icao = icao;
            req.radius_km = radius_km;
            req.dem = dem_kind;
            req.imagery = img_kind;
            req.out_dir = out;
            req.season = season;
            req.dry_run = dry || plan_only;
            req.plan_only = plan_only;
            req.near_m = near_m;
            req.near_dem_m = near_dem_m;
            req.mid_m = mid_m;
            req.mid_dem_m = mid_dem_m;
            req.far_m = far_m;
            req.far_dem_m = far_dem_m;
            req.near_radius_km = near_km;
            req.mid_radius_km = mid_km;
            req.approach_nm = approach_nm;
            req.max_export = max_export;
            req.jobs = s376::fetch_jobs_for_budget(jobs, mem_budget_mb);
            req.mem_budget_mb = mem_budget_mb;
            s376::FetchPlan plan;
            const auto bytes = s376::fetch_and_pack(req, &plan);
            const auto apt = s376::lookup_airport_ref(icao);
            if (plan_only || dry) {
                std::cout << s376::format_fetch_plan(plan, apt, req);
                if (plan_only && !out.empty()) {
                    std::filesystem::create_directories(out);
                    const std::string plan_path = out + "/fetch.plan.toml";
                    if (s376::write_fetch_plan_toml(plan_path, plan, req))
                        std::cout << "wrote " << plan_path << "  (no download)\n";
                }
                if (!plan_only) {
                    int n = 0;
                    for (const auto& u : plan.urls) {
                        std::cout << "  url " << u << "\n";
                        if (++n >= 8) {
                            std::cout << "  … " << plan.urls.size() - 8 << " more urls\n";
                            break;
                        }
                    }
                }
                std::cout << plan.attribution;
                s376::log::shutdown();
                return plan.covers_faa ? 0 : 1;
            }
            std::cout << plan.icao << " dem=" << plan.dem_kind << " img=" << plan.img_kind
                      << " lods=" << plan.lods.size() << "\n";
            std::cout << "  estimate tiles img=" << plan.estimate.img_tiles
                      << " dem=" << plan.estimate.dem_tiles << " raw~"
                      << s376::format_byte_count(plan.estimate.est_raw_bytes) << " packed~"
                      << s376::format_byte_count(plan.estimate.est_packed_bytes) << "\n";
            for (const auto& lod : plan.lods) {
                std::cout << "  lod" << lod.level << " r=" << lod.radius_km << "km  img "
                          << lod.img_m << "m/px  dem " << lod.dem_m << "m/px"
                          << (lod.bc7 ? "  BC7" : "  BC1") << (lod.tiled ? "  tiled" : "") << "\n";
            }
            std::cout << plan.attribution;
            std::cout << "wrote " << out << "  " << bytes << " bytes  "
                      << s376::count_cache_tiles(out) << " tiles (BC1/BC7 + s376dem + mips)\n";
            s376::log::shutdown();
            return 0;
        }
        if (cmd == "import-raster") {
            if (from_dir.empty()) {
                std::cerr << "import-raster needs a local airport directory\n";
                s376::log::shutdown();
                return 2;
            }
            refuse_lan_path("import-raster src", from_dir);
            s376::ImportRequest ireq;
            ireq.src_dir = from_dir;
            ireq.out_dir = (out == "cache/ksea") ? "" : out;
            ireq.icao = icao;
            ireq.dry_run = dry;
            s376::ImportScan scan;
            if (dry) {
                scan = s376::scan_import_dir(from_dir);
                if (scan.icao.empty() && !icao.empty())
                    scan.icao = icao;
                std::cout << s376::format_import_scan(scan);
                std::cout << s376::format_import_needed() << "\n";
                s376::log::shutdown();
                return scan.decodable > 0 && scan.error.empty() ? 0 : 1;
            }
            const auto n = s376::import_raster(ireq, &scan);
            std::cout << s376::format_import_scan(scan);
            std::cout << "wrote " << (ireq.out_dir.empty() ? ("cache/" + scan.icao) : ireq.out_dir)
                      << "  " << n << " bytes  (local import; do not commit)\n";
            s376::log::shutdown();
            return 0;
        }
        if (cmd == "osm") {
            if (icao.empty()) {
                std::cerr << "osm needs an ICAO (KSEA, KSFO, KLAS, …)\n";
                s376::log::shutdown();
                return 2;
            }
            const auto ref = s376::lookup_airport_ref(icao);
            const double rkm = radius_km > 0 ? std::min(radius_km, 12.0) : 6.0;
            const auto box = s376::box_from_radius(ref.lat, ref.lon, rkm);
            const auto url =
                s376::overpass_aeroway_url(box.south, box.west, box.north, box.east);
            std::string dest = out;
            if (dest.empty() || dest == "cache/ksea")
                dest = std::string("cache/") + ref.icao;
            const auto query =
                s376::overpass_aeroway_query(box.south, box.west, box.north, box.east);
            std::cout << ref.icao << " overpass " << url << "\n";
            if (!s376::curl_glob_safe(url)) {
                std::cerr << "overpass URL is not curl-glob-safe (raw [ ]); this is a bug\n";
                s376::log::shutdown();
                return 1;
            }
            if (dry) {
                s376::log::shutdown();
                return 0;
            }
            std::filesystem::create_directories(dest);
            const std::string json_path = dest + "/osm.json";
            // POST the QL so [ ] never sit in the URL; GET with -g is the fallback.
            if (!s376::http_post_file("https://overpass-api.de/api/interpreter", query, json_path) &&
                !s376::http_get_file(url, json_path)) {
                std::cerr << "overpass download failed\n";
                s376::log::shutdown();
                return 1;
            }
            s376::Airport apt = s376::airport_by_icao(ref.icao);
            const int n = s376::bake_osm_bin(json_path, dest + "/osm.bin");
            (void)s376::read_osm_bin(apt, dest + "/osm.bin");
            s376::write_airport_geojson(apt, dest + "/osm.geojson");
            std::cout << "wrote " << dest << "/osm.bin + solids.bin  " << n
                      << " OSM ways (ODbL — © OSM). IG does not load osm.json.\n";
            s376::log::shutdown();
            return 0;
        }
        if (cmd == "bake-osm") {
            std::string dest = !dem_path.empty() ? dem_path : out;
            if (dest.empty() || dest == "cache/ksea")
                dest = "cache/KJFK";
            const std::string json_path = dest + "/osm.json";
            const std::string bin_path = dest + "/osm.bin";
            const int n = s376::bake_osm_bin(json_path, bin_path);
            if (n <= 0 && !std::filesystem::exists(json_path)) {
                std::cerr << "bake-osm: missing " << json_path << "\n";
                s376::log::shutdown();
                return 1;
            }
            std::cout << "baked " << bin_path << "  " << n << " ways from " << json_path << "\n";
            s376::log::shutdown();
            return 0;
        }
        if (cmd == "geojson") {
            if (icao.empty()) {
                std::cerr << "geojson needs an ICAO (KSEA, KSFO, KLAS, …)\n";
                s376::log::shutdown();
                return 2;
            }
            s376::Airport apt = s376::airport_by_icao(icao);
            std::string dest = out;
            if (dest.empty() || dest == "cache/ksea")
                dest = std::string("cache/") + apt.icao;
            (void)s376::try_load_osm(apt, dest);
            const std::string path = dest + "/osm.geojson";
            if (!s376::write_airport_geojson(apt, path)) {
                std::cerr << "could not write " << path << "\n";
                s376::log::shutdown();
                return 1;
            }
            std::cout << "wrote " << path << "  " << apt.runways.size() << " runways  "
                      << apt.taxiways.size() << " taxi  " << apt.aprons.size() << " aprons\n";
            s376::log::shutdown();
            return 0;
        }
        if (cmd == "synth-ksea") {
            s376::CacheRegion r = s376::generate_ksea_cache();
            if (size != 128)
                r.dem = s376::DemGrid::synth_ksea(size, size);
            r.imagery = s376::ImageRgb::hillshade(r.dem);
            r.name = "ksea";
            if (lods) {
                const auto n = s376::pack_lods(r, out, s376::default_lods(25), true);
                std::cout << "wrote " << out << "  " << n << " bytes (LOD pyramid)\n";
            } else {
                s376::write_cache(r, out);
                const auto packed = std::filesystem::file_size(out + "/elev.s376dem") +
                                    std::filesystem::file_size(out + "/imagery.dds");
                std::cout << "wrote " << out << "  " << packed << " bytes (BC1 + s376dem)\n";
            }
            s376::write_airport_geojson(s376::ksea_public(), out + "/osm.geojson");
            std::cout << "synth pack is a laptop stand-in. Real fetch may exceed Raster's\n"
                      << "857 GB CERTS farm; disk is not a constraint — quality wins.\n";
            s376::log::shutdown();
            return 0;
        }
        if (cmd == "synth-kjfk-farfield") {
            if (out == "cache/ksea")
                out = "/tmp/kjfk-farfield";
            if (!s376::write_kjfk_farfield_subset(out)) {
                std::cerr << "synth-kjfk-farfield failed for " << out << "\n";
                s376::log::shutdown();
                return 1;
            }
            std::cout << "wrote " << out
                      << "  Joey layout (ATTRIBUTION, coverage.toml, lod0/lod1/lod2, "
                         "region.toml)  public land_coverage, 40 nm lod2\n";
            s376::log::shutdown();
            return 0;
        }
        if (cmd == "from-geotiff") {
            if (dem_path.empty()) {
                std::cerr << "from-geotiff needs a DEM GeoTIFF path\n";
                s376::log::shutdown();
                return 2;
            }
            if (!s376::gdal_available()) {
                std::cerr << "this binary was built without GDAL (install libgdal-dev, re-cmake)\n";
                s376::log::shutdown();
                return 1;
            }
            s376::CacheRegion r;
            r.name = "region";
            r.dem = s376::DemGrid::load_geotiff(dem_path);
            if (!imagery_path.empty() && img_kind != "auto")
                r.imagery = s376::ImageRgb::load_jpeg_or_png(imagery_path);
            else if (!imagery_path.empty())
                r.imagery = s376::ImageRgb::load_jpeg_or_png(imagery_path);
            if (r.imagery.rgb.empty())
                r.imagery = s376::ImageRgb::hillshade(r.dem);
            if (lods)
                s376::pack_lods(r, out, s376::default_lods(25), true);
            else
                s376::write_cache(r, out);
            std::cout << "wrote " << out << "  " << r.dem.w << "x" << r.dem.h << " from "
                      << dem_path << "\n";
            s376::log::shutdown();
            return 0;
        }
        if (cmd == "pack-lods") {
            const std::string src = from_dir.empty() ? dem_path : from_dir;
            if (src.empty()) {
                std::cerr << "pack-lods needs --from DIR\n";
                s376::log::shutdown();
                return 2;
            }
            auto r = s376::load_cache(src);
            const auto n = s376::pack_lods(r, out, s376::default_lods(25), true);
            std::cout << "wrote " << out << "  " << n << " bytes\n";
            s376::log::shutdown();
            return 0;
        }
        if (cmd == "info") {
            const std::string dir = dem_path.empty() ? out : dem_path;
            auto r = s376::load_cache(dir);
            std::cout << "name bounds " << r.dem.west << "," << r.dem.south << " .. " << r.dem.east
                      << "," << r.dem.north << "\n"
                      << "grid " << r.dem.w << "x" << r.dem.h << "\n"
                      << "imagery " << r.imagery.w << "x" << r.imagery.h << "\n"
                      << "dds " << r.imagery_dds << "\n"
                      << "bytes " << s376::directory_bytes(dir) << "\n"
                      << "gdal " << (s376::gdal_available() ? "yes" : "no") << "\n";
            for (int i = 0; i < 4; ++i) {
                const auto lod = dir + "/lod" + std::to_string(i);
                if (std::filesystem::exists(lod + "/elev.s376dem"))
                    std::cout << "lod" << i << " " << s376::directory_bytes(lod) << " bytes\n";
            }
            double z = 0;
            if (r.dem.sample_ft(47.4479, -122.3103, z))
                std::cout << "sample KSEA ARP " << z << " ft MSL\n";
            s376::log::shutdown();
            return 0;
        }
        if (cmd == "region") {
            if (icao.empty()) {
                std::cerr << "region needs an ICAO (or KJFK-near)\n";
                s376::log::shutdown();
                return 2;
            }
            s376::RegionPlan p;
            if (icao == "KJFK-near" || icao == "kjfk-near")
                p = s376::kjfk_nearfield_03m_plan();
            else
                p = s376::region_plan_for_icao(icao);
            const std::string dest =
                (out == "cache/ksea") ? s376::region_plan_path(p.name == "KJFK-near" ? "kjfk-near" : p.icao)
                                      : out;
            if (!s376::write_region_plan(dest, p)) {
                std::cerr << "cannot write " << dest << "\n";
                s376::log::shutdown();
                return 1;
            }
            std::cout << s376::format_region_plan(p);
            std::cout << "wrote " << dest << "  (--plan only, no download)\n";
            s376::log::shutdown();
            return 0;
        }
        std::cerr << "unknown command " << cmd << "\n";
        usage();
        s376::log::shutdown();
        return 2;
    } catch (const s376::safety::UnsafeEndpoint& ex) {
        std::cerr << ex.what() << "\n";
        s376::log::shutdown();
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << "\n";
        s376::log::shutdown();
        return 1;
    }
}
