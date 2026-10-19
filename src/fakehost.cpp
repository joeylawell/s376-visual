// s376-fakehost core: loopback guard, 737-300 approach profile, trajectory
// replay, simple manual dynamics, datagram build and HAT checks.
// Wire format is the existing clean-room xT4 subset (docs/PROTOCOL.md) built
// with the same xt4:: builders s376-synth uses. No new packet types.

#include "s376/fakehost.hpp"

#include "s376/geo.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace s376::fakehost {

namespace {

constexpr double kFtPerNm = 6076.115;
constexpr double kFtPerSm = 5280.0;
constexpr double kKtToFps = 1.687810;

std::string lower(std::string s) {
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return "";
    const auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool parse_v4(const std::string& h, std::uint32_t& out) {
    unsigned a, b, c, d;
    char tail;
    if (std::sscanf(h.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4)
        return false;
    if (a > 255 || b > 255 || c > 255 || d > 255)
        return false;
    out = (a << 24) | (b << 16) | (c << 8) | d;
    return true;
}

void offset_ft(double& lat, double& lon, double north_ft, double east_ft) {
    const double dist_m = std::hypot(north_ft, east_ft) * geo::kFtToM;
    if (!(dist_m > 1e-9))
        return;
    const double hdg = geo::rad2deg(std::atan2(east_ft, north_ft));
    geo::direct_wgs84(lat, lon, hdg, dist_m, lat, lon);
}

double clampd(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }
double approach(double cur, double tgt, double rate, double dt) {
    return cur + clampd(tgt - cur, -rate * dt, rate * dt);
}

} // namespace

// ================================================================= guard

bool is_loopback_literal(const std::string& host_in) {
    std::string h = lower(trim(host_in));
    if (h.size() > 1 && h.front() == '[' && h.back() == ']')
        h = h.substr(1, h.size() - 2);
    if (h == "localhost" || h == "::1" || h == "0:0:0:0:0:0:0:1")
        return true;
    if (h.rfind("::ffff:", 0) == 0)
        h = h.substr(7);
    std::uint32_t a = 0;
    if (!parse_v4(h, a))
        return false;
    return (a >> 24) == 127;
}

void guard(const std::string& host, std::uint16_t port, const char* purpose) {
    if (is_loopback_literal(host))
        return;
    std::string why = "not a loopback address";
    std::uint32_t a = 0;
    std::string h = lower(trim(host));
    if (h.rfind("::ffff:", 0) == 0)
        h = h.substr(7);
    if (parse_v4(h, a)) {
        if ((a & 0xFFFFFF00u) == 0x0A015F00u)
            why = "10.1.95.0/24 is the simulator visual LAN";
        else if ((a & 0xFFFFFF00u) == 0xC0A8AD00u)
            why = "192.168.173.0/24 is the simulator LAN";
        else if (a == 0)
            why = "wildcard bind would expose the fake host off-box";
    } else if (h == "::" || h.empty()) {
        why = "wildcard bind would expose the fake host off-box";
    }
    if (port == 3340 || port == 3341)
        why += "; ports 3340/3341 are host-LAN ports and allowed only on loopback";
    throw Refused(std::string("s376-fakehost refuses ") + purpose + " " + host + ":" +
                  std::to_string(port) + " (" + why + "). Loopback only, no override.");
}

void guard_sockaddr(const void* sa_v, unsigned salen, const char* purpose) {
    const auto* sa = static_cast<const sockaddr*>(sa_v);
    if (!sa)
        throw Refused(std::string(purpose) + ": null address");
    char buf[INET6_ADDRSTRLEN] = {};
    std::uint16_t port = 0;
    if (sa->sa_family == AF_INET && salen >= sizeof(sockaddr_in)) {
        const auto* s4 = reinterpret_cast<const sockaddr_in*>(sa);
        inet_ntop(AF_INET, &s4->sin_addr, buf, sizeof(buf));
        port = ntohs(s4->sin_port);
    } else if (sa->sa_family == AF_INET6 && salen >= sizeof(sockaddr_in6)) {
        const auto* s6 = reinterpret_cast<const sockaddr_in6*>(sa);
        inet_ntop(AF_INET6, &s6->sin6_addr, buf, sizeof(buf));
        port = ntohs(s6->sin6_port);
    } else {
        throw Refused(std::string(purpose) + ": unsupported address family");
    }
    const std::string h = buf;
    if (h == "localhost") // never produced by inet_ntop, but be strict
        throw Refused(purpose);
    guard(h, port, purpose);
}

void parse_guarded(const std::string& spec, std::string& host, std::uint16_t& port,
                   const char* purpose) {
    std::string h, p;
    if (!spec.empty() && spec[0] == '[') {
        const auto rb = spec.find(']');
        if (rb == std::string::npos)
            throw Refused(std::string(purpose) + ": bad address " + spec);
        h = spec.substr(1, rb - 1);
        if (rb + 1 < spec.size() && spec[rb + 1] == ':')
            p = spec.substr(rb + 2);
    } else {
        const auto c = spec.rfind(':');
        if (c == std::string::npos || spec.find(':') != c) {
            h = spec; // bare host or bare IPv6
        } else {
            h = spec.substr(0, c);
            p = spec.substr(c + 1);
        }
    }
    unsigned long pv = p.empty() ? 0 : std::strtoul(p.c_str(), nullptr, 10);
    if (pv > 65535)
        throw Refused(std::string(purpose) + ": bad port " + p);
    port = static_cast<std::uint16_t>(pv);
    if (lower(h) == "localhost")
        h = "127.0.0.1";
    guard(h, port, purpose);
    host = h;
}

// ================================================================= distance

double parse_distance_nm(const std::string& s_in) {
    const std::string s = lower(trim(s_in));
    std::size_t used = 0;
    double v = 0;
    try {
        v = std::stod(s, &used);
    } catch (...) {
        throw std::invalid_argument("bad distance '" + s_in + "' (use e.g. 6nm, 6sm, 10)");
    }
    const std::string u = trim(s.substr(used));
    if (v <= 0)
        throw std::invalid_argument("distance must be positive: " + s_in);
    if (u.empty() || u == "nm" || u == "nmi")
        return v;
    if (u == "sm" || u == "mi" || u == "mile" || u == "miles")
        return v * kFtPerSm / kFtPerNm;
    if (u == "km")
        return v * 1000.0 / 1852.0;
    if (u == "m")
        return v / 1852.0;
    if (u == "ft")
        return v / kFtPerNm;
    throw std::invalid_argument("unknown distance unit '" + u + "' in " + s_in);
}

// ================================================================= profile

double glidepath_height_ft(double x_ft, double gs_deg, double aim_ft) {
    return std::max(0.0, (aim_ft - x_ft) * std::tan(geo::deg2rad(gs_deg)));
}

double threshold_crossing_height_ft(double gs_deg, double aim_ft) {
    return glidepath_height_ft(0.0, gs_deg, aim_ft);
}

double aim_point_ft(const Runway& rwy, double gs_deg) {
    if (rwy.ils_tch_ft > 0.5 && gs_deg > 0.25)
        return rwy.ils_tch_ft / std::tan(geo::deg2rad(gs_deg));
    return kAimPointFt;
}

Runway runway_from_faa(const std::string& icao_in, const std::string& ident) {
    std::string icao = icao_in;
    for (auto& c : icao)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    const FaaAirportRef* a = faa_airport(icao);
    if (!a)
        throw std::invalid_argument("no FAA/official record for " + icao);
    std::string id = ident;
    for (auto& c : id)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    const FaaEnd* e = faa_end(*a, id);
    if (!e)
        throw std::invalid_argument("no runway end " + id + " at " + icao);
    Runway r;
    r.icao = icao;
    r.ident = id;
    r.elev_ft = e->elev_ft;
    r.brg_true = e->true_brg_deg;
    r.length_ft = e->length_ft;
    r.displaced_ft = e->displaced_ft;
    r.ils_tch_ft = e->ils_tch_ft;
    r.source = a->source + " eff " + e->effective;
    r.thr_lat = e->lat;
    r.thr_lon = e->lon;
    const double b = geo::deg2rad(r.brg_true);
    offset_ft(r.thr_lat, r.thr_lon, e->displaced_ft * std::cos(b), e->displaced_ft * std::sin(b));
    return r;
}

ApproachProfile::ApproachProfile(ProfileConfig c) : c_(std::move(c)) {
    x_ = -c_.start_nm * kFtPerNm;
    h_ = glidepath_height_ft(x_, c_.gs_deg, aim_point_ft(c_.rwy, c_.gs_deg));
    const double vapp = c_.vref_kt + c_.vapp_add_kt;
    if (c_.landing_config_at_start) {
        gear_ = true;
        flaps_ = 30;
        v_ = vapp;
        phase_ = "final (landing config)";
    } else {
        gear_ = false;
        flaps_ = 15;
        v_ = vapp + 20.0;
        phase_ = "approach";
    }
    pitch_ = 5.5 * std::pow(vapp / v_, 2) - c_.gs_deg;
}

void ApproachProfile::place(Frame& f) const {
    const auto& r = c_.rwy;
    const double b = geo::deg2rad(r.brg_true);
    double lat = r.thr_lat, lon = r.thr_lon;
    offset_ft(lat, lon, x_ * std::cos(b) - y_ * std::sin(b), x_ * std::sin(b) + y_ * std::cos(b));
    f.lat = lat;
    f.lon = lon;
    f.alt_ft = r.elev_ft + h_ + kEyeHeightFt;
    f.hdg = r.brg_true;
}

Frame ApproachProfile::step(double dt) {
    const double vapp = c_.vref_kt + c_.vapp_add_kt;
    const double tg = std::tan(geo::deg2rad(c_.gs_deg));
    const double fps = v_ * kKtToFps;
    double vs = 0; // ft/s, positive up

    if (!td_) {
        // Configuration sequence keyed on distance to threshold.
        const double nm_out = -x_ / kFtPerNm;
        if (!gear_ && nm_out <= 5.0) {
            gear_ = true;
            phase_ = "gear down";
        }
        if (gear_ && flaps_ < 30 && nm_out <= 4.0) {
            flaps_ = 30;
            phase_ = "flaps 30";
        }
        if (flaps_ == 30)
            v_ = approach(v_, flare_ ? c_.vref_kt - 3 : vapp, flare_ ? 1.2 : 1.0, dt);
        if (!flare_ && h_ <= c_.flare_ft) {
            flare_ = true;
            phase_ = "flare";
        }
        if (!flare_) {
            vs = -fps * tg;
            if (h_ < 1000 && flaps_ == 30 && gear_)
                phase_ = "stabilized";
        } else {
            const double vs0 = std::max(fps * tg, c_.td_sink_fps + 0.5);
            const double tau = c_.flare_ft / (vs0 - c_.td_sink_fps);
            vs = -(h_ / tau + c_.td_sink_fps);
        }
        x_ += fps * dt;
        h_ += vs * dt;
        if (!flare_)
            h_ = glidepath_height_ft(x_, c_.gs_deg, aim_point_ft(c_.rwy, c_.gs_deg));
        if (h_ <= 0) {
            h_ = 0;
            td_ = true;
            td_x_ = x_;
            td_t_ = t_ + dt;
            phase_ = "touchdown";
        }
        // Pitch = alpha + flight-path angle; alpha grows as speed decays.
        const double flapf = flaps_ >= 30 ? 1.0 : 1.2;
        const double alpha = 5.5 * flapf * std::pow(vapp / std::max(v_, 60.0), 2) * (gear_ ? 1 : 0.95);
        const double gamma = geo::rad2deg(std::atan2(vs, std::max(fps, 1.0)));
        pitch_ = approach(pitch_, alpha + gamma, 1.5, dt);
        // Gentle lateral wander (S-turn of 30 ft) with consistent bank,
        // faded out below 1000 ft and gone by 300 ft.
        const double fade = clampd((h_ - 300.0) / 700.0, 0.0, 1.0);
        const double w = 2 * geo::kPi / 40.0;
        y_ = 30.0 * fade * std::sin(w * t_);
        const double lat_acc = -30.0 * fade * w * w * std::sin(w * t_);
        roll_ = geo::rad2deg(std::atan2(lat_acc, 32.174));
        roll_ = clampd(roll_, -5, 5);
        if (flare_)
            roll_ = approach(roll_, 0, 2, dt);
    } else {
        // Rollout: derotate, spoilers/autobrake/reverse, then taxi speed.
        const double since = t_ - td_t_;
        pitch_ = approach(pitch_, since < 1.5 ? pitch_ : 0.0, 2.0, dt);
        roll_ = 0;
        y_ = 0;
        double decel = 1.0;
        if (since > 2.0)
            decel = v_ > 60 ? 5.0 : 3.0;
        if (v_ <= c_.taxi_kt + 0.01) {
            decel = 0;
            v_ = c_.taxi_kt;
            if (taxi_t_ < 0)
                taxi_t_ = t_;
            phase_ = "taxi";
            if (t_ - taxi_t_ > 5.0)
                done_ = true;
        } else {
            phase_ = since < 3 ? "derotate" : "rollout";
        }
        v_ = std::max(c_.taxi_kt, v_ - decel * dt);
        x_ += v_ * kKtToFps * dt;
        if (x_ > c_.rwy.length_ft - c_.rwy.displaced_ft - 300)
            done_ = true;
    }
    t_ += dt;

    Frame f;
    f.t = t_;
    place(f);
    f.pitch = pitch_;
    f.roll = roll_;
    f.gs_kt = v_;
    f.wheel_agl_ft = h_;
    f.gear_down = gear_;
    f.flaps = flaps_;
    f.on_ground = td_;
    f.strobe = true;
    // 7 lobes: all on until taxi speed, then turnoff lights only.
    f.landing_mask = (phase_ == "taxi") ? static_cast<std::uint16_t>((1 << 1) | (1 << 5)) : 0x7F;
    f.phase = phase_;
    return f;
}

// ================================================================= trajectory

std::string trajectory_csv(const std::vector<Frame>& frames) {
    std::ostringstream o;
    o << "t,lat,lon,alt_ft,hdg,pitch,roll,gs_kt,gear,flaps,landing_mask,strobe\n";
    char buf[256];
    for (const auto& f : frames) {
        std::snprintf(buf, sizeof(buf), "%.3f,%.8f,%.8f,%.2f,%.3f,%.3f,%.3f,%.2f,%d,%d,%u,%d\n",
                      f.t, f.lat, f.lon, f.alt_ft, f.hdg, f.pitch, f.roll, f.gs_kt,
                      f.gear_down ? 1 : 0, f.flaps, static_cast<unsigned>(f.landing_mask),
                      f.strobe ? 1 : 0);
        o << buf;
    }
    return o.str();
}

void save_trajectory_csv(const std::string& path, const std::vector<Frame>& frames) {
    std::ofstream f(path);
    if (!f)
        throw std::runtime_error("cannot write " + path);
    f << trajectory_csv(frames);
}

namespace {
void set_field(Frame& f, const std::string& k, double v) {
    if (k == "t") f.t = v;
    else if (k == "lat") f.lat = v;
    else if (k == "lon") f.lon = v;
    else if (k == "alt_ft" || k == "alt") f.alt_ft = v;
    else if (k == "hdg") f.hdg = v;
    else if (k == "pitch") f.pitch = v;
    else if (k == "roll") f.roll = v;
    else if (k == "gs_kt") f.gs_kt = v;
    else if (k == "gear") f.gear_down = v != 0;
    else if (k == "flaps") f.flaps = static_cast<int>(v);
    else if (k == "landing_mask") f.landing_mask = static_cast<std::uint16_t>(v);
    else if (k == "strobe") f.strobe = v != 0;
}
void validate(std::vector<Frame>& v) {
    if (v.size() < 2)
        throw std::runtime_error("trajectory needs at least 2 points");
    for (std::size_t i = 1; i < v.size(); ++i)
        if (!(v[i].t > v[i - 1].t))
            throw std::runtime_error("trajectory timestamps must increase (row " +
                                     std::to_string(i + 1) + ")");
    for (const auto& f : v)
        if (std::fabs(f.lat) > 90 || std::fabs(f.lon) > 180 || f.alt_ft < -2000 || f.alt_ft > 60000)
            throw std::runtime_error("implausible trajectory point at t=" + std::to_string(f.t));
}
} // namespace

std::vector<Frame> parse_trajectory_csv(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    std::vector<std::string> cols;
    std::vector<Frame> out;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#')
            continue;
        std::vector<std::string> cells;
        std::stringstream ss(line);
        std::string c;
        while (std::getline(ss, c, ','))
            cells.push_back(trim(c));
        if (cols.empty()) {
            cols = cells;
            for (auto& k : cols)
                k = lower(k);
            continue;
        }
        Frame f;
        for (std::size_t i = 0; i < cells.size() && i < cols.size(); ++i)
            set_field(f, cols[i], std::stod(cells[i]));
        out.push_back(f);
    }
    validate(out);
    return out;
}

std::vector<Frame> parse_trajectory_toml(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    std::vector<Frame> out;
    bool inpt = false;
    while (std::getline(in, line)) {
        const auto hash = line.find('#');
        if (hash != std::string::npos)
            line = line.substr(0, hash);
        line = trim(line);
        if (line.empty())
            continue;
        if (line == "[[point]]") {
            out.emplace_back();
            inpt = true;
            continue;
        }
        if (line[0] == '[') {
            inpt = false;
            continue;
        }
        if (!inpt)
            continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string k = lower(trim(line.substr(0, eq)));
        const std::string v = lower(trim(line.substr(eq + 1)));
        double d = v == "true" ? 1 : v == "false" ? 0 : std::stod(v);
        set_field(out.back(), k, d);
    }
    validate(out);
    return out;
}

std::vector<Frame> load_trajectory(const std::string& path) {
    std::ifstream f(path);
    if (!f)
        throw std::runtime_error("cannot read " + path);
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string l = lower(path);
    if (l.size() > 5 && l.substr(l.size() - 5) == ".toml")
        return parse_trajectory_toml(ss.str());
    return parse_trajectory_csv(ss.str());
}

Frame interpolate(const std::vector<Frame>& tr, double t) {
    if (t <= tr.front().t)
        return tr.front();
    if (t >= tr.back().t)
        return tr.back();
    auto it = std::upper_bound(tr.begin(), tr.end(), t,
                               [](double tv, const Frame& f) { return tv < f.t; });
    const Frame& b = *it;
    const Frame& a = *(it - 1);
    const double u = (t - a.t) / (b.t - a.t);
    auto lerp = [u](double x, double y) { return x + (y - x) * u; };
    Frame f = u < 0.5 ? a : b; // discrete fields from nearest
    f.t = t;
    f.lat = lerp(a.lat, b.lat);
    f.lon = lerp(a.lon, b.lon);
    f.alt_ft = lerp(a.alt_ft, b.alt_ft);
    double dh = std::fmod(b.hdg - a.hdg + 540.0, 360.0) - 180.0;
    f.hdg = geo::wrap360(a.hdg + dh * u);
    f.pitch = lerp(a.pitch, b.pitch);
    f.roll = lerp(a.roll, b.roll);
    f.gs_kt = lerp(a.gs_kt, b.gs_kt);
    return f;
}

// ================================================================= manual

CockpitIoBridge::CockpitIoBridge(CockpitIoConfig cfg) : cfg_(std::move(cfg)) {
    std::string h;
    std::uint16_t p = 0;
    parse_guarded(cfg_.endpoint, h, p, "cockpit-io bridge");
}

ManualFlight::ManualFlight(const Runway& rwy, double start_nm, double gs_deg, bool on_runway) {
    reposition(rwy, start_nm, gs_deg, on_runway);
}

void ManualFlight::reposition(const Runway& rwy, double start_nm, double gs_deg, bool on_runway) {
    rwy_ = rwy;
    const double x = on_runway ? 0.0 : -start_nm * kFtPerNm;
    lat_ = rwy.thr_lat;
    lon_ = rwy.thr_lon;
    const double b = geo::deg2rad(rwy.brg_true);
    offset_ft(lat_, lon_, x * std::cos(b), x * std::sin(b));
    alt_ = rwy.elev_ft + (on_runway ? 0.0 : glidepath_height_ft(x, gs_deg, aim_point_ft(rwy, gs_deg)));
    hdg_ = rwy.brg_true;
    roll_ = 0;
    vs_ = 0;
    v_ = on_runway ? 0.0 : 145.0;
    pitch_ = on_runway ? 0.0 : 5.5 - gs_deg;
}

void ManualFlight::reposition_lla(double eye_lat, double eye_lon, double eye_alt_ft, double hdg_deg) {
    lat_ = eye_lat;
    lon_ = eye_lon;
    alt_ = eye_alt_ft - kEyeHeightFt;
    if (alt_ < rwy_.elev_ft)
        alt_ = rwy_.elev_ft;
    hdg_ = geo::wrap360(hdg_deg);
    roll_ = 0;
    vs_ = 0;
    const double agl = alt_ - rwy_.elev_ft;
    v_ = agl <= 0.01 ? 0.0 : 145.0;
    pitch_ = agl <= 0.01 ? 0.0 : 2.5;
}

Frame ManualFlight::step(double dt, const Controls& c) {
    const double flapf = c.flaps >= 40 ? 0.95 : c.flaps >= 30 ? 1.0 : c.flaps >= 15 ? 1.2 : 1.6;
    const double drag_cfg = (c.flaps >= 40 ? 1.1 : c.flaps >= 30 ? 1.0 : c.flaps >= 15 ? 0.75 : 0.5) +
                            (c.gear_down ? 0.15 : 0);
    const bool ground = alt_ - rwy_.elev_ft <= 0.01;
    // Lift = weight / cos(bank): alpha grows with 1/v^2 and with bank.
    const double nz = 1.0 / std::max(std::cos(geo::deg2rad(roll_)), 0.5);
    const double alpha = clampd(5.5 * flapf * nz * std::pow(145.0 / std::max(v_, 60.0), 2), 0, 16);
    pitch_ = clampd(pitch_ + c.elevator * 3.0 * dt, ground ? 0.0 : -15.0, 20.0);
    if (ground) {
        roll_ = 0;
    } else {
        roll_ = clampd(roll_ + c.aileron * 8.0 * dt, -30, 30);
        if (std::fabs(c.aileron) < 0.05)
            roll_ = approach(roll_, 0, 0.5, dt); // gentle wings-level tendency
    }
    double gamma = geo::deg2rad(pitch_ - alpha);
    if (v_ < 110)
        gamma -= geo::deg2rad((110 - v_) * 0.3); // mush below ~stall
    if (ground && gamma < 0)
        gamma = 0;
    // kt/s. Thrust 3.56*throttle; drag 2.6 at 145 kt landing config;
    // gravity 19.07 kt/s * sin(gamma). Balanced at throttle 0.45, gamma -3.
    const double brake = ground ? (c.throttle < 0.05 ? 4.0 : 0.3) : 0.0;
    const double drag = 2.6 / 1.15 * drag_cfg * std::pow(v_ / 145.0, 2) + brake;
    v_ = std::max(0.0, v_ + (3.56 * clampd(c.throttle, 0, 1) - drag - 19.07 * std::sin(gamma)) * dt);
    const double fps = v_ * kKtToFps;
    vs_ = fps * std::sin(gamma);
    alt_ += vs_ * dt;
    if (alt_ < rwy_.elev_ft) {
        alt_ = rwy_.elev_ft;
        vs_ = 0;
    }
    const double turn = geo::rad2deg(32.174 * std::tan(geo::deg2rad(roll_)) / std::max(fps, 50.0));
    const double yaw = c.rudder * (ground ? 30.0 : 4.0);
    hdg_ = geo::wrap360(hdg_ + (turn + yaw) * dt);
    const double hs = fps * std::cos(gamma);
    const double h = geo::deg2rad(hdg_);
    offset_ft(lat_, lon_, hs * std::cos(h) * dt, hs * std::sin(h) * dt);
    t_ += dt;
    Frame f;
    f.t = t_;
    f.lat = lat_;
    f.lon = lon_;
    f.alt_ft = alt_ + kEyeHeightFt;
    f.hdg = hdg_;
    f.pitch = pitch_;
    f.roll = roll_;
    f.gs_kt = v_;
    f.wheel_agl_ft = alt_ - rwy_.elev_ft;
    f.on_ground = f.wheel_agl_ft <= 0.01;
    f.gear_down = c.gear_down;
    f.flaps = c.flaps;
    f.landing_mask = c.lights ? 0x7F : 0;
    f.strobe = true;
    f.phase = f.on_ground ? "ground" : "manual";
    return f;
}

// ================================================================= input map / cal / replay

namespace {

bool axis_unipolar(const std::string& src) {
    return src == "lt" || src == "rt" || src == "xinput_lt" || src == "xinput_rt";
}

} // namespace

double apply_axis(double raw_unit, const AxisCal& cal, bool unipolar) {
    double x = raw_unit;
    if (cal.invert)
        x = unipolar ? (1.0 - x) : -x;
    if (unipolar) {
        const double lo = std::max(0.0, cal.deadzone);
        if (x <= lo)
            return 0;
        const double span = 1.0 - lo;
        return span > 1e-9 ? clampd((x - lo) / span, 0, 1) : 0;
    }
    const double d = x - cal.center;
    if (std::fabs(d) < cal.deadzone)
        return 0;
    const double span = d >= 0 ? (cal.max - cal.center) : (cal.center - cal.min);
    const double usable = span - cal.deadzone;
    if (usable <= 1e-9)
        return 0;
    const double u = (d >= 0 ? 1.0 : -1.0) * (std::fabs(d) - cal.deadzone) / usable;
    return clampd(u, -1, 1);
}

const double* raw_axis_ptr(const RawAxes& a, const std::string& name) {
    const std::string n = lower(name);
    if (n == "x")
        return &a.x;
    if (n == "y")
        return &a.y;
    if (n == "z" || n == "throttle")
        return &a.z;
    if (n == "rx" || n == "rudder")
        return &a.rx;
    if (n == "ry")
        return &a.ry;
    if (n == "rz")
        return &a.rz;
    if (n == "slider")
        return &a.slider;
    if (n == "lx" || n == "xinput_lx")
        return &a.lx;
    if (n == "ly" || n == "xinput_ly")
        return &a.ly;
    if (n == "xirx" || n == "xinput_rx")
        return &a.xirx;
    if (n == "xiry" || n == "xinput_ry")
        return &a.xiry;
    if (n == "lt" || n == "xinput_lt")
        return &a.lt;
    if (n == "rt" || n == "xinput_rt")
        return &a.rt;
    if (n == "hatx")
        return &a.hatx;
    if (n == "haty")
        return &a.haty;
    return nullptr;
}

void apply_input_map(const InputMap& m, const RawAxes& raw, Controls& c, std::uint32_t& prev_buttons) {
    auto one = [&](const std::string& src, const AxisCal& cal) -> double {
        const double* p = raw_axis_ptr(raw, src);
        if (!p)
            return 0;
        const bool uni = axis_unipolar(src);
        return apply_axis(*p, cal, uni);
    };
    c.aileron = one(m.aileron_src, m.aileron);
    c.elevator = one(m.elevator_src, m.elevator);
    c.rudder = one(m.rudder_src, m.rudder);
    double th = one(m.throttle_src, m.throttle);
    if (!axis_unipolar(m.throttle_src))
        th = (th + 1.0) * 0.5;
    c.throttle = clampd(th, 0, 1);
    auto rising = [&](int bit) -> bool {
        if (bit < 0 || bit > 31)
            return false;
        const std::uint32_t msk = 1u << bit;
        return (raw.buttons & msk) && !(prev_buttons & msk);
    };
    if (rising(m.btn_gear))
        c.gear_down = !c.gear_down;
    if (rising(m.btn_flaps))
        c.flaps = c.flaps == 30 ? 40 : c.flaps == 40 ? 0 : c.flaps == 0 ? 15 : 30;
    if (rising(m.btn_lights))
        c.lights = !c.lights;
    prev_buttons = raw.buttons;
}

void cal_note(AxisCal& cal, double raw_unit) {
    if (!cal.seeded) {
        cal.min = cal.max = raw_unit;
        cal.seeded = true;
        return;
    }
    cal.min = std::min(cal.min, raw_unit);
    cal.max = std::max(cal.max, raw_unit);
}

void cal_finish(AxisCal& cal) {
    if (!cal.seeded)
        return;
    cal.center = 0.5 * (cal.min + cal.max);
    if (std::fabs(cal.center) < 0.15)
        cal.center = 0;
}

namespace {

void toml_put_axis(std::ostringstream& o, const char* name, const std::string& src, const AxisCal& c) {
    o << "[axis." << name << "]\n";
    o << "source = \"" << src << "\"\n";
    o << "invert = " << (c.invert ? "true" : "false") << "\n";
    o << "deadzone = " << c.deadzone << "\n";
    o << "min = " << c.min << "\n";
    o << "max = " << c.max << "\n";
    o << "center = " << c.center << "\n";
}

} // namespace

std::string format_input_map_toml(const InputMap& m) {
    std::ostringstream o;
    o << "# s376-fakehost USB yoke / throttle / pedals mapping\n";
    o << "# Raw axes are -1..1 from winmm, XInput, evdev or SDL2.\n\n";
    toml_put_axis(o, "aileron", m.aileron_src, m.aileron);
    o << "\n";
    toml_put_axis(o, "elevator", m.elevator_src, m.elevator);
    o << "\n";
    toml_put_axis(o, "rudder", m.rudder_src, m.rudder);
    o << "\n";
    toml_put_axis(o, "throttle", m.throttle_src, m.throttle);
    o << "\n[button]\n";
    o << "gear = " << m.btn_gear << "\n";
    o << "flaps = " << m.btn_flaps << "\n";
    o << "lights = " << m.btn_lights << "\n";
    return o.str();
}

InputMap parse_input_map_toml(const std::string& text) {
    InputMap m;
    m.elevator.invert = true;
    m.throttle.invert = true;
    std::string section;
    std::istringstream in(text);
    std::string line;
    auto unquote = [](std::string s) {
        s = trim(s);
        if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') ||
                              (s.front() == '\'' && s.back() == '\'')))
            return s.substr(1, s.size() - 2);
        return s;
    };
    auto as_bool = [](const std::string& s) {
        return s == "true" || s == "1" || s == "yes";
    };
    while (std::getline(in, line)) {
        const auto hash = line.find('#');
        if (hash != std::string::npos)
            line = line.substr(0, hash);
        line = trim(line);
        if (line.empty())
            continue;
        if (line.front() == '[' && line.back() == ']') {
            section = lower(trim(line.substr(1, line.size() - 2)));
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string key = lower(trim(line.substr(0, eq)));
        const std::string val = unquote(line.substr(eq + 1));
        AxisCal* cal = nullptr;
        std::string* src = nullptr;
        if (section == "axis.aileron" || section == "aileron") {
            cal = &m.aileron;
            src = &m.aileron_src;
        } else if (section == "axis.elevator" || section == "elevator") {
            cal = &m.elevator;
            src = &m.elevator_src;
        } else if (section == "axis.rudder" || section == "rudder") {
            cal = &m.rudder;
            src = &m.rudder_src;
        } else if (section == "axis.throttle" || section == "throttle") {
            cal = &m.throttle;
            src = &m.throttle_src;
        }
        if (cal && src) {
            if (key == "source" || key == "src" || key == "axis")
                *src = lower(val);
            else if (key == "invert")
                cal->invert = as_bool(val);
            else if (key == "deadzone")
                cal->deadzone = std::strtod(val.c_str(), nullptr);
            else if (key == "min")
                cal->min = std::strtod(val.c_str(), nullptr);
            else if (key == "max")
                cal->max = std::strtod(val.c_str(), nullptr);
            else if (key == "center")
                cal->center = std::strtod(val.c_str(), nullptr);
            continue;
        }
        if (section == "button" || section == "buttons") {
            const int b = std::atoi(val.c_str());
            if (key == "gear")
                m.btn_gear = b;
            else if (key == "flaps")
                m.btn_flaps = b;
            else if (key == "lights")
                m.btn_lights = b;
        }
    }
    return m;
}

