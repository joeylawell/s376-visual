// s376-fakehost core: loopback guard, 737-300 approach profile, trajectory
// replay, simple manual dynamics, datagram build and HAT checks.
// Wire format is the existing clean-room xT4 subset (docs/PROTOCOL.md) built
// with the same xt4:: builders s376-synth uses. No new packet types.

#include "s376/fakehost.hpp"

#include "s376/geo.hpp"
#include "s376/lights.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
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
    hdg_ = geo::wrap360(hdg_ + turn * dt);
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
    static const char* ok[] = {"reposition", "freeze", "unfreeze", "tod", "vis", "wind",
                               "ceiling", "rvr", "lead-in", "quit", nullptr};
    bool known = false;
    for (int i = 0; ok[i]; ++i)
        known |= c.op == ok[i];
    if (!known)
        c.op.clear();
    return c;
}

std::string ios_page() {
    return R"HTML(<!doctype html><html><head><meta charset="utf-8"><title>s376-fakehost IOS</title>
<style>body{font-family:sans-serif;background:#111;color:#ddd}button{margin:3px;padding:8px}
#st{white-space:pre;font-family:monospace;background:#222;padding:8px}</style></head><body>
<h3>s376-fakehost IOS (loopback)</h3>
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
13L lead-in <button onclick="c('lead-in','0')">off (ILS)</button>
<button onclick="c('lead-in','5')">on</button></div>
<div id=st></div>
<script>function v(i){return document.getElementById(i).value}
function c(op,arg){fetch('/cmd?op='+op+'&arg='+encodeURIComponent(arg)).then(r=>r.text()).then(t=>st.textContent=t)}
setInterval(()=>fetch('/status').then(r=>r.text()).then(t=>st.textContent=t),1000)</script>
</body></html>)HTML";
}

IosServer::~IosServer() {
#ifndef _WIN32
    if (fd_ >= 0)
        ::close(fd_);
#endif
}

void IosServer::start(const std::string& bind_spec) {
#ifndef _WIN32
    std::string h;
    std::uint16_t p = 0;
    parse_guarded(bind_spec, h, p, "IOS bind");
    if (h.find(':') != std::string::npos)
        throw std::runtime_error("IOS: use 127.0.0.1");
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(p);
    inet_pton(AF_INET, h.c_str(), &a.sin_addr);
    guard_sockaddr(&a, sizeof(a), "IOS bind");
    if (::bind(fd_, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 || ::listen(fd_, 8) != 0)
        throw std::runtime_error("IOS bind failed on " + bind_spec);
    fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL) | O_NONBLOCK);
    socklen_t l = sizeof(a);
    getsockname(fd_, reinterpret_cast<sockaddr*>(&a), &l);
    port_ = ntohs(a.sin_port);
#else
    (void)bind_spec;
    throw std::runtime_error("IOS server: POSIX only for now");
#endif
}

std::vector<IosCommand> IosServer::poll(const IosStatus& st) {
    std::vector<IosCommand> out;
#ifndef _WIN32
    if (fd_ < 0)
        return out;
    for (int k = 0; k < 8; ++k) {
        sockaddr_in from{};
        socklen_t fl = sizeof(from);
        const int c = ::accept(fd_, reinterpret_cast<sockaddr*>(&from), &fl);
        if (c < 0)
            break;
        try {
            guard_sockaddr(&from, fl, "IOS client");
        } catch (const Refused&) {
            ::close(c);
            continue;
        }
        timeval tv{0, 200000};
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        char buf[2048];
        const auto n = ::recv(c, buf, sizeof(buf) - 1, 0);
        std::string req = n > 0 ? std::string(buf, static_cast<std::size_t>(n)) : "";
        std::string target;
        {
            std::istringstream rs(req);
            std::string method;
            rs >> method >> target;
        }
        std::string body, type = "text/plain";
        char sb[512];
        std::snprintf(sb, sizeof(sb),
                      "mode=%s preset=%s phase=%s frozen=%d t=%.1f\nlat=%.6f lon=%.6f alt=%.0fft "
                      "wheel_agl=%.0fft gs=%.0fkt\nhat_replies=%d last_hat=%.1fft contacts=%d\n",
                      st.mode.c_str(), st.preset.c_str(), st.phase.c_str(), st.frozen ? 1 : 0,
                      st.t, st.lat, st.lon, st.alt, st.wheel_agl, st.gs_kt, st.hat_replies,
                      st.last_hat, st.contacts);
        if (target == "/" || target == "/index.html") {
            body = ios_page();
            type = "text/html";
        } else if (target == "/status") {
            body = sb;
        } else {
            IosCommand cmd = parse_ios_query(target);
            if (cmd.op.empty()) {
                body = "unknown command\n";
            } else {
                out.push_back(cmd);
                body = "ok " + cmd.op + " " + cmd.arg + "\n" + sb;
            }
        }
        std::string resp = "HTTP/1.0 200 OK\r\nContent-Type: " + type +
                           "\r\nCache-Control: no-store\r\nContent-Length: " +
                           std::to_string(body.size()) + "\r\n\r\n" + body;
        (void)::send(c, resp.data(), resp.size(), MSG_NOSIGNAL);
        ::close(c);
    }
#else
    (void)st;
#endif
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
                                    env.precip_intensity, 0, env.wind_mph, env.wind_dir));
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
    }
    if (count == 1 || count % 100 == 2) {
        pk.push_back(xt4::build_display(true));
        // Every runway group (04L/04R/13L/13R), not just switches 1–4
        // (those only cover 04L). Default step lights 13L at tod 19.
        for (int k = 1; k <= 4; ++k) {
            for (int off = 1; off <= 8; ++off)
                pk.push_back(xt4::build_light_switch(s376::lights::runway_switch(k, off),
                                                    env.light_step));
        }
        pk.push_back(xt4::build_light_switch(70, env.lead_in_step)); // 13L RLLS lead-in
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
