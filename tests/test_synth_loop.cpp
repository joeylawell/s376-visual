#include "s376/host.hpp"
#include "s376/safety.hpp"
#include "s376/state.hpp"
#include "s376/synth.hpp"
#include "s376/xt4.hpp"

#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

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

void test_synth_loop() {
    using namespace s376;
    SynthConfig sc;
    sc.word_order = xt4::WordOrder::Be;
    sc.float_fmt = xt4::FloatFmt::Ieee;
    SyntheticFlight f(sc);
    xt4::Decoder dec;
    int ok = 0;
    for (int i = 0; i < 200; ++i) {
        auto pose = f.step(0.05);
        auto dg = f.datagram(pose);
        auto parsed = dec.parse(dg.data(), dg.size());
        auto eye = dec.eyepoint(*parsed);
        CHECK(eye.has_value());
        CHECK_NEAR(eye->lat_deg, pose.lat_deg, 1e-7);
        CHECK_NEAR(eye->lon_deg, pose.lon_deg, 1e-7);
        ++ok;
    }
    CHECK(dec.stats().eyepoints == 200);
    CHECK(dec.stats().implausible == 0);
    CHECK(ok == 200);

    {
        SynthConfig wx;
        wx.word_order = xt4::WordOrder::Le;
        wx.float_fmt = xt4::FloatFmt::Ieee;
        wx.weather = true;
        SyntheticFlight fw(wx);
        xt4::Decoder wdec;
        SharedState st;
        bool saw_precip = false;
        bool saw_fog = false;
        bool saw_flash = false;
        bool saw_lower = false;
        bool saw_upper = false;
        bool saw_top = false;
        bool saw_storm = false;
        bool saw_storm_pose = false;
        int last_bolt = 0;
        for (int i = 0; i < 220; ++i) {
            auto pose = fw.step(0.05);
            auto bytes = fw.datagram(pose);
            auto parsed = wdec.parse(bytes.data(), bytes.size());
            CHECK(parsed.has_value());
            HostState snap;
            st.apply(*parsed, wdec, "loop", 0, snap);
            if (snap.have_env && snap.env.precip_type == 1 && snap.env.precip_intensity == 6) {
                saw_precip = true;
                CHECK_NEAR(snap.env.wind_speed_mph, 18.0, 0.5);
                CHECK_NEAR(snap.env.visible_range_ft, 4000.0, 4.0);
                if (last_bolt != 0 && snap.env.lightning_trigger != last_bolt)
                    saw_flash = true;
                last_bolt = snap.env.lightning_trigger;
            }
            if (snap.have_fog && snap.fog.select == 0 && snap.fog.rvr_ft > 2000.f)
                saw_fog = true;
            if (snap.have_cloud && snap.clouds[0].on() && snap.clouds[0].ceiling_ft > 2000.f)
                saw_lower = true;
            if (snap.have_cloud && snap.clouds[1].on() && snap.clouds[1].top_ft > 15000.f)
                saw_upper = true;
            if (snap.have_cloud && snap.clouds[2].on() && snap.clouds[2].select == 2)
                saw_top = true;
            if (snap.storms[0].cfg.active && snap.storms[0].cfg.precip_intensity == 7)
                saw_storm = true;
            if (snap.storms[0].have_pose && snap.storms[0].pose.lat_deg > 47.4)
                saw_storm_pose = true;
        }
        CHECK(saw_precip);
        CHECK(saw_fog);
        CHECK(saw_flash);
        CHECK(saw_lower);
        CHECK(saw_upper);
        CHECK(saw_top);
        CHECK(saw_storm);
        CHECK(saw_storm_pose);
        const auto loaded = st.load();
        CHECK(loaded.have_env && loaded.env.precip_type == 1);
        CHECK(loaded.have_fog && loaded.fog.select == 0);
        CHECK(loaded.have_cloud && loaded.clouds[0].on());
        CHECK(loaded.storms[0].cfg.active);
    }

    // Live UDP: bind loopback, send one synth datagram, decode.
    HostConfig hc;
    hc.bind = "127.0.0.1:18765";
    hc.hat_responder = true;
    hc.allow_sim_lan = false;
    SharedState st;
    HostReceiver rx(st, hc);
    rx.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    sc.rate_hz = 20;
    run_synth_sender("127.0.0.1:18765", sc, 0.25, false, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    rx.stop();
    auto snap = st.load();
    CHECK(snap.datagrams >= 1);
    CHECK(snap.have_eyepoint);
    CHECK(snap.eye.lat_deg > 47.0 && snap.eye.lat_deg < 48.0);
}