InputMap load_input_map(const std::string& path) {
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("input map: cannot read " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return parse_input_map_toml(ss.str());
}

void save_input_map(const std::string& path, const InputMap& m) {
    std::ofstream out(path);
    if (!out)
        throw std::runtime_error("input map: cannot write " + path);
    out << format_input_map_toml(m);
}

ReplayControlSource::ReplayControlSource(std::vector<double> t, std::vector<Controls> c)
    : t_(std::move(t)), c_(std::move(c)) {
    if (t_.size() != c_.size() || t_.empty())
        throw std::runtime_error("controls replay: empty or mismatched");
    for (std::size_t i = 1; i < t_.size(); ++i) {
        if (!(t_[i] > t_[i - 1]))
            throw std::runtime_error("controls replay: timestamps must increase");
    }
}

ReplayControlSource ReplayControlSource::from_csv(const std::string& text) {
    std::vector<double> ts;
    std::vector<Controls> cs;
    std::istringstream in(text);
    std::string line;
    bool header = true;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#')
            continue;
        if (header) {
            header = false;
            if (lower(line).find("elevator") != std::string::npos)
                continue;
        }
        std::stringstream ss(line);
        std::string cell;
        std::vector<std::string> cols;
        while (std::getline(ss, cell, ','))
            cols.push_back(trim(cell));
        if (cols.size() < 4)
            throw std::runtime_error("controls replay: need t,elevator,aileron,throttle");
        Controls c;
        ts.push_back(std::strtod(cols[0].c_str(), nullptr));
        c.elevator = std::strtod(cols[1].c_str(), nullptr);
        c.aileron = std::strtod(cols[2].c_str(), nullptr);
        c.throttle = std::strtod(cols[3].c_str(), nullptr);
        if (cols.size() > 4)
            c.rudder = std::strtod(cols[4].c_str(), nullptr);
        if (cols.size() > 5)
            c.gear_down = cols[5] == "1" || lower(cols[5]) == "true";
        if (cols.size() > 6)
            c.flaps = std::atoi(cols[6].c_str());
        if (cols.size() > 7)
            c.lights = cols[7] != "0" && lower(cols[7]) != "false";
        cs.push_back(c);
    }
    return ReplayControlSource(std::move(ts), std::move(cs));
}

