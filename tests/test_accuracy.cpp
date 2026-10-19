#include "s376/accuracy.hpp"
#include "s376/airport.hpp"
#include "s376/faa.hpp"
#include "s376/fetch.hpp"
#include "s376/geo.hpp"
#include "s376/terrain.hpp"

#include <cmath>
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

void test_accuracy() {
    CHECK(s376::kAccEndM == 3.0);
    CHECK(s376::kAccHeadingDeg == 0.1);
    CHECK(s376::kAccThresholdM == 3.0);
    CHECK(s376::kAccImageryM == 1.0);
    CHECK(s376::kAccElevM == 1.0);

    const auto& jfk_faa = s376::faa_kjfk();
    const auto& ewr_faa = s376::faa_kewr();
    CHECK(jfk_faa.ends.size() == 8);
    CHECK(ewr_faa.ends.size() == 6);
    CHECK(jfk_faa.effective == "2026-10-01");
    CHECK(jfk_faa.retrieved == "2026-10-18");
    const auto* r22 = s376::faa_end(jfk_faa, "22R");
    CHECK(r22 && r22->displaced_ft == 3424.0);
    CHECK(r22->als == "NONE");
    const auto* r13l = s376::faa_end(jfk_faa, "13L");
    CHECK(r13l && r13l->width_ft == 200.0 && r13l->als == "ALSF-2");
    const auto* r13r = s376::faa_end(jfk_faa, "13R");
    CHECK(r13r && r13r->als == "RLLS" && r13r->papi == "P4L");
    const auto* r31r = s376::faa_end(jfk_faa, "31R");
    CHECK(r31r && r31r->als == "MALSR" && r31r->papi == "P4R");
    const auto* r04r = s376::faa_end(jfk_faa, "04R");
    CHECK(r04r && r04r->emas && r04r->emas_len_ft == 393.0);
    const auto* r04l = s376::faa_end(jfk_faa, "04L");
    CHECK(r04l);
    CHECK_NEAR(r04l->elev_ft, 11.9, 1e-6);
    CHECK_NEAR(jfk_faa.elev_ft, 13.0, 1e-6);
    CHECK(r04l->elev_ft != jfk_faa.elev_ft); // ends are not the ARP copy
    CHECK(r22 && r22->elev_ft != r04l->elev_ft);
    CHECK_NEAR(11.9 * s376::geo::kFtToM, 3.62712, 1e-4); // doctor "3.62712 m" was FAA 11.9 ft

    const auto jfk = s376::kjfk_public();
    const auto ewr = s376::kewr_public();
    const auto jfk_acc = s376::check_airport_vs_faa(jfk, jfk_faa);
    const auto ewr_acc = s376::check_airport_vs_faa(ewr, ewr_faa);
    if (!jfk_acc.ok())
        std::cerr << s376::format_acc_report(jfk_acc);
    if (!ewr_acc.ok())
        std::cerr << s376::format_acc_report(ewr_acc);
    CHECK(jfk_acc.ok());
    CHECK(ewr_acc.ok());
    CHECK(jfk_acc.checks >= 40);
    CHECK(ewr_acc.checks >= 30);

    const auto from_faa = s376::airport_from_faa("KJFK");
    CHECK(s376::check_airport_vs_faa(from_faa, jfk_faa).ok());

    // Prior LE+heading+length 04R / 13L were hundreds of metres off FAA.
    s376::Airport drifted = jfk;
    drifted.runways[1].he_lat += 0.01; // ~1.1 km
    const auto bad = s376::check_airport_vs_faa(drifted, jfk_faa);
    CHECK(!bad.ok());
    bool saw_end = false;
    for (const auto& f : bad.fails) {
        if (f.check == "end-pos")
            saw_end = true;
    }
    CHECK(saw_end);

    s376::Airport als_wrong = jfk;
    als_wrong.runways[0].approach_le = "ALSF-2"; // FAA 04L is NONE
    const auto als = s376::check_airport_vs_faa(als_wrong, jfk_faa);
    bool saw_als = false;
    for (const auto& f : als.fails) {
        if (f.check == "als")
            saw_als = true;
    }
    CHECK(saw_als);

    s376::Airport papi_wrong = jfk;
    papi_wrong.runways[2].papi_he = "P4L"; // 31R is P4R
    papi_wrong.runways[2].papi_deg_he = 2.5;
    const auto papi = s376::check_airport_vs_faa(papi_wrong, jfk_faa);
    bool saw_side = false, saw_ang = false;
    for (const auto& f : papi.fails) {
        if (f.check == "papi-side")
            saw_side = true;
        if (f.check == "papi-angle")
            saw_ang = true;
    }
    CHECK(saw_side && saw_ang);

    s376::GeoBox box;
    box.west = -73.85;
    box.south = 40.60;
    box.east = -73.72;
    box.north = 40.68;
    CHECK(s376::geotransform_roundtrip_m(box, 4000, 4000, jfk.arp_lat, jfk.arp_lon) < 0.01);
    s376::AccReport img;
    img.icao = "KJFK";
    CHECK(s376::imagery_aligned_to_runways(box, 4000, 4000, jfk, s376::kAccImageryM, &img));
    CHECK(img.fails.empty());

    s376::GeoBox sw;
    sw.west = -74.07;
    sw.south = 40.34;
    sw.east = -73.70;
    sw.north = 40.55; // hangar pack: no lod0 north of 40.614
    s376::AccReport miss;
    miss.icao = "KJFK";
    CHECK(!s376::highest_res_covers_airport(sw, jfk, &miss));
    CHECK(!miss.fails.empty());

    // Union of opposite corners contains the ARP, but no single tile does.
    std::vector<s376::CacheTileRef> hole;
    s376::CacheTileRef a, b;
    a.lod = 0;
    a.west = -74.05;
    a.east = -73.95;
    a.south = 40.50;
    a.north = 40.56;
    b.lod = 0;
    b.west = -73.65;
    b.east = -73.55;
    b.south = 40.70;
    b.north = 40.76;
    hole.push_back(a);
    hole.push_back(b);
    s376::AccReport hole_r;
    hole_r.icao = "KJFK";
    CHECK(!s376::highest_res_covers_tiles(hole, jfk, &hole_r));

    s376::FetchRequest req;
    req.icao = "KJFK";
    req.near_radius_km = 5.0;
    req.approach_nm = 10.0;
    const auto apt = s376::lookup_airport_ref("KJFK");
    CHECK_NEAR(apt.lat, 40.639928, 1e-5);
    const auto lods = s376::fetch_lods(req);
    CHECK(!lods.empty());
    const auto lod0 = s376::lod_coverage(apt, req, lods.front());
    CHECK(s376::highest_res_covers_airport(lod0, jfk, nullptr));
    CHECK(s376::box_contains(lod0, jfk.arp_lat, jfk.arp_lon, 0));
    for (const auto& rw : jfk.runways) {
        CHECK(s376::box_contains(lod0, rw.le_lat, rw.le_lon, 0));
        CHECK(s376::box_contains(lod0, rw.he_lat, rw.he_lon, 0));
    }

    const auto ewr_ref = s376::lookup_airport_ref("KEWR");
    CHECK(ewr_ref.icao == "KEWR");
    CHECK_NEAR(ewr_ref.lat, 40.6925, 1e-4);
    req.icao = "KEWR";
    const auto ewr_lod0 = s376::lod_coverage(ewr_ref, req, s376::fetch_lods(req).front());
    CHECK(s376::highest_res_covers_airport(ewr_lod0, ewr, nullptr));

    s376::DemGrid dem;
    dem.w = 32;
    dem.h = 32;
    dem.west = -73.80;
    dem.east = -73.75;
    dem.south = 40.62;
    dem.north = 40.66;
    const float z = static_cast<float>(11.9 * s376::geo::kFtToM);
    dem.z_m.assign(32 * 32, z);
    double got = 0;
    CHECK(s376::elev_matches_faa(dem, 40.62524, -73.7709, 11.9 * s376::geo::kFtToM, s376::kAccElevM,
                                 &got));
    CHECK_NEAR(got, 11.9 * s376::geo::kFtToM, 0.01);
    CHECK(!s376::elev_matches_faa(dem, 40.62524, -73.7709, 30.0, s376::kAccElevM, nullptr));

    CHECK(s376::faa_airport("kjfk") == &jfk_faa);
    CHECK(s376::faa_airport("XXXX") == nullptr);
    CHECK(s376::faa_all().size() == 18);
    CHECK(s376::official_airport("KSEA") == &s376::faa_ksea());
    CHECK(s376::faa_ksea().effective == "2026-10-01");
    CHECK(s376::faa_egll().source.find("NATS") != std::string::npos);
    CHECK(s376::faa_skrg().source.find("Aerocivil") != std::string::npos);
    CHECK(s376::faa_vnkt().source.find("CAAN") != std::string::npos);
    CHECK(s376::official_authority(s376::faa_kjfk()) == "FAA");
    CHECK(s376::official_authority(s376::faa_eddf()) == "DFS");
    CHECK(s376::official_authority(s376::faa_egll()) == "NATS");
    CHECK(s376::official_authority(s376::faa_lebl()) == "ENAIRE");
    CHECK(s376::official_authority(s376::faa_seqm()) == "DGAC Ecuador");
    CHECK(s376::official_authority(s376::faa_skbo()) == "Aerocivil");
    CHECK(s376::official_authority(s376::faa_skrg()) == "Aerocivil");
    CHECK(s376::official_authority(s376::faa_vnkt()) == "CAAN");
    CHECK(s376::official_source_stale(s376::faa_skbo()));
    CHECK(s376::official_source_stale(s376::faa_vnkt()));
    CHECK(!s376::official_source_stale(s376::faa_kjfk()));
    CHECK(!s376::official_source_stale(s376::faa_eddf()));
    CHECK(!s376::official_source_stale(s376::faa_skrg()));
    CHECK(!s376::official_source_stale(s376::faa_egll()));

    // Same CI suite on every official airport. Official data wins.
    for (const auto* faa : s376::faa_all()) {
        const auto a = s376::airport_by_icao(faa->icao);
        const auto acc = s376::check_airport_vs_faa(a, *faa);
        if (!acc.ok())
            std::cerr << s376::format_acc_report(acc);
        CHECK(acc.ok());
        CHECK(acc.checks >= 20);
        CHECK(!a.source.name.empty());
        CHECK(!a.source.effective.empty());
        CHECK(!a.source.retrieved.empty());
    }

    const auto* las26r = s376::faa_end(s376::faa_klas(), "26R");
    CHECK(las26r && las26r->als == "MALS");
    CHECK_NEAR(las26r->length_ft, 14835.0, 0.5);
    const auto* lax24l = s376::faa_end(s376::faa_klax(), "24L");
    CHECK(lax24l && lax24l->als == "MALSR");
    CHECK_NEAR(lax24l->length_ft, 10885.0, 0.5);
    const auto* bos15r = s376::faa_end(s376::faa_kbos(), "15R");
    CHECK(bos15r && bos15r->als == "MALSR");
    const auto* bos33l = s376::faa_end(s376::faa_kbos(), "33L");
    CHECK(bos33l && bos33l->als == "ALSF-2");
}
