#include "s376/dxt.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace s376::dxt {
namespace {

std::uint16_t pack565(int r, int g, int b) {
    return static_cast<std::uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

void unpack565(std::uint16_t c, int& r, int& g, int& b) {
    r = ((c >> 11) & 31) * 255 / 31;
    g = ((c >> 5) & 63) * 255 / 63;
    b = (c & 31) * 255 / 31;
}

void encode_block(const std::uint8_t px[16 * 3], std::uint8_t out[8]) {
    int min_l = 1e9, max_l = -1;
    int min_i = 0, max_i = 0;
    for (int i = 0; i < 16; ++i) {
        const int l = px[i * 3] + 2 * px[i * 3 + 1] + px[i * 3 + 2];
        if (l < min_l) {
            min_l = l;
            min_i = i;
        }
        if (l > max_l) {
            max_l = l;
            max_i = i;
        }
    }
    std::uint16_t c0 = pack565(px[max_i * 3], px[max_i * 3 + 1], px[max_i * 3 + 2]);
    std::uint16_t c1 = pack565(px[min_i * 3], px[min_i * 3 + 1], px[min_i * 3 + 2]);
    if (c0 == c1)
        c0 = static_cast<std::uint16_t>(c0 ^ 0x0020); // force 4-color mode
    if (c0 < c1)
        std::swap(c0, c1);
    int r[4], g[4], b[4];
    unpack565(c0, r[0], g[0], b[0]);
    unpack565(c1, r[1], g[1], b[1]);
    r[2] = (2 * r[0] + r[1]) / 3;
    g[2] = (2 * g[0] + g[1]) / 3;
    b[2] = (2 * b[0] + b[1]) / 3;
    r[3] = (r[0] + 2 * r[1]) / 3;
    g[3] = (g[0] + 2 * g[1]) / 3;
    b[3] = (b[0] + 2 * b[1]) / 3;

    std::uint32_t idx = 0;
    for (int i = 0; i < 16; ++i) {
        int best = 0, best_d = 1e9;
        for (int k = 0; k < 4; ++k) {
            const int dr = px[i * 3] - r[k];
            const int dg = px[i * 3 + 1] - g[k];
            const int db = px[i * 3 + 2] - b[k];
            const int d = dr * dr + dg * dg + db * db;
            if (d < best_d) {
                best_d = d;
                best = k;
            }
        }
        idx |= static_cast<std::uint32_t>(best) << (2 * i);
    }
    out[0] = static_cast<std::uint8_t>(c0 & 0xFF);
    out[1] = static_cast<std::uint8_t>(c0 >> 8);
    out[2] = static_cast<std::uint8_t>(c1 & 0xFF);
    out[3] = static_cast<std::uint8_t>(c1 >> 8);
    out[4] = static_cast<std::uint8_t>(idx & 0xFF);
    out[5] = static_cast<std::uint8_t>((idx >> 8) & 0xFF);
    out[6] = static_cast<std::uint8_t>((idx >> 16) & 0xFF);
    out[7] = static_cast<std::uint8_t>((idx >> 24) & 0xFF);
}

} // namespace

int gpu_tex_dim(int n) {
    if (n <= 4)
        return 4;
    int pot = 4;
    while (pot < n)
        pot *= 2;
    // Snap to POT when the pad is ≤ 5% (2011 / 2033 → 2048).
    if (pot <= 4096 && (pot - n) * 20 <= n)
        return pot;
    return (n + 3) & ~3;
}

void resize_rgb(const std::uint8_t* src, int sw, int sh, std::uint8_t* dst, int dw, int dh) {
    if (!src || !dst || sw < 1 || sh < 1 || dw < 1 || dh < 1)
        return;
    for (int y = 0; y < dh; ++y) {
        const int sy = std::min(sh - 1, (y * sh) / dh);
        for (int x = 0; x < dw; ++x) {
            const int sx = std::min(sw - 1, (x * sw) / dw);
            const int si = (sy * sw + sx) * 3;
            const int di = (y * dw + x) * 3;
            dst[di] = src[si];
            dst[di + 1] = src[si + 1];
            dst[di + 2] = src[si + 2];
        }
    }
}

bool pad_rgb_for_gpu(std::vector<std::uint8_t>& rgb, int& w, int& h) {
    const int nw = gpu_tex_dim(w);
    const int nh = gpu_tex_dim(h);
    if (nw == w && nh == h)
        return false;
    std::vector<std::uint8_t> out(static_cast<std::size_t>(nw * nh * 3), 0);
    resize_rgb(rgb.data(), w, h, out.data(), nw, nh);
    rgb.swap(out);
    w = nw;
    h = nh;
    return true;
}

std::size_t bc1_size(int w, int h) {
    const int bw = (w + 3) / 4;
    const int bh = (h + 3) / 4;
    return static_cast<std::size_t>(bw * bh * 8);
}

std::vector<std::uint8_t> encode_bc1(const std::uint8_t* rgb, int w, int h) {
    const int bw = (w + 3) / 4;
    const int bh = (h + 3) / 4;
    std::vector<std::uint8_t> out(static_cast<std::size_t>(bw * bh * 8));
    std::uint8_t block[16 * 3];
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            for (int y = 0; y < 4; ++y) {
                const int iy = std::min(h - 1, by * 4 + y);
                for (int x = 0; x < 4; ++x) {
                    const int ix = std::min(w - 1, bx * 4 + x);
                    const int si = (iy * w + ix) * 3;
                    const int di = (y * 4 + x) * 3;
                    block[di] = rgb[si];
                    block[di + 1] = rgb[si + 1];
                    block[di + 2] = rgb[si + 2];
                }
            }
            encode_block(block, out.data() + (by * bw + bx) * 8);
        }
    }
    return out;
}