ReplayControlSource ReplayControlSource::load(const std::string& path) {
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("controls replay: cannot read " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return from_csv(ss.str());
}

void ReplayControlSource::poll(Controls& c) {
    if (t_.empty())
        return;
    if (now_ <= t_.front()) {
        c = c_.front();
        return;
    }
    if (now_ >= t_.back()) {
        c = c_.back();
        return;
    }
    std::size_t i = 1;
    while (i < t_.size() && t_[i] < now_)
        ++i;
    const double u = (now_ - t_[i - 1]) / (t_[i] - t_[i - 1]);
    const Controls& a = c_[i - 1];
    const Controls& b = c_[i];
    c.elevator = a.elevator + (b.elevator - a.elevator) * u;
    c.aileron = a.aileron + (b.aileron - a.aileron) * u;
    c.rudder = a.rudder + (b.rudder - a.rudder) * u;
    c.throttle = a.throttle + (b.throttle - a.throttle) * u;
    c.gear_down = u < 0.5 ? a.gear_down : b.gear_down;
    c.flaps = u < 0.5 ? a.flaps : b.flaps;
    c.lights = u < 0.5 ? a.lights : b.lights;
}

bool ReplayControlSource::done() const { return !t_.empty() && now_ >= t_.back(); }

void LiveStateFeed::push(const Frame& f) {
    f_ = f;
    active_ = true;
    last_ = std::chrono::steady_clock::now();
}

bool LiveStateFeed::stale() const {
    if (!active_)
        return false;
    const auto age = std::chrono::duration<double>(std::chrono::steady_clock::now() - last_).count();
    return age > timeout_s_;
}

void LiveStateFeed::clear() {
    active_ = false;
    f_ = Frame{};
}

// ================================================================= IOS

bool parse_preset(const std::string& p_in, std::string& icao, std::string& rwy, double& start_nm,
                  bool& on_runway) {
    const std::string p = lower(trim(p_in));
    const auto d1 = p.find('-');
    if (d1 == std::string::npos)
        return false;
    const auto d2 = p.find('-', d1 + 1);
    icao = p.substr(0, d1);
    rwy = p.substr(d1 + 1, d2 == std::string::npos ? std::string::npos : d2 - d1 - 1);
    const std::string tail = d2 == std::string::npos ? "6nm" : p.substr(d2 + 1);
    on_runway = tail == "rwy" || tail == "runway";
    start_nm = 0;
    if (!on_runway) {
        try {
            start_nm = parse_distance_nm(tail);
        } catch (...) {
            return false;
        }
    }
    return !icao.empty() && !rwy.empty();
}

namespace {

bool ios_op_known(const std::string& op) {
    static const char* ok[] = {"reposition", "freeze", "unfreeze", "tod", "vis", "wind",
                               "ceiling",    "rvr",    "weather",  "lights", "cloud",
                               "state",      "quit",   "rwy-cond", "rwy_cond",
                               "runway_condition", nullptr};
    for (int i = 0; ok[i]; ++i)
        if (op == ok[i])
            return true;
    return false;
}

int clamp_rwy_cond(int v) { return v < 0 ? 0 : v; }

int clamp_step(int s) { return s < 0 ? 0 : s > 5 ? 5 : s; }

bool json_find(const std::string& s, const char* key, std::size_t& valpos) {
    const std::string q = std::string("\"") + key + "\"";
    std::size_t i = 0;
    while ((i = s.find(q, i)) != std::string::npos) {
        std::size_t j = i + q.size();
        while (j < s.size() && std::isspace(static_cast<unsigned char>(s[j])))
            ++j;
        if (j < s.size() && s[j] == ':') {
            ++j;
            while (j < s.size() && std::isspace(static_cast<unsigned char>(s[j])))
                ++j;
            valpos = j;
            return true;
        }
        i += q.size();
    }
    return false;
}

bool json_string(const std::string& s, const char* key, std::string& out) {
    std::size_t i = 0;
    if (!json_find(s, key, i) || i >= s.size() || s[i] != '"')
        return false;
    ++i;
    out.clear();
    for (; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            out += s[i + 1];
            ++i;
            continue;
        }
        if (s[i] == '"')
            return true;
        out += s[i];
    }
    return false;
}

bool json_number(const std::string& s, const char* key, double& out) {
    std::size_t i = 0;
    if (!json_find(s, key, i))
        return false;
    char* end = nullptr;
    const double v = std::strtod(s.c_str() + i, &end);
    if (end == s.c_str() + i)
        return false;
    out = v;
    return true;
}

bool json_bool(const std::string& s, const char* key, bool& out) {
    std::size_t i = 0;
    if (!json_find(s, key, i))
        return false;
    if (s.compare(i, 4, "true") == 0) {
        out = true;
        return true;
    }
    if (s.compare(i, 5, "false") == 0) {
        out = false;
        return true;
    }
    if (s[i] == '1') {
        out = true;
        return true;
    }
    if (s[i] == '0') {
        out = false;
        return true;
    }
    return false;
}

void fill_ios_from_json(IosCommand& c, const std::string& body) {
    std::string s;
    double n = 0;
    bool b = false;
    if (json_string(body, "preset", s) || json_string(body, "arg", s))
        c.arg = s;
    if (json_string(body, "icao", s)) {
        c.icao = s;
        c.have_runway = true;
    }
    if (json_string(body, "runway", s) || json_string(body, "rwy", s)) {
        c.runway = s;
        c.have_runway = true;
    }
    if (json_string(body, "distance", s)) {
        try {
            c.distance_nm = parse_distance_nm(s);
        } catch (...) {
            c.distance_nm = 0;
        }
    }
    if (json_number(body, "lat", n)) {
        c.lat = n;
        c.have_lla = true;
    }
    if (json_number(body, "lon", n)) {
        c.lon = n;
        c.have_lla = true;
    }
    if (json_number(body, "alt_ft", n)) {
        c.alt_ft = n;
        c.have_lla = true;
    }
    if (json_number(body, "hdg", n) || json_number(body, "heading", n)) {
        c.hdg = n;
        c.have_lla = true;
    }
    if (json_bool(body, "on_runway", b))
        c.on_runway = b;
    if (json_number(body, "hours", n) || json_number(body, "tod", n) || json_number(body, "tod_hours", n)) {
        c.tod_hours = static_cast<float>(n);
        c.have_tod = true;
    }
    if (json_number(body, "vis_sm", n) || json_number(body, "vis", n)) {
        c.vis_sm = static_cast<float>(n);
        c.have_vis = true;
    }
    if (json_number(body, "wind_dir", n)) {
        c.wind_dir = static_cast<float>(n);
        c.have_wind = true;
    }
    if (json_number(body, "wind_mph", n)) {
        c.wind_mph = static_cast<float>(n);
        c.have_wind = true;
    }
    if (json_number(body, "ceiling_ft", n) || json_number(body, "ceiling", n)) {
        c.ceiling_ft = static_cast<float>(n);
        c.have_ceiling = true;
    }
    if (json_number(body, "cloud_top_ft", n) || json_number(body, "top_ft", n)) {
        c.cloud_top_ft = static_cast<float>(n);
        c.have_cloud_top = true;
    }
    if (json_number(body, "upper_ceiling_ft", n)) {
        c.upper_ceiling_ft = static_cast<float>(n);
        c.have_upper = true;
    }
    if (json_number(body, "upper_top_ft", n)) {
        c.upper_top_ft = static_cast<float>(n);
        c.have_upper = true;
    }
    if (json_number(body, "rvr_ft", n) || json_number(body, "rvr", n)) {
        c.rvr_ft = static_cast<float>(n);
        c.have_rvr = true;
    }
    if (json_number(body, "precip_type", n)) {
        c.precip_type = static_cast<int>(n);
        c.have_precip = true;
    }
    if (json_number(body, "precip_intensity", n)) {
        c.precip_intensity = static_cast<int>(n);
        c.have_precip = true;
    }
    if (json_number(body, "rwy_cond", n) || json_number(body, "runway_condition", n) ||
        json_number(body, "condition", n)) {
        c.rwy_cond = clamp_rwy_cond(static_cast<int>(n));
        c.have_rwy_cond = true;
    }
    if (json_number(body, "step", n) || json_number(body, "light_step", n))
        c.light_step = clamp_step(static_cast<int>(n));
    if (json_number(body, "runway", n) && c.op == "lights")
        c.rwy_light = clamp_step(static_cast<int>(n));
    if (json_number(body, "approach", n))
        c.apch_light = clamp_step(static_cast<int>(n));
    if (json_number(body, "environment", n))
        c.env_light = clamp_step(static_cast<int>(n));
    if (c.light_step >= 0 || c.rwy_light >= 0 || c.apch_light >= 0 || c.env_light >= 0)
        c.have_lights = true;
    if (json_number(body, "pitch", n))
        c.pitch = n;
    if (json_number(body, "roll", n))
        c.roll = n;
    if (json_number(body, "gs_kt", n) || json_number(body, "gs", n))
        c.gs_kt = n;
    if (json_bool(body, "gear_down", b) || json_bool(body, "gear", b)) {
        c.gear_down = b;
        c.have_gear = true;
    }
    if (json_number(body, "flaps", n))
        c.flaps = static_cast<int>(n);
    if (json_number(body, "timeout_s", n))
        c.timeout_s = n;
    if (json_bool(body, "frozen", b)) {
        c.have_frozen = true;
        c.frozen = b;
    }
    if (c.op == "state")
        c.have_state = true;
}

} // namespace

