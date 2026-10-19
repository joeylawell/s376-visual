#include "s376/host.hpp"
#include "s376/synth.hpp"
#include "s376/terrain.hpp"
#include "s376/xt4.hpp"

#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

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

void test_hat() {
    using namespace s376;
    auto dem = std::make_shared<DemGrid>(DemGrid::synth_ksea());
    auto src = std::make_shared<DemHeightSource>(dem);
    const double lat = 47.463684;
    const double lon = -122.307751;
    const double alt = 447.0; // field 433 + 14 ft eye
    const HatSample hs = sample_hat(src.get(), lat, lon, alt);
    CHECK(hs.covered);
    CHECK_NEAR(hs.elev_ft, 433.0, 8.0);
    CHECK_NEAR(hs.hat_ft, alt - hs.elev_ft, 0.05);
    CHECK(hs.hat_ft > 5.f && hs.hat_ft < 30.f);

    auto pkt = xt4::build_hat_response(hs.hat_ft, hs.elev_ft, true);
    int valid = 0;
    float hat = 0, elev = 0;
    CHECK(xt4::parse_hat_as_host(pkt.data(), pkt.size(), valid, hat, elev));
    CHECK(valid == 1);
    CHECK_NEAR(hat, hs.hat_ft, 0.1);
    CHECK_NEAR(elev, hs.elev_ft, 0.1);

    // Live UDP: IG replies on 18771 with DEM-sampled HAT. Loopback only.
    HostConfig hc;
    hc.bind = "127.0.0.1:18770";
    hc.reply_port = 18771;
    hc.hat_responder = true;
    hc.allow_sim_lan = false;
    SharedState st;
    HostReceiver rx(st, hc);
    rx.set_heights(src);
    rx.start();

#ifdef _WIN32
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET listen = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    SOCKET send = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#else
    int listen = ::socket(AF_INET, SOCK_DGRAM, 0);
    int send = ::socket(AF_INET, SOCK_DGRAM, 0);
#endif
    sockaddr_in laddr{};
    laddr.sin_family = AF_INET;
    laddr.sin_port = htons(18771);
    inet_pton(AF_INET, "127.0.0.1", &laddr.sin_addr);
    const int on = 1;
#ifdef _WIN32
    setsockopt(listen, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&on), sizeof(on));
