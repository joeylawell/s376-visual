#include "s376/synth.hpp"
#include "s376/geo.hpp"
#include "s376/log.hpp"
#include "s376/safety.hpp"

#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <thread>

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

namespace s376 {
namespace {

constexpr double kEyeHeightFt = 14.0;
constexpr double kEarthRft = 20925646.0;

double wrap360(double a) {
    a = std::fmod(a, 360.0);
    return a < 0 ? a + 360.0 : a;
}

void move_ned(double& lat, double& lon, double& alt, double n, double e, double d) {
    lat += geo::rad2deg(n / kEarthRft);
    const double coslat = std::max(std::cos(geo::deg2rad(lat)), 1e-6);
    lon += geo::rad2deg(e / (kEarthRft * coslat));
    lon = wrap360(lon + 180.0) - 180.0;
    alt -= d;
}

} // namespace

SyntheticFlight::SyntheticFlight(SynthConfig cfg)
    : cfg_(std::move(cfg)), lat_(cfg_.start_lat), lon_(cfg_.start_lon),
      alt_(cfg_.field_elev_ft + kEyeHeightFt), hdg_(cfg_.start_hdg), pitch_(0), roll_(0), v_(0),
      elev_(cfg_.field_elev_ft) {}

void SyntheticFlight::targets(double t, double& accel_kts, double& pitch, double& roll,
                              double& vs_fpm) const {
    if (t < 10) {
        accel_kts = 0;
        pitch = 0;
        roll = 0;
        vs_fpm = 0;
        return;
    }
    if (on_ground_ && v_ < 145) {
        accel_kts = 4;
        pitch = 0;
        roll = 0;
        vs_fpm = 0;
        return;
    }
    if (alt_ < elev_ + 3000) {
        accel_kts = v_ < 180 ? 1.0 : 0.0;
        pitch = 12.0;
        roll = (t > 90 && std::fabs(wrap360(hdg_ - 270)) > 5) ? 25.0 : 0.0;
        vs_fpm = 2200.0;
        return;
    }
    const bool turning = std::fabs(std::fmod(hdg_ - 270 + 180, 360) - 180) > 3;
    accel_kts = v_ < 220 ? 0.5 : 0.0;
    pitch = 2.5;
    roll = turning ? 25.0 : 0.0;
    vs_fpm = 0;
}

SynthPose SyntheticFlight::step(double dt) {
    double acc, tp, tr, tvs;
    targets(t_, acc, tp, tr, tvs);
    v_ = std::max(0.0, v_ + acc * dt);
    if (on_ground_ && v_ >= 145)
        on_ground_ = false;
    if (!on_ground_) {
        const double dp = std::max(-3 * dt, std::min(3 * dt, tp - pitch_));
        pitch_ += dp;
    } else {
        pitch_ = 0;
    }
    roll_ += std::max(-5 * dt, std::min(5 * dt, tr - roll_));
    const double vs_fps = on_ground_ ? 0.0 : tvs / 60.0;
    const double v_fps = v_ * 1.68781;
    const double turn_rate =
        v_ < 1 ? 0.0 : geo::rad2deg(32.174 * std::tan(geo::deg2rad(roll_)) / std::max(v_fps, 1.0));
    hdg_ = wrap360(hdg_ + turn_rate * dt);
    const double h = geo::deg2rad(hdg_);
    const double hs = std::sqrt(std::max(v_fps * v_fps - vs_fps * vs_fps, 0.0));
    move_ned(lat_, lon_, alt_, hs * std::cos(h) * dt, hs * std::sin(h) * dt, -vs_fps * dt);
    t_ += dt;
    count_ = count_ % 32767 + 1;
    SynthPose s;
    s.t = t_;
    s.lat_deg = lat_;
    s.lon_deg = lon_;
    s.alt_ft = alt_;
    s.hdg_true_deg = hdg_;
    s.pitch_deg = pitch_;
    s.roll_deg = roll_;
    s.gs_kt = v_;
    s.on_ground = on_ground_;
    s.count = count_;
    return s;
}

std::vector<std::uint8_t> SyntheticFlight::datagram(const SynthPose& s) const {
    std::vector<std::vector<std::uint16_t>> pk;
    pk.push_back(xt4::build_dcs(s.lat_deg, s.lon_deg, s.alt_ft, s.hdg_true_deg,
                                static_cast<float>(s.pitch_deg), static_cast<float>(s.roll_deg),
                                s.count, cfg_.float_fmt));
    if (cfg_.weather)
        pk.push_back(xt4::build_dcs(47.4200, -122.3078, 4500.0, 0, 0, 0, s.count, cfg_.float_fmt,
                                   xt4::kStormCsn0, -1, 1));
    if (cfg_.hat_queries)
        pk.push_back(xt4::build_hat_query());
    if (s.count % 20 == 1) {
        const std::uint16_t mask = s.alt_ft < elev_ + 2500 ? 0x0F : 0;
        pk.push_back(xt4::build_lights(true, !s.on_ground, mask, cfg_.float_fmt));
    }
    if (s.count % 100 == 1) {
        if (cfg_.weather)
            // Rain + wind + a changing lightning trigger so the IG flashes
            // on the type-124 edge, same field the host uses.
            pk.push_back(xt4::build_env(14.5f, 4000.f, 5, 4, 2016, cfg_.float_fmt, -1, 1, 6,
                                        1 + static_cast<int>(s.count / 100), 18.f, 200.f));
        else
            pk.push_back(xt4::build_env(14.5f, 10 * 6076.f, 5, 4, 2016, cfg_.float_fmt));
    }
    if (cfg_.weather && (s.count == 1 || s.count % 100 == 3)) {
        pk.push_back(xt4::build_fog(400.f, 2400.f, cfg_.float_fmt, -1, 0));
        pk.push_back(xt4::build_cloud(xt4::kTypeLowerCloud, 0, 2500.f, 6000.f, cfg_.float_fmt, -1,
                                     3, 8000.f, 18.f, 200.f));
        pk.push_back(xt4::build_cloud(xt4::kTypeUpperCloud, 1, 12000.f, 16000.f, cfg_.float_fmt));
        pk.push_back(xt4::build_top_cloud(2));
        pk.push_back(xt4::build_storm(0, true, xt4::kStormCsn0, 1, 7,
                                     2 + static_cast<int>(s.count / 100)));
    }
    if (s.count == 1)
        pk.push_back(xt4::build_display(true));
    if (s.count == 1 || s.count % 100 == 2) {
        pk.push_back(xt4::build_light_switch(1, 5)); // runway edge
        pk.push_back(xt4::build_light_switch(2, 5)); // centreline
        pk.push_back(xt4::build_light_switch(3, 5)); // approach
        pk.push_back(xt4::build_light_switch(4, 5)); // threshold / end
    }
    return xt4::build_datagram(pk, cfg_.word_order);
}

int run_synth_sender(const std::string& dest, const SynthConfig& cfg, double duration_s,
                     bool allow_sim_lan, bool allow_lan, std::atomic<bool>* stop) {
    safety::Endpoint ep;
    std::string err;
    if (!safety::parse_host_port(dest, ep, err))
        throw std::runtime_error(err);
    ep = safety::check_destination(ep.host, ep.port, "synth dest", allow_sim_lan, allow_lan);

#ifdef _WIN32
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#else
    int s = socket(AF_INET, SOCK_DGRAM, 0);
#endif
    if (s < 0)
        throw std::runtime_error("synth socket() failed");

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ep.port);
    if (inet_pton(AF_INET, ep.host.c_str(), &addr.sin_addr) != 1) {
#ifdef _WIN32
        closesocket(s);
        WSACleanup();
#else
        ::close(s);
#endif
        throw std::runtime_error("bad synth dest " + ep.host);
    }