std::vector<std::uint8_t> decode_bc1(const std::uint8_t* blocks, int w, int h) {
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(w * h * 3));
    const int bw = (w + 3) / 4;
    const int bh = (h + 3) / 4;
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            const std::uint8_t* b = blocks + (by * bw + bx) * 8;
            const std::uint16_t c0 = static_cast<std::uint16_t>(b[0] | (b[1] << 8));
            const std::uint16_t c1 = static_cast<std::uint16_t>(b[2] | (b[3] << 8));
            int r[4], g[4], bl[4];
            unpack565(c0, r[0], g[0], bl[0]);
            unpack565(c1, r[1], g[1], bl[1]);
            if (c0 > c1) {
                r[2] = (2 * r[0] + r[1]) / 3;
                g[2] = (2 * g[0] + g[1]) / 3;
                bl[2] = (2 * bl[0] + bl[1]) / 3;
                r[3] = (r[0] + 2 * r[1]) / 3;
                g[3] = (g[0] + 2 * g[1]) / 3;
                bl[3] = (bl[0] + 2 * bl[1]) / 3;
            } else {
                r[2] = (r[0] + r[1]) / 2;
                g[2] = (g[0] + g[1]) / 2;
                bl[2] = (bl[0] + bl[1]) / 2;
                r[3] = g[3] = bl[3] = 0;
            }
            std::uint32_t idx = b[4] | (b[5] << 8) | (b[6] << 16) | (static_cast<std::uint32_t>(b[7]) << 24);
            for (int y = 0; y < 4; ++y) {
                const int iy = by * 4 + y;
                if (iy >= h)
                    continue;
                for (int x = 0; x < 4; ++x) {
                    const int ix = bx * 4 + x;
                    if (ix >= w)
                        continue;
                    const int k = static_cast<int>((idx >> (2 * (y * 4 + x))) & 3);
                    const int o = (iy * w + ix) * 3;
                    rgb[o] = static_cast<std::uint8_t>(r[k]);
                    rgb[o + 1] = static_cast<std::uint8_t>(g[k]);
                    rgb[o + 2] = static_cast<std::uint8_t>(bl[k]);
                }
            }
        }
    }
    return rgb;
}

std::size_t bc7_size(int w, int h) {
    const int bw = (w + 3) / 4;
    const int bh = (h + 3) / 4;
    return static_cast<std::size_t>(bw * bh * 16);
}

int mip_count(int w, int h) {
    int n = 1;
    while (w > 1 || h > 1) {
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
        ++n;
    }
    return n;
}

