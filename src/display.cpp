#include "s376/display.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>

namespace s376 {
namespace {

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
        ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        --b;
    return s.substr(a, b - a);
}

void identity_lut(std::array<std::uint8_t, 256>& l) {
    for (int i = 0; i < 256; ++i)
        l[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
}

void identity_warp(DisplayCorrection& c) {
    for (int r = 0; r < kWarpRows; ++r) {
        for (int col = 0; col < kWarpCols; ++col) {
            c.warp[static_cast<std::size_t>(r)][static_cast<std::size_t>(col)].src_h_deg =
                warp_h_deg(col);
            c.warp[static_cast<std::size_t>(r)][static_cast<std::size_t>(col)].src_v_deg =
                warp_v_deg(r);
        }
    }
}

float clampf(float x, float lo, float hi) {
    return std::max(lo, std::min(hi, x));
}

void sample_warp(const DisplayCorrection& c, float h, float v, float& sh, float& sv) {
    const float h0 = warp_h_deg(0);
    const float h1 = warp_h_deg(kWarpCols - 1);
    const float v0 = warp_v_deg(0);
    const float v1 = warp_v_deg(kWarpRows - 1);
    const float tx = clampf((h - h0) / (h1 - h0), 0.f, 1.f) * (kWarpCols - 1);
    const float ty = clampf((v - v0) / (v1 - v0), 0.f, 1.f) * (kWarpRows - 1);
    const int c0 = static_cast<int>(std::floor(tx));
    const int r0 = static_cast<int>(std::floor(ty));
    const int c1 = std::min(c0 + 1, kWarpCols - 1);
    const int r1 = std::min(r0 + 1, kWarpRows - 1);
    const float fx = tx - static_cast<float>(c0);
    const float fy = ty - static_cast<float>(r0);
    const auto& a = c.warp[static_cast<std::size_t>(r0)][static_cast<std::size_t>(c0)];
    const auto& b = c.warp[static_cast<std::size_t>(r0)][static_cast<std::size_t>(c1)];
    const auto& d = c.warp[static_cast<std::size_t>(r1)][static_cast<std::size_t>(c0)];
    const auto& e = c.warp[static_cast<std::size_t>(r1)][static_cast<std::size_t>(c1)];
    sh = a.src_h_deg * (1 - fx) * (1 - fy) + b.src_h_deg * fx * (1 - fy) +
         d.src_h_deg * (1 - fx) * fy + e.src_h_deg * fx * fy;
    sv = a.src_v_deg * (1 - fx) * (1 - fy) + b.src_v_deg * fx * (1 - fy) +
         d.src_v_deg * (1 - fx) * fy + e.src_v_deg * fx * fy;
}

void sample_rgb(const RgbImage& im, float u, float v, float& r, float& g, float& b) {
    if (im.w < 1 || im.h < 1 || im.rgb.empty()) {
        r = g = b = 0;
        return;
    }
    const float x = clampf(u, 0.f, 1.f) * static_cast<float>(im.w - 1);
    const float y = clampf(v, 0.f, 1.f) * static_cast<float>(im.h - 1);
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(x0 + 1, im.w - 1);
    const int y1 = std::min(y0 + 1, im.h - 1);
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);
    auto pix = [&](int px, int py, int c) -> float {
        return im.rgb[static_cast<std::size_t>((py * im.w + px) * 3 + c)] / 255.f;
    };
    r = pix(x0, y0, 0) * (1 - fx) * (1 - fy) + pix(x1, y0, 0) * fx * (1 - fy) +
        pix(x0, y1, 0) * (1 - fx) * fy + pix(x1, y1, 0) * fx * fy;
    g = pix(x0, y0, 1) * (1 - fx) * (1 - fy) + pix(x1, y0, 1) * fx * (1 - fy) +
        pix(x0, y1, 1) * (1 - fx) * fy + pix(x1, y1, 1) * fx * fy;
    b = pix(x0, y0, 2) * (1 - fx) * (1 - fy) + pix(x1, y0, 2) * fx * (1 - fy) +
        pix(x0, y1, 2) * (1 - fx) * fy + pix(x1, y1, 2) * fx * fy;
}

float mask_at(const DisplayCorrection& c, float u, float v) {
    if (c.mask.empty() || c.mask_w < 1 || c.mask_h < 1)
        return 1.f;
    const int x = std::max(0, std::min(c.mask_w - 1, static_cast<int>(u * (c.mask_w - 1) + 0.5f)));
    const int y = std::max(0, std::min(c.mask_h - 1, static_cast<int>(v * (c.mask_h - 1) + 0.5f)));
    return c.mask[static_cast<std::size_t>(y * c.mask_w + x)] / 255.f;
}

float edge_gain(const DisplayCorrection& c, float u, float v, int w, int h) {
    if (w < 1 || h < 1)
        return 1.f;
    const float x = u * static_cast<float>(w);
    const float y = v * static_cast<float>(h);
    float t = 0.f;
    if (c.blend.left_px > 0.f)
        t = std::max(t, clampf((c.blend.left_px - x) / c.blend.left_px, 0.f, 1.f));
    if (c.blend.right_px > 0.f)
        t = std::max(t, clampf((x - (static_cast<float>(w) - c.blend.right_px)) / c.blend.right_px,
                               0.f, 1.f));
    if (c.blend.top_px > 0.f)
        t = std::max(t, clampf((c.blend.top_px - y) / c.blend.top_px, 0.f, 1.f));
    if (c.blend.bottom_px > 0.f)
        t = std::max(t, clampf((y - (static_cast<float>(h) - c.blend.bottom_px)) / c.blend.bottom_px,
                               0.f, 1.f));
    // ramp is the darken amount at the edge (0 = leave full bright).
    // Lower ramp → brighter remaining edge (hangar survey).
    return 1.f - t * clampf(c.blend.ramp, 0.f, 1.f);
}

} // namespace

