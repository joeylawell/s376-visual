#include "s376/airport.hpp"
#include "s376/faa.hpp"
#include "s376/parity.hpp"

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

void test_parity() {
    int n = 0;
    const auto* items = s376::scenery_parity_items(&n);
    CHECK(n >= 20);
    CHECK(items[0].status == s376::ParityStatus::Ok);
    const auto list = s376::format_scenery_checklist();
    CHECK(list.find("C1") != std::string::npos);
    CHECK(list.find("T5") != std::string::npos);
    CHECK(list.find("night") != std::string::npos || list.find("street") != std::string::npos);

    const auto jfk = s376::kjfk_public();
    const auto c = s376::count_parity(jfk);
    CHECK(c.icao == "KJFK");
    CHECK(c.has_c1_c6);
    CHECK(c.has_u1_u3);
    CHECK(!c.has_cc);
    CHECK(c.has_t5);
    CHECK(c.has_tower && c.has_beacon);
    CHECK(c.signs >= 6);
    CHECK(c.hold_bars >= 6);
    CHECK(c.trees >= 10);
    CHECK(c.parked >= 3);
    CHECK(c.jetbridges >= 3);
    CHECK(c.docking >= 3);
    CHECK(c.marshallers >= 3);
    CHECK(c.routes >= 12);
    CHECK(c.night_spacing_m >= 25 && c.night_spacing_m <= 50);
    CHECK(c.night_points > 20);
    CHECK(c.alsf2 && c.malsr && c.rlls && c.papi && c.tdz);
    CHECK(c.roads >= 4 && c.has_van_wyck && c.has_belt && c.has_jfk_terminal_rd);
    const auto text = s376::format_parity_counts(c);
    CHECK(text.find("KJFK") != std::string::npos);
    CHECK(text.find("CC=0") != std::string::npos);

    const auto ewr = s376::kewr_public();
    const auto e = s376::count_parity(ewr);
    CHECK(e.has_tower && e.has_beacon);
    CHECK(e.alsf2 && e.malsr && e.papi);
    CHECK(e.routes >= 4);
    CHECK(e.has_njtp);

    CHECK(!s376::looks_like_s376_repo("/tmp"));
    auto scan = s376::scan_local_raster_tree("/tmp/s376-no-such-raster");
    CHECK(!scan.warning.empty());
    const auto official = s376::format_official_vs_raster(s376::faa_kjfk(), scan);
    CHECK(official.find("official") != std::string::npos);
    CHECK(official.find("22R") != std::string::npos);
    CHECK(official.find("wins") != std::string::npos);
    const auto root = std::string(__FILE__);
    // Only the path-missing / refuse paths are exercised in CI — never a Raster tree.
    (void)root;
}