IosCommand parse_ios_query(const std::string& target) {
    IosCommand c;
    const auto q = target.find('?');
    if (target.compare(0, 4, "/cmd") != 0 || q == std::string::npos)
        return c;
    std::stringstream ss(target.substr(q + 1));
    std::string kv;
    while (std::getline(ss, kv, '&')) {
        const auto eq = kv.find('=');
        std::string k = kv.substr(0, eq), v = eq == std::string::npos ? "" : kv.substr(eq + 1);
        std::string dec;
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (v[i] == '+') dec += ' ';
            else if (v[i] == '%' && i + 2 < v.size()) {
                dec += static_cast<char>(std::strtol(v.substr(i + 1, 2).c_str(), nullptr, 16));
                i += 2;
            } else dec += v[i];
        }
        if (k == "op") c.op = lower(dec);
        else if (k == "arg") c.arg = dec;
    }
    if (!ios_op_known(c.op))
        c.op.clear();
    return c;
}

IosCommand parse_ios_json(const std::string& path_in, const std::string& body) {
    IosCommand c;
    std::string path = path_in;
    const auto q = path.find('?');
    if (q != std::string::npos)
        path = path.substr(0, q);
    if (!path.empty() && path.back() == '/' && path.size() > 1)
        path.pop_back();
    const std::string prefix = "/api/v1/";
    if (path.rfind(prefix, 0) != 0)
        return c;
    c.op = lower(path.substr(prefix.size()));
    if (!ios_op_known(c.op)) {
        c.op.clear();
        return c;
    }
    fill_ios_from_json(c, body);
    if (c.op == "freeze" && c.have_frozen && !c.frozen)
        c.op = "unfreeze";
    return c;
}

