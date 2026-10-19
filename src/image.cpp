#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "s376/image.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace s376 {
namespace {

std::string stem_of(const std::string& path) {
    auto dot = path.find_last_of('.');
    auto slash = path.find_last_of("/\\");
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
        return path.substr(0, dot);
    return path;
}

std::string ext_of(const std::string& path) {
    auto dot = path.find_last_of('.');
    auto slash = path.find_last_of("/\\");
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        std::string e = path.substr(dot);
        for (char& c : e)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return e;
    }
    return {};
}

} // namespace

RgbImage make_solid_rgb(int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    RgbImage im;
    im.w = std::max(1, w);
    im.h = std::max(1, h);
    im.rgb.assign(static_cast<std::size_t>(im.w * im.h * 3), 0);
    for (int i = 0; i < im.w * im.h; ++i) {
        im.rgb[static_cast<std::size_t>(i * 3 + 0)] = r;
        im.rgb[static_cast<std::size_t>(i * 3 + 1)] = g;
        im.rgb[static_cast<std::size_t>(i * 3 + 2)] = b;
    }
    return im;
}

RgbImage make_gradient_rgb(int w, int h) {
    RgbImage im;
    im.w = std::max(1, w);
    im.h = std::max(1, h);
    im.rgb.resize(static_cast<std::size_t>(im.w * im.h * 3));
    for (int y = 0; y < im.h; ++y) {
        for (int x = 0; x < im.w; ++x) {
            const int o = (y * im.w + x) * 3;
            im.rgb[static_cast<std::size_t>(o + 0)] =
                static_cast<std::uint8_t>(x * 255 / std::max(1, im.w - 1));
            im.rgb[static_cast<std::size_t>(o + 1)] =
                static_cast<std::uint8_t>(y * 255 / std::max(1, im.h - 1));
            im.rgb[static_cast<std::size_t>(o + 2)] = 128;
        }
    }
    return im;
}

void flip_vert(std::uint8_t* p, int w, int h, int comp) {
    if (!p || w < 1 || h < 2 || comp < 1)
        return;
    const int stride = w * comp;
    std::vector<std::uint8_t> row(static_cast<std::size_t>(stride));
    for (int y = 0; y < h / 2; ++y) {
        std::uint8_t* a = p + y * stride;
        std::uint8_t* b = p + (h - 1 - y) * stride;
        std::memcpy(row.data(), a, static_cast<std::size_t>(stride));
        std::memcpy(a, b, static_cast<std::size_t>(stride));
        std::memcpy(b, row.data(), static_cast<std::size_t>(stride));
    }
}

void rgba_to_rgb(const std::uint8_t* rgba, int n, std::uint8_t* rgb) {
    for (int i = 0; i < n; ++i) {
        rgb[i * 3 + 0] = rgba[i * 4 + 0];
        rgb[i * 3 + 1] = rgba[i * 4 + 1];
        rgb[i * 3 + 2] = rgba[i * 4 + 2];
    }
}

bool write_ppm(const std::string& path, const RgbImage& im) {
    if (im.w < 1 || im.h < 1 ||
        im.rgb.size() < static_cast<std::size_t>(im.w * im.h * 3))
        return false;
    std::ofstream out(path, std::ios::binary);
    if (!out)
        return false;
    out << "P6\n" << im.w << " " << im.h << "\n255\n";
    out.write(reinterpret_cast<const char*>(im.rgb.data()),
              static_cast<std::streamsize>(im.w * im.h * 3));
    return static_cast<bool>(out);
}

bool write_png(const std::string& path, const RgbImage& im) {
    if (im.w < 1 || im.h < 1 ||
        im.rgb.size() < static_cast<std::size_t>(im.w * im.h * 3))
        return false;
    return stbi_write_png(path.c_str(), im.w, im.h, 3, im.rgb.data(), im.w * 3) != 0;
}

RgbImage stitch_horizontal(const std::vector<RgbImage>& parts) {
    RgbImage out;
    if (parts.empty())
        return out;
    int w = 0, h = 0;
    for (const auto& p : parts) {
        if (p.w < 1 || p.h < 1 ||
            p.rgb.size() < static_cast<std::size_t>(p.w * p.h * 3))
            return {};
        w += p.w;
        h = std::max(h, p.h);
    }
    out.w = w;
    out.h = h;
    out.rgb.assign(static_cast<std::size_t>(w * h * 3), 0);
    int x0 = 0;
    for (const auto& p : parts) {
        for (int y = 0; y < p.h; ++y) {
            std::memcpy(out.rgb.data() + static_cast<std::size_t>((y * w + x0) * 3),
                        p.rgb.data() + static_cast<std::size_t>(y * p.w * 3),
                        static_cast<std::size_t>(p.w * 3));
        }
        x0 += p.w;
    }
    return out;
}

