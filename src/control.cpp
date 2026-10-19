#include "s376/control.hpp"

#include "s376/airport.hpp"
#include "s376/build_info.hpp"
#include "s376/diagnostics.hpp"
#include "s376/landmarks.hpp"
#include "s376/missiles.hpp"
#include "s376/log.hpp"
#include "s376/safety.hpp"
#include "s376/terrain.hpp"
#include "s376/version.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
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

double now_s() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
        ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        --b;
    return s.substr(a, b - a);
}

bool to_bool(const std::string& s) {
    return s == "1" || s == "true" || s == "yes" || s == "on";
}

#ifdef _WIN32
void ensure_wsa(bool& flag) {
    if (flag)
        return;
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        throw std::runtime_error("WSAStartup failed");
    flag = true;
}
#endif

Sock open_udp() {
#ifdef _WIN32
    return socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#else
    return socket(AF_INET, SOCK_DGRAM, 0);
#endif
}

sockaddr_in make_addr(const std::string& host, std::uint16_t port) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &a.sin_addr) != 1)
        throw std::runtime_error("bad address " + host);
    return a;
}

} // namespace

std::string ControlMsg::get(const char* key, const char* fallback) const {
    auto it = kv.find(key);
    return it == kv.end() ? std::string(fallback) : it->second;
}

void ControlMsg::set(const char* key, const std::string& value) {
    kv[key] = value;
}

void ControlMsg::set(const char* key, double value) {
    std::ostringstream o;
    o.setf(std::ios::fixed);
    o.precision(4);
    o << value;
    kv[key] = o.str();
}

void ControlMsg::set(const char* key, int value) {
    kv[key] = std::to_string(value);
}

bool ControlMsg::ok() const {
    return get("ok") == "1";
}

std::string encode_control(const ControlMsg& m) {
    std::ostringstream o;
    o << kControlMagic << "\n";
    o << "op=" << m.op << "\n";
    o << "id=" << m.id << "\n";
    for (const auto& e : m.kv) {
        if (e.first == "op" || e.first == "id")
            continue;
        o << e.first << "=" << e.second << "\n";
    }
    return o.str();
}

bool decode_control(const std::string& text, ControlMsg& out) {
    out = ControlMsg{};
    std::istringstream in(text);
    std::string line;
    bool magic = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        line = trim(line);
        if (line.empty())
            continue;
        if (!magic) {
            if (line != kControlMagic)
                return false;
            magic = true;
            continue;
        }
        auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string k = trim(line.substr(0, eq));
        const std::string v = trim(line.substr(eq + 1));
        if (k == "op")
            out.op = v;
        else if (k == "id") {
            try {
                out.id = static_cast<std::uint32_t>(std::stoul(v));
            } catch (...) {
            }
        } else {
            out.kv[k] = v;
        }
    }
    return magic && !out.op.empty();
}

ControlHub& control_hub() {
    static ControlHub h;
    return h;
}

void ControlHub::attach(const AppConfig& cfg, std::vector<ChannelConfig> selected) {
    std::lock_guard<std::mutex> g(mu_);
    cfg_ = cfg;
    chans_.clear();
    restart_ = false;
    if (selected.empty())
        selected = cfg.channels;
    for (auto& ch : selected) {
        ChannelLive live;
        live.cfg = ch;
        live.corr = identity_correction();
        live.corr.hfov_deg = static_cast<float>(ch.hfov_deg);
        live.corr.vfov_deg = static_cast<float>(ch.vfov_deg);
        if (!ch.pattern.empty())
            live.corr.pattern = parse_display_pattern(ch.pattern);
        std::string err;
        if (!ch.correction_file.empty())
            load_correction(ch.correction_file, live.corr, err);
        chans_.push_back(std::move(live));
    }
}