std::vector<std::uint8_t> downsample2(const std::uint8_t* rgb, int w, int h, int& ow, int& oh) {
    ow = std::max(1, w / 2);
    oh = std::max(1, h / 2);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(ow * oh * 3));
    for (int y = 0; y < oh; ++y) {
        for (int x = 0; x < ow; ++x) {
            int r = 0, g = 0, b = 0, n = 0;
            for (int dy = 0; dy < 2; ++dy) {
                const int iy = std::min(h - 1, y * 2 + dy);
                for (int dx = 0; dx < 2; ++dx) {
                    const int ix = std::min(w - 1, x * 2 + dx);
                    const int i = (iy * w + ix) * 3;
                    r += rgb[i];
                    g += rgb[i + 1];
                    b += rgb[i + 2];
                    ++n;
                }
            }
            const int o = (y * ow + x) * 3;
            out[o] = static_cast<std::uint8_t>(r / n);
            out[o + 1] = static_cast<std::uint8_t>(g / n);
            out[o + 2] = static_cast<std::uint8_t>(b / n);
        }
    }
    return out;
}

void encode_bc7_block(const std::uint8_t px[16 * 3], std::uint8_t out[16]) {
    int min_l = 1e9, max_l = -1, min_i = 0, max_i = 0;
    for (int i = 0; i < 16; ++i) {
        const int l = px[i * 3] + 2 * px[i * 3 + 1] + px[i * 3 + 2];
        if (l < min_l) {
            min_l = l;
            min_i = i;
        }
        if (l > max_l) {
            max_l = l;
            max_i = i;
        }
    }
    const int r0 = px[max_i * 3], g0 = px[max_i * 3 + 1], b0 = px[max_i * 3 + 2];
    const int r1 = px[min_i * 3], g1 = px[min_i * 3 + 1], b1 = px[min_i * 3 + 2];
    auto q7 = [](int v) { return std::min(127, (v * 127 + 127) / 255); };
    const int R0 = q7(r0), G0 = q7(g0), B0 = q7(b0);
    const int R1 = q7(r1), G1 = q7(g1), B1 = q7(b1);
    int pal_r[16], pal_g[16], pal_b[16];
    for (int i = 0; i < 16; ++i) {
        pal_r[i] = (R0 * (15 - i) + R1 * i) * 255 / (15 * 127);
        pal_g[i] = (G0 * (15 - i) + G1 * i) * 255 / (15 * 127);
        pal_b[i] = (B0 * (15 - i) + B1 * i) * 255 / (15 * 127);
    }
    int idx[16];
    for (int i = 0; i < 16; ++i) {
        int best = 0, best_d = 1e9;
        for (int k = 0; k < 16; ++k) {
            const int dr = px[i * 3] - pal_r[k];
            const int dg = px[i * 3 + 1] - pal_g[k];
            const int db = px[i * 3 + 2] - pal_b[k];
            const int d = dr * dr + dg * dg + db * db;
            if (d < best_d) {
                best_d = d;
                best = k;
            }
        }
        idx[i] = best;
    }
    std::uint64_t lo = 0, hi = 0;
    auto put = [&](int bit, int nbits, std::uint64_t v) {
        for (int i = 0; i < nbits; ++i) {
            const int b = bit + i;
            const std::uint64_t one = (v >> i) & 1;
            if (b < 64)
                lo |= one << b;
            else
                hi |= one << (b - 64);
        }
    };
    put(0, 7, 64);
    put(7, 7, R0);
    put(14, 7, R1);
    put(21, 7, G0);
    put(28, 7, G1);
    put(35, 7, B0);
    put(42, 7, B1);
    put(49, 7, 127);
    put(56, 7, 127);
    put(63, 1, 1);
    put(64, 1, 1);
    int bit = 65;
    put(bit, 3, idx[0] & 7);
    bit += 3;
    for (int i = 1; i < 16; ++i) {
        put(bit, 4, idx[i]);
        bit += 4;
    }
    for (int i = 0; i < 8; ++i)
        out[i] = static_cast<std::uint8_t>(lo >> (8 * i));
    for (int i = 0; i < 8; ++i)
        out[8 + i] = static_cast<std::uint8_t>(hi >> (8 * i));
}