std::string ios_status_json(const IosStatus& st) {
    char buf[1600];
    std::snprintf(
        buf, sizeof(buf),
        "{\"ok\":true,\"mode\":\"%s\",\"preset\":\"%s\",\"phase\":\"%s\",\"frozen\":%s,"
        "\"t\":%.3f,\"lat\":%.8f,\"lon\":%.8f,\"alt_ft\":%.2f,\"hdg\":%.3f,\"pitch\":%.3f,"
        "\"roll\":%.3f,\"gs_kt\":%.2f,\"wheel_agl_ft\":%.2f,\"gear_down\":%s,\"flaps\":%d,"
        "\"hat_replies\":%d,\"last_hat_ft\":%.2f,\"contacts\":%d,\"tod_hours\":%.3f,"
        "\"vis_sm\":%.3f,\"wind_dir\":%.1f,\"wind_mph\":%.1f,\"ceiling_ft\":%.1f,"
        "\"cloud_top_ft\":%.1f,\"upper_ceiling_ft\":%.1f,\"upper_top_ft\":%.1f,\"rvr_ft\":%.1f,"
        "\"rwy_cond\":%d,"
        "\"lights\":{\"step\":%d,\"runway\":%d,\"approach\":%d,\"environment\":%d}}",
        st.mode.c_str(), st.preset.c_str(), st.phase.c_str(), st.frozen ? "true" : "false", st.t,
        st.lat, st.lon, st.alt, st.hdg, st.pitch, st.roll, st.gs_kt, st.wheel_agl,
        st.gear_down ? "true" : "false", st.flaps, st.hat_replies, st.last_hat, st.contacts,
        st.tod_hours, st.vis_sm, st.wind_dir, st.wind_mph, st.ceiling_ft, st.cloud_top_ft,
        st.upper_ceiling_ft, st.upper_top_ft, st.rvr_ft, st.rwy_cond, st.light_step, st.rwy_light,
        st.apch_light, st.env_light);
    return buf;
}