void ControlHub::note_frame(double t) {
    std::lock_guard<std::mutex> g(mu_);
    ++frames_;
    if (last_frame_s_ > 0 && t > last_frame_s_) {
        const double inst = 1.0 / (t - last_frame_s_);
        fps_ = fps_ > 0 ? (0.85 * fps_ + 0.15 * inst) : inst;
    }
    last_frame_s_ = t;
}

void ControlHub::note_host(const HostState& host, double t) {
    std::lock_guard<std::mutex> g(mu_);
    if (host.datagrams > 0 || host.have_eyepoint) {
        have_host_ = true;
        last_host_s_ = t;
    }
}

void ControlHub::note_error(const std::string& msg) {
    std::lock_guard<std::mutex> g(mu_);
    ++errors_;
    last_error_ = msg;
}

ChannelLive* ControlHub::find(const std::string& name) {
    if (name.empty() && chans_.size() == 1)
        return &chans_.front();
    for (auto& c : chans_) {
        if (c.cfg.name == name)
            return &c;
    }
    return nullptr;
}

void ControlHub::fill_status(ControlMsg& r) const {
    r.set("ok", "1");
    r.set("version", version());
    r.set("git", git_describe());
    r.set("build_date", build_date());
    const auto gpu = sample_gpu();
    r.set("gpu", gpu.name.empty() ? "unknown" : gpu.name);
    r.set("driver", gpu.driver);
    r.set("gpu_c", gpu.temp_c);
    r.set("vram_mb", gpu.vram_mb);
    r.set("ram_mb", static_cast<double>(current_rss_bytes()) / (1024.0 * 1024.0));
    r.set("fps", fps_);
    r.set("errors", errors_);
    r.set("last_error", last_error_);
    r.set("airports", loaded_airport_list(cfg_.render.cache_dir));
    r.set("era", skyline_era_name(session_era()));
    r.set("era_unlocked", session_era_unlocked() ? 1 : 0);
    r.set("missiles", session_missiles() ? 1 : 0);
    r.set("log_file", log::session_file() ? log::session_file() : "");
    const double t = now_s();
    r.set("host_age_ms", have_host_ ? (t - last_host_s_) * 1000.0 : -1.0);
    r.set("n", static_cast<int>(chans_.size()));
    for (std::size_t i = 0; i < chans_.size(); ++i) {
        const auto& c = chans_[i];
        const std::string p = "ch." + std::to_string(i) + ".";
        r.set((p + "name").c_str(), c.cfg.name);
        r.set((p + "hdg").c_str(), c.cfg.heading_offset_deg);
        r.set((p + "hfov").c_str(), c.cfg.hfov_deg);
        r.set((p + "vfov").c_str(), c.cfg.vfov_deg);
        r.set((p + "flip").c_str(), c.cfg.flip_vertical ? 1 : 0);
        r.set((p + "width").c_str(), c.cfg.width);
        r.set((p + "height").c_str(), c.cfg.height);
        r.set((p + "resolution").c_str(), format_resolution(c.cfg.width, c.cfg.height));
        r.set((p + "pattern").c_str(), display_pattern_name(c.corr.pattern));
        r.set((p + "gamma").c_str(), c.corr.gamma);
        r.set((p + "blend_ramp").c_str(), c.corr.blend.ramp);
        r.set((p + "blend_l").c_str(), c.corr.blend.left_px);
        r.set((p + "blend_r").c_str(), c.corr.blend.right_px);
        r.set((p + "blend_t").c_str(), c.corr.blend.top_px);
        r.set((p + "blend_b").c_str(), c.corr.blend.bottom_px);
        r.set((p + "corr").c_str(), c.cfg.correction_file);
    }
}

