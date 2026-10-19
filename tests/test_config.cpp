#include "s376/config.hpp"
#include "s376/render.hpp"

#include <cstdio>
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

#include <cmath>

void test_config() {
    auto d = s376::default_config();
    CHECK(d.channels.size() == 4);
    CHECK(d.channels[0].name == "far_left");
    CHECK(d.channels[1].name == "mid_left");
    CHECK(d.channels[2].name == "mid_right");
    CHECK(d.channels[3].name == "far_right");
    CHECK_NEAR(d.channels[0].heading_offset_deg, -50.0, 1e-6);
    CHECK_NEAR(d.channels[1].heading_offset_deg, 0.0, 1e-6);
    CHECK_NEAR(d.channels[2].heading_offset_deg, 0.0, 1e-6);
    CHECK_NEAR(d.channels[3].heading_offset_deg, 50.0, 1e-6);
    CHECK_NEAR(d.channels[0].roll_offset_deg, 0.0, 1e-9);
    CHECK_NEAR(d.channels[3].roll_offset_deg, 0.0, 1e-9);
    CHECK_NEAR(d.channels[0].hfov_deg, 50.0, 1e-9);
    CHECK_NEAR(d.channels[0].vfov_deg, 32.5, 1e-9);
    CHECK(d.channels[0].width == 1920 && d.channels[0].height == 1200);
    CHECK(d.channels[0].flip_vertical);
    CHECK(!d.channels[1].flip_vertical);
    {
        int w = 0, h = 0;
        CHECK(s376::parse_resolution("1920x1200", w, h) && w == 1920 && h == 1200);
        CHECK(s376::parse_resolution("1280X800", w, h) && w == 1280 && h == 800);
        CHECK(s376::parse_resolution(std::string("1920") + "\xC3\x97" + "1200", w, h) &&
              w == 1920 && h == 1200);
        CHECK(s376::format_resolution(1920, 1200) == "1920x1200");
        CHECK(!s376::parse_resolution("nope", w, h));
        CHECK(!s376::parse_resolution("4x4", w, h));
    }
    CHECK(d.host.bind == "0.0.0.0:2300");
    CHECK(d.host.reply_port == 2301);
    CHECK(d.control.bind == "127.0.0.1:2376");
    CHECK(d.control.enabled);
    CHECK(d.egg.ptt_source == "switch");
    CHECK(d.egg.ptt_switch == 255);
    CHECK(d.egg.ptt_landing_bit == 7);
    CHECK(d.egg.ptt_key == "m");
    CHECK(d.host.reply_master == "mid_left");
    CHECK(!d.render.gl_debug);
    CHECK(d.render.quality == "gtx980ti");
    CHECK(d.render.hdr && d.render.atmosphere && d.render.water && d.render.markings);
    CHECK(d.render.detail_textures && d.render.buildings);
    CHECK(d.render.ssao && d.render.shadows);
    CHECK(d.render.bloom && d.render.fxaa && d.render.filmic);
    CHECK(d.render.taa && d.render.clustered && d.render.lod_fade);
    CHECK(!d.render.solar_astronomy);
    CHECK(d.render.sync_to_host);
    CHECK(d.render.ram_mb == 2048);
    CHECK(d.render.vram_mb == 5120);
    CHECK(d.render.mesh_n == 96);
    {
        auto rtx = d.render;
        s376::apply_quality_preset(rtx, "rtx");
        CHECK(rtx.quality == "rtx");
        CHECK(rtx.ssao && rtx.shadows);
        CHECK(rtx.msaa >= 4 && rtx.aniso >= 16);
        CHECK_NEAR(rtx.exposure, 1.05, 1e-4);
        CHECK(rtx.ram_mb >= 3072);
        CHECK(rtx.vram_mb >= 10240);
        CHECK(rtx.mesh_n >= 128);
        auto gtx = d.render;
        s376::apply_quality_preset(gtx, "gtx980ti");
        CHECK(gtx.ssao && gtx.shadows);
        CHECK(gtx.hdr && gtx.atmosphere);
        CHECK(gtx.bloom && gtx.fxaa && gtx.filmic);
        CHECK(gtx.taa && gtx.clustered && gtx.lod_fade);
        CHECK(gtx.msaa == 0);
        CHECK(gtx.ram_mb == 2048);
        CHECK(gtx.vram_mb == 5120);
        CHECK(gtx.mesh_n == 96);
        CHECK(gtx.auto_drop);
        CHECK_NEAR(gtx.gpu_budget_ms, 12.20, 0.05);
        CHECK(!rtx.auto_drop);
        std::string dropped;
        auto drop = gtx;
        CHECK(!s376::apply_quality_autodrop(drop, 10.0f, 12.20f, &dropped));
        CHECK(drop.ssao);
        CHECK(s376::apply_quality_autodrop(drop, 13.0f, 12.20f, &dropped));
        CHECK(!drop.ssao);
        CHECK(dropped.find("ssao") != std::string::npos);
        CHECK(drop.water && drop.markings && drop.buildings && drop.atmosphere);
        const auto dumped = s376::dump_config(d);
        CHECK(dumped.find("render.ram_mb=2048") != std::string::npos);
        CHECK(dumped.find("render.vram_mb=5120") != std::string::npos);
        CHECK(dumped.find("render.bloom=true") != std::string::npos);
        CHECK(dumped.find("render.fxaa=true") != std::string::npos);
        CHECK(dumped.find("render.filmic=true") != std::string::npos);
        CHECK(dumped.find("render.solar_astronomy=false") != std::string::npos);
        CHECK(dumped.find("render.auto_drop=true") != std::string::npos);
        CHECK(dumped.find("render.offscreen=false") != std::string::npos);
        CHECK(dumped.find("render.taa=true") != std::string::npos);
        CHECK(dumped.find("render.clustered=true") != std::string::npos);
        CHECK(dumped.find("render.lod_fade=true") != std::string::npos);
        CHECK(dumped.find("host.reply_port=2301") != std::string::npos);
        CHECK(dumped.find("host.reply_master=mid_left") != std::string::npos);
        CHECK(!d.render.offscreen);
        int nb = 0;
        const auto* budgets = s376::quality_gpu_budgets(nb);
        CHECK(nb >= 8 && budgets != nullptr);
        CHECK(std::string(budgets[0].name) == "atmosphere");
        CHECK_NEAR(s376::quality_gpu_budget_total_ms(false), 11.35, 0.05);
        CHECK_NEAR(s376::quality_gpu_budget_total_ms(true), 12.20, 0.05);
        CHECK(s376::quality_gpu_budget_total_ms(false) < 16.67f);
        CHECK(s376::quality_gpu_budget_total_ms(true) < 16.67f);
    }
    {
        const auto lin = s376::plan_gl_display(true, false);
        CHECK(lin.version.empty());
        CHECK(lin.profile_mask == 0);
        CHECK(!lin.check_errors);
        const auto win = s376::plan_gl_display(false, true);
        CHECK(win.version == "3.3");
        CHECK(win.profile_mask == s376::kGlCompatProfileBit);
        CHECK(win.check_errors);
    }

    const char* path = "s376-test-config.toml";
    {
        std::ofstream o(path);
        o << "[host]\nbind = \"127.0.0.1:2300\"\nword_order = \"be\"\nfloat_format = \"ieee\"\n"
             "hat_responder = false\nreply_port = 23991\n"
             "[render]\nengine = \"osg\"\nframe_rate = 30\ngl_debug = true\n"
             "[[channels]]\nname = \"front\"\nheading_offset_deg = 0\nhfov_deg = 50\n"
             "vfov_deg = 32.5\nresolution = \"800x600\"\n"
             "[[channels]]\nname = \"left\"\nheading_offset_deg = 35.515\nenabled = false\n";
    }
    auto c = s376::load_config(path);
    CHECK(c.host.bind == "127.0.0.1:2300");
    CHECK(c.host.word_order == s376::xt4::WordOrder::Be);
    CHECK(c.host.float_fmt == s376::xt4::FloatFmt::Ieee);
    CHECK(c.host.reply_port == 23991);
    CHECK(c.host.reply_master.empty()); // mid_left is not in this file
    CHECK(!c.host.hat_responder);
    CHECK(c.render.engine == "osg");
    CHECK_NEAR(c.render.frame_rate, 30.0, 1e-9);
    CHECK(c.render.gl_debug);
    CHECK(d.render.msaa == 0);
    CHECK(d.render.aniso == 16);
    CHECK(c.channels.size() == 2);
    CHECK(c.channels[0].width == 800);
    CHECK(!c.channels[1].enabled);
    CHECK(c.egg.ptt_source == "switch");
    std::remove(path);

    {
        const char* epath = "s376-test-egg.toml";
        {
            std::ofstream o(epath);
            o << "[host]\nbind = \"127.0.0.1:2300\"\n"
                 "[egg]\nptt_source = \"landing_bit\"\nptt_landing_bit = 7\n"
                 "ptt_key = \"p\"\n";
        }
        auto e = s376::load_config(epath);
        CHECK(e.egg.ptt_source == "landing_bit");
        CHECK(e.egg.ptt_landing_bit == 7);
        CHECK(e.egg.ptt_key == "p");
        CHECK(e.egg.ptt_switch == 255);
        std::remove(epath);
    }

    {
        const char* qpath = "s376-test-quality.toml";
        {
            std::ofstream o(qpath);
            o << "[host]\nbind = \"127.0.0.1:2300\"\n"
                 "[render]\nquality = \"rtx\"\nssao = false\nbloom = false\n";
        }
        auto q = s376::load_config(qpath);
        CHECK(q.render.quality == "rtx");
        CHECK(q.render.shadows);
        CHECK(!q.render.ssao); // individual flag overrides the preset
        CHECK(!q.render.bloom);
        CHECK(q.render.fxaa && q.render.filmic);
        CHECK(q.render.hdr && q.render.buildings);
        std::remove(qpath);
    }
    auto cfg_path = [](const char* rel) {
        const std::string src = std::string("../") + rel;
        std::ifstream a(src);
        if (a)
            return src;
        return std::string(rel);
    };
    {
        std::string rtx_path = cfg_path("config/quality-rtx.toml");
        auto rtx_file = s376::load_config(rtx_path);
        CHECK(rtx_file.render.quality == "rtx");
        CHECK(rtx_file.render.ssao && rtx_file.render.shadows);
        CHECK(rtx_file.host.reply_master == "mid_left");
        CHECK_NEAR(rtx_file.channels[1].heading_offset_deg, 0.0, 1e-6);
        CHECK_NEAR(rtx_file.channels[0].heading_offset_deg, -50.0, 1e-6);
    }
    {
        std::string ch3 = cfg_path("config/channels-3.toml");
        auto c3 = s376::load_config(ch3);
        CHECK(c3.channels.size() == 3);
        CHECK(c3.channels[0].name == "front");
        CHECK(c3.host.reply_master == "front");
        CHECK(!c3.render.sync_to_host);
        CHECK_NEAR(c3.channels[1].heading_offset_deg, -50.0, 1e-6);
        CHECK(c3.channels[0].width == 1920 && c3.channels[0].height == 1200);
        CHECK(c3.channels[1].flip_vertical);
        auto wrap3 = s376::wrap_channels(c3.channels);
        CHECK(wrap3.size() == 3);
        CHECK(wrap3[0].name == "left" && wrap3[1].name == "front" && wrap3[2].name == "right");
    }
    {
        std::string r3 = cfg_path("config/raster-3.toml");
        auto rc = s376::load_config(r3);
        CHECK(rc.channels.size() == 4);
        auto wrapr = s376::wrap_channels(rc.channels);
        CHECK(wrapr.size() == 3);
        CHECK(wrapr[0].name == "left" && wrapr[0].flip_vertical);
        CHECK(wrapr[1].name == "front" && wrapr[2].name == "right");
        CHECK(wrapr[0].width == 1920 && wrapr[0].height == 1200);
    }

    std::ifstream ksea_earth("data/earth/ksea.earth");
    CHECK(static_cast<bool>(ksea_earth));
    std::string earth((std::istreambuf_iterator<char>(ksea_earth)), std::istreambuf_iterator<char>());
    CHECK(earth.find("cache/ksea/elev.tif") != std::string::npos);
    CHECK(earth.find("cache/ksea/imagery.tif") != std::string::npos);
    CHECK(earth.find("cache/ksea/osm.geojson") != std::string::npos);
    CHECK(earth.find("feature_geom") != std::string::npos);
    std::ifstream globe("data/earth/s376.earth");
    CHECK(static_cast<bool>(globe));

    std::string desk = cfg_path("config/desk-4monitor.toml");
    auto desk_cfg = s376::load_config(desk);
    CHECK(desk_cfg.channels.size() == 4);
    CHECK(desk_cfg.render.msaa == 4);
    CHECK(desk_cfg.render.aniso == 16);
    CHECK(desk_cfg.render.earth_file.find("ksea.earth") != std::string::npos);
    bool ios = false, left = false, front = false, right = false;
    for (const auto& ch : desk_cfg.channels) {
        if (ch.name == "ios") {
            ios = ch.window_x == 0 && ch.pitch_offset_deg < 0;
        }
        if (ch.name == "left") {
            left = ch.window_x == 1920;
            CHECK_NEAR(ch.heading_offset_deg, -50.0, 1e-6);
        }
        if (ch.name == "front")
            front = ch.window_x == 3840;
        if (ch.name == "right")
            right = ch.window_x == 5760 && ch.heading_offset_deg > 0;
    }
    CHECK(ios && left && front && right);
    auto placed = s376::layout_channels(s376::select_channels(desk_cfg, "all"), 1, 7680, 1080);
    CHECK(placed.size() == 4);
    for (const auto& p : placed) {
        if (p.name == "ios")
            CHECK(p.x == 0);
        if (p.name == "front")
            CHECK(p.x == 3840);
    }
    {
        // Channel count is TOML, not a hardcoded 3. Four unique headings → 4.
        auto four = s376::default_config();
        CHECK(four.channels.size() == 4);
        auto wrap_dup = s376::wrap_channels(four.channels);
        CHECK(wrap_dup.size() == 3); // FO front shares heading 0 with captain front
        auto strip4 = s376::strip4_channels(four.channels);
        CHECK(strip4.size() == 4);
        four.channels[2].heading_offset_deg = 25.0;
        auto wrap4 = s376::wrap_channels(four.channels);
        CHECK(wrap4.size() == 4);
        CHECK(wrap4[0].heading_offset_deg < wrap4[1].heading_offset_deg);
        CHECK(wrap4[2].heading_offset_deg < wrap4[3].heading_offset_deg);
    }
}