IosEffect apply_ios_command(const IosCommand& cmd, Environment& env, LiveStateFeed* live) {
    IosEffect e;
    if (cmd.op == "quit")
        e.quit = true;
    else if (cmd.op == "freeze") {
        e.set_frozen = true;
        e.frozen = true;
    } else if (cmd.op == "unfreeze") {
        e.set_frozen = true;
        e.frozen = false;
        if (live)
            live->clear();
    } else if (cmd.op == "tod" || cmd.have_tod) {
        if (cmd.have_tod)
            env.tod_hours = cmd.tod_hours;
        else if (!cmd.arg.empty())
            env.tod_hours = std::stof(cmd.arg);
    }
    if (cmd.op == "vis" || cmd.have_vis) {
        if (cmd.have_vis)
            env.vis_ft = cmd.vis_sm * 5280.f;
        else if (!cmd.arg.empty())
            env.vis_ft = std::stof(cmd.arg) * 5280.f;
    }
    if (cmd.op == "wind" || cmd.have_wind) {
        if (cmd.have_wind) {
            env.wind_dir = cmd.wind_dir;
            env.wind_mph = cmd.wind_mph;
        } else if (!cmd.arg.empty()) {
            const auto sl = cmd.arg.find('/');
            env.wind_dir = std::stof(cmd.arg.substr(0, sl));
            env.wind_mph = sl == std::string::npos ? 0 : std::stof(cmd.arg.substr(sl + 1));
        }
    }
    if (cmd.op == "ceiling" || cmd.have_ceiling) {
        if (cmd.have_ceiling)
            env.ceiling_ft_msl = cmd.ceiling_ft;
        else if (!cmd.arg.empty())
            env.ceiling_ft_msl = std::stof(cmd.arg);
    }
    if (cmd.have_cloud_top)
        env.cloud_top_ft_msl = cmd.cloud_top_ft;
    if (cmd.have_upper) {
        env.upper_ceiling_ft_msl = cmd.upper_ceiling_ft;
        env.upper_top_ft_msl = cmd.upper_top_ft;
    }
    if (cmd.op == "rvr" || cmd.have_rvr) {
        if (cmd.have_rvr)
            env.rvr_ft = cmd.rvr_ft;
        else if (!cmd.arg.empty())
            env.rvr_ft = std::stof(cmd.arg);
        if (env.rvr_ft > 0)
            env.fog_top_ft = 300;
    }
    if (cmd.have_precip) {
        env.precip_type = cmd.precip_type;
        env.precip_intensity = cmd.precip_intensity;
    }
    if (cmd.op == "rwy-cond" || cmd.op == "rwy_cond" || cmd.op == "runway_condition" ||
        cmd.have_rwy_cond) {
        if (cmd.have_rwy_cond)
            env.runway_condition = clamp_rwy_cond(cmd.rwy_cond);
        else if (!cmd.arg.empty())
            env.runway_condition = clamp_rwy_cond(std::atoi(cmd.arg.c_str()));
    }
    if (cmd.op == "lights" && !cmd.have_lights && !cmd.arg.empty()) {
        const int step = clamp_step(std::atoi(cmd.arg.c_str()));
        env.light_step = env.rwy_light_step = env.apch_light_step = env.env_light_step = step;
    }
    if (cmd.have_lights) {
        if (cmd.light_step >= 0) {
            env.light_step = cmd.light_step;
            if (cmd.rwy_light < 0)
                env.rwy_light_step = cmd.light_step;
            if (cmd.apch_light < 0)
                env.apch_light_step = cmd.light_step;
            if (cmd.env_light < 0)
                env.env_light_step = cmd.light_step;
        }
        if (cmd.rwy_light >= 0)
            env.rwy_light_step = cmd.rwy_light;
        if (cmd.apch_light >= 0)
            env.apch_light_step = cmd.apch_light;
        if (cmd.env_light >= 0)
            env.env_light_step = cmd.env_light;
    }
    if (cmd.op == "reposition") {
        if (cmd.have_lla && !(cmd.lat == 0 && cmd.lon == 0 && cmd.alt_ft == 0)) {
            e.reposition_lla = true;
            e.lat = cmd.lat;
            e.lon = cmd.lon;
            e.alt_ft = cmd.alt_ft;
            e.hdg = cmd.hdg;
        } else if (cmd.have_runway && !cmd.icao.empty() && !cmd.runway.empty()) {
            e.reposition = true;
            e.icao = cmd.icao;
            e.runway = cmd.runway;
            e.start_nm = cmd.distance_nm;
            e.on_runway = cmd.on_runway;
            e.preset = lower(cmd.icao) + "-" + lower(cmd.runway) +
                       (cmd.on_runway ? "-rwy" : "");
        } else if (!cmd.arg.empty()) {
            e.reposition = true;
            e.preset = cmd.arg;
        }
    }
    if (cmd.op == "state" && cmd.have_state) {
        e.have_state = true;
        e.state.lat = cmd.lat;
        e.state.lon = cmd.lon;
        e.state.alt_ft = cmd.alt_ft;
        e.state.hdg = cmd.hdg;
        e.state.pitch = cmd.pitch;
        e.state.roll = cmd.roll;
        e.state.gs_kt = cmd.gs_kt;
        e.state.gear_down = cmd.have_gear ? cmd.gear_down : true;
        e.state.flaps = cmd.flaps >= 0 ? cmd.flaps : 30;
        e.state.phase = "adapter";
        e.state.landing_mask = 0x7F;
        e.state.strobe = true;
        if (live) {
            if (cmd.timeout_s > 0)
                live->set_timeout(cmd.timeout_s);
            live->push(e.state);
        }
    }
    return e;
}