    SyntheticFlight flight(cfg);
    const double dt = 1.0 / std::max(cfg.rate_hz, 1.0);
    const int nframes = duration_s < 0 ? 0 : static_cast<int>(duration_s * cfg.rate_hz + 0.5);
    int sent = 0;
    const bool forever = duration_s < 0;
    for (int i = 0; forever || i < nframes; ++i) {
        if (stop && stop->load(std::memory_order_relaxed))
            break;
        auto pose = flight.step(dt);
        auto dg = flight.datagram(pose);
#ifdef _WIN32
        sendto(s, reinterpret_cast<const char*>(dg.data()), static_cast<int>(dg.size()), 0,
               reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
#else
        sendto(s, dg.data(), dg.size(), 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
#endif
        ++sent;
        if (sent == 1 || (sent % std::max(1, static_cast<int>(cfg.rate_hz))) == 0) {
            S376_LOG_INFO("synth", "t=%.1f lat=%.5f lon=%.5f alt=%.0fft hdg=%.1f gs=%.0f%s", pose.t,
                          pose.lat_deg, pose.lon_deg, pose.alt_ft, pose.hdg_true_deg, pose.gs_kt,
                          pose.on_ground ? " gnd" : "");
        }
        std::this_thread::sleep_for(std::chrono::duration<double>(dt));
    }
#ifdef _WIN32
    closesocket(s);
    WSACleanup();
#else
    ::close(s);
#endif
    return sent;
}

} // namespace s376