#else
    setsockopt(listen, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
#endif
    CHECK(bind(listen, reinterpret_cast<sockaddr*>(&laddr), sizeof(laddr)) == 0);
#ifdef _WIN32
    DWORD tv = 400;
    setsockopt(listen, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = 400000;
    setsockopt(listen, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    std::vector<std::vector<std::uint16_t>> pk;
    pk.push_back(xt4::build_dcs(lat, lon, alt, 180.34f, 0, 0, 1, xt4::FloatFmt::Ieee));
    pk.push_back(xt4::build_hat_query());
    auto dg = xt4::build_datagram(pk, xt4::WordOrder::Be);

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(18770);
    inet_pton(AF_INET, "127.0.0.1", &dest.sin_addr);
#ifdef _WIN32
    sendto(send, reinterpret_cast<const char*>(dg.data()), static_cast<int>(dg.size()), 0,
           reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
    char buf[128];
    int n = recv(listen, buf, sizeof(buf), 0);
#else
    sendto(send, dg.data(), dg.size(), 0, reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
    std::uint8_t buf[128];
    const ssize_t n = recv(listen, buf, sizeof(buf), 0);
#endif
    CHECK(n == 64);
    if (n == 64) {
        int v = 0;
        float rh = 0, re = 0;
        CHECK(xt4::parse_hat_as_host(reinterpret_cast<const std::uint8_t*>(buf), 64, v, rh, re));
        CHECK(v == 1);
        CHECK_NEAR(re, hs.elev_ft, 1.0);
        CHECK_NEAR(rh, hs.hat_ft, 1.0);
        CHECK(re > 400.f && re < 460.f);
        CHECK(rh > 5.f && rh < 30.f);
    }

    rx.stop();
#ifdef _WIN32
    closesocket(listen);
    closesocket(send);
    WSACleanup();
#else
    ::close(listen);
    ::close(send);
#endif

    // Multi-channel: only the reply_master HostReceiver sends type 200.
    {
#ifdef _WIN32
        WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
        HostConfig master;
        master.bind = "127.0.0.1:18880";
        master.reply_port = 18881;
        master.hat_responder = true;
        master.reply_master = "mid_left";
        master.allow_sim_lan = false;
        HostConfig slave = master;
        slave.bind = "127.0.0.1:18882";
        slave.hat_responder = false;
        SharedState st_m, st_s;
        HostReceiver rx_m(st_m, master);
        HostReceiver rx_s(st_s, slave);
        rx_m.set_heights(src);
        rx_s.set_heights(src);
        rx_m.start();
        rx_s.start();

#ifdef _WIN32
        SOCKET listen2 = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        SOCKET send2 = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#else
        int listen2 = ::socket(AF_INET, SOCK_DGRAM, 0);
        int send2 = ::socket(AF_INET, SOCK_DGRAM, 0);
#endif
        sockaddr_in l2{};
        l2.sin_family = AF_INET;
        l2.sin_port = htons(18881);
        inet_pton(AF_INET, "127.0.0.1", &l2.sin_addr);
#ifdef _WIN32
        setsockopt(listen2, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&on),
                   sizeof(on));
#else
        setsockopt(listen2, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
#endif
        CHECK(bind(listen2, reinterpret_cast<sockaddr*>(&l2), sizeof(l2)) == 0);
#ifdef _WIN32
        DWORD tv2 = 300;
        setsockopt(listen2, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv2),
                   sizeof(tv2));
#else
        timeval tv2{};
        tv2.tv_sec = 0;
        tv2.tv_usec = 300000;
        setsockopt(listen2, SOL_SOCKET, SO_RCVTIMEO, &tv2, sizeof(tv2));
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        std::vector<std::vector<std::uint16_t>> pk2;
        pk2.push_back(xt4::build_dcs(lat, lon, alt, 180.34f, 0, 0, 2, xt4::FloatFmt::Ieee));
        pk2.push_back(xt4::build_hat_query());
        auto dg2 = xt4::build_datagram(pk2, xt4::WordOrder::Be);
        auto send_one = [&](std::uint16_t port) {
            sockaddr_in dest{};
            dest.sin_family = AF_INET;
            dest.sin_port = htons(port);
            inet_pton(AF_INET, "127.0.0.1", &dest.sin_addr);
#ifdef _WIN32
            sendto(send2, reinterpret_cast<const char*>(dg2.data()), static_cast<int>(dg2.size()), 0,
                   reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
#else
            sendto(send2, dg2.data(), dg2.size(), 0, reinterpret_cast<sockaddr*>(&dest),
                   sizeof(dest));
#endif
        };
        send_one(18880);
        send_one(18882);
        int replies = 0;
        for (int i = 0; i < 4; ++i) {
#ifdef _WIN32
            char b2[128];
            int n2 = recv(listen2, b2, sizeof(b2), 0);
#else
            std::uint8_t b2[128];
            const ssize_t n2 = recv(listen2, b2, sizeof(b2), 0);
#endif
            if (n2 == 64) {
                int v = 0;
                float rh = 0, re = 0;
                if (xt4::parse_hat_as_host(reinterpret_cast<const std::uint8_t*>(b2), 64, v, rh, re) &&
                    v == 1)
                    ++replies;
            }
        }
        CHECK(replies == 1);
        rx_m.stop();
        rx_s.stop();
#ifdef _WIN32
        closesocket(listen2);
        closesocket(send2);
        WSACleanup();
#else
        ::close(listen2);
        ::close(send2);
#endif
    }
}