std::string ios_page() {
    return R"HTML(<!doctype html><html><head><meta charset="utf-8"><title>s376-fakehost IOS</title>
<style>body{font-family:sans-serif;background:#111;color:#ddd}button{margin:3px;padding:8px}
#st{white-space:pre;font-family:monospace;background:#222;padding:8px}</style></head><body>
<h3>s376-fakehost IOS (loopback)</h3>
<p>JSON API: <a href="/api/v1/status">GET /api/v1/status</a> — see docs/FAKEHOST_API.md</p>
<div>Reposition:
<button onclick="c('reposition','kjfk-04r-6nm')">04R 6 nm final</button>
<button onclick="c('reposition','kjfk-04r-6sm')">04R 6 sm final</button>
<button onclick="c('reposition','kjfk-04r-rwy')">04R on runway</button>
<button onclick="c('reposition','kjfk-13l-6nm')">13L 6 nm final</button>
<button onclick="c('reposition','kjfk-13l-6sm')">13L 6 sm final</button>
<button onclick="c('reposition','kjfk-13l-rwy')">13L on runway</button></div>
<div><button onclick="c('freeze','')">Freeze</button><button onclick="c('unfreeze','')">Unfreeze</button></div>
<div>TOD h <input id=tod size=4 value=14.5><button onclick="c('tod',v('tod'))">set</button>
Vis sm <input id=vis size=4 value=10><button onclick="c('vis',v('vis'))">set</button>
Wind dir/mph <input id=wind size=6 value=040/10><button onclick="c('wind',v('wind'))">set</button>
Ceiling ft MSL <input id=ceil size=5 value=-1><button onclick="c('ceiling',v('ceil'))">set</button>
RVR ft <input id=rvr size=5 value=-1><button onclick="c('rvr',v('rvr'))">set</button>
Lights 0-5 <input id=lit size=2 value=5><button onclick="c('lights',v('lit'))">set</button>
Rwy cond 0 dry / 1 damp / 2 wet / 3+ flooded <input id=rwy size=2 value=0>
<button onclick="c('rwy-cond',v('rwy'))">set</button></div>
<div id=st></div>
<script>function v(i){return document.getElementById(i).value}
function c(op,arg){fetch('/cmd?op='+op+'&arg='+encodeURIComponent(arg)).then(r=>r.text()).then(t=>st.textContent=t)}
setInterval(()=>fetch('/api/v1/status').then(r=>r.text()).then(t=>st.textContent=t),1000)</script>
</body></html>)HTML";
}

namespace {

#ifdef _WIN32
using IosSock = SOCKET;
constexpr IosSock kIosInvalid = INVALID_SOCKET;
void ios_close(IosSock s) {
    if (s != kIosInvalid)
        closesocket(s);
}
void ios_ensure_wsa() {
    static bool once = false;
    if (once)
        return;
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        throw std::runtime_error("IOS: WSAStartup failed");
    once = true;
}
void ios_nonblock(IosSock s) {
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
}
void ios_recv_timeout(IosSock s) {
    DWORD tv = 200;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
}
int ios_send_all(IosSock s, const std::string& r) {
    return ::send(s, r.data(), static_cast<int>(r.size()), 0);
}
#else
using IosSock = int;
constexpr IosSock kIosInvalid = -1;
void ios_close(IosSock s) {
    if (s >= 0)
        ::close(s);
}
void ios_ensure_wsa() {}
void ios_nonblock(IosSock s) { fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK); }
void ios_recv_timeout(IosSock s) {
    timeval tv{0, 200000};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}
int ios_send_all(IosSock s, const std::string& r) {
    return ::send(s, r.data(), r.size(), MSG_NOSIGNAL);
}
#endif

IosSock ios_as(std::intptr_t fd) { return static_cast<IosSock>(fd); }

bool ios_read_request(IosSock c, std::string& method, std::string& target, std::string& body) {
    std::string req;
    char buf[2048];
    for (int i = 0; i < 8; ++i) {
#ifdef _WIN32
        const int n = ::recv(c, buf, sizeof(buf), 0);
#else
        const auto n = ::recv(c, buf, sizeof(buf), 0);
#endif
        if (n <= 0)
            break;
        req.append(buf, static_cast<std::size_t>(n));
        if (req.find("\r\n\r\n") != std::string::npos)
            break;
    }
    if (req.empty())
        return false;
    std::istringstream rs(req);
    rs >> method >> target;
    const auto hdr_end = req.find("\r\n\r\n");
    body.clear();
    std::size_t want = 0;
    const auto cl = req.find("Content-Length:");
    if (cl != std::string::npos)
        want = static_cast<std::size_t>(std::atoi(req.c_str() + cl + 15));
    if (hdr_end != std::string::npos)
        body = req.substr(hdr_end + 4);
    while (body.size() < want) {
#ifdef _WIN32
        const int n = ::recv(c, buf, sizeof(buf), 0);
#else
        const auto n = ::recv(c, buf, sizeof(buf), 0);
#endif
        if (n <= 0)
            break;
        body.append(buf, static_cast<std::size_t>(n));
    }
    return true;
}

void ios_reply(IosSock c, int code, const std::string& type, const std::string& body) {
    const char* reason = code == 200 ? "OK" : code == 400 ? "Bad Request" : "Not Found";
    std::string resp = "HTTP/1.0 " + std::to_string(code) + " " + reason +
                       "\r\nContent-Type: " + type +
                       "\r\nCache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\n"
                       "Content-Length: " +
                       std::to_string(body.size()) + "\r\n\r\n" + body;
    (void)ios_send_all(c, resp);
}

} // namespace

IosServer::~IosServer() {
    if (fd_ != -1)
        ios_close(ios_as(fd_));
    fd_ = -1;
}

void IosServer::start(const std::string& bind_spec) {
    ios_ensure_wsa();
    std::string h;
    std::uint16_t p = 0;
    parse_guarded(bind_spec, h, p, "IOS bind");
    if (h.find(':') != std::string::npos)
        throw std::runtime_error("IOS: use 127.0.0.1");
#ifdef _WIN32
    IosSock s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
    IosSock s = ::socket(AF_INET, SOCK_STREAM, 0);
#endif
    if (s == kIosInvalid)
        throw std::runtime_error("IOS socket failed");
    int one = 1;
#ifdef _WIN32
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
#else
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#endif
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(p);
    inet_pton(AF_INET, h.c_str(), &a.sin_addr);
    guard_sockaddr(&a, sizeof(a), "IOS bind");
    if (::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 || ::listen(s, 8) != 0) {
        ios_close(s);
        throw std::runtime_error("IOS bind failed on " + bind_spec);
    }
    ios_nonblock(s);
    socklen_t l = sizeof(a);
    getsockname(s, reinterpret_cast<sockaddr*>(&a), &l);
    port_ = ntohs(a.sin_port);
    fd_ = static_cast<std::intptr_t>(s);
}

std::vector<IosCommand> IosServer::poll(const IosStatus& st) {
    std::vector<IosCommand> out;
    if (fd_ == -1)
        return out;
    const IosSock ls = ios_as(fd_);
    for (int k = 0; k < 8; ++k) {
        sockaddr_in from{};
        socklen_t fl = sizeof(from);
        const IosSock c = ::accept(ls, reinterpret_cast<sockaddr*>(&from), &fl);
        if (c == kIosInvalid)
            break;
        try {
            guard_sockaddr(&from, fl, "IOS client");
        } catch (const Refused&) {
            ios_close(c);
            continue;
        }
        ios_recv_timeout(c);
        std::string method, target, req_body;
        if (!ios_read_request(c, method, target, req_body)) {
            ios_close(c);
            continue;
        }
        const std::string path_only = target.substr(0, target.find('?'));
        char sb[640];
        std::snprintf(sb, sizeof(sb),
                      "mode=%s preset=%s phase=%s frozen=%d t=%.1f\nlat=%.6f lon=%.6f alt=%.0fft "
                      "wheel_agl=%.0fft gs=%.0fkt\nhat_replies=%d last_hat=%.1fft contacts=%d\n",
                      st.mode.c_str(), st.preset.c_str(), st.phase.c_str(), st.frozen ? 1 : 0, st.t,
                      st.lat, st.lon, st.alt, st.wheel_agl, st.gs_kt, st.hat_replies, st.last_hat,
                      st.contacts);
        if (path_only == "/" || path_only == "/index.html") {
            ios_reply(c, 200, "text/html", ios_page());
        } else if (path_only == "/status") {
            ios_reply(c, 200, "text/plain", sb);
        } else if (path_only == "/api/v1/status") {
            ios_reply(c, 200, "application/json", ios_status_json(st));
        } else if (path_only.rfind("/api/v1/", 0) == 0) {
            IosCommand cmd = parse_ios_json(path_only, req_body);
            if (cmd.op.empty()) {
                ios_reply(c, 400, "application/json",
                          "{\"ok\":false,\"error\":\"unknown command\"}");
            } else {
                out.push_back(cmd);
                ios_reply(c, 200, "application/json",
                          "{\"ok\":true,\"op\":\"" + cmd.op + "\"}");
            }
        } else {
            IosCommand cmd = parse_ios_query(target);
            if (cmd.op.empty()) {
                ios_reply(c, 400, "text/plain", "unknown command\n");
            } else {
                out.push_back(cmd);
                ios_reply(c, 200, "text/plain", "ok " + cmd.op + " " + cmd.arg + "\n" + sb);
            }
        }
        ios_close(c);
    }
    return out;
}