void ControlHub::apply_set(const ControlMsg& req) {
    const std::string want = req.get("channel");
    auto apply_one = [&](ChannelLive& c) {
        auto num = [&](const char* k, double& dst) {
            const std::string v = req.get(k);
            if (v.empty())
                return;
            try {
                dst = std::stod(v);
            } catch (...) {
            }
        };
        num("hdg", c.cfg.heading_offset_deg);
        num("hfov", c.cfg.hfov_deg);
        num("vfov", c.cfg.vfov_deg);
        if (!req.get("flip").empty())
            c.cfg.flip_vertical = to_bool(req.get("flip"));
        if (!req.get("resolution").empty()) {
            int w = c.cfg.width, h = c.cfg.height;
            if (parse_resolution(req.get("resolution"), w, h)) {
                c.cfg.width = w;
                c.cfg.height = h;
            }
        }
        if (!req.get("width").empty()) {
            try {
                c.cfg.width = std::max(8, std::stoi(req.get("width")));
            } catch (...) {
            }
        }
        if (!req.get("height").empty()) {
            try {
                c.cfg.height = std::max(8, std::stoi(req.get("height")));
            } catch (...) {
            }
        }
        if (!req.get("pattern").empty()) {
            c.cfg.pattern = req.get("pattern");
            c.corr.pattern = parse_display_pattern(c.cfg.pattern);
        }
        if (!req.get("gamma").empty())
            c.corr.gamma = static_cast<float>(std::stod(req.get("gamma")));
        if (!req.get("blend_ramp").empty())
            c.corr.blend.ramp = static_cast<float>(std::stod(req.get("blend_ramp")));
        if (!req.get("blend_l").empty())
            c.corr.blend.left_px = static_cast<float>(std::stod(req.get("blend_l")));
        if (!req.get("blend_r").empty())
            c.corr.blend.right_px = static_cast<float>(std::stod(req.get("blend_r")));
        if (!req.get("blend_t").empty())
            c.corr.blend.top_px = static_cast<float>(std::stod(req.get("blend_t")));
        if (!req.get("blend_b").empty())
            c.corr.blend.bottom_px = static_cast<float>(std::stod(req.get("blend_b")));
        c.corr.hfov_deg = static_cast<float>(c.cfg.hfov_deg);
        c.corr.vfov_deg = static_cast<float>(c.cfg.vfov_deg);
        for (int row = 0; row < kWarpRows; ++row) {
            for (int col = 0; col < kWarpCols; ++col) {
                const std::string key =
                    "warp." + std::to_string(row) + "." + std::to_string(col);
                const std::string v = req.get(key.c_str());
                if (v.empty())
                    continue;
                float h = warp_h_deg(col), p = warp_v_deg(row);
                std::sscanf(v.c_str(), "%f , %f", &h, &p);
                c.corr.warp[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)].src_h_deg =
                    h;
                c.corr.warp[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)].src_v_deg =
                    p;
            }
        }
    };
    if (want.empty() || want == "all") {
        for (auto& c : chans_)
            apply_one(c);
        return;
    }
    if (auto* c = find(want))
        apply_one(*c);
}

bool ControlHub::save_display(std::string& err) {
    for (auto& c : chans_) {
        if (c.cfg.correction_file.empty())
            c.cfg.correction_file = "data/display/correction-" + c.cfg.name + ".toml";
        if (!save_correction(c.cfg.correction_file, c.corr, err))
            return false;
        if (!cfg_.control.bind.empty()) {
            // Best-effort: remember the visual config path via save_dir_.
        }
    }
    if (!save_dir_.empty()) {
        for (const auto& c : chans_) {
            if (!patch_channel_display(save_dir_, c.cfg, err))
                return false;
        }
    }
    return true;
}