bool write_shot_images(const std::string& path, const RgbImage& im, std::string* ppm_out,
                       std::string* png_out) {
    const std::string stem = stem_of(path);
    const std::string ext = ext_of(path);
    std::string ppm = stem + ".ppm";
    std::string png = stem + ".png";
    if (ext == ".ppm")
        ppm = path;
    if (ext == ".png")
        png = path;
    const bool ok_ppm = write_ppm(ppm, im);
    const bool ok_png = write_png(png, im);
    if (ppm_out)
        *ppm_out = ppm;
    if (png_out)
        *png_out = png;
    return ok_ppm && ok_png;
}

bool read_ppm(const std::string& path, RgbImage& im) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::string magic;
    in >> magic;
    if (magic != "P6")
        return false;
    int w = 0, h = 0, maxv = 0;
    in >> w >> h >> maxv;
    if (w < 1 || h < 1 || maxv != 255)
        return false;
    in.get(); // single whitespace after header
    im.w = w;
    im.h = h;
    im.rgb.resize(static_cast<std::size_t>(w * h * 3));
    in.read(reinterpret_cast<char*>(im.rgb.data()), static_cast<std::streamsize>(im.rgb.size()));
    return static_cast<bool>(in);
}

bool pixels_near_solid(const RgbImage& im, std::uint8_t r, std::uint8_t g, std::uint8_t b, int tol) {
    if (im.w < 1 || im.h < 1 ||
        im.rgb.size() < static_cast<std::size_t>(im.w * im.h * 3))
        return false;
    for (int i = 0; i < im.w * im.h; ++i) {
        const int o = i * 3;
        if (std::abs(static_cast<int>(im.rgb[static_cast<std::size_t>(o + 0)]) - r) > tol)
            return false;
        if (std::abs(static_cast<int>(im.rgb[static_cast<std::size_t>(o + 1)]) - g) > tol)
            return false;
        if (std::abs(static_cast<int>(im.rgb[static_cast<std::size_t>(o + 2)]) - b) > tol)
            return false;
    }
    return true;
}

float sky_like_fraction(const RgbImage& im, float x0, float x1, float y0, float y1) {
    if (im.w < 8 || im.h < 8 ||
        im.rgb.size() < static_cast<std::size_t>(im.w * im.h * 3))
        return 1.f;
    const int xa = std::max(0, std::min(im.w - 1, static_cast<int>(x0 * static_cast<float>(im.w))));
    const int xb = std::max(xa + 1, std::min(im.w, static_cast<int>(x1 * static_cast<float>(im.w))));
    const int ya = std::max(0, std::min(im.h - 1, static_cast<int>(y0 * static_cast<float>(im.h))));
    const int yb = std::max(ya + 1, std::min(im.h, static_cast<int>(y1 * static_cast<float>(im.h))));
    int sky = 0, n = 0;
    for (int y = ya; y < yb; ++y) {
        for (int x = xa; x < xb; ++x) {
            const std::size_t o = static_cast<std::size_t>(y * im.w + x) * 3u;
            const int r = im.rgb[o], g = im.rgb[o + 1], b = im.rgb[o + 2];
            // Hangar empty-far defect: flat light sky-blue (horizon/zenith).
            // Globe water is dark navy; land is olive — neither matches.
            const bool sky_px = b >= 130 && b >= g - 8 && g >= r - 4 && (b - r) >= 24 &&
                                (r + g + b) >= 360;
            if (sky_px)
                ++sky;
            ++n;
        }
    }
    return n > 0 ? static_cast<float>(sky) / static_cast<float>(n) : 1.f;
}