// ================================================================= wire

std::vector<std::uint8_t> build_frame_datagram(const Frame& f, unsigned count,
                                               const Environment& env, xt4::WordOrder wo,
                                               xt4::FloatFmt ff, bool hat_query) {
    std::vector<std::vector<std::uint16_t>> pk;
    pk.push_back(xt4::build_dcs(f.lat, f.lon, f.alt_ft, f.hdg, static_cast<float>(f.pitch),
                                static_cast<float>(f.roll), count, ff));
    if (hat_query)
        pk.push_back(xt4::build_hat_query());
    pk.push_back(xt4::build_lights(true, f.strobe, f.landing_mask, ff));
    pk.push_back(xt4::build_ownship(f.gear_down ? 1 : 0, f.flaps));
    if (count == 1 || count % 30 == 0) {
        pk.push_back(xt4::build_env(env.tod_hours, std::min(env.vis_ft, xt4::kMaxVisibilityFt),
                                    env.month, env.day, env.year, ff, -1, env.precip_type,
                                    env.precip_intensity, 0, env.wind_mph, env.wind_dir,
                                    env.runway_condition));
        if (env.rvr_ft > 0)
            pk.push_back(xt4::build_fog(env.fog_top_ft, env.rvr_ft, ff, -1, 0));
        else
            pk.push_back(xt4::build_fog(0, 0, ff, -1, -1));
        if (env.ceiling_ft_msl > 0)
            pk.push_back(xt4::build_cloud(xt4::kTypeLowerCloud, 0, env.ceiling_ft_msl,
                                         env.cloud_top_ft_msl > env.ceiling_ft_msl
                                             ? env.cloud_top_ft_msl
                                             : env.ceiling_ft_msl + 2000.f,
                                         ff, -1, 3, -1.f, env.wind_mph / 1.15078f, env.wind_dir));
        else
            pk.push_back(xt4::build_cloud(xt4::kTypeLowerCloud, -1, 0, 0, ff));
        if (env.upper_ceiling_ft_msl > 0)
            pk.push_back(xt4::build_cloud(xt4::kTypeUpperCloud, 0, env.upper_ceiling_ft_msl,
                                         env.upper_top_ft_msl > env.upper_ceiling_ft_msl
                                             ? env.upper_top_ft_msl
                                             : env.upper_ceiling_ft_msl + 2000.f,
                                         ff, -1, 3, -1.f, env.wind_mph / 1.15078f, env.wind_dir));
        else
            pk.push_back(xt4::build_cloud(xt4::kTypeUpperCloud, -1, 0, 0, ff));
    }
    if (count == 1 || count % 100 == 2) {
        pk.push_back(xt4::build_display(true));
        // s376-synth: 1 edge, 2 centreline, 3 approach, 4 threshold/end.
        const int rwy = env.rwy_light_step >= 0 ? env.rwy_light_step : env.light_step;
        const int apch = env.apch_light_step >= 0 ? env.apch_light_step : env.light_step;
        const int envl = env.env_light_step >= 0 ? env.env_light_step : env.light_step;
        pk.push_back(xt4::build_light_switch(1, rwy));
        pk.push_back(xt4::build_light_switch(2, rwy));
        pk.push_back(xt4::build_light_switch(3, apch));
        pk.push_back(xt4::build_light_switch(4, rwy));
        pk.push_back(xt4::build_light_switch(41, envl)); // taxi edge
        pk.push_back(xt4::build_light_switch(56, envl)); // stop bar
        pk.push_back(xt4::build_light_switch(60, envl)); // taxi centreline
    }
    return xt4::build_datagram(pk, wo);
}

void run_approach_sender(const std::string& dest_spec, const std::string& listen_spec,
                         const ProfileConfig& pc, const Environment& env, double fps,
                         double duration_s, std::atomic<bool>* stop) {
    std::string dhost, lhost;
    std::uint16_t dport = 2300, lport = 2301;
    parse_guarded(dest_spec, dhost, dport, "demo dest");
    parse_guarded(listen_spec, lhost, lport, "demo listen");
#ifdef _WIN32
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#else
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
#endif
    if (
#ifdef _WIN32
        fd == INVALID_SOCKET
#else
        fd < 0
#endif
    )
        throw std::runtime_error("fakehost sender: socket");
    sockaddr_in la{};
    la.sin_family = AF_INET;
    la.sin_port = htons(lport);
    if (inet_pton(AF_INET, lhost.c_str(), &la.sin_addr) != 1)
        throw std::runtime_error("fakehost sender: listen");
    guard_sockaddr(&la, sizeof(la), "demo bind");
    const int one = 1;
#ifdef _WIN32
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
#else
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#endif
    if (::bind(fd, reinterpret_cast<sockaddr*>(&la), sizeof(la)) != 0)
        throw std::runtime_error("fakehost sender: bind " + listen_spec);
    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(dport);
    if (inet_pton(AF_INET, dhost.c_str(), &dest.sin_addr) != 1)
        throw std::runtime_error("fakehost sender: dest");
    guard_sockaddr(&dest, sizeof(dest), "demo send");

    ApproachProfile ap(pc);
    const double dt = 1.0 / std::max(1.0, fps);
    unsigned count = 0;
    auto next = std::chrono::steady_clock::now();
    const auto t0 = next;
    while (!ap.done() && !(stop && stop->load())) {
        if (duration_s >= 0) {
            const double elapsed =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (elapsed >= duration_s)
                break;
        }
        const Frame f = ap.step(dt);
        count = count % 32767 + 1;
        const auto dg = build_frame_datagram(f, count, env, xt4::WordOrder::Le, xt4::FloatFmt::Vax);
        guard_sockaddr(&dest, sizeof(dest), "demo send");
#ifdef _WIN32
        sendto(fd, reinterpret_cast<const char*>(dg.data()), static_cast<int>(dg.size()), 0,
               reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
#else
        ::sendto(fd, dg.data(), dg.size(), 0, reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
#endif
        next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(dt));
        std::this_thread::sleep_until(next);
    }
#ifdef _WIN32
    closesocket(fd);
    WSACleanup();
#else
    ::close(fd);
#endif
}

// ================================================================= checks

bool decode_hat_reply(const std::uint8_t* d, std::size_t n, HatReply& out) {
    if (n < 64)
        return false;
    auto w = [d](int i) { return static_cast<std::uint16_t>((d[2 * i] << 8) | d[2 * i + 1]); };
    if (w(0) != xt4::kMagicWord0 || w(1) != xt4::kMagicWord1)
        return false;
    if (((static_cast<std::uint32_t>(w(4)) << 16) | w(5)) != static_cast<std::uint32_t>(xt4::kTypeHat))
        return false;
    auto f = [&](int i) {
        std::uint32_t u = (static_cast<std::uint32_t>(w(i)) << 16) | w(i + 1);
        float x;
        std::memcpy(&x, &u, 4);
        return x;
    };
    out.valid = w(10) != 0;
    out.hat_ft = f(22);
    out.elev_ft = f(28);
    return true;
}

void Checker::note(const Frame& sent, const HatReply& r, double rwy_elev_ft) {
    ++replies;
    if (!r.valid) {
        ++invalid;
        return;
    }
    char buf[256];
    if (r.hat_ft <= 0.f) {
        ++contacts;
        std::snprintf(buf, sizeof(buf),
                      "CONTACT t=%.2f hat=%.1fft elev=%.1fft host_wheel_agl=%.1fft lat=%.6f lon=%.6f",
                      sent.t, r.hat_ft, r.elev_ft, sent.wheel_agl_ft, sent.lat, sent.lon);
        if (contacts <= 50)
            events.push_back(buf);
    }
    if (sent.on_ground && !have_td) {
        have_td = true;
        td_hat_wheel_ft = r.hat_ft - kEyeHeightFt;
        std::snprintf(buf, sizeof(buf),
                      "TOUCHDOWN t=%.2f reply_hat=%.1fft (wheel %.1fft) ig_elev=%.1fft rwy_elev=%.1fft",
                      sent.t, r.hat_ft, td_hat_wheel_ft, r.elev_ft, rwy_elev_ft);
        events.push_back(buf);
    }
    if (!sent.on_ground) {
        const double expect = sent.alt_ft - rwy_elev_ft;
        max_air_err_ft = std::max(max_air_err_ft, std::fabs(r.hat_ft - expect));
    }
}

bool Checker::touchdown_ok() const {
    return have_td && std::fabs(td_hat_wheel_ft) <= td_tol_ft;
}

std::string Checker::summary() const {
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "hat replies=%d invalid=%d contacts=%d touchdown=%s wheel_hat=%.1fft "
                  "(tol %.0f) %s max_airborne_dev=%.1fft",
                  replies, invalid, contacts, have_td ? "seen" : "none", td_hat_wheel_ft, td_tol_ft,
                  have_td ? (touchdown_ok() ? "OK" : "FAIL") : "-", max_air_err_ft);
    return buf;
}

} // namespace s376::fakehost
