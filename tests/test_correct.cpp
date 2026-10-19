#include "s376/accuracy.hpp"
#include "s376/airport.hpp"
#include "s376/correct.hpp"
#include "s376/faa.hpp"
#include "s376/image.hpp"
#include "s376/terrain.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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

void test_correct() {
    const auto jfk = s376::kjfk_public();
    const auto& faa = s376::faa_kjfk();
    const auto plan = s376::plan_corrections(jfk, faa);
    CHECK(plan.ok());
    CHECK(plan.items.size() >= 10);
    bool saw_surface = false, saw_disp = false, saw_emas = false;
    for (const auto& c : plan.items) {
        if (c.feature.find("runway-surface") != std::string::npos)
            saw_surface = true;
        if (c.feature.find("22R") != std::string::npos &&
            c.feature.find("displaced") != std::string::npos)
            saw_disp = true;
        if (c.feature.find("EMAS") != std::string::npos)
            saw_emas = true;
        CHECK(!c.official.empty());
        CHECK(!c.action.empty());
    }
    CHECK(saw_surface && saw_disp && saw_emas);
    const auto text = s376::format_correction_report(plan);
    CHECK(text.find("CORRECT") != std::string::npos);
    CHECK(text.find("official") != std::string::npos);

    s376::GeoBox box;
    box.west = -73.85;
    box.south = 40.60;
    box.east = -73.72;
    box.north = 40.68;
    const auto img = s376::check_imagery_vs_official(jfk, faa, &box, 4000, 4000);
    CHECK(img.ok());

    s376::Airport drifted = jfk;
    drifted.runways[1].he_lat += 0.01;
    const auto bad = s376::check_imagery_vs_official(drifted, faa, &box, 4000, 4000);
    CHECK(!bad.ok());

    s376::RgbImage im = s376::make_solid_rgb(64, 64, 180, 40, 40); // stale red paint
    const int n = s376::apply_correction_overlay(im, box, jfk);
    CHECK(n >= 4);
    bool saw_asphalt = false;
    for (std::size_t i = 0; i + 2 < im.rgb.size(); i += 3) {
        if (im.rgb[i] < 80 && im.rgb[i + 1] < 80 && im.rgb[i + 2] < 80)
            saw_asphalt = true;
    }
    CHECK(saw_asphalt);

    // Tight box around 04L/22R: markings, yellow displaced arrows, blend-out.
    s376::GeoBox tight;
    tight.west = -73.792;
    tight.south = 40.618;
    tight.east = -73.752;
    tight.north = 40.656;
    s376::RgbImage hi = s376::make_solid_rgb(256, 256, 36, 92, 40); // grass
    // Stale white paint just beyond the 22R DER (official data: no pavement there).
    auto to_px = [&](double lat, double lon, int& x, int& y) {
        x = static_cast<int>(std::lround((lon - tight.west) / (tight.east - tight.west) * 255));
        y = static_cast<int>(std::lround((tight.north - lat) / (tight.north - tight.south) * 255));
    };
    const s376::Runway* r22 = nullptr;
    for (const auto& rw : jfk.runways) {
        if (rw.ident_he == "22R")
            r22 = &rw;
    }
    CHECK(r22 != nullptr);
    if (!r22)
        r22 = &jfk.runways[0];
    double slat = 0, slon = 0;
    // Beyond the 22R physical end (outbound along 04L heading) — not on official pavement.
    s376::offset_m(r22->he_lat, r22->he_lon, r22->heading_deg, 180.0, slat, slon);
    int sx = 0, sy = 0;
    to_px(slat, slon, sx, sy);
    for (int oy = -2; oy <= 2; ++oy) {
        for (int ox = -2; ox <= 2; ++ox) {
            const int x = sx + ox;
            const int y = sy + oy;
            if (x < 0 || y < 0 || x >= 256 || y >= 256)
                continue;
            const int di = (y * 256 + x) * 3;
            hi.rgb[static_cast<std::size_t>(di)] = 230;
            hi.rgb[static_cast<std::size_t>(di + 1)] = 230;
            hi.rgb[static_cast<std::size_t>(di + 2)] = 230;
        }
    }
    const int hn = s376::apply_correction_overlay(hi, tight, jfk);
    CHECK(hn >= 4);
    bool saw_white = false, saw_yellow = false, saw_asphalt2 = false;
    int leftover_white = 0;
    for (std::size_t i = 0; i + 2 < hi.rgb.size(); i += 3) {
        const int r = hi.rgb[i], g = hi.rgb[i + 1], b = hi.rgb[i + 2];
        if (r >= 200 && g >= 200 && b >= 200)
            saw_white = true;
        if (r > 180 && g > 140 && b < 80)
            saw_yellow = true;
        if (r < 80 && g < 80 && b < 80)
            saw_asphalt2 = true;
    }
    // The seeded stub beyond 22R must not stay bright white.
    for (int oy = -2; oy <= 2; ++oy) {
        for (int ox = -2; ox <= 2; ++ox) {
            const int x = sx + ox;
            const int y = sy + oy;
            if (x < 0 || y < 0 || x >= 256 || y >= 256)
                continue;
            const int di = (y * 256 + x) * 3;
            if (hi.rgb[static_cast<std::size_t>(di)] >= 200 &&
                hi.rgb[static_cast<std::size_t>(di + 1)] >= 200 &&
                hi.rgb[static_cast<std::size_t>(di + 2)] >= 200)
                ++leftover_white;
        }
    }
    {
        const int di = (sy * 256 + sx) * 3;
        CHECK(hi.rgb[static_cast<std::size_t>(di)] < 160);
        CHECK(hi.rgb[static_cast<std::size_t>(di + 1)] < 160);
    }
    CHECK(saw_asphalt2 && saw_white && saw_yellow);
    CHECK(leftover_white <= 2);

    const auto tmp = (std::filesystem::temp_directory_path() / "s376-correct-rpt").string();
    std::filesystem::create_directories(tmp);
    CHECK(s376::write_airport_correction_report(tmp, "KJFK", hn));
    CHECK(std::filesystem::exists(tmp + "/corrections.txt"));
    {
        std::ifstream in(tmp + "/corrections.txt");
        std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(all.find("CORRECT") != std::string::npos);
        CHECK(all.find("KJFK") != std::string::npos);
        CHECK(all.find("painted=") != std::string::npos);
    }
    std::filesystem::remove_all(tmp);

    s376::ImageRgb pack;
    pack.w = 32;
    pack.h = 32;
    pack.rgb.assign(32 * 32 * 3, 80);
    CHECK(s376::apply_correction_overlay_icao(pack, box, "KJFK") >= 4);
    CHECK(s376::apply_correction_overlay_icao(pack, box, "not-an-airport") == 0);

    for (const auto* ref : s376::faa_all()) {
        const auto a = s376::airport_by_icao(ref->icao);
        const auto acc = s376::check_imagery_vs_official(a, *ref);
        if (!acc.ok())
            std::cerr << s376::format_acc_report(acc);
        CHECK(acc.ok());
        const auto cr = s376::plan_corrections(a, *ref);
        CHECK(!cr.items.empty());
    }
}
