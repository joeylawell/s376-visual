#include "s376/state.hpp"
#include "s376/xt4.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
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
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #a << "=" << _a << " "  \
                      << #b << "=" << _b << " eps=" << (eps) << "\n";                              \
        }                                                                                          \
    } while (0)

static std::vector<std::uint8_t> unhex(const char* s) {
    std::vector<std::uint8_t> o;
    for (std::size_t i = 0; s[i] && s[i + 1]; i += 2) {
        unsigned v = 0;
        std::sscanf(s + i, "%2x", &v);
        o.push_back(static_cast<std::uint8_t>(v));
    }
    return o;
}

// Golden vectors produced by the s376bridge Python builders (synthetic only).
static const char* kBeIeeeKsea =
    "2d3fab01000000480000006400000004000000000001ffff0001002ac334570a40200000c1"
    "2000004047bb59ff4fd6d8c05e93b2314013ec407bf000000000000000000000000000";
static const char* kLeIeeeKsea =
    "3f2d01ab000048000000640000000400000000000100ffff01002a0034c30a572040000020"
    "c10000474059bb4fffd8d65ec0b2934031ec137b4000f0000000000000000000000000";
static const char* kBeVaxKsea =
    "2d3fab01000000480000006400000004000000000001ffff0001002a570ac4340000412000"
    "00c220b6c0fa7edacf433d9f608a009d91c3f400000000800044df0000000000000000";
static const char* kLeVaxEnv =
    "3f2d01ab00004a0000007c000000040000000000010000000000050007000400ea07000000"
    "00050000000000000092420060ea470000a041000000000000000000000000000000000000";
static const char* kHatReply =
    "2d3fab0100000040000000c800000000000000000001000000000000000000000000000000"
    "00000000000000449a5000000000000000000043d8800000000000";

static void check_ksea(const s376::xt4::Dcs& eye, unsigned count) {
    CHECK(eye.is_eyepoint());
    CHECK(eye.count == count);
    CHECK_NEAR(eye.lat_deg, 47.463684, 1e-7);
    CHECK_NEAR(eye.lon_deg, -122.307751, 1e-7);
    CHECK_NEAR(eye.alt_ft, 447.0, 1e-3);
    CHECK_NEAR(eye.true_heading_deg(), 180.34, 1e-3);
    CHECK_NEAR(eye.pitch_deg, 2.5, 1e-4);
    CHECK_NEAR(eye.roll_deg, -10.0, 1e-4);
}

