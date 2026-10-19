#include "s376/dxt.hpp"
#include "s376/geo.hpp"
#include "s376/terrain.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
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

void test_terrain() {
    auto dem = s376::DemGrid::synth_ksea(64, 64);
    CHECK(dem.w == 64 && dem.h == 64);
    CHECK(dem.in_bounds(47.4479, -122.3103));
    CHECK(!dem.in_bounds(0, 0));

    double z_m = 0, z_ft = 0;
    CHECK(dem.sample_m(47.4479, -122.3103, z_m));
    CHECK_NEAR(z_m, 132.0, 0.6); // flattened airport pad
    CHECK(dem.sample_ft(47.4479, -122.3103, z_ft));
    CHECK_NEAR(z_ft, 132.0 * s376::geo::kMToFt, 2.0);
    CHECK_NEAR(z_ft, 433.0, 3.0);

    // 16L threshold is on the pad.
    CHECK(dem.sample_ft(47.463684, -122.307751, z_ft));
    CHECK_NEAR(z_ft, 433.0, 8.0);

    const auto tmp = std::filesystem::temp_directory_path() / "s376-terrain-test";
    std::filesystem::create_directories(tmp);
    const auto dem_path = (tmp / "elev.s376dem").string();
    dem.save(dem_path);
    auto loaded = s376::DemGrid::load(dem_path);
    CHECK(loaded.w == dem.w && loaded.h == dem.h);
    double z2 = 0;
    CHECK(loaded.sample_m(47.4479, -122.3103, z2));
    CHECK_NEAR(z2, z_m, 1e-4);
    double zf = 0;
    CHECK(s376::DemGrid::sample_file_m(dem_path, 47.4479, -122.3103, zf));
    CHECK_NEAR(zf, z2, 1e-3);
    CHECK(!s376::DemGrid::sample_file_m(dem_path, 0, 0, zf));

    s376::DemHeightSource src(std::make_shared<s376::DemGrid>(dem));
    double elev = -1;
    CHECK(src.sample_ft(47.4479, -122.3103, elev));
    CHECK_NEAR(elev, 433.0, 3.0);
    CHECK(!src.sample_ft(0, 0, elev));

    auto hs = s376::ImageRgb::hillshade(dem);
    CHECK(hs.w == dem.w && hs.h == dem.h);
    CHECK(hs.rgb.size() == static_cast<std::size_t>(hs.w * hs.h * 3));

    // Solid red 8×8 → BC1 round-trip stays reddish.
    std::vector<std::uint8_t> red(8 * 8 * 3);
    for (std::size_t i = 0; i < red.size(); i += 3) {
        red[i] = 220;
        red[i + 1] = 20;
        red[i + 2] = 20;
    }
    auto blocks = s376::dxt::encode_bc1(red.data(), 8, 8);
    CHECK(blocks.size() == s376::dxt::bc1_size(8, 8));
    CHECK(blocks.size() == 32); // 2×2 blocks × 8 bytes
    auto back = s376::dxt::decode_bc1(blocks.data(), 8, 8);
    CHECK(back.size() == red.size());
    CHECK(back[0] > 160 && back[1] < 80 && back[2] < 80);

    const auto dds = (tmp / "red.dds").string();
    s376::dxt::write_dds_bc1(dds, red.data(), 8, 8);
    CHECK(std::filesystem::file_size(dds) == 128 + 32);

    auto region = s376::generate_ksea_cache();
    const auto cdir = (tmp / "ksea").string();
    s376::write_cache(region, cdir);
    CHECK(std::filesystem::exists(cdir + "/elev.s376dem"));
    CHECK(std::filesystem::exists(cdir + "/imagery.dds"));
    CHECK(std::filesystem::exists(cdir + "/region.toml"));
#ifdef S376_HAS_GDAL
    CHECK(std::filesystem::exists(cdir + "/elev.tif"));
    CHECK(std::filesystem::exists(cdir + "/imagery.tif"));
#endif
    const auto packed = std::filesystem::file_size(cdir + "/elev.s376dem") +
                        std::filesystem::file_size(cdir + "/imagery.dds");
    CHECK(packed < 200000);          // hundreds of KiB, not 857 GB
    CHECK(packed > 64);              // not empty
    auto again = s376::load_cache(cdir);
    CHECK(again.dem.w == region.dem.w);
    CHECK(!again.imagery_dds.empty());
    CHECK(again.imagery.rgb.empty()); // load_cache must not hillshade / inflate RGB

    auto peeked = s376::DemGrid::peek(dem_path);
    CHECK(peeked.w == dem.w && peeked.h == dem.h);
    CHECK(peeked.z_m.empty());
    auto sub = s376::DemGrid::load_subsampled(dem_path, 16);
    CHECK(sub.w == 16 && sub.h == 16);
    CHECK(sub.z_m.size() == 256);
    double zsub = 0;
    CHECK(sub.sample_m(47.4479, -122.3103, zsub));
    CHECK_NEAR(zsub, z_m, 8.0);
    auto meshed = s376::load_cache_mesh(cdir, 32);
    CHECK(meshed.dem.w <= 32 && meshed.dem.h <= 32);
    CHECK(meshed.imagery.rgb.empty());

    s376::dxt::CompressedImage dds_img{};
    CHECK(s376::dxt::read_dds(dds, dds_img));
    CHECK(dds_img.w == 8 && dds_img.h == 8);
    CHECK(dds_img.fmt == s376::dxt::BlockFmt::Bc1);
    CHECK(dds_img.blocks.size() == 32);
    {
        const auto bc7p = (tmp / "red7.dds").string();
        s376::dxt::write_dds_bc7(bc7p, red.data(), 8, 8);
        s376::dxt::CompressedImage c7{};
        CHECK(s376::dxt::read_dds(bc7p, c7));
        CHECK(c7.fmt == s376::dxt::BlockFmt::Bc7);
        auto rgb7 = s376::dxt::decode_bc7(c7.blocks.data(), 8, 8);
        CHECK(rgb7.size() == red.size());
        CHECK(rgb7[0] > 140 && rgb7[1] < 90 && rgb7[2] < 90);
        s376::dxt::make_osg_safe(c7);
        CHECK(c7.fmt == s376::dxt::BlockFmt::Bc1);
        CHECK(c7.blocks.size() == s376::dxt::bc1_size(8, 8));
    }

    const auto tiles = s376::scan_cache_tiles(cdir);
    CHECK(!tiles.empty());
    {
        const auto leaf = cdir + "/lod0/t0_0";
        std::filesystem::create_directories(leaf);
        std::filesystem::copy_file(cdir + "/elev.s376dem", leaf + "/elev.s376dem",
                                   std::filesystem::copy_options::overwrite_existing);
        std::filesystem::copy_file(cdir + "/elev.s376dem", cdir + "/lod0/elev.s376dem",
                                   std::filesystem::copy_options::overwrite_existing);
        const auto scanned = s376::scan_cache_tiles(cdir);
        bool saw_leaf = false, saw_parent = false, saw_root = false;
        // path::operator== treats `/` and `\` as the same element separator.
        const auto same = [](const std::string& a, const std::string& b) {
            return std::filesystem::path(a) == std::filesystem::path(b);
        };
        for (const auto& t : scanned) {
            if (same(t.path, leaf))
                saw_leaf = true;
            if (same(t.path, cdir + "/lod0"))
                saw_parent = true;
            if (same(t.path, cdir))
                saw_root = true;
        }
        CHECK(saw_leaf);
        CHECK(!saw_parent); // parent lod0/ must not page in over tY_X
        CHECK(!saw_root);
    }
    CHECK(s376::cache_dir_looks_packed(cdir));
    CHECK(s376::resolve_cache_dir(cdir, "KSEA") == cdir);
    CHECK(s376::resolve_cache_dir("", "XXXX").empty());
    s376::CacheTileRef fake;
    fake.dem_w = 4000;
    fake.dem_h = 4000;
    fake.dds_bytes = 21ull * 1024ull * 1024ull;
    const auto naive = 4000ull * 4000ull * 48ull;
    const auto paged = s376::estimate_tile_cpu_bytes(fake, 96);
    CHECK(paged < 2ull * 1024ull * 1024ull);
    CHECK(naive > 500ull * 1024ull * 1024ull);
    CHECK(paged * 14ull < (2ull << 30));
    const auto rss = s376::peak_rss_bytes();
    CHECK(rss > 0);
    CHECK(rss < (2ull << 30)); // unit tests must stay under the 980 Ti RAM budget

    const auto hat = s376::sample_hat(&src, 47.463684, -122.307751, 447.0);
    CHECK(hat.covered);
    CHECK_NEAR(hat.elev_ft, 433.0, 8.0);
    CHECK_NEAR(hat.hat_ft, 447.0 - hat.elev_ft, 0.05);
    const auto miss = s376::sample_hat(&src, 0.0, 0.0, 1000.0);
    CHECK(!miss.covered);
    CHECK_NEAR(miss.hat_ft, 1000.0, 0.01);
    CHECK_NEAR(miss.elev_ft, 0.0, 0.01);
    const auto standin = s376::sample_hat(nullptr, 47.46, -122.31, 500.0);
    CHECK(!standin.covered);
    CHECK_NEAR(standin.hat_ft, 500.0, 0.01);

    CHECK(s376::dxt::gpu_tex_dim(8) == 8);
    CHECK(s376::dxt::gpu_tex_dim(15) == 16);
    CHECK(s376::dxt::gpu_tex_dim(2011) == 2048);
    CHECK(s376::dxt::gpu_tex_dim(2033) == 2048);
    {
        std::vector<std::uint8_t> rgb(15 * 17 * 3, 90);
        int w = 15, h = 17;
        CHECK(s376::dxt::pad_rgb_for_gpu(rgb, w, h));
        CHECK(w == 16 && h == 20);
        CHECK(rgb.size() == static_cast<std::size_t>(16 * 20 * 3));
        const auto odd = (tmp / "odd.dds").string();
        std::vector<std::uint8_t> src(15 * 17 * 3, 200);
        s376::dxt::write_dds_bc1(odd, src.data(), 15, 17);
        s376::dxt::CompressedImage ci{};
        CHECK(s376::dxt::read_dds(odd, ci));
        CHECK(ci.w % 4 == 0 && ci.h % 4 == 0);
        CHECK(ci.w >= 16 && ci.h >= 16);
        CHECK(!std::filesystem::exists(odd + ".tmp"));
    }
    CHECK(s376::dem_file_complete(cdir + "/elev.s376dem"));
    {
        const auto far = (tmp / "kjfk-farfield").string();
        CHECK(s376::write_kjfk_farfield_subset(far));
        {
            const auto dem = s376::DemGrid::load(far + "/lod0/t0_0/elev.s376dem");
            CHECK(dem.w >= 64 && dem.h >= 64);
            bool pad_flat = true;
            for (int y = 0; y < dem.h && pad_flat; ++y) {
                const double lat =
                    dem.north - (dem.north - dem.south) * y / std::max(1, dem.h - 1);
                for (int x = 0; x < dem.w; ++x) {
                    const double lon =
                        dem.west + (dem.east - dem.west) * x / std::max(1, dem.w - 1);
                    if (lat <= 40.626 || lat >= 40.662 || lon <= -73.828 || lon >= -73.752)
                        continue;
                    if (s376::geo::land_coverage(lat, lon) < 0.40f)
                        continue;
                    const float z = dem.z_m[static_cast<std::size_t>(y * dem.w + x)];
                    if (z < 3.5f || z > 4.5f)
                        pad_flat = false;
                }
            }
            CHECK(pad_flat);
        }
        CHECK(std::filesystem::exists(far + "/ATTRIBUTION.txt"));
        CHECK(std::filesystem::exists(far + "/coverage.toml"));
        CHECK(std::filesystem::exists(far + "/region.toml"));
        CHECK(std::filesystem::exists(far + "/lod0/t0_0/elev.s376dem"));
        CHECK(std::filesystem::exists(far + "/lod1/t0_0/elev.s376dem"));
        CHECK(std::filesystem::exists(far + "/lod2/t0_0/elev.s376dem"));
        const auto packed = s376::scan_cache_tiles(far);
        int n0 = 0, n1 = 0, n2 = 0;
        for (const auto& t : packed) {
            if (t.lod == 0)
                ++n0;
            else if (t.lod == 1)
                ++n1;
            else if (t.lod == 2)
                ++n2;
        }
        CHECK(n0 >= 1 && n1 >= 1 && n2 >= 1);
        // 2.99 nm 04R: eye ~5.5 km SW of the threshold. lod2 must be reserved.
        std::vector<s376::TilePageWant> want;
        s376::score_tiles_for_view(packed, 40.612, -73.805, 40.640, -73.779, 36.0, true, false,
                                   want);
        bool reserved_lod2 = false, reserved_lod1 = false;
        double lod2_span_km = 0;
        for (const auto& s : want) {
            if (!s.reserve_coarse || !s.t)
                continue;
            if (s.t->lod == 2) {
                reserved_lod2 = true;
                lod2_span_km = (s.t->north - s.t->south) * 111.32;
            }
            if (s.t->lod == 1)
                reserved_lod1 = true;
        }
        CHECK(reserved_lod1);
        CHECK(reserved_lod2);
        CHECK(lod2_span_km + 1.0 >= 70.0); // 40 nm disk, Jamaica Bay / Rockaway / Queens
        CHECK(s376::kFarFieldNm >= 40.0);
        CHECK(s376::kFarFieldKm + 0.01 >= 40.0 * 1.852);
        s376::dxt::CompressedImage mips{};
        CHECK(s376::dxt::read_dds(far + "/lod2/t0_0/imagery.dds", mips));
        CHECK(mips.mips > 1);
    }
    {
        const auto trunc = (tmp / "trunc.s376dem").string();
        {
            std::ifstream in(cdir + "/elev.s376dem", std::ios::binary);
            std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            CHECK(bytes.size() > 80);
            std::ofstream out(trunc, std::ios::binary);
            out.write(bytes.data(), 64); // header only — fetch-in-progress
        }
        CHECK(!s376::dem_file_complete(trunc));
        bool threw = false;
        try {
            (void)s376::DemGrid::load(trunc);
        } catch (const std::exception& ex) {
            threw = std::string(ex.what()).find("truncated") != std::string::npos;
        }
        CHECK(threw);
    }

#ifdef S376_HAS_GDAL
    CHECK(s376::gdal_available());
    const auto tif = (tmp / "ksea.tif").string();
    dem.save_geotiff(tif);
    auto from_tif = s376::DemGrid::load_geotiff(tif);
    CHECK(from_tif.w == dem.w && from_tif.h == dem.h);
    double zt = 0;
    CHECK(from_tif.sample_m(47.4479, -122.3103, zt));
    CHECK_NEAR(zt, z_m, 1.0);
#else
    CHECK(!s376::gdal_available());
#endif

    std::filesystem::remove_all(tmp);
}
