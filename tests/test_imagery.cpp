#include "s376/fetch.hpp"
#include "s376/imagery.hpp"
#include "s376/safety.hpp"
#include "s376/terrain.hpp"

#include <cmath>
#include <filesystem>
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
                      << #b << "=" << _b << " eps=" << (eps) << "\n";                              \
        }                                                                                          \
    } while (0)

void test_imagery() {
    int n = 0;
    const auto* t = s376::airport_imagery_table(&n);
    CHECK(n >= 18);
    CHECK(t);

    const auto* jfk = s376::airport_imagery("kjfk");
    CHECK(jfk && jfk->public_kind == std::string("nys"));
    CHECK_NEAR(jfk->public_m, 0.15, 1e-9);
    CHECK(jfk->policy == s376::ImageryPolicy::Public);
    CHECK(!s376::needs_raster_fallback("KJFK"));

    const auto* ewr = s376::airport_imagery("KEWR");
    CHECK(ewr && ewr->public_kind == std::string("njogis"));
    CHECK(std::string(ewr->public_label).find("njogis-imagery") != std::string::npos);

    const auto* rno = s376::airport_imagery("KRNO");
    CHECK(rno && rno->policy == s376::ImageryPolicy::Either);
    CHECK(rno->public_kind == std::string("naip"));

    CHECK(s376::needs_raster_fallback("SKRG"));
    CHECK(s376::needs_raster_fallback("VNKT"));
    const auto* vkt = s376::airport_imagery("VNKT");
    CHECK(vkt && vkt->public_m >= 10.0);

    CHECK(s376::imagery_kind_is_live_pull("nys"));
    CHECK(s376::imagery_kind_is_live_pull("sanmateo"));
    CHECK(s376::imagery_kind_is_live_pull("kingco"));
    CHECK(!s376::imagery_kind_is_live_pull("njogis"));
    CHECK(!s376::imagery_kind_is_live_pull("raster-local"));

    const auto table = s376::format_imagery_sources_table();
    CHECK(table.find("KJFK") != std::string::npos);
    CHECK(table.find("SKRG") != std::string::npos);
    CHECK(table.find("import-raster") != std::string::npos);

    CHECK(s376::guess_icao_from_path("/data/Raster/KJFK_KEWR") == "KJFK" ||
          s376::guess_icao_from_path("/data/Raster/KJFK_KEWR") == "KEWR");
    CHECK(s376::guess_icao_from_path("/home/joey/VNKT") == "VNKT");
    CHECK(s376::guess_icao_from_path("/tmp/nope").empty());

    s376::FetchRequest auto_vkt;
    auto_vkt.icao = "VNKT";
    const auto rv = s376::resolved_fetch(auto_vkt);
    CHECK(rv.imagery == "raster-local");
    s376::FetchRequest auto_rno;
    auto_rno.icao = "KRNO";
    CHECK(s376::resolved_fetch(auto_rno).imagery == "naip");
    s376::FetchRequest auto_sfo;
    auto_sfo.icao = "KSFO";
    CHECK(s376::resolved_fetch(auto_sfo).imagery == "sanmateo");

    bool threw = false;
    try {
        s376::FetchRequest bad;
        bad.icao = "VNKT";
        bad.out_dir = (std::filesystem::temp_directory_path() / "s376-no-vkt").string();
        s376::fetch_and_pack(bad);
    } catch (const std::exception& ex) {
        threw = std::string(ex.what()).find("import-raster") != std::string::npos;
    }
    CHECK(threw);

    const auto repo = std::filesystem::path(__FILE__).parent_path().parent_path().string();
    auto scan_repo = s376::scan_import_dir(repo);
    CHECK(!scan_repo.error.empty());

    const auto tmp = (std::filesystem::temp_directory_path() / "s376-import-opaque").string();
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp + "/VNKT");
    {
        std::ofstream o(tmp + "/VNKT/tile.bin");
        o << "not a raster";
    }
    {
        std::ofstream o(tmp + "/VNKT/pack.nxt");
        o << "opaque";
    }
    auto scan = s376::scan_import_dir(tmp + "/VNKT");
    CHECK(scan.icao == "VNKT");
    CHECK(scan.decodable == 0);
    CHECK(scan.opaque >= 1);
    CHECK(s376::format_import_scan(scan).find(".nxt") != std::string::npos);
    threw = false;
    try {
        s376::ImportRequest ireq;
        ireq.src_dir = tmp + "/VNKT";
        ireq.out_dir = tmp + "/out";
        ireq.icao = "VNKT";
        s376::import_raster(ireq);
    } catch (const std::exception& ex) {
        const std::string w = ex.what();
        threw = w.find("no georeferenced") != std::string::npos &&
                w.find("needed:") != std::string::npos;
    }
    CHECK(threw);
    CHECK(s376::format_import_needed().find("GeoTIFF") != std::string::npos);
    std::filesystem::remove_all(tmp);

    if (s376::gdal_available()) {
        const auto src = (std::filesystem::temp_directory_path() / "s376-import-ok").string();
        std::filesystem::remove_all(src);
        std::filesystem::create_directories(src + "/KJFK");
        auto dem = s376::DemGrid::synth_ksea(32, 32);
        dem.west = -73.80;
        dem.east = -73.75;
        dem.south = 40.62;
        dem.north = 40.66;
        const auto img = s376::ImageRgb::hillshade(dem);
        img.save_geotiff(src + "/KJFK/ortho.tif", dem);
        s376::ImportRequest ireq;
        ireq.src_dir = src + "/KJFK";
        ireq.out_dir = src + "/out";
        ireq.icao = "KJFK";
        s376::ImportScan ok;
        const auto bytes = s376::import_raster(ireq, &ok);
        CHECK(ok.decodable >= 1);
        CHECK(bytes > 1000);
        CHECK(std::filesystem::exists(ireq.out_dir + "/elev.s376dem"));
        CHECK(std::filesystem::exists(ireq.out_dir + "/ATTRIBUTION.txt"));
        std::filesystem::remove_all(src);
    }

    CHECK(s376::safety::is_allowed_data_host("gis.smcgov.org"));
    CHECK(s376::safety::is_allowed_data_host("gismaps.kingcounty.gov"));
}