float warp_h_deg(int col) {
    col = std::max(0, std::min(kWarpCols - 1, col));
    return -30.f + 10.f * static_cast<float>(col);
}

float warp_v_deg(int row) {
    static const float kV[kWarpRows] = {-16.25f, -10.f, 0.f, 10.f, 16.25f};
    row = std::max(0, std::min(kWarpRows - 1, row));
    return kV[row];
}

DisplayCorrection identity_correction() {
    DisplayCorrection c;
    identity_warp(c);
    identity_lut(c.lut_r);
    identity_lut(c.lut_g);
    identity_lut(c.lut_b);
    return c;
}

bool warp_is_identity(const DisplayCorrection& c) {
    for (int r = 0; r < kWarpRows; ++r) {
        for (int col = 0; col < kWarpCols; ++col) {
            const auto& n = c.warp[static_cast<std::size_t>(r)][static_cast<std::size_t>(col)];
            if (std::fabs(n.src_h_deg - warp_h_deg(col)) > 1e-3f ||
                std::fabs(n.src_v_deg - warp_v_deg(r)) > 1e-3f)
                return false;
        }
    }
    return true;
}

bool lut_is_identity(const DisplayCorrection& c) {
    for (int i = 0; i < 256; ++i) {
        const auto u = static_cast<std::uint8_t>(i);
        if (c.lut_r[static_cast<std::size_t>(i)] != u ||
            c.lut_g[static_cast<std::size_t>(i)] != u ||
            c.lut_b[static_cast<std::size_t>(i)] != u)
            return false;
    }
    return true;
}

bool correction_is_identity(const DisplayCorrection& c) {
    if (!warp_is_identity(c) || !lut_is_identity(c))
        return false;
    if (std::fabs(c.gamma - 1.f) > 1e-4f)
        return false;
    if (c.blend.left_px > 0.5f || c.blend.right_px > 0.5f || c.blend.top_px > 0.5f ||
        c.blend.bottom_px > 0.5f)
        return false;
    if (!c.mask.empty())
        return false;
    return true;
}

bool needs_gpu_pass(const DisplayCorrection& c) {
    return c.pattern != DisplayPattern::None || !correction_is_identity(c);
}

DisplayPattern parse_display_pattern(const std::string& s) {
    if (s == "grid5" || s == "grid" || s == "5deg")
        return DisplayPattern::Grid5;
    if (s == "white70" || s == "white")
        return DisplayPattern::White70;
    if (s == "black")
        return DisplayPattern::Black;
    return DisplayPattern::None;
}

const char* display_pattern_name(DisplayPattern p) {
    switch (p) {
    case DisplayPattern::Grid5:
        return "grid5";
    case DisplayPattern::White70:
        return "white70";
    case DisplayPattern::Black:
        return "black";
    case DisplayPattern::None:
        break;
    }
    return "none";
}