ControlMsg ControlHub::handle(const ControlMsg& req) {
    ControlMsg r;
    r.id = req.id;
    r.op = req.op.empty() ? "reply" : req.op;
    std::lock_guard<std::mutex> g(mu_);
    if (req.op == "ping") {
        r.set("ok", "1");
        return r;
    }
    if (req.op == "status") {
        fill_status(r);
        return r;
    }
    if (req.op == "set") {
        apply_set(req);
        fill_status(r);
        return r;
    }
    if (req.op == "save") {
        if (!req.get("config").empty())
            save_dir_ = req.get("config");
        std::string err;
        if (!save_display(err)) {
            r.set("ok", "0");
            r.set("err", err);
            return r;
        }
        r.set("ok", "1");
        return r;
    }
    if (req.op == "refresh") {
        // Channel reset: clear the Classic egg, then ask main to exit so
        // the wizard can close and reopen the process.
        refresh_channels();
        restart_ = true;
        r.set("ok", "1");
        r.set("restart", "1");
        return r;
    }
    if (req.op == "unlock") {
        set_session_era(session_era(), true);
        r.set("ok", "1");
        r.set("era", skyline_era_name(session_era()));
        r.set("era_unlocked", 1);
        return r;
    }
    if (req.op == "set_missiles") {
        set_session_missiles(req.get("on") == "1" || req.get("on") == "true");
        r.set("ok", "1");
        r.set("missiles", session_missiles() ? 1 : 0);
        return r;
    }
    if (req.op == "set_era") {
        try {
            const auto e = parse_skyline_era(req.get("era", "current"));
            if (!session_era_unlocked() && e == SkylineEra::Classic) {
                r.set("ok", "0");
                r.set("err", "locked");
                return r;
            }
            set_session_era(e, true);
            r.set("ok", "1");
            r.set("era", skyline_era_name(session_era()));
        } catch (const std::exception& ex) {
            r.set("ok", "0");
            r.set("err", ex.what());
        }
        return r;
    }
    if (req.op == "era_click") {
        std::int64_t t = 0;
        try {
            t = static_cast<std::int64_t>(std::stoll(req.get("t_ms", "0")));
        } catch (...) {
        }
        if (t <= 0) {
            t = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count();
        }
        unlock_.click(t);
        if (unlock_.unlocked)
            set_session_era(session_era(), true);
        r.set("ok", "1");
        r.set("toast", unlock_.toast);
        r.set("unlocked", unlock_.unlocked ? 1 : 0);
        return r;
    }
    if (req.op == "logs") {
        int n = 80;
        try {
            if (!req.get("n").empty())
                n = std::stoi(req.get("n"));
        } catch (...) {
        }
        r.set("ok", "1");
        r.set("text", log::recent_lines(n));
        return r;
    }
    r.set("ok", "0");
    r.set("err", "unknown op");
    return r;
}

ControlStatus ControlHub::snapshot() const {
    ControlMsg m;
    {
        std::lock_guard<std::mutex> g(mu_);
        fill_status(m);
    }
    ControlStatus s;
    s.version = m.get("version");
    s.git = m.get("git");
    s.build_date = m.get("build_date");
    s.gpu = m.get("gpu");
    s.driver = m.get("driver");
    s.airports = m.get("airports");
    s.era = m.get("era");
    s.era_unlocked = m.get("era_unlocked") == "1";
    s.missiles = m.get("missiles") == "1";
    s.log_file = m.get("log_file");
    s.errors = errors_;
    s.last_error = last_error_;
    std::lock_guard<std::mutex> g(mu_);
    s.channels = chans_;
    for (auto& c : s.channels) {
        c.fps = fps_;
        try {
            c.gpu_temp_c = std::stof(m.get("gpu_c", "-1"));
            c.vram_mb = std::stof(m.get("vram_mb", "-1"));
            c.ram_mb = std::stof(m.get("ram_mb", "-1"));
            c.host_age_ms = std::stod(m.get("host_age_ms", "-1"));
        } catch (...) {
        }
        c.errors = errors_;
        c.last_error = last_error_;
    }
    return s;
}

std::vector<ChannelConfig> ControlHub::live_channels() const {
    std::lock_guard<std::mutex> g(mu_);
    std::vector<ChannelConfig> out;
    for (const auto& c : chans_)
        out.push_back(c.cfg);
    return out;
}

