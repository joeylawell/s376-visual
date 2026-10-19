#include "s376/fetch.hpp"
#include "s376/imagery.hpp"
#include "s376/region.hpp"

#include <cmath>
#include <filesystem>
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

void test_region() {
    const auto icaos = s376::raster_airport_icaos();
    CHECK(icaos.size() >= 18);
    bool saw_jfk = false, saw_ewr = false, saw_rno = false, saw_skrg = false, saw_vnkt = false;
    for (const auto& id : icaos) {
        if (id == "KJFK")
            saw_jfk = true;
        if (id == "KEWR")
            saw_ewr = true;
        if (id == "KRNO")
            saw_rno = true;
        if (id == "SKRG")
            saw_skrg = true;
        if (id == "VNKT")
            saw_vnkt = true;
        const auto generated = s376::region_plan_for_icao(id);
        CHECK(generated.icao == id);
        CHECK(generated.north > generated.south);
        CHECK(generated.east > generated.west);
        CHECK(generated.radius_km + 0.01 >= 40.0 * 1.852);
        const auto path = s376::region_plan_path(id);
        CHECK(std::filesystem::exists(path));
        const auto p = s376::load_region_plan(path);
        CHECK(p.icao == id);
        CHECK(p.north > p.south && p.east > p.west);
        const auto apt = s376::lookup_airport_ref(id);
        CHECK(apt.lat >= p.south && apt.lat <= p.north);
        CHECK(apt.lon >= p.west && apt.lon <= p.east);
        if (id == "SKRG" || id == "VNKT") {
            CHECK(p.local_import);
            CHECK(p.policy == "raster-local" || p.imagery == "raster-local");
        }
        if (id == "KRNO")
            CHECK(p.policy == "either" && p.local_import);
        auto req = s376::fetch_request_from_region(p, "/tmp/s376-plan-" + id);
        CHECK(req.plan_only && req.dry_run);
        req.max_export = 256;
        if (p.near_m > 0.5)
            req.near_m = std::max(p.near_m, 10.0); // keep --plan cheap in unit tests
        if (!p.local_import || id == "KRNO") {
            const auto plan = s376::plan_fetch(req);
            CHECK(plan.icao == id);
            CHECK(!plan.tiles.empty());
        }
    }
    CHECK(saw_jfk && saw_ewr && saw_rno && saw_skrg && saw_vnkt);

    const auto near = s376::load_region_plan("data/regions/kjfk-near.toml");
    CHECK(near.icao == "KJFK");
    CHECK_NEAR(near.near_m, 0.30, 1e-9);
    CHECK(near.near_km <= 3.01);
    CHECK(near.approach_nm <= 3.01);
    CHECK(near.imagery == "nys");
    CHECK(!near.local_import);
    const auto built = s376::kjfk_nearfield_03m_plan();
    CHECK_NEAR(built.near_m, 0.30, 1e-9);
    CHECK(built.near_km <= 3.01);

    const auto ewr = s376::load_region_plan("data/regions/kewr.toml");
    CHECK(ewr.icao == "KEWR");
    CHECK(ewr.imagery == "njogis");
    CHECK_NEAR(ewr.near_m, 0.30, 1e-9);
    auto ereq = s376::fetch_request_from_region(ewr, "/tmp/s376-plan-kewr");
    ereq.max_export = 256;
    ereq.near_m = 10.0;
    const auto eplan = s376::plan_fetch(ereq);
    CHECK(eplan.icao == "KEWR");
    CHECK(eplan.img_kind == "njogis" || eplan.img_kind == "nys" || eplan.img_kind == "naip");
    const auto tmp = (std::filesystem::temp_directory_path() / "s376-ewr-plan").string();
    std::filesystem::create_directories(tmp);
    CHECK(s376::write_fetch_plan_toml(tmp + "/fetch.plan.toml", eplan, ereq));
    CHECK(std::filesystem::exists(tmp + "/fetch.plan.toml"));
    CHECK(std::filesystem::file_size(tmp + "/fetch.plan.toml") > 32);
    std::filesystem::remove_all(tmp);

    const auto ny = s376::load_region_plan("data/regions/ny.toml");
    CHECK(ny.south < 40.5 && ny.north > 40.9);
}
