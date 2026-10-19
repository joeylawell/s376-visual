#include "s376/config.hpp"
#include "s376/geo.hpp"

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
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #a << "=" << _a         \
                      << " " << #b << "=" << _b << "\n";                                           \
        }                                                                                          \
    } while (0)

void test_channels() {
    auto cfg = s376::default_config();
    auto all = s376::select_channels(cfg, "");
    CHECK(all.size() == 4);
    CHECK(all[0].name == "far_left" && all[1].name == "mid_left");
    CHECK(all[2].name == "mid_right" && all[3].name == "far_right");
    CHECK_NEAR(all[0].heading_offset_deg, -50.0, 1e-6);
    CHECK_NEAR(all[1].heading_offset_deg, 0.0, 1e-6);
    CHECK_NEAR(all[2].heading_offset_deg, 0.0, 1e-6);
    CHECK_NEAR(all[3].heading_offset_deg, 50.0, 1e-6);
    CHECK_NEAR(all[0].pitch_offset_deg, 0.0, 1e-9);
    CHECK_NEAR(all[0].roll_offset_deg, 0.0, 1e-9);
    CHECK_NEAR(all[3].pitch_offset_deg, 0.0, 1e-9);
    CHECK_NEAR(all[3].roll_offset_deg, 0.0, 1e-9);
    CHECK(all[0].width == 1920 && all[0].height == 1200);
    CHECK(all[0].flip_vertical);
    CHECK(!all[1].flip_vertical && !all[2].flip_vertical && !all[3].flip_vertical);
    CHECK_NEAR(all[0].hfov_deg, 50.0, 1e-9);
    CHECK_NEAR(all[0].vfov_deg, 32.5, 1e-9);
    {
        const double aspect = static_cast<double>(all[0].width) / static_cast<double>(all[0].height);
        const double tan_aspect = std::tan(25.0 * 3.14159265358979323846 / 180.0) /
                                  std::tan(16.25 * 3.14159265358979323846 / 180.0);
        CHECK_NEAR(aspect, tan_aspect, 0.02);
    }
    auto wrap = s376::wrap_channels(all);
    CHECK(wrap.size() == 3);
    CHECK(wrap[0].name == "far_left" && wrap[1].name == "mid_left" && wrap[2].name == "far_right");
    int sw = 0, sh = 0;
    s376::wrap_strip_size(wrap, sw, sh);
    CHECK(sw == 1920 * 3 && sh == 1200);
    auto one = s376::select_channels(cfg, "mid_left");
    CHECK(one.size() == 1 && one[0].name == "mid_left");
    CHECK_NEAR(one[0].heading_offset_deg, 0.0, 1e-6);
    auto alias = s376::select_channels(cfg, "front");
    CHECK(alias.size() == 1 && alias[0].name == "mid_left");
    CHECK(s376::select_channels(cfg, "left")[0].name == "far_left");
    CHECK(s376::select_channels(cfg, "right")[0].name == "far_right");
    CHECK(s376::select_channels(cfg, "front_fo")[0].name == "mid_right");
    CHECK(s376::select_channels(cfg, "fo_front")[0].name == "mid_right");
    auto none = s376::select_channels(cfg, "bogus");
    CHECK(none.empty());
    auto all2 = s376::select_channels(cfg, "all");
    CHECK(all2.size() == 4);

    CHECK(cfg.host.reply_master == "mid_left");
    CHECK(s376::hat_reply_enabled(cfg.host, one));
    CHECK(!s376::hat_reply_enabled(cfg.host, s376::select_channels(cfg, "far_left")));
    CHECK(!s376::hat_reply_enabled(cfg.host, s376::select_channels(cfg, "mid_right")));
    CHECK(!s376::hat_reply_enabled(cfg.host, s376::select_channels(cfg, "far_right")));
    CHECK(s376::hat_reply_enabled(cfg.host, all));
    auto no_hat = cfg.host;
    no_hat.hat_responder = false;
    CHECK(!s376::hat_reply_enabled(no_hat, one));
    no_hat.hat_responder = true;
    no_hat.reply_master.clear();
    CHECK(s376::hat_reply_enabled(no_hat, s376::select_channels(cfg, "far_right")));
    CHECK(s376::default_shot_channel(cfg) == "mid_left");

    {
        const std::string mid = s376::dump_config(cfg, {"mid_left"});
        CHECK(mid.find("selected=mid_left") != std::string::npos);
        CHECK(mid.find("channel mid_left hdg=0") != std::string::npos);
        CHECK(mid.find("channel far_right") == std::string::npos);
        std::vector<std::string> alias_names;
        for (const auto& ch : s376::select_channels(cfg, "front"))
            alias_names.push_back(ch.name);
        const std::string alias = s376::dump_config(cfg, alias_names);
        CHECK(alias.find("selected=mid_left") != std::string::npos);
        CHECK(alias.find("channel far_right") == std::string::npos);
    }

    cfg.channels[1].enabled = false;
    CHECK(s376::select_channels(cfg, "").size() == 3);

    // Single display: tile far_left | mid_left | mid_right | far_right.
    auto tiled = s376::layout_channels(all, 1, 1920, 1080);
    CHECK(tiled.size() == 4);
    CHECK(tiled[0].name == "far_left");
    CHECK(tiled[1].name == "mid_left");
    CHECK(tiled[2].name == "mid_right");
    CHECK(tiled[3].name == "far_right");
    CHECK(tiled[0].x < tiled[1].x && tiled[2].x < tiled[3].x);
    CHECK(tiled[0].w + tiled[1].w + tiled[2].w + tiled[3].w <= 1920 + 4);
    CHECK(!tiled[0].fullscreen);

    // Four monitors: each channel on its configured screen, native size.
    auto multi = s376::layout_channels(all, 4, 1920, 1080);
    CHECK(multi.size() == 4);
    int screens_used = 0;
    for (const auto& p : multi) {
        if (p.name == "far_left")
            CHECK(p.screen == 0 && p.w == 1920);
        if (p.name == "mid_left")
            CHECK(p.screen == 1);
        if (p.name == "mid_right")
            CHECK(p.screen == 2);
        if (p.name == "far_right")
            CHECK(p.screen == 3);
        screens_used += p.screen;
    }
    CHECK(screens_used == 0 + 1 + 2 + 3);

    // Explicit window_x/y wins.
    all[0].window_x = 40;
    all[0].window_y = 50;
    auto ex = s376::layout_channels(all, 1, 1920, 1080);
    bool found = false;
    for (const auto& p : ex) {
        if (p.name == "far_left") {
            CHECK(p.x == 40 && p.y == 50);
            found = true;
        }
    }
    CHECK(found);

    // Shared eyepoint, left is −50° of front (aviation heading).
    const auto front = s376::geo::make_channel_view(47.46, -122.31, 136.0, 180.34, 0, 0, 0, 0, 0,
                                                    50, 32.5, 1920, 1200);
    const auto left = s376::geo::make_channel_view(47.46, -122.31, 136.0, 180.34, 0, 0, -50.0, 0, 0,
                                                   50, 32.5, 1920, 1200);
    const auto right = s376::geo::make_channel_view(47.46, -122.31, 136.0, 180.34, 0, 0, 50.0, 0, 0,
                                                    50, 32.5, 1920, 1200);
    CHECK_NEAR(front.eye.x, left.eye.x, 1e-6);
    CHECK_NEAR(front.eye.x, right.eye.x, 1e-6);
    CHECK_NEAR(left.heading_deg, 180.34 - 50.0, 1e-6);
    CHECK_NEAR(right.heading_deg, 180.34 + 50.0, 1e-6);
    // Forward vectors should differ (not parallel).
    const double dot = front.forward.x * left.forward.x + front.forward.y * left.forward.y +
                       front.forward.z * left.forward.z;
    CHECK(dot < 0.95);

    // mid_left and mid_right share the front frustum; FO eye offset is optional.
    CHECK_NEAR(all[1].heading_offset_deg, all[2].heading_offset_deg, 1e-9);
    const auto fo = s376::geo::make_channel_view(47.46, -122.31, 136.0, 180.34, 0, 0, 0, 0, 0, 50,
                                                32.5, 1920, 1200, 0, 0.51, 0);
    CHECK(std::fabs(fo.eye.x - front.eye.x) + std::fabs(fo.eye.y - front.eye.y) > 0.1);
}