bool ControlHub::restart_requested() const {
    std::lock_guard<std::mutex> g(mu_);
    return restart_;
}

void ControlHub::clear_restart() {
    std::lock_guard<std::mutex> g(mu_);
    restart_ = false;
}

void ControlHub::reset() {
    std::lock_guard<std::mutex> g(mu_);
    cfg_ = AppConfig{};
    chans_.clear();
    last_frame_s_ = 0;
    fps_ = 0;
    frames_ = 0;
    last_host_s_ = 0;
    have_host_ = false;
    errors_ = 0;
    last_error_.clear();
    restart_ = false;
    save_dir_.clear();
    unlock_ = EraUnlock{};
}

ControlServer::ControlServer() = default;

ControlServer::~ControlServer() {
    stop();
}

void ControlServer::start(const std::string& spec, bool allow_sim_lan, bool allow_lan) {
    stop();
#ifdef _WIN32
    ensure_wsa(wsa_);
#endif
    safety::Endpoint ep;
    std::string err;
    if (!safety::parse_host_port(spec, ep, err))
        throw std::runtime_error(err);
    ep = safety::check_bind(ep.host, ep.port, "control bind", allow_sim_lan, allow_lan);
    Sock s = open_udp();
    if (s == kInvalid)
        throw std::runtime_error("control socket() failed");
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
    sockaddr_in addr = make_addr(ep.host, ep.port);
    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        const int e = last_err();
        close_sock(s);
        throw std::runtime_error("control bind " + ep.host + ":" + std::to_string(ep.port) +
                                 " failed (" + std::to_string(e) + ")");
    }
    sockaddr_in got{};
#ifdef _WIN32
    int glen = sizeof(got);
#else
    socklen_t glen = sizeof(got);
#endif
    if (getsockname(s, reinterpret_cast<sockaddr*>(&got), &glen) == 0)
        port_ = ntohs(got.sin_port);
    else
        port_ = ep.port;
    fd_ = static_cast<int>(s);
    run_ = true;
    th_ = std::thread([this] { loop(); });
    S376_LOG_INFO("wizard", "control listening on %s:%u (not xT4)", ep.host.c_str(),
                  static_cast<unsigned>(port_));
}

void ControlServer::stop() {
    run_ = false;
    if (fd_ >= 0) {
        close_sock(static_cast<Sock>(fd_));
        fd_ = -1;
    }
    if (th_.joinable())
        th_.join();
#ifdef _WIN32
    if (wsa_) {
        WSACleanup();
        wsa_ = false;
    }
#endif
}

void ControlServer::loop() {
    char buf[8192];
    while (run_) {
        fd_set rf;
        FD_ZERO(&rf);
        FD_SET(static_cast<Sock>(fd_), &rf);
        timeval tv{};
        tv.tv_usec = 200000;
        const int n = select(fd_ + 1, &rf, nullptr, nullptr, &tv);
        if (n <= 0 || !run_)
            continue;
        sockaddr_in from{};
#ifdef _WIN32
        int flen = sizeof(from);
        const int got =
            recvfrom(static_cast<Sock>(fd_), buf, sizeof(buf) - 1, 0,
                     reinterpret_cast<sockaddr*>(&from), &flen);
#else
        socklen_t flen = sizeof(from);
        const int got = static_cast<int>(recvfrom(fd_, buf, sizeof(buf) - 1, 0,
                                                  reinterpret_cast<sockaddr*>(&from), &flen));
#endif
        if (got <= 0)
            continue;
        buf[got] = 0;
        ControlMsg req;
        if (!decode_control(std::string(buf, static_cast<std::size_t>(got)), req))
            continue;
        const ControlMsg reply = control_hub().handle(req);
        const std::string out = encode_control(reply);
#ifdef _WIN32
        sendto(static_cast<Sock>(fd_), out.data(), static_cast<int>(out.size()), 0,
               reinterpret_cast<sockaddr*>(&from), flen);
#else
        sendto(fd_, out.data(), out.size(), 0, reinterpret_cast<sockaddr*>(&from), flen);
#endif
    }
}