void decode_bc7_block(const std::uint8_t in[16], std::uint8_t px[16 * 3]) {
    std::uint64_t lo = 0, hi = 0;
    for (int i = 0; i < 8; ++i)
        lo |= static_cast<std::uint64_t>(in[i]) << (8 * i);
    for (int i = 0; i < 8; ++i)
        hi |= static_cast<std::uint64_t>(in[8 + i]) << (8 * i);
    auto get = [&](int bit, int nbits) -> int {
        int v = 0;
        for (int i = 0; i < nbits; ++i) {
            const int b = bit + i;
            const std::uint64_t one = (b < 64) ? ((lo >> b) & 1) : ((hi >> (b - 64)) & 1);
            v |= static_cast<int>(one) << i;
        }
        return v;
    };
    const int R0 = get(7, 7), R1 = get(14, 7);
    const int G0 = get(21, 7), G1 = get(28, 7);
    const int B0 = get(35, 7), B1 = get(42, 7);
    int idx[16];
    idx[0] = get(65, 3);
    int bit = 68;
    for (int i = 1; i < 16; ++i) {
        idx[i] = get(bit, 4);
        bit += 4;
    }
    for (int i = 0; i < 16; ++i) {
        const int k = std::max(0, std::min(15, idx[i]));
        px[i * 3] = static_cast<std::uint8_t>((R0 * (15 - k) + R1 * k) * 255 / (15 * 127));
        px[i * 3 + 1] = static_cast<std::uint8_t>((G0 * (15 - k) + G1 * k) * 255 / (15 * 127));
        px[i * 3 + 2] = static_cast<std::uint8_t>((B0 * (15 - k) + B1 * k) * 255 / (15 * 127));
    }
}

std::vector<std::uint8_t> encode_bc7(const std::uint8_t* rgb, int w, int h) {
    const int bw = (w + 3) / 4;
    const int bh = (h + 3) / 4;
    std::vector<std::uint8_t> out(static_cast<std::size_t>(bw * bh * 16));
    std::uint8_t block[16 * 3];
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            for (int y = 0; y < 4; ++y) {
                const int iy = std::min(h - 1, by * 4 + y);
                for (int x = 0; x < 4; ++x) {
                    const int ix = std::min(w - 1, bx * 4 + x);
                    const int si = (iy * w + ix) * 3;
                    const int di = (y * 4 + x) * 3;
                    block[di] = rgb[si];
                    block[di + 1] = rgb[si + 1];
                    block[di + 2] = rgb[si + 2];
                }
            }
            encode_bc7_block(block, out.data() + (by * bw + bx) * 16);
        }
    }
    return out;
}

