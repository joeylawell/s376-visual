#include "s376/display.hpp"

#include <cmath>
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
                      << #b << "=" << _b << "\n";                                                  \
        }                                                                                          \
    } while (0)

void test_display() {
    auto id = s376::identity_correction();
    CHECK(s376::kWarpCols == 7 && s376::kWarpRows == 5);
    CHECK_NEAR(s376::warp_h_deg(0), -30.0, 1e-6);
    CHECK_NEAR(s376::warp_h_deg(3), 0.0, 1e-6);
    CHECK_NEAR(s376::warp_h_deg(6), 30.0, 1e-6);
    CHECK_NEAR(s376::warp_v_deg(0), -16.25, 1e-6);
    CHECK_NEAR(s376::warp_v_deg(1), -10.0, 1e-6);
    CHECK_NEAR(s376::warp_v_deg(2), 0.0, 1e-6);
    CHECK_NEAR(s376::warp_v_deg(3), 10.0, 1e-6);
    CHECK_NEAR(s376::warp_v_deg(4), 16.25, 1e-6);
    CHECK(s376::warp_is_identity(id));
    CHECK(s376::lut_is_identity(id));
    CHECK(s376::correction_is_identity(id));
    CHECK(!s376::needs_gpu_pass(id));
    CHECK_NEAR(id.blend.ramp, 0.5, 1e-6);

    auto src = s376::make_solid_rgb(32, 24, 40, 80, 160);
    auto out = s376::apply_correction(src, id, s376::TodBucket::Day);
    CHECK(s376::pixels_near_solid(out, 40, 80, 160, 2));

    auto grid = s376::make_pattern_rgb(160, 100, s376::DisplayPattern::Grid5, 50, 32.5);
    CHECK(grid.w == 160 && grid.h == 100);
    int bright = 0, dark = 0;
    for (std::size_t i = 0; i < grid.rgb.size(); i += 3) {
        if (grid.rgb[i] > 180)
            ++bright;
        else if (grid.rgb[i] < 30)
            ++dark;
    }
    CHECK(bright > 80 && dark > 200);

    auto white = s376::make_pattern_rgb(8, 8, s376::DisplayPattern::White70, 50, 32.5);
    CHECK(s376::pixels_near_solid(white, 178, 178, 178, 1));
    auto black = s376::make_pattern_rgb(8, 8, s376::DisplayPattern::Black, 50, 32.5);
    CHECK(s376::pixels_near_solid(black, 0, 0, 0, 0));

    CHECK(s376::tod_bucket(14.5f, -1) == s376::TodBucket::Day);
    CHECK(s376::tod_bucket(6.0f, -1) == s376::TodBucket::Dawn);
    CHECK(s376::tod_bucket(19.0f, -1) == s376::TodBucket::Dusk);
    CHECK(s376::tod_bucket(1.0f, -1) == s376::TodBucket::Night);
    CHECK(s376::tod_bucket(0.f, 0) == s376::TodBucket::Day);
    CHECK(s376::tod_bucket(0.f, 3) == s376::TodBucket::Night);
    CHECK_NEAR(s376::effective_tod_hours(12.f, 1), 19.0, 1e-4);
    CHECK_NEAR(s376::effective_tod_hours(19.f, -1), 19.0, 1e-4);
    CHECK(s376::visual_sun_elev_deg(12.0f, -1) > 50.f);
    CHECK(s376::visual_sun_elev_deg(14.5f, -1) > 25.f);
    CHECK(s376::visual_sun_elev_deg(19.f, -1) < 5.f && s376::visual_sun_elev_deg(19.f, -1) > -8.f);
    CHECK(s376::visual_sun_elev_deg(3.f, -1) < -40.f);
    CHECK_NEAR(s376::visual_sun_elev_deg(0.f, 1), -1.5, 1e-3);
    CHECK_NEAR(s376::visual_sun_elev_deg(0.f, 3), -55.0, 1e-3);
    CHECK(s376::visual_sun_az_deg(12.f, -1, 40.64) > 170.f &&
          s376::visual_sun_az_deg(12.f, -1, 40.64) < 190.f);
    CHECK_NEAR(s376::runway_wetness(0), 0.0, 1e-6);
    CHECK_NEAR(s376::runway_wetness(1), 0.35, 1e-6);
    CHECK_NEAR(s376::runway_wetness(2), 0.75, 1e-6);
    CHECK_NEAR(s376::runway_wetness(3), 1.0, 1e-6);
    CHECK_NEAR(s376::runway_wetness(5), 1.0, 1e-6);

    s376::DisplayCorrection warped = id;
    warped.warp[2][3].src_h_deg = 5.f;
    CHECK(!s376::warp_is_identity(warped));
    CHECK(s376::needs_gpu_pass(warped));

    s376::DisplayCorrection blend = id;
    blend.blend.left_px = 8;
    blend.blend.ramp = 0.5f;
    auto edged = s376::apply_correction(src, blend, s376::TodBucket::Day);
    CHECK(edged.rgb[0] < src.rgb[0]); // left edge darker at ramp 0.5
    blend.blend.ramp = 0.2f;
    auto brighter = s376::apply_correction(src, blend, s376::TodBucket::Day);
    CHECK(brighter.rgb[0] > edged.rgb[0]); // lower ramp = brighter

    std::string path = "data/display/correction.toml";
    {
        std::ifstream probe(path);
        if (!probe)
            path = "../data/display/correction.toml";
    }
    s376::DisplayCorrection loaded;
    std::string err;
    CHECK(s376::load_correction(path, loaded, err));
    CHECK(s376::correction_is_identity(loaded));
    CHECK_NEAR(loaded.blend.ramp, 0.5, 1e-6);
}
