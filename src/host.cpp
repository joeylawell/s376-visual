#include "s376/host.hpp"
#include "s376/landmarks.hpp"
#include "s376/log.hpp"
#include "s376/safety.hpp"

#include <chrono>
#include <cstring>
#include <stdexcept>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace s376 {
namespace {

#ifdef _WIN32
using Sock = SOCKET;
constexpr Sock kInvalid = INVALID_SOCKET;
void close_sock(Sock s) {
    if (s != kInvalid)
        closesocket(s);
}
int last_err() {
    return WSAGetLastError();
}
#else
using Sock = int;
constexpr Sock kInvalid = -1;
void close_sock(Sock s) {
    if (s >= 0)
        ::close(s);
}
int last_err() {
    return errno;
}
#endif

Sock native(int fd) {
    return static_cast<Sock>(fd);
}

} // namespace

HostSocket::HostSocket() = default;

HostSocket::~HostSocket() {
    close();
}

void HostSocket::open(const HostConfig& cfg) {
    close();
    cfg_ = cfg;
#ifdef _WIN32
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        throw std::runtime_error("WSAStartup failed");
    wsa_ = true;
#endif
    safety::Endpoint ep;
    std::string err;
    if (!safety::parse_host_port(cfg.bind, ep, err))
        throw std::runtime_error(err);
    ep = safety::check_bind(ep.host, ep.port, "xt4 bind", cfg.allow_sim_lan, cfg.allow_lan);

#ifdef _WIN32
    Sock s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#else
    Sock s = socket(AF_INET, SOCK_DGRAM, 0);
#endif
    if (s == kInvalid)
        throw std::runtime_error("socket() failed");

    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
#ifdef SO_BROADCAST
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&yes), sizeof(yes));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ep.port);
    if (inet_pton(AF_INET, ep.host.c_str(), &addr.sin_addr) != 1) {
        close_sock(s);
        throw std::runtime_error("bad bind address " + ep.host);
    }
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        const int e = last_err();
        close_sock(s);
        throw std::runtime_error("bind " + ep.host + ":" + std::to_string(ep.port) + " failed (" +
                                 std::to_string(e) + ")");
    }
    fd_ = static_cast<int>(s);
    S376_LOG_INFO("visual", "listening on %s:%u (reply port %u)", ep.host.c_str(),
                  static_cast<unsigned>(ep.port), static_cast<unsigned>(cfg.reply_port));
}

void HostSocket::close() {
    if (fd_ >= 0) {
        close_sock(native(fd_));
        fd_ = -1;
    }
#ifdef _WIN32
    if (wsa_) {
        WSACleanup();
        wsa_ = false;
    }
#endif
}

bool HostSocket::recv(std::vector<std::uint8_t>& buf, std::string& peer_host,
                      std::uint16_t& peer_port, int timeout_ms) {
    if (fd_ < 0)
        return false;
    Sock s = native(fd_);
#ifdef _WIN32
    DWORD tv = static_cast<DWORD>(timeout_ms);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    buf.resize(2048);
    sockaddr_in from{};
#ifdef _WIN32
    int flen = sizeof(from);
    int n = recvfrom(s, reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()), 0,
                     reinterpret_cast<sockaddr*>(&from), &flen);
#else
    socklen_t flen = sizeof(from);
    const ssize_t n =
        recvfrom(s, buf.data(), buf.size(), 0, reinterpret_cast<sockaddr*>(&from), &flen);
#endif
    if (n <= 0)
        return false;
    buf.resize(static_cast<std::size_t>(n));
    char ip[64] = {};
    inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
    peer_host = ip;
    peer_port = ntohs(from.sin_port);
    if (log::packets()) {
        S376_LOG_DEBUG("visual", "rx %zu bytes from %s:%u\n%s", buf.size(), peer_host.c_str(),
                       static_cast<unsigned>(peer_port),
                       log::hexdump(buf.data(), buf.size(), 64).c_str());
    }
    return true;
}