namespace {

void write_dds_header(std::ostream& o, int w, int h, int mips, bool bc7) {
    std::uint8_t hdr[128] = {};
    hdr[0] = 'D';
    hdr[1] = 'D';
    hdr[2] = 'S';
    hdr[3] = ' ';
    auto put32 = [&](int off, std::uint32_t v) {
        hdr[off] = static_cast<std::uint8_t>(v);
        hdr[off + 1] = static_cast<std::uint8_t>(v >> 8);
        hdr[off + 2] = static_cast<std::uint8_t>(v >> 16);
        hdr[off + 3] = static_cast<std::uint8_t>(v >> 24);
    };
    std::uint32_t flags = 0x00001007;
    if (mips > 1)
        flags |= 0x00020000;
    put32(4, 124);
    put32(8, flags);
    put32(12, static_cast<std::uint32_t>(h));
    put32(16, static_cast<std::uint32_t>(w));
    const int block = bc7 ? 16 : 8;
    put32(20, static_cast<std::uint32_t>(std::max(1, (w + 3) / 4) * block));
    put32(28, static_cast<std::uint32_t>(mips));
    put32(76, 32);
    put32(80, 0x4);
    if (bc7) {
        hdr[84] = 'D';
        hdr[85] = 'X';
        hdr[86] = '1';
        hdr[87] = '0';
    } else {
        hdr[84] = 'D';
        hdr[85] = 'X';
        hdr[86] = 'T';
        hdr[87] = '1';
    }
    std::uint32_t caps = 0x1000;
    if (mips > 1)
        caps |= 0x400008;
    put32(108, caps);
    o.write(reinterpret_cast<const char*>(hdr), 128);
    if (bc7) {
        std::uint8_t dx10[20] = {};
        auto p = [&](int off, std::uint32_t v) {
            dx10[off] = static_cast<std::uint8_t>(v);
            dx10[off + 1] = static_cast<std::uint8_t>(v >> 8);
            dx10[off + 2] = static_cast<std::uint8_t>(v >> 16);
            dx10[off + 3] = static_cast<std::uint8_t>(v >> 24);
        };
        p(0, 98);
        p(4, 3);
        p(12, 1);
        o.write(reinterpret_cast<const char*>(dx10), 20);
    }
}

void write_mip_chain(std::ostream& o, const std::uint8_t* rgb, int w, int h, bool mips, bool bc7) {
    std::vector<std::uint8_t> cur(rgb, rgb + static_cast<std::size_t>(w * h * 3));
    int cw = w, ch = h;
    const int n = mips ? mip_count(w, h) : 1;
    for (int i = 0; i < n; ++i) {
        auto blocks = bc7 ? encode_bc7(cur.data(), cw, ch) : encode_bc1(cur.data(), cw, ch);
        o.write(reinterpret_cast<const char*>(blocks.data()),
                static_cast<std::streamsize>(blocks.size()));
        if (i + 1 < n) {
            int nw = 0, nh = 0;
            cur = downsample2(cur.data(), cw, ch, nw, nh);
            cw = nw;
            ch = nh;
        }
    }
}

} // namespace

namespace {

void write_dds_padded(const std::string& path, const std::uint8_t* rgb, int w, int h, bool mips,
                      bool bc7) {
    if (!rgb || w < 1 || h < 1)
        throw std::runtime_error("cannot write empty DDS " + path);
    std::vector<std::uint8_t> padded(rgb, rgb + static_cast<std::size_t>(w * h * 3));
    int pw = w, ph = h;
    pad_rgb_for_gpu(padded, pw, ph);
    const std::string tmp = path + ".tmp";
    {
        std::ofstream o(tmp, std::ios::binary);
        if (!o)
            throw std::runtime_error("cannot write " + tmp);
        write_dds_header(o, pw, ph, mips ? mip_count(pw, ph) : 1, bc7);
        write_mip_chain(o, padded.data(), pw, ph, mips, bc7);
        o.flush();
        if (!o)
            throw std::runtime_error("failed writing " + tmp);
    }
    std::filesystem::rename(tmp, path);
}

} // namespace

void write_dds_bc1(const std::string& path, const std::uint8_t* rgb, int w, int h, bool mips) {
    write_dds_padded(path, rgb, w, h, mips, false);
}

void write_dds_bc7(const std::string& path, const std::uint8_t* rgb, int w, int h, bool mips) {
    write_dds_padded(path, rgb, w, h, mips, true);
}

std::vector<std::uint8_t> decode_bc7(const std::uint8_t* blocks, int w, int h) {
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(w * h * 3));
    const int bw = (w + 3) / 4;
    const int bh = (h + 3) / 4;
    std::uint8_t px[16 * 3];
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            decode_bc7_block(blocks + (by * bw + bx) * 16, px);
            for (int y = 0; y < 4; ++y) {
                const int iy = by * 4 + y;
                if (iy >= h)
                    continue;
                for (int x = 0; x < 4; ++x) {
                    const int ix = bx * 4 + x;
                    if (ix >= w)
                        continue;
                    const int di = (iy * w + ix) * 3;
                    const int si = (y * 4 + x) * 3;
                    rgb[di] = px[si];
                    rgb[di + 1] = px[si + 1];
                    rgb[di + 2] = px[si + 2];
                }
            }
        }
    }
    return rgb;
}

std::vector<std::uint8_t> transcode_bc7_to_bc1(const std::uint8_t* blocks, int w, int h) {
    const int bw = (w + 3) / 4;
    const int bh = (h + 3) / 4;
    std::vector<std::uint8_t> out(static_cast<std::size_t>(bw * bh * 8));
    std::uint8_t px[16 * 3];
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            decode_bc7_block(blocks + (by * bw + bx) * 16, px);
            encode_block(px, out.data() + (by * bw + bx) * 8);
        }
    }
    return out;
}

