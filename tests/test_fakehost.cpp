// s376-fakehost tests: guard, distance units, profile math, replay
// round-trip, wire decode, flight model trim, IOS parsing, in-process
// end-to-end against the renderer's own HostReceiver, and (if a path is
// given as argv[1]) the real s376-visual --headless binary on loopback.

#include "s376/fakehost.hpp"
#include "s376/geo.hpp"
#include "s376/host.hpp"
#include "s376/state.hpp"
#include "s376/xt4.hpp"

#include <arpa/inet.h>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace fh = s376::fakehost;
static int fails = 0, passes = 0;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (c) ++passes;                                                                           \
        else { ++fails; std::cerr << "FAIL " << __LINE__ << " " #c "\n"; }                         \
    } while (0)
#define NEAR(a, b, e)                                                                              \
    do {                                                                                           \
        const double _a = (a), _b = (b);                                                           \
        if (std::fabs(_a - _b) <= (e)) ++passes;                                                   \
        else { ++fails; std::cerr << "FAIL " << __LINE__ << " " #a "=" << _a << " " #b "=" << _b << "\n"; } \
    } while (0)

static bool refused(const std::string& h, std::uint16_t p) {
    try { fh::guard(h, p, "test"); } catch (const fh::Refused&) { return true; }
    return false;
}

static void test_guard() {
    CHECK(!refused("127.0.0.1", 2300));
    CHECK(!refused("127.1.2.3", 2301));
    CHECK(!refused("::1", 2300));
    CHECK(!refused("[::1]", 2300));
    CHECK(!refused("::ffff:127.0.0.1", 2300));
    CHECK(!refused("localhost", 2300));
    CHECK(!refused("127.0.0.1", 3340)); // allowed on loopback only
    CHECK(!refused("127.0.0.1", 3341));
    for (const char* h : {"10.1.95.1", "10.1.95.254", "192.168.173.1", "192.168.173.20", "0.0.0.0",
                          "::", "", "8.8.8.8", "10.0.0.1", "192.168.1.10", "::ffff:10.1.95.3",
                          "fe80::1", "2001:db8::1", "128.0.0.1", "simhost", "127.0.0.1.evil",
                          "1.2.3.4.5"})
        CHECK(refused(h, 2300));
    for (std::uint16_t p : {3340, 3341}) {
        CHECK(refused("10.1.95.10", p));
        CHECK(refused("192.168.173.5", p));
        CHECK(refused("172.16.0.5", p));
    }
    // Message names the reason.
    try { fh::guard("10.1.95.7", 3340, "send"); CHECK(false); }
    catch (const fh::Refused& e) {
        CHECK(std::string(e.what()).find("10.1.95") != std::string::npos);
        CHECK(std::string(e.what()).find("3340") != std::string::npos);
    }
    // Environment variables do NOT relax the fakehost guard.
    setenv("S376_ALLOW_LAN", "1", 1);
    setenv("S376_ALLOW_SIM_LAN", "1", 1);
    CHECK(refused("10.1.95.2", 2300));
    CHECK(refused("192.168.1.2", 2300));
    unsetenv("S376_ALLOW_LAN");
    unsetenv("S376_ALLOW_SIM_LAN");
    // Socket-address form.
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(2300);
    inet_pton(AF_INET, "10.1.95.9", &a.sin_addr);
    bool r = false;
    try { fh::guard_sockaddr(&a, sizeof(a), "t"); } catch (const fh::Refused&) { r = true; }
    CHECK(r);
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    r = false;
    try { fh::guard_sockaddr(&a, sizeof(a), "t"); } catch (const fh::Refused&) { r = true; }
    CHECK(!r);
    // parse_guarded
    std::string h; std::uint16_t p;
    fh::parse_guarded("127.0.0.1:2300", h, p, "t");
    CHECK(h == "127.0.0.1" && p == 2300);
    fh::parse_guarded("[::1]:2301", h, p, "t");
    CHECK(h == "::1" && p == 2301);
    fh::parse_guarded("localhost:9", h, p, "t");
    CHECK(h == "127.0.0.1");
    r = false;
    try { fh::parse_guarded("192.168.173.1:3341", h, p, "t"); } catch (const fh::Refused&) { r = true; }
    CHECK(r);
    // Cockpit-IO stub and IOS refuse non-loopback too.
    r = false;
    try { fh::CockpitIoConfig c; c.endpoint = "10.1.95.4:3340"; fh::CockpitIoBridge b(c); }
    catch (const fh::Refused&) { r = true; }
    CHECK(r);
    r = false;
    try { fh::IosServer s; s.start("0.0.0.0:8376"); } catch (const fh::Refused&) { r = true; }
    CHECK(r);
}

static void test_distance() {
    NEAR(fh::parse_distance_nm("6nm"), 6.0, 1e-9);
    NEAR(fh::parse_distance_nm("6"), 6.0, 1e-9);
    NEAR(fh::parse_distance_nm("6sm"), 6 * 5280.0 / 6076.115, 1e-6);
    NEAR(fh::parse_distance_nm("11.112km"), 6.0, 1e-3);
    bool thrown = false;
    try { fh::parse_distance_nm("6furlongs"); } catch (...) { thrown = true; }
    CHECK(thrown);
}

static void test_profile_math() {
    NEAR(fh::threshold_crossing_height_ft(3.0), 52.4, 0.5); // generic 1000 ft aim
    NEAR(fh::glidepath_height_ft(-6 * 6076.115, 3.0), 1963, 3);         // 6 nm
    NEAR(fh::glidepath_height_ft(-6 * 5280.0, 3.0), 1713, 3);           // 6 sm
    NEAR(fh::glidepath_height_ft(-10 * 6076.115, 3.0), 3237, 3);        // 10 nm
    const auto r04 = fh::runway_from_faa("KJFK", "04R");
    NEAR(r04.elev_ft, 11.9, 0.01);
    NEAR(r04.brg_true, 30.768, 0.01);
    NEAR(r04.ils_tch_ft, 53.0, 0.01);
    NEAR(fh::aim_point_ft(r04, 3.0), 53.0 / std::tan(s376::geo::deg2rad(3.0)), 1.0);
    NEAR(r04.thr_lat, 40.625428, 1e-5);
    const auto r13 = fh::runway_from_faa("kjfk", "13l");
    NEAR(r13.brg_true, 120.755, 0.01);
    NEAR(r13.ils_tch_ft, 58.0, 0.01);
    NEAR(r13.displaced_ft, 907, 0.1);
    // Displaced threshold moved ~907 ft along 121 T from the physical end.
    NEAR(r13.thr_lat < 40.6577, 1, 0);

    for (const char* rw : {"04R", "13L"}) {
        for (bool six : {false, true}) {
            fh::ProfileConfig pc;
            pc.rwy = fh::runway_from_faa("KJFK", rw);
            pc.start_nm = six ? 6.0 : 10.0;
            pc.landing_config_at_start = six;
            fh::ApproachProfile ap(pc);
            const double dt = 1.0 / 60;
            double tch = -1, prev_x = ap.along_ft(), h1000_v = 0;
            bool gear_at_1000 = false, flaps30_at_1000 = false;
            double max_bank = 0, td_pitch = 0, prev_wh = 1e9, max_pitch = -99, min_pitch = 99;
            fh::Frame f0 = ap.step(dt), f;
            const double aim = fh::aim_point_ft(pc.rwy, 3.0);
            const double tch_exp = fh::threshold_crossing_height_ft(3.0, aim);
            if (six) {
                CHECK(f0.gear_down && f0.flaps == 30);
                NEAR(f0.gs_kt, 145, 0.5);
                NEAR(f0.wheel_agl_ft, fh::glidepath_height_ft(-6 * 6076.115, 3.0, aim), 5);
            } else {
                CHECK(!f0.gear_down && f0.flaps == 15);
            }
            int n = 0;
            while (!ap.done() && n++ < 60 * 600) {
                f = ap.step(dt);
                const double x = ap.along_ft();
                if (prev_x < 0 && x >= 0)
                    tch = f.wheel_agl_ft;
                if (prev_wh > 1000 && f.wheel_agl_ft <= 1000) {
                    gear_at_1000 = f.gear_down;
                    flaps30_at_1000 = f.flaps == 30;
                    h1000_v = f.gs_kt;
                }
                if (!f.on_ground) {
                    max_bank = std::max(max_bank, std::fabs(f.roll));
                    max_pitch = std::max(max_pitch, f.pitch);
                    min_pitch = std::min(min_pitch, f.pitch);
                    td_pitch = f.pitch;
                    // On glidepath above the flare.
                    if (f.wheel_agl_ft > 100)
                        NEAR(f.wheel_agl_ft, fh::glidepath_height_ft(x, 3.0, aim), 1.0);
                }
                prev_x = x;
                prev_wh = f.wheel_agl_ft;
            }
            CHECK(ap.done());
            NEAR(tch, tch_exp, 6); // plate ILS TCH (04R 53 / 13L 58)
            CHECK(gear_at_1000 && flaps30_at_1000);
            NEAR(h1000_v, 145, 2);
            CHECK(max_bank < 3.0);
            CHECK(min_pitch > -1.0 && max_pitch < 8.0);
            NEAR(td_pitch, 5.5, 1.5); // flare attitude
            CHECK(ap.touchdown_x_ft() > 900 && ap.touchdown_x_ft() < 2500);
            NEAR(f.gs_kt, 20, 0.5);
            CHECK(f.on_ground && f.phase == "taxi");
            CHECK(f.landing_mask == ((1 << 1) | (1 << 5)));
            NEAR(f.hdg, pc.rwy.brg_true, 1e-6);
        }
    }
}

static void test_replay() {
    fh::ProfileConfig pc;
    pc.rwy = fh::runway_from_faa("KJFK", "04R");
    pc.start_nm = 2;
    fh::ApproachProfile ap(pc);
    std::vector<fh::Frame> fr;
    while (!ap.done())
        fr.push_back(ap.step(0.1));
    const std::string csv = fh::trajectory_csv(fr);
    const auto back = fh::parse_trajectory_csv(csv);
    CHECK(back.size() == fr.size());
    for (std::size_t i = 0; i < fr.size(); i += 37) {
        NEAR(back[i].lat, fr[i].lat, 1e-8);
        NEAR(back[i].lon, fr[i].lon, 1e-8);
        NEAR(back[i].alt_ft, fr[i].alt_ft, 0.01);
        NEAR(back[i].pitch, fr[i].pitch, 0.001);
        CHECK(back[i].gear_down == fr[i].gear_down && back[i].flaps == fr[i].flaps);
        CHECK(back[i].landing_mask == fr[i].landing_mask);
    }
    // Interpolation midway and heading wrap.
    const auto m = fh::interpolate(back, (back[10].t + back[11].t) / 2);
    NEAR(m.alt_ft, (back[10].alt_ft + back[11].alt_ft) / 2, 0.01);
    std::vector<fh::Frame> w(2);
    w[0].t = 0; w[0].hdg = 350; w[0].lat = 40; w[0].lon = -73;
    w[1].t = 1; w[1].hdg = 10; w[1].lat = 40; w[1].lon = -73;
    NEAR(fh::interpolate(w, 0.5).hdg, 0, 1e-9);
    // TOML
    const auto tt = fh::parse_trajectory_toml(
        "# sample\n[[point]]\nt = 0\nlat = 40.6\nlon = -73.8\nalt_ft = 1000\nhdg = 31\n"
        "gear = true\n[[point]]\nt = 2.0\nlat = 40.61\nlon = -73.79\nalt_ft = 900\nhdg = 31\n");
    CHECK(tt.size() == 2 && tt[0].gear_down);
    NEAR(fh::interpolate(tt, 1.0).alt_ft, 950, 1e-9);
    bool thrown = false;
    try { fh::parse_trajectory_csv("t,lat,lon,alt_ft\n1,40,-73,100\n0.5,40,-73,100\n"); }
    catch (...) { thrown = true; }
    CHECK(thrown);
    // Sample file in the repo (optional when run from elsewhere).
    try {
        const auto s = fh::load_trajectory(std::string(S376_SRC_DIR) + "/data/fakehost/kjfk-04r-sample.csv");
        CHECK(s.size() > 10);
    } catch (const std::exception& e) {
        std::cerr << "sample: " << e.what() << "\n";
        CHECK(false);
    }
}

static void test_wire() {
    fh::Frame f;
    f.lat = 40.6; f.lon = -73.8; f.alt_ft = 500; f.hdg = 31; f.pitch = 2.5; f.roll = -1;
    f.landing_mask = 0x7F; f.strobe = true;
    f.gear_down = true; f.flaps = 30;
    fh::Environment env;
    env.tod_hours = 19.25f; env.vis_ft = 3 * 5280.f; env.rvr_ft = 2400; env.ceiling_ft_msl = 800;
    for (auto wo : {s376::xt4::WordOrder::Le, s376::xt4::WordOrder::Be})
        for (auto ff : {s376::xt4::FloatFmt::Vax, s376::xt4::FloatFmt::Ieee}) {
            auto dg = fh::build_frame_datagram(f, 1, env, wo, ff);
            s376::xt4::Decoder dec;
            auto p = dec.parse(dg.data(), dg.size());
            CHECK(p.has_value());
            auto eye = dec.eyepoint(*p);
            CHECK(eye.has_value());
            NEAR(eye->lat_deg, 40.6, 1e-7);
            NEAR(eye->true_heading_deg(), 31, 1e-3);
            bool env_ok = false, q = false, lights = false, fog = false, own = false;
            for (const auto& pk : p->packets) {
                if (pk.type == s376::xt4::kTypeEnv) {
                    auto e = s376::xt4::decode_env(pk, dec.locked_fmt());
                    env_ok = std::fabs(e.tod_hours - 19.25f) < 1e-3 && std::fabs(e.visible_range_ft - 15840) < 1;
                }
                if (pk.type == s376::xt4::kTypeQuery)
                    q = s376::xt4::decode_query(pk).query_type == s376::xt4::kTypeHat;
                if (pk.type == s376::xt4::kTypeAcLights)
                    lights = s376::xt4::decode_lights(pk).landing_mask == 0x7F;
                if (pk.type == s376::xt4::kTypeFog)
                    fog = std::fabs(s376::xt4::decode_fog(pk, dec.locked_fmt()).rvr_ft - 2400) < 1;
                if (pk.type == s376::xt4::kTypeOwnship) {
                    const auto o = s376::xt4::decode_ownship(pk);
                    own = o.gear == 1 && o.flaps == 30;
                }
            }
            CHECK(env_ok && q && lights && fog && own);
        }
    // HAT reply decode matches the IG builder.
    auto r = s376::xt4::build_hat_response(12.5f, 11.9f, true);
    fh::HatReply h;
    CHECK(fh::decode_hat_reply(r.data(), r.size(), h));
    CHECK(h.valid);
    NEAR(h.hat_ft, 12.5, 1e-4);
    NEAR(h.elev_ft, 11.9, 1e-4);
    // Checker logic: touchdown + contact.
    fh::Checker c;
    fh::Frame air; air.alt_ft = 300; air.wheel_agl_ft = 280;
    fh::HatReply ra; ra.valid = true; ra.hat_ft = 290; ra.elev_ft = 10;
    c.note(air, ra, 11.9);
    fh::Frame td = air; td.on_ground = true; td.alt_ft = 11.9 + fh::kEyeHeightFt; td.wheel_agl_ft = 0;
    fh::HatReply rt; rt.valid = true; rt.hat_ft = 14.2f; rt.elev_ft = 11.7f;
    c.note(td, rt, 11.9);
    CHECK(c.touchdown_ok());
    fh::HatReply rc; rc.valid = true; rc.hat_ft = -3; rc.elev_ft = 400;
    c.note(air, rc, 11.9);
    CHECK(c.contacts == 1);
}

static void test_b737300_eye() {
    NEAR(fh::kEyeHeightFt, 13.2, 1e-9);
    NEAR(fh::kB737300EyeFwdOfMainM, 12.0, 1e-9);
    double lat = 0, lon = 0, alt = 0;
    fh::place_b737300_eye(40.64, -73.78, 13.0, 0.0, 31.0, 0.0, lat, lon, alt);
    NEAR(alt, 13.0 + 13.2, 1e-6); // parked: design eye AGL
    const double d = s376::geo::ground_distance_m(40.64, -73.78, lat, lon);
    NEAR(d, 12.0, 0.15); // cockpit is 12 m forward of the main gear
    double lat2 = 0, lon2 = 0, alt2 = 0;
    fh::place_b737300_eye(40.64, -73.78, 13.0, 200.0, 31.0, 2.5, lat2, lon2, alt2);
    CHECK(alt2 > 13.0 + 200.0 + 13.2); // pitch lever-arm raises the eye
    CHECK(alt2 < 13.0 + 200.0 + 13.2 + 3.0);
}

static void test_flight_model() {
    const auto rwy = fh::runway_from_faa("KJFK", "04R");
    fh::ManualFlight m(rwy, 6.0, 3.0);
    fh::Controls c; // trimmed defaults
    fh::Frame f;
    for (int i = 0; i < 60 * 60; ++i) // 60 s hands-off
        f = m.step(1.0 / 60, c);
    NEAR(f.gs_kt, 145, 3);
    // Still near the 3 deg path ~1.4 nm closer.
    const double b = s376::geo::deg2rad(rwy.brg_true);
    (void)b;
    {
        const double dist = s376::geo::ground_distance_m(rwy.thr_lat, rwy.thr_lon, f.lat, f.lon) * 3.28084;
        NEAR(f.wheel_agl_ft, fh::glidepath_height_ft(-dist, 3.0, fh::aim_point_ft(rwy, 3.0)), 40);
    }
    NEAR(f.roll, 0, 0.1);
    NEAR(f.hdg, rwy.brg_true, 0.1);
    // Speed stability: disturb, it returns.
    fh::Controls up = c; up.elevator = 0.3;
    for (int i = 0; i < 60; ++i) f = m.step(1.0 / 60, up); // +0.9 deg pitch
    for (int i = 0; i < 60 * 40; ++i) f = m.step(1.0 / 60, c);
    CHECK(f.gs_kt > 115 && f.gs_kt < 145); // slower, but not stalled/diverged
    // Bank and level out.
    fh::Controls rt = c; rt.aileron = 1;
    for (int i = 0; i < 120; ++i) f = m.step(1.0 / 60, rt);
    CHECK(f.roll > 10);
    for (int i = 0; i < 60 * 60; ++i) f = m.step(1.0 / 60, c);
    NEAR(f.roll, 0, 0.5);
    // On-runway reposition: stationary on the threshold.
    m.reposition(rwy, 0, 3.0, true);
    fh::Controls idle = c; idle.throttle = 0;
    f = m.step(0.1, idle);
    CHECK(f.on_ground && f.gs_kt < 0.5);
    {
        const double d = s376::geo::ground_distance_m(rwy.thr_lat, rwy.thr_lon, f.lat, f.lon);
        NEAR(d, fh::kB737300EyeFwdOfMainM, 0.4); // cockpit, not the main-gear truck
        NEAR(f.alt_ft, rwy.elev_ft + fh::kEyeHeightFt, 0.3);
    }
}

static void test_ios() {
    std::string ic, rw; double nm; bool onr;
    CHECK(fh::parse_preset("kjfk-04r-6nm", ic, rw, nm, onr) && ic == "kjfk" && rw == "04r" && !onr);
    NEAR(nm, 6, 1e-9);
    CHECK(fh::parse_preset("kjfk-13l-6sm", ic, rw, nm, onr));
    NEAR(nm, 5.2138, 1e-3);
    CHECK(fh::parse_preset("kjfk-13l-rwy", ic, rw, nm, onr) && onr);
    auto c = fh::parse_ios_query("/cmd?op=reposition&arg=kjfk-04r-6sm");
    CHECK(c.op == "reposition" && c.arg == "kjfk-04r-6sm");
    CHECK(fh::parse_ios_query("/cmd?op=wind&arg=040%2F10").arg == "040/10");
    CHECK(fh::parse_ios_query("/cmd?op=rm&arg=x").op.empty());
    // Live server on an ephemeral loopback port.
    fh::IosServer s;
    s.start("127.0.0.1:0");
    CHECK(s.port() != 0);
    std::thread cl([&] {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(s.port());
        inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
        if (connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0) {
            const char* req = "GET /cmd?op=tod&arg=19.5 HTTP/1.0\r\n\r\n";
            send(fd, req, strlen(req), 0);
            char b[512]; (void)recv(fd, b, sizeof(b), 0);
        }
        close(fd);
    });
    std::vector<fh::IosCommand> got;
    for (int i = 0; i < 200 && got.empty(); ++i) {
        got = s.poll(fh::IosStatus{});
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    cl.join();
    CHECK(got.size() == 1 && got[0].op == "tod" && got[0].arg == "19.5");
}

// Fly the 04R profile through the renderer's own HostReceiver (in-process,
// loopback, ephemeral ports). Off-DEM the IG reports elev 0, hat = MSL alt.
static void test_inprocess_e2e() {
    s376::SharedState st;
    s376::HostConfig hc;
    hc.bind = "127.0.0.1:23900";
    hc.reply_port = 23901;
    hc.reply_master = "";
    hc.hat_responder = true;
    s376::HostReceiver rx(st, hc);
    rx.start();
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in me{}; me.sin_family = AF_INET; me.sin_port = htons(23901);
    inet_pton(AF_INET, "127.0.0.1", &me.sin_addr);
    fh::guard_sockaddr(&me, sizeof(me), "bind");
    CHECK(bind(fd, reinterpret_cast<sockaddr*>(&me), sizeof(me)) == 0);
    timeval tv{0, 50000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    sockaddr_in ig = me; ig.sin_port = htons(23900);
    fh::ProfileConfig pc;
    pc.rwy = fh::runway_from_faa("KJFK", "04R");
    pc.start_nm = 0.5;
    pc.landing_config_at_start = true;
    fh::ApproachProfile ap(pc);
    fh::Checker chk;
    unsigned count = 0;
    while (!ap.done()) {
        auto f = ap.step(1.0 / 20);
        count = count % 32767 + 1;
        auto dg = fh::build_frame_datagram(f, count, fh::Environment{}, s376::xt4::WordOrder::Le,
                                           s376::xt4::FloatFmt::Vax);
        fh::guard_sockaddr(&ig, sizeof(ig), "send");
        sendto(fd, dg.data(), dg.size(), 0, reinterpret_cast<sockaddr*>(&ig), sizeof(ig));
        std::uint8_t b[256];
        const auto n = recv(fd, b, sizeof(b), 0);
        fh::HatReply h;
        if (n > 0 && fh::decode_hat_reply(b, static_cast<std::size_t>(n), h))
            chk.note(f, h, pc.rwy.elev_ft);
    }
    // Eyepoint reached the IG state.
    const auto hs = st.load();
    CHECK(hs.have_eyepoint);
    CHECK(hs.have_ownship);
    CHECK(hs.ownship.gear == 1);
    CHECK(hs.ownship.flaps == 30);
    NEAR(hs.eye.lat_deg, ap.config().rwy.thr_lat, 0.05);
    CHECK(chk.replies > 50);
    CHECK(chk.have_td);
    std::cerr << "  in-process: " << chk.summary() << "\n";
    CHECK(chk.touchdown_ok());
    // Contact path: put the eye below ground; the IG must report HAT <= 0.
    fh::Frame low;
    low.lat = pc.rwy.thr_lat; low.lon = pc.rwy.thr_lon; low.alt_ft = -50; low.hdg = 31;
    low.wheel_agl_ft = 30; // host thinks it is airborne
    auto dg = fh::build_frame_datagram(low, 9, fh::Environment{}, s376::xt4::WordOrder::Le,
                                       s376::xt4::FloatFmt::Vax);
    sendto(fd, dg.data(), dg.size(), 0, reinterpret_cast<sockaddr*>(&ig), sizeof(ig));
    std::uint8_t b[256];
    const auto n = recv(fd, b, sizeof(b), 0);
    fh::HatReply h;
    CHECK(n > 0 && fh::decode_hat_reply(b, static_cast<std::size_t>(n), h));
    chk.note(low, h, pc.rwy.elev_ft);
    CHECK(chk.contacts >= 1);
    close(fd);
    rx.stop();
}

// Real renderer binary, headless, loopback: s376-visual --headless
// --no-control --bind 127.0.0.1:2300 and s376-fakehost --check.
static void test_binary_e2e(const char* visual) {
    const std::string dir = std::string(visual).substr(0, std::string(visual).rfind('/'));
    const std::string fake = dir + "/s376-fakehost";
    pid_t pid = fork();
    if (pid == 0) {
        setenv("S376_ALLOW_LAN", "", 1);
        execl(visual, visual, "--headless", "--no-control", "--bind", "127.0.0.1:2300",
              "--duration", "60", (char*)nullptr);
        _exit(127);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    const std::string cmd = fake +
                            " --profile kjfk-04r --six-mile-final --start 1nm --fps 60 --fast --quiet"
                            " --check --tod 19 --vis-sm 5 2>&1";
    FILE* p = popen(cmd.c_str(), "r");
    std::string out;
    char line[512];
    while (p && fgets(line, sizeof(line), p))
        out += line;
    const int rc = p ? pclose(p) : -1;
    kill(pid, SIGTERM);
    waitpid(pid, nullptr, 0);
    std::cerr << "  binary e2e output tail:\n";
    const auto pos = out.size() > 600 ? out.size() - 600 : 0;
    std::cerr << out.substr(pos);
    CHECK(rc == 0);
    CHECK(out.find("TOUCHDOWN") != std::string::npos);
}

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);
    test_guard();
    test_distance();
    test_profile_math();
    test_replay();
    test_wire();
    test_b737300_eye();
    test_flight_model();
    test_ios();
    test_inprocess_e2e();
    if (argc > 1)
        test_binary_e2e(argv[1]);
    else
        std::cerr << "  (binary e2e skipped: pass path to s376-visual)\n";
    std::cerr << "fakehost tests: " << passes << " passed, " << fails << " failed\n";
    return fails ? 1 : 0;
}
