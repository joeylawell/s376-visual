#include "s376/xt4.hpp"
#include "s376/log.hpp"
#include "s376/vaxfloat.hpp"

#include <cmath>
#include <cstring>
#include <stdexcept>

// GCC/Clang define __BYTE_ORDER__. MSVC does not — both macros expand to 0,
// so `#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__` is true and #error fires
// on every little-endian Windows box.
#if defined(_WIN32) ||                                                                 \
    (defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) &&                    \
     __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#define S376_LITTLE_ENDIAN 1
#else
#define S376_LITTLE_ENDIAN 0
#endif

namespace s376::xt4 {
namespace {

constexpr std::uint8_t kMagicLe[4] = {0x3F, 0x2D, 0x01, 0xAB};
constexpr std::uint8_t kMagicBe[4] = {0x2D, 0x3F, 0xAB, 0x01};

int find_magic(const std::uint8_t* data, std::size_t n, const std::uint8_t mag[4]) {
    if (n < 4)
        return -1;
    int best = -1;
    for (std::size_t i = 0; i + 4 <= n; ++i) {
        if (i % 2)
            continue;
        if (std::memcmp(data + i, mag, 4) == 0) {
            best = static_cast<int>(i);
            break;
        }
    }
    return best;
}

void pack_vax_f_words(float x, std::uint16_t* out) {
    std::uint8_t mem[4];
    vax::encode_f(x, mem);
    // VAX memory: word0 (sign/exp) then word1. Wire: word1, word0.
    out[0] = static_cast<std::uint16_t>(mem[2] | (mem[3] << 8));
    out[1] = static_cast<std::uint16_t>(mem[0] | (mem[1] << 8));
}

void pack_vax_d_words(double x, std::uint16_t* out) {
    std::uint8_t mem[8];
    vax::encode_d(x, mem);
    // Wire words are VAX memory words reversed.
    for (int i = 0; i < 4; ++i) {
        const int src = 3 - i;
        out[i] = static_cast<std::uint16_t>(mem[2 * src] | (mem[2 * src + 1] << 8));
    }
}

float unpack_vax_f_words(const std::uint16_t* w) {
    std::uint8_t mem[4];
    mem[0] = static_cast<std::uint8_t>(w[1] & 0xFF);
    mem[1] = static_cast<std::uint8_t>(w[1] >> 8);
    mem[2] = static_cast<std::uint8_t>(w[0] & 0xFF);
    mem[3] = static_cast<std::uint8_t>(w[0] >> 8);
    return static_cast<float>(vax::decode_f(mem));
}

double unpack_vax_d_words(const std::uint16_t* w) {
    std::uint8_t mem[8];
    for (int i = 0; i < 4; ++i) {
        const std::uint16_t src = w[3 - i];
        mem[2 * i] = static_cast<std::uint8_t>(src & 0xFF);
        mem[2 * i + 1] = static_cast<std::uint8_t>(src >> 8);
    }
    return vax::decode_d(mem);
}

float unpack_ieee_f_words(const std::uint16_t* w) {
    std::uint32_t bits = (static_cast<std::uint32_t>(w[0]) << 16) | w[1];
    float x;
    std::memcpy(&x, &bits, 4);
#if S376_LITTLE_ENDIAN
    // bits was assembled as big-endian IEEE bit pattern in a host uint32.
    // memcpy of that uint32 on LE gives the wrong byte order. Rebuild via bytes.
    std::uint8_t b[4] = {
        static_cast<std::uint8_t>(w[0] >> 8), static_cast<std::uint8_t>(w[0] & 0xFF),
        static_cast<std::uint8_t>(w[1] >> 8), static_cast<std::uint8_t>(w[1] & 0xFF)};
    std::memcpy(&x, b, 4);
    // b[] is already IEEE big-endian bytes; memcpy on LE is wrong. Swap.
    std::uint8_t le[4] = {b[3], b[2], b[1], b[0]};
    std::memcpy(&x, le, 4);
#endif
    return x;
}

double unpack_ieee_d_words(const std::uint16_t* w) {
    std::uint8_t be[8] = {
        static_cast<std::uint8_t>(w[0] >> 8), static_cast<std::uint8_t>(w[0] & 0xFF),
        static_cast<std::uint8_t>(w[1] >> 8), static_cast<std::uint8_t>(w[1] & 0xFF),
        static_cast<std::uint8_t>(w[2] >> 8), static_cast<std::uint8_t>(w[2] & 0xFF),
        static_cast<std::uint8_t>(w[3] >> 8), static_cast<std::uint8_t>(w[3] & 0xFF)};
    std::uint8_t le[8] = {be[7], be[6], be[5], be[4], be[3], be[2], be[1], be[0]};
    double x;
    std::memcpy(&x, le, 8);
    return x;
}

void pack_ieee_f_words(float x, std::uint16_t* out) {
    std::uint8_t le[4];
    std::memcpy(le, &x, 4);
    // IEEE big-endian words.
    out[0] = static_cast<std::uint16_t>((le[3] << 8) | le[2]);
    out[1] = static_cast<std::uint16_t>((le[1] << 8) | le[0]);
}

void pack_ieee_d_words(double x, std::uint16_t* out) {
    std::uint8_t le[8];
    std::memcpy(le, &x, 8);
    out[0] = static_cast<std::uint16_t>((le[7] << 8) | le[6]);
    out[1] = static_cast<std::uint16_t>((le[5] << 8) | le[4]);
    out[2] = static_cast<std::uint16_t>((le[3] << 8) | le[2]);
    out[3] = static_cast<std::uint16_t>((le[1] << 8) | le[0]);
}

#if !S376_LITTLE_ENDIAN
#error "s376-visual IEEE packers assume a little-endian host"
#endif

int type_slot(int type) {
    switch (type) {
    case kTypeDcs:
        return 0;
    case kTypeAcLights:
        return 1;
    case kTypeLightSwitch:
        return 2;
    case kTypeQuery:
        return 3;
    case kTypeEnv:
        return 4;
    case kTypeFog:
        return 5;
    case kTypeDisplay:
        return 6;
    case kTypeHat:
        return 7;
    case kTypeStormSelect:
        return 8;
    case kTypeLowerCloud:
        return 9;
    case kTypeUpperCloud:
        return 10;
    case kTypeTopCloud:
        return 11;
    default:
        return -1;
    }
}

} // namespace

const char* word_order_name(WordOrder o) {
    switch (o) {
    case WordOrder::Auto:
        return "auto";
    case WordOrder::Le:
        return "le";
    case WordOrder::Be:
        return "be";
    }
    return "?";
}

const char* float_fmt_name(FloatFmt f) {
    switch (f) {
    case FloatFmt::Auto:
        return "auto";
    case FloatFmt::Vax:
        return "vax";
    case FloatFmt::Ieee:
        return "ieee";
    }
    return "?";
}

const char* type_name(int type) {
    switch (type) {
    case kTypeDcs:
        return "DCS";
    case kTypeAcLights:
        return "AC_LIGHTS";
    case kTypeLightSwitch:
        return "LIGHTSWITCH";
    case kTypeQuery:
        return "QUERY";
    case kTypeEnv:
        return "ENV";
    case kTypeLowerCloud:
        return "LOWER_CLOUD";
    case kTypeUpperCloud:
        return "UPPER_CLOUD";
    case kTypeTopCloud:
        return "TOP_CLOUD";
    case kTypeFog:
        return "FOG";
    case kTypeStormSelect:
        return "STORM";
    case kTypeDisplay:
        return "DISPLAY";
    case kTypeOwnship:
        return "OWNSHIP";
    case kTypeHat:
        return "HAT";
    default:
        return "OTHER";
    }
}

WordOrder detect_word_order(const std::uint8_t* data, std::size_t n) {
    const int le = find_magic(data, n, kMagicLe);
    const int be = find_magic(data, n, kMagicBe);
    if (le < 0 && be < 0)
        return WordOrder::Auto;
    if (le < 0)
        return WordOrder::Be;
    if (be < 0)
        return WordOrder::Le;
    return le <= be ? WordOrder::Le : WordOrder::Be;
}

std::int16_t s16(std::uint16_t w) {
    return (w & 0x8000) ? static_cast<std::int16_t>(static_cast<int>(w) - 65536)
                        : static_cast<std::int16_t>(w);
}

std::int32_t get_i32(const std::uint16_t* w, int k) {
    const std::uint32_t v = (static_cast<std::uint32_t>(w[k]) << 16) | w[k + 1];
    return (v & 0x80000000u) ? static_cast<std::int32_t>(static_cast<std::int64_t>(v) - (1ll << 32))
                             : static_cast<std::int32_t>(v);
}

float get_f32(const std::uint16_t* w, int k, FloatFmt fmt) {
    if (fmt == FloatFmt::Ieee)
        return unpack_ieee_f_words(w + k);
    return unpack_vax_f_words(w + k);
}

double get_f64(const std::uint16_t* w, int k, FloatFmt fmt) {
    if (fmt == FloatFmt::Ieee)
        return unpack_ieee_d_words(w + k);
    return unpack_vax_d_words(w + k);
}

void put_i32(std::uint16_t* w, std::int32_t v) {
    const auto u = static_cast<std::uint32_t>(v);
    w[0] = static_cast<std::uint16_t>(u >> 16);
    w[1] = static_cast<std::uint16_t>(u & 0xFFFF);
}

void put_f32(std::uint16_t* w, float x, FloatFmt fmt) {
    if (fmt == FloatFmt::Ieee)
        pack_ieee_f_words(x, w);
    else
        pack_vax_f_words(x, w);
}

void put_f64(std::uint16_t* w, double x, FloatFmt fmt) {
    if (fmt == FloatFmt::Ieee)
        pack_ieee_d_words(x, w);
    else
        pack_vax_d_words(x, w);
}

std::vector<std::uint16_t> words_from_bytes(const std::uint8_t* data, std::size_t n,
                                            WordOrder order) {
    const std::size_t nw = n / 2;
    std::vector<std::uint16_t> w(nw);
    for (std::size_t i = 0; i < nw; ++i) {
        if (order == WordOrder::Le)
            w[i] = static_cast<std::uint16_t>(data[2 * i] | (data[2 * i + 1] << 8));
        else
            w[i] = static_cast<std::uint16_t>((data[2 * i] << 8) | data[2 * i + 1]);
    }
    return w;
}

std::vector<std::uint8_t> bytes_from_words(const std::uint16_t* w, std::size_t nwords,
                                           WordOrder order) {
    std::vector<std::uint8_t> b(nwords * 2);
    for (std::size_t i = 0; i < nwords; ++i) {
        if (order == WordOrder::Le) {
            b[2 * i] = static_cast<std::uint8_t>(w[i] & 0xFF);
            b[2 * i + 1] = static_cast<std::uint8_t>(w[i] >> 8);
        } else {
            b[2 * i] = static_cast<std::uint8_t>(w[i] >> 8);
            b[2 * i + 1] = static_cast<std::uint8_t>(w[i] & 0xFF);
        }
    }
    return b;
}

std::vector<Packet> split_packets(const std::uint16_t* words, std::size_t nwords, int& junk) {
    std::vector<Packet> out;
    junk = 0;
    std::size_t p = 0;
    while (p + kPreambleWords <= nwords) {
        if (words[p] != kMagicWord0 || words[p + 1] != kMagicWord1) {
            if (words[p])
                ++junk;
            ++p;
            continue;
        }
        const int size =
            static_cast<int>((static_cast<std::uint32_t>(words[p + 2]) << 16) | words[p + 3]);
        const int nw = size / 2;
        if (size % 2 || nw < kPreambleWords || p + static_cast<std::size_t>(nw) > nwords) {
            ++junk;
            ++p;
            continue;
        }
        Packet pkt;
        pkt.type =
            static_cast<int>((static_cast<std::uint32_t>(words[p + 4]) << 16) | words[p + 5]);
        pkt.flags = (static_cast<std::uint32_t>(words[p + 6]) << 16) | words[p + 7];
        pkt.words.assign(words + p, words + p + nw);
        pkt.offset_words = static_cast<int>(p);
        out.push_back(std::move(pkt));
        p += static_cast<std::size_t>(nw);
    }
    return out;
}

double Dcs::true_heading_deg() const {
    double h = std::fmod(-heading_deg, 360.0);
    if (h < 0)
        h += 360.0;
    return h;
}

Dcs decode_dcs(const Packet& p, FloatFmt fmt) {
    Dcs d;
    const auto* b = p.body();
    if (!b || p.body_words() < 22)
        return d;
    d.csn = s16(b[0]);
    d.minor_csn = s16(b[1]);
    d.select = s16(b[2]);
    d.count = b[3];
    d.heading_deg = get_f32(b, 4, fmt);
    d.pitch_deg = get_f32(b, 6, fmt);
    d.roll_deg = get_f32(b, 8, fmt);
    d.lat_deg = get_f64(b, 10, fmt);
    d.lon_deg = get_f64(b, 14, fmt);
    d.alt_ft = get_f64(b, 18, fmt);
    if (p.body_words() >= 26)
        d.timestamp = get_f64(b, 22, fmt);
    return d;
}

Lights decode_lights(const Packet& p) {
    Lights l;
    const auto* b = p.body();
    if (!b || p.body_words() < 4)
        return l;
    l.beacon = b[0] != 0;
    l.strobe = b[1] != 0;
    l.landing_mask = b[2];
    l.bright_mask = b[3];
    return l;
}

Env decode_env(const Packet& p, FloatFmt fmt) {
    Env e;
    const auto* b = p.body();
    if (!b || p.body_words() < 16)
        return e;
    e.tod_select = s16(b[0]);
    e.precip_type = s16(b[2]);
    e.precip_intensity = s16(b[3]);
    e.month = s16(b[4]);
    e.day = s16(b[5]);
    e.year = s16(b[6]);
    e.runway_condition = s16(b[7]);
    if (p.body_words() >= 12)
        e.lightning_trigger = get_i32(b, 10);
    e.tod_hours = get_f32(b, 12, fmt);
    e.visible_range_ft = get_f32(b, 14, fmt);
    if (p.body_words() >= 22) {
        e.wind_speed_mph = get_f32(b, 18, fmt);
        e.wind_dir_deg = get_f32(b, 20, fmt);
    }
    return e;
}

Fog decode_fog(const Packet& p, FloatFmt fmt) {
    Fog f;
    const auto* b = p.body();
    if (!b || p.body_words() < 8)
        return f;
    f.region = s16(b[0]);
    f.select = s16(b[1]);
    f.patchy = b[2] != 0;
    f.speed_cue = s16(b[3]);
    f.height_ft = get_f32(b, 4, fmt);
    f.rvr_ft = get_f32(b, 6, fmt);
    return f;
}

CloudLayer decode_cloud(const Packet& p, FloatFmt fmt) {
    CloudLayer c;
    const auto* b = p.body();
    if (!b || p.body_words() < 8)
        return c;
    c.region = s16(b[0]);
    c.select = s16(b[1]);
    c.scud = s16(b[2]);
    c.alt_transition = s16(b[3]);
    c.ceiling_ft = get_f32(b, 4, fmt);
    c.top_ft = get_f32(b, 6, fmt);
    if (p.body_words() >= 18) {
        c.vis_above_ft = get_f32(b, 8, fmt);
        c.wind_kt = get_f32(b, 10, fmt);
        c.wind_dir_deg = get_f32(b, 12, fmt);
        c.lower_trans_ft = get_f32(b, 14, fmt);
        c.upper_trans_ft = get_f32(b, 16, fmt);
    }
    return c;
}

CloudLayer decode_top_cloud(const Packet& p) {
    CloudLayer c;
    const auto* b = p.body();
    if (!b || p.body_words() < 2)
        return c;
    c.region = s16(b[0]);
    c.select = s16(b[1]);
    return c;
}

StormSelect decode_storm(const Packet& p) {
    StormSelect s;
    const auto* b = p.body();
    if (!b || p.body_words() < 6)
        return s;
    s.select = s16(b[0]);
    s.active = b[1] != 0;
    s.csn = s16(b[2]);
    s.precip_type = s16(b[3]);
    s.precip_intensity = s16(b[4]);
    s.lightning = s16(b[5]);
    return s;
}

LightSwitch decode_light_switch(const Packet& p) {
    LightSwitch s;
    const auto* b = p.body();
    if (!b || p.body_words() < 2)
        return s;
    s.switch_num = s16(b[0]);
    s.step = s16(b[1]);
    return s;
}

DisplayControl decode_display(const Packet& p) {
    DisplayControl d;
    const auto* b = p.body();
    if (!b || p.body_words() < 2)
        return d;
    d.channel = s16(b[0]);
    d.mode = s16(b[1]);
    return d;
}

Query decode_query(const Packet& p) {
    Query q;
    const auto* b = p.body();
    if (!b || p.body_words() < 2)
        return q;
    q.query_type = get_i32(b, 0);
    if (p.body_words() > 4)
        q.csn = s16(b[4]);
    return q;
}

OwnshipConfig decode_ownship(const Packet& p) {
    OwnshipConfig o;
    const auto* b = p.body();
    if (!b || p.body_words() < 2)
        return o;
    o.gear = s16(b[0]);
    o.flaps = s16(b[1]);
    return o;
}

bool plausible_dcs(const Dcs& d) {
    const double vals[] = {d.heading_deg, d.pitch_deg, d.roll_deg, d.lat_deg, d.lon_deg, d.alt_ft};
    for (double v : vals) {
        if (!std::isfinite(v))
            return false;
    }
    return std::fabs(d.lat_deg) <= 90.0 && std::fabs(d.lon_deg) <= 180.0 &&
           std::fabs(d.heading_deg) <= 720.0 && std::fabs(d.pitch_deg) <= 90.0 &&
           std::fabs(d.roll_deg) <= 180.0 && d.alt_ft >= -2000.0 && d.alt_ft <= 60000.0 &&
           (std::fabs(d.lat_deg) > 1e-6 || std::fabs(d.lon_deg) > 1e-6);
}

Decoder::Decoder(WordOrder order, FloatFmt fmt)
    : forced_order_(order), forced_fmt_(fmt), fmt_(fmt) {}

std::optional<Datagram> Decoder::parse(const std::uint8_t* data, std::size_t n,
                                       const char* origin) {
    ++stats_.datagrams;
    WordOrder order = forced_order_;
    if (order == WordOrder::Auto)
        order = detect_word_order(data, n);
    if (n == 0 || order == WordOrder::Auto) {
        ++stats_.bad_magic;
        // Silent drop. Count only; a rate-limited debug line keeps the
        // running total (the original IG logged hundreds of these).
        log::limited(log::Debug, "visual", "bad-magic",
                     "dropped empty/bad-magic xT4 datagrams: %llu",
                     static_cast<unsigned long long>(stats_.bad_magic));
        (void)origin;
        return std::nullopt;
    }
    auto words = words_from_bytes(data, n, order);
    int junk = 0;
    auto pkts = split_packets(words.data(), words.size(), junk);
    stats_.packets += pkts.size();
    stats_.junk_words += static_cast<std::uint64_t>(junk);
    for (const auto& p : pkts) {
        const int slot = type_slot(p.type);
        if (slot >= 0)
            ++stats_.by_type[static_cast<std::size_t>(slot)];
        else
            ++stats_.other_types;
    }
    Datagram dg;
    dg.word_order = order;
    dg.float_fmt = fmt_ == FloatFmt::Auto ? FloatFmt::Vax : fmt_;
    dg.packets = std::move(pkts);
    dg.junk_words = junk;
    return dg;
}

std::optional<Dcs> Decoder::eyepoint(Datagram& dg) {
    for (const auto& p : dg.packets) {
        if (p.type != kTypeDcs || p.body_words() < 22)
            continue;
        const FloatFmt try_fmt[2] = {fmt_ == FloatFmt::Auto ? FloatFmt::Vax : fmt_, FloatFmt::Ieee};
        const int ntry = fmt_ == FloatFmt::Auto ? 2 : 1;
        for (int i = 0; i < ntry; ++i) {
            Dcs d = decode_dcs(p, try_fmt[i]);
            if (d.is_eyepoint() && plausible_dcs(d)) {
                if (fmt_ == FloatFmt::Auto) {
                    fmt_ = try_fmt[i];
                    S376_LOG_INFO("visual", "xT4 encoding detected: word order %s, float %s",
                                  word_order_name(dg.word_order), float_fmt_name(fmt_));
                }
                dg.float_fmt = fmt_;
                ++stats_.eyepoints;
                return d;
            }
        }
        Dcs probe = decode_dcs(p, fmt_ == FloatFmt::Auto ? FloatFmt::Vax : fmt_);
        if (probe.is_eyepoint()) {
            ++stats_.implausible;
            log::limited(log::Warn, "visual", "implausible",
                         "eyepoint DCS at word %d decodes implausibly (lat %.6g lon %.6g alt %.6g)",
                         p.offset_words, probe.lat_deg, probe.lon_deg, probe.alt_ft);
            if (forced_fmt_ == FloatFmt::Auto && stats_.implausible % 50 == 0) {
                S376_LOG_WARN("visual", "50 implausible eyepoints, re-detecting float format");
                fmt_ = FloatFmt::Auto;
            }
        }
    }
    return std::nullopt;
}

std::vector<std::uint16_t> build_packet(int type, const std::uint16_t* body, int nbody,
                                        std::uint32_t flags) {
    const int size = 2 * (kPreambleWords + nbody);
    std::vector<std::uint16_t> w(static_cast<std::size_t>(kPreambleWords + nbody), 0);
    w[0] = kMagicWord0;
    w[1] = kMagicWord1;
    w[2] = static_cast<std::uint16_t>(static_cast<std::uint32_t>(size) >> 16);
    w[3] = static_cast<std::uint16_t>(size & 0xFFFF);
    w[4] = static_cast<std::uint16_t>(static_cast<std::uint32_t>(type) >> 16);
    w[5] = static_cast<std::uint16_t>(type & 0xFFFF);
    w[6] = static_cast<std::uint16_t>(flags >> 16);
    w[7] = static_cast<std::uint16_t>(flags & 0xFFFF);
    if (body && nbody)
        std::memcpy(w.data() + kPreambleWords, body, static_cast<std::size_t>(nbody) * 2);
    return w;
}

std::vector<std::uint16_t> build_dcs(double lat, double lon, double alt_ft, double true_hdg,
                                     float pitch, float roll, unsigned count, FloatFmt fmt, int csn,
                                     int minor, int select) {
    std::uint16_t body[26] = {};
    body[0] = static_cast<std::uint16_t>(csn);
    body[1] = static_cast<std::uint16_t>(minor);
    body[2] = static_cast<std::uint16_t>(select);
    body[3] = static_cast<std::uint16_t>(count);
    put_f32(body + 4, static_cast<float>(-true_hdg), fmt);
    put_f32(body + 6, pitch, fmt);
    put_f32(body + 8, roll, fmt);
    put_f64(body + 10, lat, fmt);
    put_f64(body + 14, lon, fmt);
    put_f64(body + 18, alt_ft, fmt);
    put_f64(body + 22, 0.0, fmt);
    return build_packet(kTypeDcs, body, 26);
}

std::vector<std::uint16_t> build_lights(bool beacon, bool strobe, std::uint16_t landing_mask,
                                        FloatFmt) {
    std::uint16_t body[26] = {};
    body[0] = beacon ? 1 : 0;
    body[1] = strobe ? 1 : 0;
    body[2] = landing_mask;
    return build_packet(kTypeAcLights, body, 26);
}

std::vector<std::uint16_t> build_env(float tod_hours, float visible_range_ft, int month, int day,
                                     int year, FloatFmt fmt, int tod_select, int precip_type,
                                     int precip_intensity, int lightning, float wind_mph,
                                     float wind_dir, int runway_condition) {
    std::uint16_t body[27] = {};
    body[0] = static_cast<std::uint16_t>(tod_select);
    body[2] = static_cast<std::uint16_t>(precip_type);
    body[3] = static_cast<std::uint16_t>(precip_intensity);
    body[4] = static_cast<std::uint16_t>(month);
    body[5] = static_cast<std::uint16_t>(day);
    body[6] = static_cast<std::uint16_t>(year);
    body[7] = static_cast<std::uint16_t>(runway_condition < 0 ? 0 : runway_condition);
    put_i32(body + 10, lightning);
    put_f32(body + 12, tod_hours, fmt);
    put_f32(body + 14, visible_range_ft, fmt);
    put_f32(body + 18, wind_mph, fmt);
    put_f32(body + 20, wind_dir, fmt);
    return build_packet(kTypeEnv, body, 27);
}

std::vector<std::uint16_t> build_fog(float height_ft, float rvr_ft, FloatFmt fmt, int region,
                                     int select) {
    std::uint16_t body[8] = {};
    body[0] = static_cast<std::uint16_t>(region);
    body[1] = static_cast<std::uint16_t>(select);
    body[3] = 3;
    put_f32(body + 4, height_ft, fmt);
    put_f32(body + 6, rvr_ft, fmt);
    return build_packet(kTypeFog, body, 8);
}

std::vector<std::uint16_t> build_cloud(int type, int select, float ceiling_ft, float top_ft,
                                       FloatFmt fmt, int region, int scud, float vis_above_ft,
                                       float wind_kt, float wind_dir) {
    std::uint16_t body[18] = {};
    body[0] = static_cast<std::uint16_t>(region);
    body[1] = static_cast<std::uint16_t>(select);
    body[2] = static_cast<std::uint16_t>(scud);
    body[3] = static_cast<std::uint16_t>(-1);
    put_f32(body + 4, ceiling_ft, fmt);
    put_f32(body + 6, top_ft, fmt);
    put_f32(body + 8, vis_above_ft, fmt);
    put_f32(body + 10, wind_kt, fmt);
    put_f32(body + 12, wind_dir, fmt);
    put_f32(body + 14, -1.f, fmt);
    put_f32(body + 16, -1.f, fmt);
    return build_packet(type, body, 18);
}

std::vector<std::uint16_t> build_top_cloud(int select, int region) {
    std::uint16_t body[2] = {static_cast<std::uint16_t>(region),
                             static_cast<std::uint16_t>(select)};
    return build_packet(kTypeTopCloud, body, 2);
}

std::vector<std::uint16_t> build_storm(int select, bool active, int csn, int precip_type,
                                       int precip_intensity, int lightning) {
    std::uint16_t body[6] = {
        static_cast<std::uint16_t>(select),
        static_cast<std::uint16_t>(active ? 1 : 0),
        static_cast<std::uint16_t>(csn),
        static_cast<std::uint16_t>(precip_type),
        static_cast<std::uint16_t>(precip_intensity),
        static_cast<std::uint16_t>(lightning),
    };
    return build_packet(kTypeStormSelect, body, 6);
}

std::vector<std::uint16_t> build_light_switch(int switch_num, int step) {
    std::uint16_t body[2] = {static_cast<std::uint16_t>(switch_num),
                             static_cast<std::uint16_t>(step)};
    return build_packet(kTypeLightSwitch, body, 2);
}

std::vector<std::uint16_t> build_display(bool on) {
    std::uint16_t body[2] = {0xFFFF, static_cast<std::uint16_t>(on ? 1 : 0)};
    return build_packet(kTypeDisplay, body, 2);
}

std::vector<std::uint16_t> build_ownship(int gear, int flaps) {
    std::uint16_t body[2] = {static_cast<std::uint16_t>(gear),
                             static_cast<std::uint16_t>(flaps)};
    return build_packet(kTypeOwnship, body, 2);
}

std::vector<std::uint16_t> build_hat_query() {
    std::uint16_t body[6] = {};
    put_i32(body, kTypeHat);
    body[4] = kEyepointCsn;
    body[5] = 0xFFFF;
    return build_packet(kTypeQuery, body, 6);
}

std::vector<std::uint8_t> build_datagram(const std::vector<std::vector<std::uint16_t>>& packets,
                                         WordOrder order, int total_words) {
    std::vector<std::uint16_t> words;
    for (const auto& p : packets)
        words.insert(words.end(), p.begin(), p.end());
    if (total_words > 0 && static_cast<int>(words.size()) < total_words)
        words.resize(static_cast<std::size_t>(total_words), 0);
    return bytes_from_words(words.data(), words.size(), order);
}

std::vector<std::uint8_t> build_hat_response(float hat_ft, float terrain_elev_ft, bool valid) {
    std::uint16_t w[32] = {};
    w[0] = kMagicWord0;
    w[1] = kMagicWord1;
    w[2] = 0;
    w[3] = 64;
    w[5] = kTypeHat;
    w[10] = valid ? 1 : 0;
    pack_ieee_f_words(hat_ft, w + 22);
    pack_ieee_f_words(terrain_elev_ft, w + 28);
    return bytes_from_words(w, 32, WordOrder::Be);
}

bool parse_hat_as_host(const std::uint8_t* data, std::size_t n, int& valid, float& hat,
                       float& elev) {
    if (n < 64)
        return false;
    auto le = words_from_bytes(data, n, WordOrder::Le);
    if (le[0] != 0x3F2D)
        return false;
    // Host byte-swaps each word, then reads signed valid.
    auto swap16 = [](std::uint16_t x) { return static_cast<std::uint16_t>((x >> 8) | (x << 8)); };
    valid = s16(swap16(le[10]));
    auto host_float = [&](int k) {
        std::uint16_t hi = static_cast<std::uint16_t>((le[k] + 0x0001) & 0xFFFF);
        std::uint16_t lo = le[k + 1];
        hi = swap16(hi);
        lo = swap16(lo);
        std::uint8_t mem[4] = {
            static_cast<std::uint8_t>(hi & 0xFF), static_cast<std::uint8_t>(hi >> 8),
            static_cast<std::uint8_t>(lo & 0xFF), static_cast<std::uint8_t>(lo >> 8)};
        return static_cast<float>(vax::decode_f(mem));
    };
    hat = host_float(22);
    elev = host_float(28);
    return true;
}

} // namespace s376::xt4