void HostSocket::send_hat(const std::string& dest_host, std::uint16_t dest_port, float hat_ft,
                          float elev_ft, bool valid) {
    if (fd_ < 0)
        return;
    safety::Endpoint dest = safety::check_destination(dest_host, dest_port, "hat reply",
                                                      cfg_.allow_sim_lan, cfg_.allow_lan);
    auto pkt = xt4::build_hat_response(hat_ft, elev_ft, valid);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(dest.port);
    if (inet_pton(AF_INET, dest.host.c_str(), &addr.sin_addr) != 1)
        return;
    sendto(native(fd_), reinterpret_cast<const char*>(pkt.data()), static_cast<int>(pkt.size()), 0,
           reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
}

void HostReceiver::set_heights(std::shared_ptr<HeightSource> h) {
    heights_ = std::move(h);
    session_apply_ground(heights_);
}

HostReceiver::HostReceiver(SharedState& state, HostConfig cfg)
    : state_(state), cfg_(std::move(cfg)), dec_(cfg_.word_order, cfg_.float_fmt) {}

HostReceiver::~HostReceiver() {
    stop();
}

void HostReceiver::start() {
    sock_.open(cfg_);
    run_ = true;
    th_ = std::thread(&HostReceiver::loop, this);
}

void HostReceiver::stop() {
    run_ = false;
    if (th_.joinable())
        th_.join();
    sock_.close();
}

void HostReceiver::loop() {
    std::vector<std::uint8_t> buf;
    std::string peer;
    std::uint16_t pport = 0;
    while (run_) {
        if (!sock_.recv(buf, peer, pport, 200))
            continue;
        auto dg = dec_.parse(buf.data(), buf.size(), peer.c_str());
        if (!dg)
            continue;
        HostState snap;
        const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::steady_clock::now().time_since_epoch())
                             .count();
        state_.apply(*dg, dec_, peer + ":" + std::to_string(pport), static_cast<std::uint64_t>(now),
                     snap);
        if (cfg_.hat_responder) {
            for (const auto& p : dg->packets) {
                if (p.type != xt4::kTypeQuery)
                    continue;
                auto q = xt4::decode_query(p);
                if (q.query_type != xt4::kTypeHat)
                    continue;
                float elev = 0.f;
                float hat = 0.f;
                bool valid = snap.have_eyepoint;
                if (valid) {
                    tick_session_view(snap.eye.lat_deg, snap.eye.lon_deg);
                    const HatSample hs = sample_session_hat(
                        heights_.get(), snap.eye.lat_deg, snap.eye.lon_deg, snap.eye.alt_ft,
                        have_prev_eye_ ? &prev_lat_ : nullptr, have_prev_eye_ ? &prev_lon_ : nullptr,
                        have_prev_eye_ ? &prev_alt_ft_ : nullptr);
                    hat = hs.hat_ft;
                    elev = hs.elev_ft;
                    state_.set_hat(hat, elev);
                    prev_lat_ = snap.eye.lat_deg;
                    prev_lon_ = snap.eye.lon_deg;
                    prev_alt_ft_ = snap.eye.alt_ft;
                    have_prev_eye_ = true;
                }
                try {
                    std::string dest = peer;
                    std::uint16_t port = cfg_.reply_port;
                    if (!cfg_.reply_to.empty()) {
                        safety::Endpoint ep;
                        std::string err;
                        std::string spec = cfg_.reply_to;
                        if (spec.find(':') == std::string::npos)
                            spec += ":" + std::to_string(cfg_.reply_port);
                        if (safety::parse_host_port(spec, ep, err)) {
                            dest = ep.host;
                            if (ep.port)
                                port = ep.port;
                        }
                    }
                    sock_.send_hat(dest, port, hat, elev, valid);
                } catch (const safety::UnsafeEndpoint& ex) {
                    log::limited(log::Warn, "visual", "hat-unsafe", "%s", ex.what());
                }
            }
        }
    }
}

} // namespace s376