bool far_field_is_empty_sky(const RgbImage& im) {
    if (im.w < 32 || im.h < 32)
        return true;
    // Strip just above the airport on a 2.99 nm 3° 04R shot (horizon
    // sits near mid-frame; behind the field is this band).
    const float far = sky_like_fraction(im, 0.36f, 0.74f, 0.34f, 0.50f);
    long sr = 0, sg = 0, sb = 0, n = 0;
    const int x0 = static_cast<int>(0.36f * im.w), x1 = static_cast<int>(0.74f * im.w);
    const int y0 = static_cast<int>(0.34f * im.h), y1 = static_cast<int>(0.50f * im.h);
    for (int y = y0; y < std::max(y0 + 1, y1) && y < im.h; ++y) {
        for (int x = x0; x < std::max(x0 + 1, x1) && x < im.w; ++x) {
            const std::size_t o = static_cast<std::size_t>(y * im.w + x) * 3u;
            sr += im.rgb[o];
            sg += im.rgb[o + 1];
            sb += im.rgb[o + 2];
            ++n;
        }
    }
    if (n < 8)
        return true;
    const double mr = static_cast<double>(sr) / n, mg = static_cast<double>(sg) / n,
                 mb = static_cast<double>(sb) / n;
    double var = 0;
    for (int y = y0; y < std::max(y0 + 1, y1) && y < im.h; ++y) {
        for (int x = x0; x < std::max(x0 + 1, x1) && x < im.w; ++x) {
            const std::size_t o = static_cast<std::size_t>(y * im.w + x) * 3u;
            const double dr = im.rgb[o] - mr, dg = im.rgb[o + 1] - mg, db = im.rgb[o + 2] - mb;
            var += dr * dr + dg * dg + db * db;
        }
    }
    const double stdev = std::sqrt(var / static_cast<double>(n));
    return far > 0.82f && stdev < 22.0;
}

float horizon_luma_step(const RgbImage& im) {
    if (im.w < 16 || im.h < 16 ||
        im.rgb.size() < static_cast<std::size_t>(im.w * im.h * 3))
        return 0.f;
    const int x0 = im.w / 3, x1 = (im.w * 2) / 3;
    const int y0 = static_cast<int>(0.40f * im.h);
    const int y1 = std::max(y0 + 4, static_cast<int>(0.58f * im.h));
    float prev = 0.f;
    float max_step = 0.f;
    bool have = false;
    for (int y = y0; y < y1 && y < im.h; ++y) {
        double s = 0;
        int n = 0;
        for (int x = x0; x < x1 && x < im.w; ++x) {
            const std::size_t o = static_cast<std::size_t>(y * im.w + x) * 3u;
            s += 0.30 * im.rgb[o] + 0.59 * im.rgb[o + 1] + 0.11 * im.rgb[o + 2];
            ++n;
        }
        if (n < 4)
            continue;
        const float luma = static_cast<float>(s / n);
        if (have)
            max_step = std::max(max_step, std::fabs(luma - prev));
        prev = luma;
        have = true;
    }
    return max_step;
}

bool horizon_is_hard_edge(const RgbImage& im) {
    return horizon_luma_step(im) > 42.f;
}

float water_wash_luma(const RgbImage& im) {
    if (im.w < 16 || im.h < 16 ||
        im.rgb.size() < static_cast<std::size_t>(im.w * im.h * 3))
        return 0.f;
    const int y0 = static_cast<int>(0.72f * im.h);
    double s = 0, chroma = 0;
    int n = 0;
    for (int y = y0; y < im.h; ++y) {
        for (int x = im.w / 4; x < (im.w * 3) / 4; ++x) {
            const std::size_t o = static_cast<std::size_t>(y * im.w + x) * 3u;
            const int r = im.rgb[o], g = im.rgb[o + 1], b = im.rgb[o + 2];
            s += 0.30 * r + 0.59 * g + 0.11 * b;
            chroma += std::abs(b - r) + std::abs(b - g);
            ++n;
        }
    }
    (void)chroma;
    return n > 0 ? static_cast<float>(s / n) : 0.f;
}

bool water_is_washed_white(const RgbImage& im) {
    return water_wash_luma(im) > 198.f;
}

bool pixels_near_gradient(const RgbImage& im, int tol) {
    const auto expect = make_gradient_rgb(im.w, im.h);
    if (im.rgb.size() != expect.rgb.size())
        return false;
    for (std::size_t i = 0; i < im.rgb.size(); ++i) {
        if (std::abs(static_cast<int>(im.rgb[i]) - static_cast<int>(expect.rgb[i])) > tol)
            return false;
    }
    return true;
}

bool self_test_shot_files(const std::string& dir, std::string& err) {
    const std::string stem = dir.empty() ? "s376-shot-self" : (dir + "/s376-shot-self");
    const auto grad = make_gradient_rgb(64, 48);
    const auto solid = make_solid_rgb(16, 16, 40, 80, 160);
    std::string ppm, png;
    if (!write_shot_images(stem + ".ppm", grad, &ppm, &png)) {
        err = "write_shot_images failed";
        return false;
    }
    RgbImage back;
    if (!read_ppm(ppm, back) || !pixels_near_gradient(back, 0)) {
        err = "gradient PPM roundtrip failed";
        return false;
    }
    if (!write_ppm(stem + "-solid.ppm", solid) || !read_ppm(stem + "-solid.ppm", back) ||
        !pixels_near_solid(back, 40, 80, 160, 0)) {
        err = "solid PPM roundtrip failed";
        return false;
    }
    if (!write_png(stem + "-solid.png", solid)) {
        err = "write_png failed";
        return false;
    }
    err.clear();
    return true;
}

