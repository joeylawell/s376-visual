#include "s376/airport.hpp"
#include "s376/config.hpp"
#include "s376/diagnostics.hpp"
#include "s376/image.hpp"
#include "s376/render.hpp"
#include "s376/state.hpp"
#include "s376/terrain.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
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

void test_image() {
    const auto solid = s376::make_solid_rgb(8, 6, 40, 80, 160);
    CHECK(solid.w == 8 && solid.h == 6);
    CHECK(solid.rgb.size() == 8u * 6u * 3u);
    CHECK(s376::pixels_near_solid(solid, 40, 80, 160, 0));
    CHECK(!s376::pixels_near_solid(solid, 255, 0, 0, 2));

    const auto grad = s376::make_gradient_rgb(16, 12);
    CHECK(grad.w == 16 && grad.h == 12);
    CHECK(s376::pixels_near_gradient(grad, 0));
    CHECK(grad.rgb[0] == 0 && grad.rgb[1] == 0 && grad.rgb[2] == 128);
    CHECK(grad.rgb[static_cast<std::size_t>((16 - 1) * 3)] == 255);

    std::vector<std::uint8_t> rgba = {10, 20, 30, 255, 40, 50, 60, 128};
    std::vector<std::uint8_t> rgb(6, 0);
    s376::rgba_to_rgb(rgba.data(), 2, rgb.data());
    CHECK(rgb[0] == 10 && rgb[1] == 20 && rgb[2] == 30);
    CHECK(rgb[3] == 40 && rgb[4] == 50 && rgb[5] == 60);

    std::uint8_t flip[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    s376::flip_vert(flip, 2, 2, 3);
    CHECK(flip[0] == 7 && flip[5] == 12 && flip[6] == 1 && flip[11] == 6);

    const auto tmp = (std::filesystem::temp_directory_path() / "s376-test-image").string();
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp);
    std::string err;
    CHECK(s376::self_test_shot_files(tmp, err));
    CHECK(err.empty());
    CHECK(std::filesystem::exists(tmp + "/s376-shot-self.ppm"));
    CHECK(std::filesystem::exists(tmp + "/s376-shot-self.png"));
    CHECK(std::filesystem::file_size(tmp + "/s376-shot-self.png") > 32);

    s376::RgbImage back;
    CHECK(s376::read_ppm(tmp + "/s376-shot-self.ppm", back));
    CHECK(back.w == 64 && back.h == 48);
    CHECK(s376::pixels_near_gradient(back, 0));

    std::string ppm, png;
    CHECK(s376::write_shot_images(tmp + "/pair.png", solid, &ppm, &png));
    CHECK(ppm.find(".ppm") != std::string::npos);
    CHECK(png.find(".png") != std::string::npos);
    CHECK(std::filesystem::exists(ppm) && std::filesystem::exists(png));

    CHECK(s376::looks_like_video_path("out.mp4"));
    CHECK(s376::looks_like_video_path("/tmp/jfk.MOV"));
    CHECK(!s376::looks_like_video_path("/tmp/frames"));
    {
        const auto a = s376::make_solid_rgb(4, 3, 10, 20, 30);
        const auto b = s376::make_solid_rgb(5, 3, 40, 50, 60);
        const auto strip = s376::stitch_horizontal({a, b});
        CHECK(strip.w == 9 && strip.h == 3);
        CHECK(strip.rgb[0] == 10 && strip.rgb[2] == 30);
        CHECK(strip.rgb[4 * 3] == 40 && strip.rgb[4 * 3 + 2] == 60);
    }

    CHECK(s376::record_frames_dir("out.mp4") == "out-frames");
    CHECK(s376::record_frames_dir("/tmp/jfk-frames") == "/tmp/jfk-frames");
    CHECK(s376::frame_png_path("/tmp/f", 1).find("frame-000001.png") != std::string::npos);
    const auto raw = s376::ffmpeg_rawvideo_cmd(1920, 1080, 30, "out.mp4");
    CHECK(raw.find("ffmpeg") != std::string::npos);
    CHECK(raw.find("1920x1080") != std::string::npos);
    CHECK(raw.find("rgb24") != std::string::npos);
    CHECK(raw.find("'out.mp4'") != std::string::npos);
    const auto mux = s376::ffmpeg_png_seq_cmd("out-frames", 30, "out.mp4");
    CHECK(mux.find("frame-%06d.png") != std::string::npos);
    CHECK(mux.find("'out.mp4'") != std::string::npos);
    CHECK(s376::shell_single_quote("a'b").find("'\\''") != std::string::npos);

    {
        auto sky = s376::make_solid_rgb(64, 40, 140, 184, 242);
        CHECK(s376::far_field_is_empty_sky(sky));
        CHECK(s376::sky_like_fraction(sky, 0.36f, 0.74f, 0.34f, 0.50f) > 0.9f);
        auto land = s376::make_solid_rgb(64, 40, 78, 98, 52);
        CHECK(!s376::far_field_is_empty_sky(land));
        auto water = s376::make_solid_rgb(64, 40, 18, 55, 110);
        CHECK(!s376::far_field_is_empty_sky(water));
        auto mixed = s376::make_solid_rgb(64, 40, 140, 184, 242);
        // Paint the far-field band (y 34–50%) as water so the detector
        // must see terrain, not sky, behind the airport.
        for (int y = 13; y < 21; ++y) {
            for (int x = 0; x < 64; ++x) {
                const std::size_t o = static_cast<std::size_t>(y * 64 + x) * 3u;
                mixed.rgb[o] = 18;
                mixed.rgb[o + 1] = 55;
                mixed.rgb[o + 2] = 110;
            }
        }
        CHECK(!s376::far_field_is_empty_sky(mixed));
        CHECK(s376::horizon_luma_step(sky) < 5.f);
        CHECK(!s376::horizon_is_hard_edge(sky));
        auto knife = s376::make_solid_rgb(64, 40, 140, 184, 242);
        for (int y = 20; y < 40; ++y) {
            for (int x = 0; x < 64; ++x) {
                const std::size_t o = static_cast<std::size_t>(y * 64 + x) * 3u;
                knife.rgb[o] = 40;
                knife.rgb[o + 1] = 90;
                knife.rgb[o + 2] = 40;
            }
        }
        CHECK(s376::horizon_is_hard_edge(knife));
        auto wash = s376::make_solid_rgb(64, 40, 230, 235, 240);
        CHECK(s376::water_is_washed_white(wash));
        auto bay = s376::make_solid_rgb(64, 40, 18, 55, 110);
        CHECK(!s376::water_is_washed_white(bay));
    }
    {
        std::uint8_t r = 0, g = 0, b = 0;
        bool water = false;
        s376::kjfk_subset_land_rgb(40.620, -73.774, r, g, b, water);
        CHECK(water);
        CHECK(g > r && g > b); // Jamaica Bay olive, not navy
        s376::kjfk_subset_land_rgb(40.500, -73.800, r, g, b, water);
        CHECK(water);
        CHECK(b > r && b > g); // Atlantic stays blue
        s376::kjfk_subset_land_rgb(40.640, -73.779, r, g, b, water);
        CHECK(!water);
        s376::kjfk_subset_land_rgb(40.72, -74.00, r, g, b, water);
        CHECK(!water);
        CHECK(std::abs(static_cast<int>(r) - static_cast<int>(g)) < 16); // urban gray
        s376::kjfk_subset_land_rgb(40.565, -73.85, r, g, b, water);
        CHECK(!water);
        CHECK(r > 140 && g > 120); // Rockaway sand
    }
#ifdef S376_HAS_OSG
    if (std::getenv("DISPLAY")) {
        const auto pack = (std::filesystem::temp_directory_path() / "s376-farfield-gl").string();
        std::filesystem::remove_all(pack);
        if (s376::write_kjfk_farfield_subset(pack)) {
            auto cfg = s376::default_config();
            cfg.render.cache_dir = pack;
            cfg.render.fxaa = false;
            cfg.render.msaa = 0;
            cfg.render.shadows = false;
            cfg.render.ram_mb = 256;
            cfg.render.vram_mb = 256;
            s376::ChannelConfig ch;
            ch.name = "front";
            ch.width = 320;
            ch.height = 200;
            ch.hfov_deg = 50;
            ch.vfov_deg = 32.5;
            auto rend = s376::make_renderer("osg");
            if (rend && rend->begin(cfg, {ch})) {
                const auto apt = s376::airport_by_icao("KJFK");
                s376::ShotPose pose;
                CHECK(s376::runway_shot_pose(apt, "04R", 2.99, pose));
                s376::HostState host;
                host.have_eyepoint = true;
                host.eye.csn = 1;
                host.eye.minor_csn = -1;
                host.eye.lat_deg = pose.lat;
                host.eye.lon_deg = pose.lon;
                host.eye.alt_ft = pose.alt_ft;
                host.eye.heading_deg = static_cast<float>(-pose.hdg_true_deg);
                host.eye.pitch_deg = static_cast<float>(pose.pitch_deg);
                host.have_env = true;
                host.env.tod_select = -1;
                host.env.tod_hours = 12.f;
                host.env.visible_range_ft = 528000.f;
                host.env.month = 6;
                host.env.day = 21;
                for (int i = 0; i < 24; ++i)
                    rend->frame(host);
                s376::RgbImage im;
                if (rend->capture_rgb(im) && im.w >= 32 && im.h >= 32) {
                    const float lower = s376::sky_like_fraction(im, 0.1f, 0.9f, 0.70f, 0.98f);
                    if (lower < 0.85f) // not a whole-frame FBO miss
                        CHECK(!s376::far_field_is_empty_sky(im));
                    (void)s376::write_shot_images("/tmp/kjfk_farfield_regtest.png", im);
                }
                rend->end();
            }
        }
        std::filesystem::remove_all(pack);
    }
#endif

    s376::RecordWriter rec;
    CHECK(rec.open(tmp + "/clip.mp4", 8, 6, 30, true));
    CHECK(!rec.used_ffmpeg());
    CHECK(rec.write(solid));
    CHECK(rec.write(solid));
    CHECK(rec.close());
    CHECK(rec.frames() == 2);
    CHECK(std::filesystem::exists(s376::frame_png_path(rec.frames_dir(), 1)));
    CHECK(std::filesystem::exists(s376::frame_png_path(rec.frames_dir(), 2)));
    CHECK(rec.mux_cmd().find("ffmpeg") != std::string::npos);

    if (s376::ffmpeg_on_path()) {
        const auto mp4 = tmp + "/tiny.mp4";
        const auto tiny = s376::make_solid_rgb(16, 16, 40, 80, 160);
        s376::RecordWriter pipe;
        CHECK(pipe.open(mp4, 16, 16, 30, false));
        CHECK(pipe.used_ffmpeg());
        CHECK(pipe.write(tiny));
        CHECK(pipe.write(tiny));
        CHECK(pipe.close());
        CHECK(pipe.frames() == 2);
        CHECK(std::filesystem::exists(mp4));
        CHECK(std::filesystem::file_size(mp4) > 32);
    }
    std::filesystem::remove_all(tmp);

    auto cfg = s376::default_config();
    const std::string all = s376::dump_config(cfg);
    CHECK(all.find("channel far_left") != std::string::npos);
    CHECK(all.find("channel far_right") != std::string::npos);
    CHECK(all.find("selected=") == std::string::npos);

    const std::string mid = s376::dump_config(cfg, {"mid_left"});
    CHECK(mid.find("selected=mid_left") != std::string::npos);
    CHECK(mid.find("channel mid_left") != std::string::npos);
    CHECK(mid.find("hdg=0") != std::string::npos);
    CHECK(mid.find("channel far_right") == std::string::npos);
    CHECK(mid.find("channel far_left") == std::string::npos);
    CHECK(mid.find("1920x1200") != std::string::npos);

    const std::string two = s376::dump_config(cfg, {"far_left", "mid_left"});
    CHECK(two.find("selected=far_left,mid_left") != std::string::npos);
    CHECK(two.find("channel far_left") != std::string::npos);
    CHECK(two.find("channel mid_left") != std::string::npos);
    CHECK(two.find("channel far_right") == std::string::npos);

    s376::HostState shot;
    shot.have_eyepoint = true;
    shot.eyepoints = 1;
    shot.datagrams = 0;
    shot.eye.lat_deg = 40.4932;
    shot.eye.lon_deg = -73.9191;
    shot.eye.alt_ft = 12000;
    shot.eye.heading_deg = -36.f;
    const auto dump = s376::diagnostics_dump(cfg, &shot, {"mid_left"});
    CHECK(dump.find("selected=mid_left") != std::string::npos);
    CHECK(dump.find("channel mid_left") != std::string::npos);
    CHECK(dump.find("channel far_right") == std::string::npos);
    CHECK(dump.find("datagrams=0") != std::string::npos);
    CHECK(dump.find("eyepoints=1") != std::string::npos);
    CHECK(dump.find("alt_ft=12000") != std::string::npos);
}