TodBucket tod_bucket(float tod_hours, int tod_select) {
    if (tod_select >= 0) {
        switch (tod_select) {
        case 0:
            return TodBucket::Day;
        case 1:
            return TodBucket::Dusk;
        case 2:
            return TodBucket::Dawn;
        case 3:
            return TodBucket::Night;
        default:
            break;
        }
    }
    float h = tod_hours;
    if (!std::isfinite(h))
        h = 12.f;
    while (h < 0.f)
        h += 24.f;
    while (h >= 24.f)
        h -= 24.f;
    if (h >= 8.f && h < 16.f)
        return TodBucket::Day;
    if (h >= 5.f && h < 8.f)
        return TodBucket::Dawn;
    if (h >= 16.f && h < 20.f)
        return TodBucket::Dusk;
    return TodBucket::Night;
}

float wrap_tod_hours(float h) {
    if (!std::isfinite(h))
        h = 12.f;
    while (h < 0.f)
        h += 24.f;
    while (h >= 24.f)
        h -= 24.f;
    return h;
}

float effective_tod_hours(float tod_hours, int tod_select) {
    if (tod_select >= 0) {
        switch (tod_select) {
        case 0:
            return 14.5f;
        case 1:
            return 19.0f;
        case 2:
            return 6.0f;
        case 3:
            return 0.5f;
        default:
            break;
        }
    }
    return wrap_tod_hours(tod_hours);
}

float visual_sun_elev_deg(float tod_hours, int tod_select) {
    if (tod_select >= 0) {
        switch (tod_select) {
        case 0:
            return 52.f;
        case 1:
            return -1.5f;
        case 2:
            return 2.f;
        case 3:
            return -55.f;
        default:
            break;
        }
    }
    const float h = wrap_tod_hours(tod_hours);
    if (h >= 8.f && h < 16.f) {
        const float t = (h - 8.f) / 8.f;
        const float mid = 1.f - std::fabs(t - 0.5f) * 2.f;
        return 18.f + 37.f * mid;
    }
    if (h >= 5.f && h < 8.f)
        return -8.f + 26.f * (h - 5.f) / 3.f;
    if (h >= 16.f && h < 20.f)
        return 18.f - 26.f * (h - 16.f) / 4.f;
    return -55.f;
}

float visual_sun_az_deg(float tod_hours, int tod_select, double lat_deg) {
    const float h = effective_tod_hours(tod_hours, tod_select);
    float az = 180.f + (h - 12.f) * 15.f;
    if (lat_deg < 0.0)
        az += 180.f;
    while (az < 0.f)
        az += 360.f;
    while (az >= 360.f)
        az -= 360.f;
    return az;
}

float runway_wetness(int runway_condition) {
    if (runway_condition <= 0)
        return 0.f;
    if (runway_condition == 1)
        return 0.35f;
    if (runway_condition == 2)
        return 0.75f;
    return 1.f;
}

const ColorSet& color_for_tod(const DisplayCorrection& c, TodBucket b) {
    switch (b) {
    case TodBucket::Dawn:
        return c.dawn;
    case TodBucket::Dusk:
        return c.dusk;
    case TodBucket::Night:
        return c.night;
    case TodBucket::Day:
        break;
    }
    return c.day;
}

RgbImage make_pattern_rgb(int w, int h, DisplayPattern p, float hfov_deg, float vfov_deg) {
    RgbImage im;
    im.w = std::max(1, w);
    im.h = std::max(1, h);
    im.rgb.assign(static_cast<std::size_t>(im.w * im.h * 3), 0);
    if (p == DisplayPattern::Black)
        return im;
    if (p == DisplayPattern::White70) {
        const std::uint8_t v = 178; // 70% of 255
        for (std::size_t i = 0; i < im.rgb.size(); ++i)
            im.rgb[i] = v;
        return im;
    }
    // 5 deg spherical grid on a dark field.
    const float hf = hfov_deg > 1.f ? hfov_deg : 50.f;
    const float vf = vfov_deg > 1.f ? vfov_deg : 32.5f;
    for (int y = 0; y < im.h; ++y) {
        const float v = ((y + 0.5f) / static_cast<float>(im.h) - 0.5f) * vf;
        for (int x = 0; x < im.w; ++x) {
            const float hdg = ((x + 0.5f) / static_cast<float>(im.w) - 0.5f) * hf;
            const float mh = std::fmod(std::fabs(hdg) + 2.5f, 5.f) - 2.5f;
            const float mv = std::fmod(std::fabs(v) + 2.5f, 5.f) - 2.5f;
            const float px = hf / static_cast<float>(im.w);
            const float py = vf / static_cast<float>(im.h);
            const bool line = std::fabs(mh) < px * 1.2f || std::fabs(mv) < py * 1.2f;
            const int i = (y * im.w + x) * 3;
            if (line) {
                im.rgb[static_cast<std::size_t>(i)] = 220;
                im.rgb[static_cast<std::size_t>(i + 1)] = 220;
                im.rgb[static_cast<std::size_t>(i + 2)] = 220;
            } else {
                im.rgb[static_cast<std::size_t>(i)] = 12;
                im.rgb[static_cast<std::size_t>(i + 1)] = 12;
                im.rgb[static_cast<std::size_t>(i + 2)] = 16;
            }
        }
    }
    return im;
}