void ensure_bc1_mips(CompressedImage& img) {
    if (img.fmt != BlockFmt::Bc1 || img.blocks.empty())
        return;
    if (img.mips > 1 || img.w < 4 || img.h < 4)
        return;
    auto rgb = decode_bc1(img.blocks.data(), img.w, img.h);
    if (rgb.empty())
        return;
    std::vector<std::uint8_t> all;
    int w = img.w, h = img.h;
    auto lvl = encode_bc1(rgb.data(), w, h);
    all.insert(all.end(), lvl.begin(), lvl.end());
    int mips = 1;
    auto cur = std::move(rgb);
    while (w > 1 || h > 1) {
        int ow = 0, oh = 0;
        cur = downsample2(cur.data(), w, h, ow, oh);
        w = ow;
        h = oh;
        lvl = encode_bc1(cur.data(), w, h);
        all.insert(all.end(), lvl.begin(), lvl.end());
        ++mips;
        if (w <= 1 && h <= 1)
            break;
    }
    img.blocks = std::move(all);
    img.mips = mips;
}

void make_osg_safe(CompressedImage& img) {
    if (img.fmt != BlockFmt::Bc7 || img.blocks.empty())
        return;
    std::vector<std::uint8_t> bc1;
    const std::uint8_t* p = img.blocks.data();
    int w = img.w, h = img.h;
    for (int i = 0; i < img.mips; ++i) {
        auto level = transcode_bc7_to_bc1(p, w, h);
        p += bc7_size(w, h);
        bc1.insert(bc1.end(), level.begin(), level.end());
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
    }
    img.fmt = BlockFmt::Bc1;
    img.blocks = std::move(bc1);
}

bool read_dds(const std::string& path, CompressedImage& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::uint8_t hdr[128] = {};
    in.read(reinterpret_cast<char*>(hdr), 128);
    if (!in || hdr[0] != 'D' || hdr[1] != 'D' || hdr[2] != 'S' || hdr[3] != ' ')
        return false;
    auto u32 = [&](int off) -> std::uint32_t {
        return static_cast<std::uint32_t>(hdr[off] | (hdr[off + 1] << 8) | (hdr[off + 2] << 16) |
                                          (hdr[off + 3] << 24));
    };
    out.h = static_cast<int>(u32(12));
    out.w = static_cast<int>(u32(16));
    out.mips = std::max(1, static_cast<int>(u32(28)));
    if (out.w < 1 || out.h < 1 || out.w > 16384 || out.h > 16384)
        return false;
    const bool dx10 = hdr[84] == 'D' && hdr[85] == 'X' && hdr[86] == '1' && hdr[87] == '0';
    const bool dxt1 = hdr[84] == 'D' && hdr[85] == 'X' && hdr[86] == 'T' && hdr[87] == '1';
    if (dx10) {
        std::uint8_t dx[20] = {};
        in.read(reinterpret_cast<char*>(dx), 20);
        const std::uint32_t fmt = static_cast<std::uint32_t>(dx[0] | (dx[1] << 8) | (dx[2] << 16) |
                                                             (dx[3] << 24));
        out.fmt = (fmt == 98) ? BlockFmt::Bc7 : BlockFmt::Bc1;
    } else if (dxt1) {
        out.fmt = BlockFmt::Bc1;
    } else {
        return false;
    }
    std::size_t need = 0;
    int cw = out.w, ch = out.h;
    for (int i = 0; i < out.mips; ++i) {
        need += (out.fmt == BlockFmt::Bc7) ? bc7_size(cw, ch) : bc1_size(cw, ch);
        cw = std::max(1, cw / 2);
        ch = std::max(1, ch / 2);
    }
    out.blocks.resize(need);
    in.read(reinterpret_cast<char*>(out.blocks.data()), static_cast<std::streamsize>(need));
    return static_cast<bool>(in) || in.gcount() > 0;
}

} // namespace s376::dxt
