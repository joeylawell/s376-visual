#include "s376/accuracy.hpp"
#include "s376/dxt.hpp"
#include "s376/fetch.hpp"
#include "s376/safety.hpp"
#include "s376/terrain.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
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

void test_fetch() {
    auto ksea = s376::lookup_airport_ref("ksea");
    CHECK(ksea.icao == "KSEA");
    CHECK(ksea.conus);
    CHECK_NEAR(ksea.lat, 47.449833, 1e-4);
    auto sfo = s376::lookup_airport_ref("KSFO");
    CHECK(sfo.conus);
    auto egll = s376::lookup_airport_ref("EGLL");
    CHECK(!egll.conus);

    const auto box = s376::box_from_radius(47.45, -122.31, 25);
    CHECK(box.west < -122.31 && box.east > -122.31);
    CHECK(box.south < 47.45 && box.north > 47.45);

    const auto url = s376::glo30_tile_url(47, -123);
    CHECK(url.find("N47") != std::string::npos);
    CHECK(url.find("W123") != std::string::npos);
    CHECK(url.find("Copernicus_DSM_COG_10") != std::string::npos);
    s376::safety::check_data_url(url);

    const auto tiles = s376::glo30_urls_for_box(box);
    CHECK(!tiles.empty());
    CHECK(tiles.size() <= 9);

    bool threw = false;
    try {
        s376::safety::check_data_url("http://10.1.95.1/x");
    } catch (const s376::safety::UnsafeEndpoint&) {
        threw = true;
    }
    CHECK(threw);
    threw = false;
    try {
        s376::safety::check_data_url("https://evil.example/x");
    } catch (const s376::safety::UnsafeEndpoint&) {
        threw = true;
    }
    CHECK(threw);
    CHECK(s376::safety::is_allowed_data_host("copernicus-dem-30m.s3.eu-central-1.amazonaws.com"));
    CHECK(s376::safety::is_allowed_data_host("elevation.nationalmap.gov"));
    CHECK(s376::safety::is_allowed_data_host("tiles.maps.eox.at"));
    CHECK(s376::safety::is_allowed_data_host("dl.polyhaven.org"));
    CHECK(s376::safety::is_allowed_data_host("ambientcg.com"));
    CHECK(s376::safety::is_allowed_data_host("orthos.its.ny.gov"));
    CHECK(s376::safety::is_allowed_data_host("its.ny.gov"));
    CHECK(s376::safety::is_allowed_data_host("imagery.nationalmap.gov"));
    CHECK(s376::safety::is_allowed_data_host("gis.smcgov.org"));
    CHECK(s376::safety::is_allowed_data_host("gismaps.kingcounty.gov"));
    CHECK(!s376::safety::is_allowed_data_host("10.1.95.1"));
    CHECK(s376::imagery_covers_nys(40.64, -73.78));
    CHECK(!s376::imagery_covers_nys(47.45, -122.31));
    s376::GeoBox nyc;
    nyc.west = -73.90;
    nyc.south = 40.60;
    nyc.east = -73.70;
    nyc.north = 40.70;
    const auto nys_url = s376::imagery_export_url("nys", nyc, 512, 512);
    CHECK(nys_url.find("orthos.its.ny.gov") != std::string::npos);
    CHECK(s376::curl_glob_safe(nys_url));
    s376::safety::check_data_url(nys_url);
    const auto hro_url = s376::imagery_export_url("hro", nyc, 512, 512);
    CHECK(hro_url.find("USGSNAIPPlus") != std::string::npos);
    s376::safety::check_data_url(hro_url);

    CHECK_NEAR(s376::native_imagery_m("nys"), 0.15, 1e-9);
    CHECK_NEAR(s376::native_imagery_m("hro"), 0.30, 1e-9);
    CHECK_NEAR(s376::native_imagery_m("naip"), 0.60, 1e-9);
    CHECK_NEAR(s376::native_imagery_m("s2"), 10.0, 1e-9);
    CHECK_NEAR(s376::native_dem_m("3dep"), 1.0, 1e-9);
    CHECK_NEAR(s376::native_dem_m("glo30"), 30.0, 1e-9);

    s376::FetchRequest req;
    req.icao = "KSEA";
    req.radius_km = 25;
    req.dry_run = true;
    CHECK(req.jobs == 1);
    req.jobs = 16;
    CHECK(req.jobs == 16);
    req.jobs = 1;
    CHECK(req.season == "summer");
    req.season = "winter";
    CHECK(req.season == "winter");
    req.season = "summer";
    const auto resolved_ksea = s376::resolved_fetch(req);
    CHECK(resolved_ksea.dem == "3dep");
    CHECK(resolved_ksea.imagery == "kingco");
    CHECK_NEAR(resolved_ksea.near_m, 0.08, 1e-6);
    CHECK_NEAR(resolved_ksea.near_dem_m, 1.0, 1e-6);
    CHECK_NEAR(resolved_ksea.mid_m, 0.32, 1e-6);
    CHECK_NEAR(resolved_ksea.far_m, 1.28, 1e-6);
    CHECK_NEAR(resolved_ksea.near_radius_km, 8.0, 1e-6);
    CHECK_NEAR(resolved_ksea.approach_nm, 12.0, 1e-6);
    {
        s376::FetchRequest def;
        def.icao = "KJFK";
        def.radius_km = 0;
        def.dry_run = true;
        const auto r = s376::resolved_fetch(def);
        CHECK(r.radius_km + 0.01 >= s376::kFarFieldKm);
        const auto lods = s376::fetch_lods(def);
        CHECK(lods.size() >= 3);
        CHECK(lods.back().level == 2);
        CHECK(lods.back().radius_km + 0.01 >= 40.0 * 1.852);
    }
    {
        s376::FetchRequest b;
        CHECK(s376::fetch_rss_bound_bytes(b) == s376::kFetchRssBoundBytes);
        b.mem_budget_mb = 512;
        CHECK(s376::fetch_rss_bound_bytes(b) == (512ull << 20));
        b.mem_budget_mb = 2048;
        CHECK(s376::fetch_rss_bound_bytes(b) == (2048ull << 20));
        CHECK(s376::fetch_jobs_for_budget(16, 0) == 16);
        CHECK(s376::fetch_jobs_for_budget(16, 512) == 2);
        CHECK(s376::fetch_jobs_for_budget(16, 1536) == 4);
        CHECK(s376::fetch_jobs_for_budget(16, 2048) == 16);
        CHECK(s376::fetch_jobs_for_budget(1, 512) == 1);
    }
    auto plan = s376::plan_fetch(req);
    CHECK(plan.dem_kind == "3dep");
    CHECK(plan.img_kind == "kingco");
    CHECK(plan.lods.size() >= 2);
    CHECK(plan.lods[0].bc7);
    CHECK(plan.lods[0].tiled);
    CHECK_NEAR(plan.lods[0].img_m, 0.08, 1e-6);
    CHECK_NEAR(plan.lods[0].dem_m, 1.0, 1e-6);
    CHECK(!plan.urls.empty());
    CHECK(plan.estimate.img_tiles >= 4);
    CHECK(plan.estimate.dem_tiles >= 2);
    CHECK(plan.estimate.est_packed_bytes > 1024 * 1024);
    for (const auto& u : plan.urls)
        CHECK(s376::curl_glob_safe(u));

    s376::GeoBox tb;
    tb.west = -122.40;
    tb.east = -122.20;
    tb.south = 47.40;
    tb.north = 47.52;
    const auto rast = s376::tile_raster(tb, 1.0, 4000);
    CHECK(rast.size() >= 4);
    CHECK(rast.front().iy == 0);
    CHECK_NEAR(rast.front().box.south, tb.south, 1e-9); // iy=0 is south
    CHECK(rast.front().ix == 0);
    CHECK_NEAR(rast.front().box.west, tb.west, 1e-9);
    for (const auto& t : rast) {
        CHECK(t.w <= 4000 && t.h <= 4000);
        CHECK(t.w >= 8 && t.h >= 8);
        CHECK(t.box.east > t.box.west);
        CHECK(t.box.north > t.box.south);
    }
    CHECK(s376::clamp_export_px(4000) == s376::kMaxServiceExportPx);
    CHECK(s376::clamp_export_px(0) == s376::kMaxServiceExportPx);
    CHECK(s376::clamp_export_px(1024) == 1024);
    CHECK(s376::kMaxServiceExportPx == 2048);
    const auto cov = s376::coverage_boxes(ksea, req);
    CHECK(cov.size() >= 2);
    const auto uni = s376::union_boxes(cov);
    CHECK(uni.east > uni.west);
    const auto est = s376::estimate_fetch(req);
    CHECK(est.img_tiles == plan.estimate.img_tiles);
    CHECK(plan.attribution.find("GLO-30") != std::string::npos ||
          plan.attribution.find("3DEP") != std::string::npos);

    req.icao = "EGLL";
    plan = s376::plan_fetch(req);
    CHECK(plan.dem_kind == "glo30");
    CHECK(plan.img_kind == "ea-vap");

    req.icao = "KJFK";
    req.imagery = "auto";
    const auto resolved_jfk = s376::resolved_fetch(req);
    CHECK(resolved_jfk.imagery == "nys");
    CHECK_NEAR(resolved_jfk.near_m, 0.15, 1e-6);
    CHECK_NEAR(resolved_jfk.mid_m, 0.60, 1e-6);
    CHECK_NEAR(resolved_jfk.far_m, 2.40, 1e-6);
    s376::FetchRequest override_nys;
    override_nys.icao = "KJFK";
    override_nys.near_m = 1.0; // operator downsample stays 1; do not force native
    CHECK_NEAR(s376::resolved_fetch(override_nys).near_m, 1.0, 1e-6);
    plan = s376::plan_fetch(req);
    CHECK(plan.dem_kind == "3dep");
    CHECK(plan.img_kind == "nys");
    CHECK_NEAR(plan.lods[0].img_m, 0.15, 1e-6);
    CHECK_NEAR(plan.lods[0].dem_m, 1.0, 1e-6);
    bool saw_nys = false;
    for (const auto& u : plan.urls) {
        CHECK(s376::curl_glob_safe(u));
        if (u.find("orthos.its.ny.gov") != std::string::npos)
            saw_nys = true;
    }
    CHECK(saw_nys);
    CHECK(plan.attribution.find("NYS") != std::string::npos);
    CHECK(!plan.tiles.empty());
    CHECK(plan.covers_faa);
    {
        s376::FetchRequest jfk_hangar;
        jfk_hangar.icao = "KJFK";
        jfk_hangar.dem = "3dep";
        jfk_hangar.imagery = "nys";
        jfk_hangar.near_m = 1.0;
        jfk_hangar.near_dem_m = 1.0;
        jfk_hangar.near_radius_km = 5.0;
        jfk_hangar.approach_nm = 10.0;
        jfk_hangar.radius_km = 35.0;
        jfk_hangar.max_export = 4000; // clamped to 2048
        const auto japt = s376::lookup_airport_ref("KJFK");
        const auto jplan = s376::plan_fetch(jfk_hangar);
        CHECK(jplan.covers_faa);
        CHECK(jplan.uncovered.empty());
        CHECK(!jplan.tiles.empty());
        int max_px = 0, min_iy = 99, max_ix = -1, n0 = 0;
        bool saw_arp = false;
        for (const auto& t : jplan.tiles) {
            max_px = std::max(max_px, std::max(t.dem_w, std::max(t.dem_h, std::max(t.img_w, t.img_h))));
            if (t.lod != 0)
                continue;
            ++n0;
            min_iy = std::min(min_iy, t.iy);
            max_ix = std::max(max_ix, t.ix);
            if (s376::box_contains(t.box, japt.lat, japt.lon, 0))
                saw_arp = true;
        }
        CHECK(max_px <= s376::kMaxServiceExportPx);
        CHECK(min_iy == 0);
        CHECK(n0 >= 4);
        CHECK(saw_arp);
        const std::string text = s376::format_fetch_plan(jplan, japt, jfk_hangar);
        CHECK(text.find("t0_0") != std::string::npos);
        CHECK(text.find("COVERAGE ok") != std::string::npos);
        CHECK(text.find("ARP") != std::string::npos);
        // Dense ids: every (ix,iy) in the lod0 rectangle exists.
        int nx = 0, ny = 0;
        for (const auto& t : jplan.tiles) {
            if (t.lod == 0) {
                nx = std::max(nx, t.ix + 1);
                ny = std::max(ny, t.iy + 1);
            }
        }
        std::vector<int> seen(static_cast<std::size_t>(nx * ny), 0);
        for (const auto& t : jplan.tiles) {
            if (t.lod == 0)
                seen[static_cast<std::size_t>(t.iy * nx + t.ix)] = 1;
        }
        int holes = 0;
        for (int v : seen)
            holes += v ? 0 : 1;
        CHECK(holes == 0);
    }

    auto src = s376::generate_ksea_cache();
    const auto tmp = (std::filesystem::temp_directory_path() / "s376-lod-test").string();
    std::filesystem::remove_all(tmp);
    const auto n = s376::pack_lods(src, tmp, s376::default_lods(25), true);
    CHECK(n > 1000);
    CHECK(n < 50ull * 1024 * 1024);
    CHECK(std::filesystem::exists(tmp + "/elev.s376dem"));
    CHECK(std::filesystem::exists(tmp + "/lod0/elev.s376dem"));
    CHECK(std::filesystem::exists(tmp + "/lod1/imagery.dds"));
    CHECK(std::filesystem::exists(tmp + "/corrections.txt"));
    const auto dds0 = std::filesystem::file_size(tmp + "/lod0/imagery.dds");
    CHECK(dds0 > 128 + s376::dxt::bc7_size(4, 4)); // BC7 near field + header

    // BC7 encode is larger than BC1 and still far smaller than raw RGB.
    std::vector<std::uint8_t> red(16 * 16 * 3, 200);
    auto b7 = s376::dxt::encode_bc7(red.data(), 16, 16);
    CHECK(b7.size() == s376::dxt::bc7_size(16, 16));
    CHECK(s376::dxt::mip_count(16, 16) == 5);

    std::filesystem::remove_all(tmp);

    // Streaming fetch: many large DEM tiles must not accumulate in RSS.
    // 48 × 1024² float = 192 MiB if kept; one tile is ~4 MiB.
    const auto rdir = (std::filesystem::temp_directory_path() / "s376-fetch-rss").string();
    std::filesystem::remove_all(rdir);
    const auto rss0 = s376::current_rss_bytes();
    const auto peak = s376::bench_stream_pack_rss(rdir, 48, 1024, 128);
    CHECK(peak > 0);
    CHECK(peak < s376::kFetchRssBoundBytes);
    CHECK(peak < rss0 + (128ull << 20));
    CHECK(std::filesystem::exists(rdir + "/elev.s376dem"));
    CHECK(s376::count_cache_tiles(rdir) >= 40);
    {
        s376::GeoBox uni;
        uni.west = -73.80;
        uni.south = 40.60;
        uni.east = -73.72;
        uni.north = 40.68;
        const auto merged = s376::merge_root_dem_from_cache(rdir, uni, 32);
        CHECK(merged.w == 32 && merged.h == 32);
        double z = 0;
        CHECK(merged.sample_m(40.605, -73.795, z));
        CHECK_NEAR(z, 12.0, 0.5);
    }
    std::filesystem::remove_all(rdir);
}