RgbImage apply_correction(const RgbImage& src, const DisplayCorrection& c, TodBucket bucket) {
    RgbImage in = src;
    if (c.pattern != DisplayPattern::None)
        in = make_pattern_rgb(src.w > 0 ? src.w : 64, src.h > 0 ? src.h : 64, c.pattern, c.hfov_deg,
                              c.vfov_deg);
    if (in.w < 1 || in.h < 1)
        return in;

    RgbImage masked = in;
    if (!c.mask.empty() && c.mask_w > 0 && c.mask_h > 0) {
        for (int y = 0; y < in.h; ++y) {
            for (int x = 0; x < in.w; ++x) {
                const float u = (x + 0.5f) / static_cast<float>(in.w);
                const float v = (y + 0.5f) / static_cast<float>(in.h);
                const float m = mask_at(c, u, v);
                const int i = (y * in.w + x) * 3;
                masked.rgb[static_cast<std::size_t>(i)] =
                    static_cast<std::uint8_t>(masked.rgb[static_cast<std::size_t>(i)] * m);
                masked.rgb[static_cast<std::size_t>(i + 1)] =
                    static_cast<std::uint8_t>(masked.rgb[static_cast<std::size_t>(i + 1)] * m);
                masked.rgb[static_cast<std::size_t>(i + 2)] =
                    static_cast<std::uint8_t>(masked.rgb[static_cast<std::size_t>(i + 2)] * m);
            }
        }
    }

    RgbImage out;
    out.w = in.w;
    out.h = in.h;
    out.rgb.resize(static_cast<std::size_t>(out.w * out.h * 3));
    const float hf = c.hfov_deg > 1.f ? c.hfov_deg : 50.f;
    const float vf = c.vfov_deg > 1.f ? c.vfov_deg : 32.5f;
    const bool ident_warp = warp_is_identity(c);
    const ColorSet& tint = color_for_tod(c, bucket);
    const float gpow = (c.gamma > 1e-4f) ? c.gamma : 1.f;
    for (int y = 0; y < out.h; ++y) {
        const float v = (y + 0.5f) / static_cast<float>(out.h);
        const float ang_v = (v - 0.5f) * vf;
        for (int x = 0; x < out.w; ++x) {
            const float u = (x + 0.5f) / static_cast<float>(out.w);
            const float ang_h = (u - 0.5f) * hf;
            float r = 0, g = 0, b = 0;
            if (ident_warp) {
                sample_rgb(masked, u, v, r, g, b);
            } else {
                float sh = ang_h, sv = ang_v;
                sample_warp(c, ang_h, ang_v, sh, sv);
                const float su = sh / hf + 0.5f;
                const float svu = sv / vf + 0.5f;
                sample_rgb(masked, su, svu, r, g, b);
            }
            const float gain = edge_gain(c, u, v, out.w, out.h);
            r = clampf(r * gain * (1.f - tint.mix + tint.r * tint.mix), 0.f, 1.f);
            g = clampf(g * gain * (1.f - tint.mix + tint.g * tint.mix), 0.f, 1.f);
            b = clampf(b * gain * (1.f - tint.mix + tint.b * tint.mix), 0.f, 1.f);
            if (std::fabs(gpow - 1.f) > 1e-4f) {
                r = std::pow(r, gpow);
                g = std::pow(g, gpow);
                b = std::pow(b, gpow);
            }
            auto enc = [](float x) -> std::uint8_t {
                return static_cast<std::uint8_t>(clampf(x, 0.f, 1.f) * 255.f + 0.5f);
            };
            const int i = (y * out.w + x) * 3;
            out.rgb[static_cast<std::size_t>(i)] = c.lut_r[enc(r)];
            out.rgb[static_cast<std::size_t>(i + 1)] = c.lut_g[enc(g)];
            out.rgb[static_cast<std::size_t>(i + 2)] = c.lut_b[enc(b)];
        }
    }
    return out;
}

