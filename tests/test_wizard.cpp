#include "s376/control.hpp"
#include "s376/display.hpp"
#include "s376/landmarks.hpp"
#include "s376/missiles.hpp"
#include "s376/safety.hpp"
#include "s376/wizard.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
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

void test_wizard() {
    s376::ControlMsg m;
    m.op = "status";
    m.id = 7;
    m.set("fps", 59.9);
    const std::string wire = s376::encode_control(m);
    CHECK(wire.find("S376C1") != std::string::npos);
    CHECK(wire.find("op=status") != std::string::npos);
    s376::ControlMsg back;
    CHECK(s376::decode_control(wire, back));
    CHECK(back.op == "status");
    CHECK(back.id == 7);
    CHECK(back.get("fps").find("59.9") != std::string::npos);
    CHECK(!s376::decode_control("nope\nop=x\n", back));

    // Simulator LAN is never a legal control destination in tests.
    bool threw = false;
    try {
        s376::ControlMsg ping;
        ping.op = "ping";
        (void)s376::control_request("10.1.95.10:2376", ping, 50, false, false);
    } catch (const s376::safety::UnsafeEndpoint&) {
        threw = true;
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
    threw = false;
    try {
        s376::ControlMsg ping;
        ping.op = "ping";
        (void)s376::control_request("192.168.173.10:2376", ping, 50, false, true);
    } catch (const s376::safety::UnsafeEndpoint&) {
        threw = true;
    }
    CHECK(threw);

    auto wz = s376::default_wizard_config();
    CHECK(wz.targets.size() == 1);
    CHECK(wz.targets[0].control.find("127.0.0.1") != std::string::npos);
    CHECK(!s376::safety::is_forbidden(wz.targets[0].control.substr(0, 9)));
    CHECK(!wz.allow_sim_lan);
    CHECK(!wz.allow_lan);
    const auto dumped = s376::dump_wizard_config(wz);
    CHECK(dumped.find("127.0.0.1:2376") != std::string::npos);

    const auto argv = s376::ig_launch_argv(wz, wz.targets[0]);
    CHECK(!argv.empty());
    bool saw_control = false, saw_xt4 = false;
    for (const auto& a : argv) {
        if (a.find("2376") != std::string::npos)
            saw_control = true;
        if (a.find("2300") != std::string::npos || a.find("2301") != std::string::npos)
            saw_xt4 = true;
    }
    CHECK(saw_control);
    CHECK(!saw_xt4);

    {
        // CTest cwd is the build dir; do not prefix "build/" or the write fails.
        const std::string path = "s376-test-wizard.toml";
        std::ofstream out(path);
        CHECK(static_cast<bool>(out));
        out << "visual = \"s376-visual\"\n"
               "ig_config = \"config/channels-3.toml\"\n"
               "[[targets]]\n"
               "name = \"front\"\n"
               "channel = \"front\"\n"
               "control = \"127.0.0.1:2376\"\n"
               "[[targets]]\n"
               "name = \"left\"\n"
               "channel = \"left\"\n"
               "control = \"127.0.0.1:2377\"\n";
        out.close();
        auto loaded = s376::load_wizard_config(path);
        CHECK(loaded.targets.size() == 2);
        CHECK(loaded.targets[1].control == "127.0.0.1:2377");
        threw = false;
        try {
            std::ofstream bad("s376-test-wizard-bad.toml");
            CHECK(static_cast<bool>(bad));
            bad << "[[targets]]\nname = \"x\"\ncontrol = \"10.1.95.1:2376\"\n";
            bad.close();
            (void)s376::load_wizard_config("s376-test-wizard-bad.toml");
        } catch (const s376::safety::UnsafeEndpoint&) {
            threw = true;
        } catch (const std::exception&) {
            threw = true;
        }
        CHECK(threw);
        std::remove(path.c_str());
        std::remove("s376-test-wizard-bad.toml");
    }

    s376::control_hub().reset();
    s376::refresh_channels();
    auto cfg = s376::default_config();
    auto selected = s376::select_channels(cfg, "mid_left");
    s376::control_hub().attach(cfg, selected);
    s376::ControlMsg ping;
    ping.op = "ping";
    ping.id = 1;
    CHECK(s376::control_hub().handle(ping).ok());

    s376::ControlMsg st;
    st.op = "status";
    auto status = s376::control_hub().handle(st);
    CHECK(status.ok());
    CHECK(status.get("ch.0.name") == "mid_left");
    CHECK(status.get("era") == "current");
    CHECK(status.get("era_unlocked") == "0");
    CHECK(status.get("missiles") == "0");

    s376::ControlMsg set;
    set.op = "set";
    set.set("channel", "mid_left");
    set.set("hdg", 12.5);
    set.set("hfov", 48.0);
    set.set("pattern", "grid5");
    set.set("gamma", 1.2);
    set.set("flip", 1);
    set.set("resolution", "1280x800");
    auto after = s376::control_hub().handle(set);
    CHECK(after.ok());
    CHECK_NEAR(std::stod(after.get("ch.0.hdg")), 12.5, 1e-4);
    CHECK(after.get("ch.0.pattern") == "grid5");
    CHECK(after.get("ch.0.flip") == "1");
    CHECK(after.get("ch.0.resolution") == "1280x800");
    const auto live = s376::control_hub().live_channels();
    CHECK(live.size() == 1);
    CHECK(live[0].flip_vertical);
    CHECK(live[0].width == 1280 && live[0].height == 800);
    CHECK_NEAR(live[0].heading_offset_deg, 12.5, 1e-4);

    {
        auto corr = s376::identity_correction();
        corr.gamma = 1.25f;
        corr.pattern = s376::DisplayPattern::White70;
        corr.blend.left_px = 40;
        std::string err;
        CHECK(s376::save_correction("s376-test-corr.toml", corr, err));
        s376::DisplayCorrection backc;
        CHECK(s376::load_correction("s376-test-corr.toml", backc, err));
        CHECK_NEAR(backc.gamma, 1.25, 1e-4);
        CHECK(backc.pattern == s376::DisplayPattern::White70);
        CHECK_NEAR(backc.blend.left_px, 40, 1e-4);
        std::remove("s376-test-corr.toml");
    }

    {
        const std::string cpath = "s376-test-chan.toml";
        std::ofstream o(cpath);
        o << "[[channels]]\nname = \"front\"\nheading_offset_deg = 0\nhfov_deg = 50\n"
             "vfov_deg = 32.5\nflip_vertical = false\nresolution = \"1920x1080\"\n";
        o.close();
        s376::ChannelConfig ch;
        ch.name = "front";
        ch.heading_offset_deg = 35.515;
        ch.hfov_deg = 49;
        ch.flip_vertical = true;
        ch.width = 1920;
        ch.height = 1200;
        ch.pattern = "black";
        std::string err;
        CHECK(s376::patch_channel_display(cpath, ch, err));
        auto loaded = s376::load_config(cpath);
        CHECK(loaded.channels.size() == 1);
        CHECK_NEAR(loaded.channels[0].heading_offset_deg, 35.515, 1e-6);
        CHECK(loaded.channels[0].flip_vertical);
        CHECK(loaded.channels[0].pattern == "black");
        CHECK(loaded.channels[0].width == 1920 && loaded.channels[0].height == 1200);
        std::remove(cpath.c_str());
    }

    s376::refresh_channels();
    s376::ControlMsg click;
    click.op = "era_click";
    std::string last_toast;
    for (int i = 0; i < 7; ++i) {
        click.set("t_ms", 1000 + i * 100);
        auto r = s376::control_hub().handle(click);
        last_toast = r.get("toast");
        if (i == 3)
            CHECK(last_toast.find("Click 3 more times") != std::string::npos);
        if (i == 5)
            CHECK(last_toast.find("Click 1 more time") != std::string::npos);
    }
    CHECK(last_toast == "Classic skyline unlocked");
    s376::ControlMsg un;
    un.op = "unlock";
    CHECK(s376::control_hub().handle(un).ok());
    CHECK(s376::session_era_unlocked());
    s376::ControlMsg era;
    era.op = "set_era";
    era.set("era", "classic");
    CHECK(s376::control_hub().handle(era).ok());
    CHECK(s376::session_era() == s376::SkylineEra::Classic);
    s376::ControlMsg sm;
    sm.op = "set_missiles";
    sm.set("on", "1");
    auto smr = s376::control_hub().handle(sm);
    CHECK(smr.ok());
    CHECK(smr.get("missiles") == "1");
    CHECK(s376::session_missiles());

    s376::ControlMsg ref;
    ref.op = "refresh";
    auto rr = s376::control_hub().handle(ref);
    CHECK(rr.ok());
    CHECK(rr.get("restart") == "1");
    CHECK(s376::control_hub().restart_requested());
    CHECK(s376::session_era() == s376::SkylineEra::Current);
    CHECK(!s376::session_era_unlocked());
    CHECK(!s376::session_missiles());

    s376::ControlServer srv;
    srv.start("127.0.0.1:0", false, false);
    CHECK(srv.port() != 0);
    CHECK(srv.port() != 2300);
    CHECK(srv.port() != 2301);
    s376::ControlMsg live_ping;
    live_ping.op = "ping";
    live_ping.id = 99;
    const std::string dest = "127.0.0.1:" + std::to_string(srv.port());
    auto live_r = s376::control_request(dest, live_ping, 400, false, false);
    CHECK(live_r.ok());
    CHECK(live_r.id == 99);
    srv.stop();

#ifndef _WIN32
    {
        s376::ChannelSupervisor sup;
        CHECK(sup.launch_argv("sleep", {"/bin/sleep", "30"}));
        CHECK(sup.children().size() == 1);
        CHECK(sup.children()[0].running());
        const int pid1 = sup.children()[0].pid;
        CHECK(sup.refresh_all(2000));
        CHECK(sup.children().size() == 1);
        CHECK(sup.children()[0].running());
        CHECK(sup.children()[0].pid != pid1);
        CHECK(sup.stop("sleep", 2000));
        CHECK(!sup.children()[0].running());
    }
#endif

    s376::control_hub().reset();
    s376::refresh_channels();
}