ControlMsg control_request(const std::string& dest, const ControlMsg& req, int timeout_ms,
                           bool allow_sim_lan, bool allow_lan) {
    safety::Endpoint ep;
    std::string err;
    if (!safety::parse_host_port(dest, ep, err))
        throw std::runtime_error(err);
    ep = safety::check_destination(ep.host, ep.port, "control", allow_sim_lan, allow_lan);

#ifdef _WIN32
    bool wsa = false;
    ensure_wsa(wsa);
#endif
    Sock s = open_udp();
    if (s == kInvalid)
        throw std::runtime_error("control client socket() failed");
    const auto addr = make_addr(ep.host, ep.port);
    const std::string wire = encode_control(req);
#ifdef _WIN32
    sendto(s, wire.data(), static_cast<int>(wire.size()), 0, reinterpret_cast<const sockaddr*>(&addr),
           sizeof(addr));
    DWORD ms = timeout_ms > 0 ? static_cast<DWORD>(timeout_ms) : 250;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));
    char buf[8192];
    const int got = recvfrom(s, buf, sizeof(buf) - 1, 0, nullptr, nullptr);
    close_sock(s);
    if (wsa)
        WSACleanup();
    if (got <= 0)
        throw std::runtime_error("control timeout " + dest);
    buf[got] = 0;
    ControlMsg out;
    if (!decode_control(std::string(buf, static_cast<std::size_t>(got)), out))
        throw std::runtime_error("bad control reply");
    return out;
#else
    sendto(s, wire.data(), wire.size(), 0, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    char buf[8192];
    const int got = static_cast<int>(recvfrom(s, buf, sizeof(buf) - 1, 0, nullptr, nullptr));
    close_sock(s);
    if (got <= 0)
        throw std::runtime_error("control timeout " + dest);
    buf[got] = 0;
    ControlMsg out;
    if (!decode_control(std::string(buf, static_cast<std::size_t>(got)), out))
        throw std::runtime_error("bad control reply");
    return out;
#endif
}

GpuSample sample_gpu() {
    GpuSample g;
#ifndef _WIN32
    FILE* f = popen("nvidia-smi --query-gpu=name,driver_version,temperature.gpu,memory.used "
                    "--format=csv,noheader,nounits 2>/dev/null",
                    "r");
    if (f) {
        char line[256];
        if (fgets(line, sizeof(line), f)) {
            std::string s = line;
            // name, driver, temp, mem
            std::vector<std::string> parts;
            std::string cur;
            for (char c : s) {
                if (c == ',') {
                    parts.push_back(trim(cur));
                    cur.clear();
                } else if (c != '\n') {
                    cur += c;
                }
            }
            if (!cur.empty())
                parts.push_back(trim(cur));
            if (parts.size() >= 1)
                g.name = parts[0];
            if (parts.size() >= 2)
                g.driver = parts[1];
            if (parts.size() >= 3) {
                try {
                    g.temp_c = std::stof(parts[2]);
                } catch (...) {
                }
            }
            if (parts.size() >= 4) {
                try {
                    g.vram_mb = std::stof(parts[3]);
                } catch (...) {
                }
            }
        }
        pclose(f);
    }
#endif
    return g;
}

std::string loaded_airport_list(const std::string& cache_dir) {
    const auto packs = scan_airport_caches("cache", cache_dir);
    std::string out;
    for (const auto& p : packs) {
        if (p.tiles == 0 && p.bytes == 0)
            continue;
        if (!out.empty())
            out += ",";
        out += p.icao;
    }
    if (out.empty()) {
        for (const auto& a : all_public_airports()) {
            if (!out.empty())
                out += ",";
            out += a.icao;
            if (out.size() > 48)
                break;
        }
    }
    return out;
}

} // namespace s376