bool load_correction(const std::string& path, DisplayCorrection& out, std::string& err) {
    std::ifstream in(path);
    if (!in) {
        err = "correction file not found: " + path;
        return false;
    }
    out = identity_correction();
    std::string line;
    int lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        auto hash = line.find('#');
        if (hash != std::string::npos)
            line = line.substr(0, hash);
        line = trim(line);
        if (line.empty() || line.front() == '[')
            continue;
        auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string key = trim(line.substr(0, eq));
        const std::string val = trim(line.substr(eq + 1));
        auto num = [&](float& d) {
            try {
                d = std::stof(val);
            } catch (...) {
            }
        };
        if (key == "blend.ramp")
            num(out.blend.ramp);
        else if (key == "blend.left_px")
            num(out.blend.left_px);
        else if (key == "blend.right_px")
            num(out.blend.right_px);
        else if (key == "blend.top_px")
            num(out.blend.top_px);
        else if (key == "blend.bottom_px")
            num(out.blend.bottom_px);
        else if (key == "gamma")
            num(out.gamma);
        else if (key == "hfov_deg")
            num(out.hfov_deg);
        else if (key == "vfov_deg")
            num(out.vfov_deg);
        else if (key == "pattern") {
            std::string p = val;
            if (p.size() >= 2 && ((p.front() == '"' && p.back() == '"') ||
                                  (p.front() == '\'' && p.back() == '\'')))
                p = p.substr(1, p.size() - 2);
            out.pattern = parse_display_pattern(p);
        }
        else if (key.rfind("warp.", 0) == 0) {
            // warp.r.c = src_h,src_v
            int r = -1, col = -1;
            if (std::sscanf(key.c_str(), "warp.%d.%d", &r, &col) == 2 && r >= 0 && r < kWarpRows &&
                col >= 0 && col < kWarpCols) {
                float h = warp_h_deg(col), v = warp_v_deg(r);
                std::sscanf(val.c_str(), "%f , %f", &h, &v);
                out.warp[static_cast<std::size_t>(r)][static_cast<std::size_t>(col)].src_h_deg = h;
                out.warp[static_cast<std::size_t>(r)][static_cast<std::size_t>(col)].src_v_deg = v;
            }
        }
    }
    (void)lineno;
    return true;
}

std::string dump_correction(const DisplayCorrection& c) {
    std::ostringstream o;
    o << "hfov_deg = " << c.hfov_deg << "\n";
    o << "vfov_deg = " << c.vfov_deg << "\n";
    o << "gamma = " << c.gamma << "\n";
    o << "pattern = \"" << display_pattern_name(c.pattern) << "\"\n\n";
    o << "blend.ramp = " << c.blend.ramp << "\n";
    o << "blend.left_px = " << c.blend.left_px << "\n";
    o << "blend.right_px = " << c.blend.right_px << "\n";
    o << "blend.top_px = " << c.blend.top_px << "\n";
    o << "blend.bottom_px = " << c.blend.bottom_px << "\n";
    if (!warp_is_identity(c)) {
        o << "\n";
        for (int r = 0; r < kWarpRows; ++r) {
            for (int col = 0; col < kWarpCols; ++col) {
                const auto& n = c.warp[static_cast<std::size_t>(r)][static_cast<std::size_t>(col)];
                o << "warp." << r << "." << col << " = " << n.src_h_deg << ", " << n.src_v_deg
                  << "\n";
            }
        }
    }
    return o.str();
}

bool save_correction(const std::string& path, const DisplayCorrection& c, std::string& err) {
    std::ofstream out(path);
    if (!out) {
        err = "cannot write correction: " + path;
        return false;
    }
    out << "# Per-channel display correction (s376-wizard).\n";
    out << dump_correction(c);
    return true;
}

} // namespace s376