void test_xt4() {
    using namespace s376::xt4;

    CHECK(build_dcs(0, 0, 0, 0, 0, 0, 1, FloatFmt::Vax).size() * 2 == 0x48);
    CHECK(build_hat_query().size() * 2 == 0x20);
    CHECK(build_lights(false, false, 0, FloatFmt::Vax).size() * 2 == 0x48);
    CHECK(build_env(12.f, 1000.f, 1, 1, 2000, FloatFmt::Vax).size() * 2 == 0x4A);
    CHECK(build_display(true).size() * 2 == 0x18);
    CHECK(build_ownship(1, 30).size() * 2 == 0x18);
    CHECK(std::string(type_name(kTypeOwnship)) == "OWNSHIP");
    CHECK(build_fog(200.f, 1200.f, FloatFmt::Vax).size() * 2 == 36);
    CHECK(build_cloud(kTypeLowerCloud, 0, 2000.f, 3000.f, FloatFmt::Vax).size() * 2 == 56);
    CHECK(build_top_cloud(2).size() * 2 == 24);
    CHECK(build_storm(0, true, kStormCsn0, 1, 7, 2).size() * 2 == 32);
    CHECK(build_light_switch(3, 5).size() * 2 == 24);

    {
        auto pk = build_env(14.5f, 4000.f, 6, 21, 2016, FloatFmt::Ieee, -1, 1, 7, 42, 12.f, 270.f);
        auto bytes = build_datagram({pk}, WordOrder::Le);
        Decoder dec;
        auto parsed = dec.parse(bytes.data(), bytes.size());
        CHECK(parsed.has_value());
        CHECK(!parsed->packets.empty());
        auto e = decode_env(parsed->packets[0], FloatFmt::Ieee);
        CHECK(e.tod_select == -1);
        CHECK(e.precip_type == 1);
        CHECK(e.precip_intensity == 7);
        CHECK(e.lightning_trigger == 42);
        CHECK_NEAR(e.tod_hours, 14.5, 0.05);
        CHECK_NEAR(e.visible_range_ft, 4000.0, 2.0);
        CHECK_NEAR(e.wind_speed_mph, 12.0, 0.2);
        CHECK_NEAR(e.wind_dir_deg, 270.0, 0.5);
        Packet fp;
        fp.type = kTypeFog;
        fp.words = build_fog(400.f, 2400.f, FloatFmt::Ieee, -1, 0);
        auto f = decode_fog(fp, FloatFmt::Ieee);
        CHECK(f.region == -1);
        CHECK(f.select == 0);
        CHECK_NEAR(f.height_ft, 400.0, 1.0);
        CHECK_NEAR(f.rvr_ft, 2400.0, 2.0);

        Packet lc;
        lc.type = kTypeLowerCloud;
        lc.words = build_cloud(kTypeLowerCloud, 0, 2500.f, 6000.f, FloatFmt::Ieee, -1, 3, 8000.f,
                               22.f, 200.f);
        auto cl = decode_cloud(lc, FloatFmt::Ieee);
        CHECK(cl.on());
        CHECK(cl.region == -1);
        CHECK(cl.select == 0);
        CHECK(cl.scud == 3);
        CHECK_NEAR(cl.ceiling_ft, 2500.0, 2.0);
        CHECK_NEAR(cl.top_ft, 6000.0, 4.0);
        CHECK_NEAR(cl.vis_above_ft, 8000.0, 8.0);
        CHECK_NEAR(cl.wind_kt, 22.0, 0.3);
        CHECK_NEAR(cl.wind_dir_deg, 200.0, 0.5);

        Packet uc;
        uc.type = kTypeUpperCloud;
        uc.words = build_cloud(kTypeUpperCloud, 1, 12000.f, 16000.f, FloatFmt::Ieee);
        auto cu = decode_cloud(uc, FloatFmt::Ieee);
        CHECK(cu.select == 1);
        CHECK_NEAR(cu.ceiling_ft, 12000.0, 8.0);
        CHECK_NEAR(cu.top_ft, 16000.0, 8.0);

        Packet tc;
        tc.type = kTypeTopCloud;
        tc.words = build_top_cloud(2, -1);
        auto ct = decode_top_cloud(tc);
        CHECK(ct.on());
        CHECK(ct.select == 2);
        CHECK(ct.region == -1);

        Packet st;
        st.type = kTypeStormSelect;
        st.words = build_storm(4, true, kStormCsn0 + 1, 2, 8, 3);
        auto sm = decode_storm(st);
        CHECK(sm.active);
        CHECK(sm.select == 4);
        CHECK(sm.csn == kStormCsn0 + 1);
        CHECK(sm.precip_type == 2);
        CHECK(sm.precip_intensity == 8);
        CHECK(sm.lightning == 3);
        CHECK(sm.select < kMaxDefinedStorms);
    }

    struct Enc {
        WordOrder o;
        FloatFmt f;
    };
    const Enc encs[] = {{WordOrder::Le, FloatFmt::Vax},
                        {WordOrder::Be, FloatFmt::Vax},
                        {WordOrder::Le, FloatFmt::Ieee},
                        {WordOrder::Be, FloatFmt::Ieee}};
    for (auto e : encs) {
        auto pk = build_dcs(47.463684, -122.307751, 447.0, 180.34, 2.5f, -10.f, 42, e.f);
        auto q = build_hat_query();
        auto li = build_lights(true, false, 3, e.f);
        auto dg = build_datagram({pk, q, li}, e.o);
        Decoder dec;
        auto parsed = dec.parse(dg.data(), dg.size());
        CHECK(parsed.has_value());
        CHECK(parsed->word_order == e.o);
        auto eye = dec.eyepoint(*parsed);
        CHECK(eye.has_value());
        CHECK(dec.locked_fmt() == e.f);
        check_ksea(*eye, 42);
        CHECK(parsed->packets.size() == 3);
        CHECK(parsed->packets[0].type == kTypeDcs);
        CHECK(parsed->packets[1].type == kTypeQuery);
        CHECK(parsed->packets[2].type == kTypeAcLights);
    }

    // Golden bytes from the Python builders — proves the C++ codec matches.
    {
        auto b = unhex(kBeIeeeKsea);
        Decoder dec;
        auto dg = dec.parse(b.data(), b.size());
        CHECK(dg.has_value());
        CHECK(dg->word_order == WordOrder::Be);
        auto eye = dec.eyepoint(*dg);
        CHECK(eye.has_value());
        CHECK(dec.locked_fmt() == FloatFmt::Ieee);
        check_ksea(*eye, 42);
    }
    {
        auto b = unhex(kLeIeeeKsea);
        Decoder dec;
        auto dg = dec.parse(b.data(), b.size());
        auto eye = dec.eyepoint(*dg);
        CHECK(eye.has_value());
        CHECK(dec.locked_fmt() == FloatFmt::Ieee);
        check_ksea(*eye, 42);
    }
    {
        auto b = unhex(kBeVaxKsea);
        Decoder dec;
        auto dg = dec.parse(b.data(), b.size());
        auto eye = dec.eyepoint(*dg);
        CHECK(eye.has_value());
        CHECK(dec.locked_fmt() == FloatFmt::Vax);
        check_ksea(*eye, 42);
    }
    {
        auto b = unhex(kLeVaxEnv);
        Decoder dec(WordOrder::Auto, FloatFmt::Vax);
        auto dg = dec.parse(b.data(), b.size());
        CHECK(dg.has_value());
        CHECK(!dg->packets.empty());
        auto env = decode_env(dg->packets[0], FloatFmt::Vax);
        CHECK_NEAR(env.tod_hours, 18.25, 1e-4);
        CHECK_NEAR(env.visible_range_ft, 30000.0, 1e-2);
        CHECK(env.month == 7 && env.day == 4 && env.year == 2026);
    }

    {
        auto tcas = build_dcs(47.5, -122.3, 5000, 10, 0, 0, 3, FloatFmt::Vax, 5, -1, 200);
        auto dg = build_datagram({tcas}, WordOrder::Le);
        Decoder dec;
        auto parsed = dec.parse(dg.data(), dg.size());
        CHECK(!dec.eyepoint(*parsed).has_value());
    }

    {
        auto b = unhex(kHatReply);
        int valid = 0;
        float hat = 0, elev = 0;
        CHECK(parse_hat_as_host(b.data(), b.size(), valid, hat, elev));
        CHECK(valid == 1);
        CHECK_NEAR(hat, 1234.5, 0.05);
        CHECK_NEAR(elev, 433.0, 0.05);
    }
    {
        auto b = build_hat_response(1234.5f, 433.0f);
        int valid = 0;
        float hat = 0, elev = 0;
        CHECK(parse_hat_as_host(b.data(), b.size(), valid, hat, elev));
        CHECK(valid == 1);
        CHECK_NEAR(hat, 1234.5, 0.05);
        CHECK_NEAR(elev, 433.0, 0.05);
    }

    {
        auto sw = build_light_switch(12, 4);
        Packet p;
        p.type = kTypeLightSwitch;
        p.words = sw;
        auto d = decode_light_switch(p);
        CHECK(d.switch_num == 12 && d.step == 4);
    }

    // Type 180 — s376 ownship (gear / flaps). Not an ICD type.
    {
        auto pk = build_ownship(1, 30);
        Packet p;
        p.type = kTypeOwnship;
        p.words = pk;
        auto o = decode_ownship(p);
        CHECK(o.gear == 1 && o.flaps == 30);
        auto pk2 = build_ownship(2, 40);
        p.words = pk2;
        o = decode_ownship(p);
        CHECK(o.gear == 2 && o.flaps == 40);
        Packet empty;
        empty.type = kTypeOwnship;
        o = decode_ownship(empty);
        CHECK(o.gear == 0 && o.flaps == 0);
        auto bytes = build_datagram({build_ownship(1, 25)}, WordOrder::Le);
        Decoder dec;
        auto parsed = dec.parse(bytes.data(), bytes.size());
        CHECK(parsed.has_value());
        CHECK(!parsed->packets.empty());
        CHECK(parsed->packets[0].type == kTypeOwnship);
        s376::SharedState st;
        s376::HostState snap;
        st.apply(*parsed, dec, "127.0.0.1:9", 1, snap);
        CHECK(snap.have_ownship);
        CHECK(snap.ownship.gear == 1);
        CHECK(snap.ownship.flaps == 25);
    }

    {
        Decoder dec;
        std::uint8_t junk[8] = {0, 1, 2, 3, 4, 5, 6, 7};
        CHECK(!dec.parse(junk, sizeof(junk)).has_value());
        CHECK(dec.stats().bad_magic == 1);
    }
}