bool looks_like_video_path(const std::string& path) {
    const std::string e = ext_of(path);
    return e == ".mp4" || e == ".mov" || e == ".mkv" || e == ".webm" || e == ".avi";
}

bool ffmpeg_on_path() {
#if defined(_WIN32)
    return std::system("where ffmpeg >NUL 2>&1") == 0;
#else
    return std::system("command -v ffmpeg >/dev/null 2>&1") == 0;
#endif
}

std::string shell_single_quote(const std::string& s) {
    std::string o = "'";
    for (char c : s) {
        if (c == '\'')
            o += "'\\''";
        else
            o += c;
    }
    o += "'";
    return o;
}

std::string ffmpeg_rawvideo_cmd(int w, int h, double fps, const std::string& out_mp4) {
    std::ostringstream o;
    o << "ffmpeg -y -loglevel error -f rawvideo -pix_fmt rgb24 -s " << std::max(1, w) << "x"
      << std::max(1, h) << " -r " << fps << " -i - -an -c:v libx264 -preset veryfast -crf 20 "
      << "-pix_fmt yuv420p " << shell_single_quote(out_mp4);
    return o.str();
}

std::string ffmpeg_png_seq_cmd(const std::string& frames_dir, double fps, const std::string& out_mp4) {
    std::ostringstream o;
    const auto pat = (std::filesystem::path(frames_dir) / "frame-%06d.png").string();
    o << "ffmpeg -y -loglevel error -framerate " << fps << " -i " << shell_single_quote(pat)
      << " -c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p " << shell_single_quote(out_mp4);
    return o.str();
}

std::string record_frames_dir(const std::string& record_path) {
    if (record_path.empty())
        return "frames";
    if (!looks_like_video_path(record_path))
        return record_path;
    return stem_of(record_path) + "-frames";
}

std::string frame_png_path(const std::string& dir, int i) {
    char name[64];
    std::snprintf(name, sizeof(name), "frame-%06d.png", std::max(1, i));
    return (std::filesystem::path(dir) / name).string();
}

bool RecordWriter::open(const std::string& path, int w, int h, double fps, bool force_png) {
    close();
    w_ = std::max(1, w);
    h_ = std::max(1, h);
    fps_ = fps > 0.0 ? fps : 30.0;
    frames_ = 0;
    used_ffmpeg_ = false;
    frames_dir_.clear();
    video_path_.clear();
    mux_cmd_.clear();
    if (path.empty())
        return false;
    const bool video = looks_like_video_path(path);
    if (video)
        video_path_ = path;
    if (video && !force_png && ffmpeg_on_path()) {
        const std::string cmd = ffmpeg_rawvideo_cmd(w_, h_, fps_, path);
#if defined(_WIN32)
        pipe_ = _popen(cmd.c_str(), "wb");
#else
        pipe_ = popen(cmd.c_str(), "w");
#endif
        if (pipe_) {
            used_ffmpeg_ = true;
            return true;
        }
    }
    frames_dir_ = record_frames_dir(path);
    std::error_code ec;
    std::filesystem::create_directories(frames_dir_, ec);
    if (ec)
        return false;
    if (video)
        mux_cmd_ = ffmpeg_png_seq_cmd(frames_dir_, fps_, path);
    return true;
}

bool RecordWriter::write(const RgbImage& im) {
    if (im.w != w_ || im.h != h_ ||
        im.rgb.size() < static_cast<std::size_t>(w_) * static_cast<std::size_t>(h_) * 3)
        return false;
    const std::size_t n = static_cast<std::size_t>(w_) * static_cast<std::size_t>(h_) * 3;
    if (pipe_) {
        const auto wrote = std::fwrite(im.rgb.data(), 1, n, pipe_);
        if (wrote != n)
            return false;
        ++frames_;
        return true;
    }
    if (frames_dir_.empty())
        return false;
    const std::string png = frame_png_path(frames_dir_, frames_ + 1);
    if (!write_png(png, im))
        return false;
    ++frames_;
    return true;
}

bool RecordWriter::close() {
    int rc = 0;
    if (pipe_) {
#if defined(_WIN32)
        rc = _pclose(pipe_);
#else
        rc = pclose(pipe_);
#endif
        pipe_ = nullptr;
    }
    return rc == 0;
}

} // namespace s376
