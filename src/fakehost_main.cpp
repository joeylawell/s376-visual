// s376-fakehost — offline 737-300 host stand-in for s376-visual.
// Loopback only. Same xT4 subset as s376-synth (docs/PROTOCOL.md).

#include "s376/config.hpp"
#include "s376/fakehost.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#ifndef _WIN32
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/input.h>
#include <sys/ioctl.h>
#endif
#endif

namespace fh = s376::fakehost;

namespace {

std::atomic<bool> g_stop{false};
void on_sig(int) { g_stop = true; }

void usage() {
    std::cerr <<
        "s376-fakehost — offline 737-300 host stand-in for s376-visual (loopback only)\n"
        "usage: s376-fakehost (--profile NAME | --replay FILE | --manual) [options]\n"
        "  --profile NAME      kjfk-04r | kjfk-13l | <icao>-<rwy> (any end in the FAA table)\n"
        "  --six-mile-final    IOS-style reposition: on centreline + glideslope, gear down,\n"
        "                      flaps 30, Vref+5 from the first frame\n"
        "  --start DIST        start distance from threshold: 6nm (default), 6sm, 10nm, 11km\n"
        "  --gs DEG            glideslope (default 3.0)\n"
        "  --vref KT           Vref flaps 30 (default 140; approach speed Vref+5)\n"
        "  --replay FILE       timestamped .csv or .toml trajectory\n"
        "  --manual            keyboard flight (w/s pitch, a/d roll, r/f throttle, g gear,\n"
        "                      p flaps, l lights, q quit); --joystick /dev/input/eventN\n"
        "  --fps N             host frame rate (default 60)\n"
        "  --duration SEC      stop after SEC (default: end of profile/replay)\n"
        "  --dest HOST:PORT    renderer host port (default 127.0.0.1:2300); repeat for\n"
        "                      N channels (each renderer --bind 127.0.0.1:23x0)\n"
        "  --ios HOST:PORT     IOS web page on loopback, e.g. 127.0.0.1:8376\n"
        "  --cockpit-io EP     future cockpit-IO bridge (stub interface, loopback EP)\n"
        "  --listen HOST:PORT  HAT reply port (default 127.0.0.1:2301)\n"
        "  --encoding O/F      le/vax (default) | be/vax | le/ieee | be/ieee\n"
        "  --tod HOURS         time of day (default 14.5)\n"
        "  --vis-sm SM         global visibility statute miles (default 10)\n"
        "  --wind DIR/MPH      surface wind (default calm)\n"
        "  --ceiling FT        lower cloud base, ft MSL (default none)\n"
        "  --rvr FT            fog layer with this RVR (default none)\n"
        "  --record-out CSV    write the frames sent (replayable)\n"
        "  --fast              do not pace in real time (tests)\n"
        "  --dry-run           no sockets; print frames as CSV to stdout\n"
        "  --hat-tol FT        touchdown HAT tolerance (default 20)\n"
        "  --check             exit 3 if no HAT replies or touchdown HAT is off\n"
        "  -q, --quiet\n";
}

#ifndef _WIN32
struct RawTerm {
    termios old{};
    bool on = false;
    void enable() {
        if (!isatty(0) || tcgetattr(0, &old) != 0)
            return;
        termios t = old;
        t.c_lflag &= ~(ICANON | ECHO);
        t.c_cc[VMIN] = 0;
        t.c_cc[VTIME] = 0;
        tcsetattr(0, TCSANOW, &t);
        on = true;
    }
    ~RawTerm() {
        if (on)
            tcsetattr(0, TCSANOW, &old);
    }
};
#endif

#ifndef _WIN32
class KeyboardSource : public fh::ControlSource {
  public:
    const char* name() const override { return "keyboard"; }
    void poll(fh::Controls& ctl) override {
        char c;
        ctl.elevator *= 0.7;
        ctl.aileron *= 0.7;
        while (::read(0, &c, 1) == 1) {
            switch (c) {
            case 'w': ctl.elevator = -1; break; // stick forward
            case 's': ctl.elevator = 1; break;
            case 'a': ctl.aileron = -1; break;
            case 'd': ctl.aileron = 1; break;
            case 'r': ctl.throttle = std::min(1.0, ctl.throttle + 0.05); break;
            case 'f': ctl.throttle = std::max(0.0, ctl.throttle - 0.05); break;
            case 'g': ctl.gear_down = !ctl.gear_down; break;
            case 'p': ctl.flaps = ctl.flaps == 30 ? 40 : ctl.flaps == 40 ? 0 : ctl.flaps == 0 ? 15 : 30; break;
            case 'l': ctl.lights = !ctl.lights; break;
            case 'q': g_stop = true; break;
            default: break;
            }
        }
    }
};
#ifdef __linux__
// USB joystick / yoke / throttle quadrant through Linux evdev.
class EvdevSource : public fh::ControlSource {
  public:
    explicit EvdevSource(const std::string& path) : path_(path) {
        fd_ = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd_ < 0)
            std::perror(path.c_str());
    }
    ~EvdevSource() override { if (fd_ >= 0) ::close(fd_); }
    const char* name() const override { return "evdev"; }
    bool ok() const override { return fd_ >= 0; }
    void poll(fh::Controls& ctl) override {
        input_event ev;
        while (fd_ >= 0 && ::read(fd_, &ev, sizeof(ev)) == sizeof(ev)) {
            if (ev.type == EV_KEY && ev.value == 1) {
                if (ev.code == BTN_TRIGGER || ev.code == BTN_SOUTH) ctl.gear_down = !ctl.gear_down;
                if (ev.code == BTN_THUMB || ev.code == BTN_EAST)
                    ctl.flaps = ctl.flaps == 30 ? 40 : ctl.flaps == 40 ? 15 : 30;
                continue;
            }
            if (ev.type != EV_ABS)
                continue;
            input_absinfo ai{};
            if (ioctl(fd_, EVIOCGABS(ev.code), &ai) != 0 || ai.maximum == ai.minimum)
                continue;
            double u = 2.0 * (ev.value - ai.minimum) / (ai.maximum - ai.minimum) - 1.0;
            if (std::fabs(u) < 0.04) u = 0; // dead zone
            if (ev.code == ABS_X) ctl.aileron = u;
            else if (ev.code == ABS_Y) ctl.elevator = -u;
            else if (ev.code == ABS_THROTTLE || ev.code == ABS_Z) ctl.throttle = (1 - u) / 2;
        }
    }
  private:
    std::string path_;
    int fd_ = -1;
};
#endif
#endif

struct Sock {
    int fd = -1;
    sockaddr_storage dest{};
    socklen_t dlen = 0;
    std::vector<std::pair<sockaddr_storage, socklen_t>> extra; // more renderer channels
    void add_dest(const std::string& h, std::uint16_t p) {
        sockaddr_storage d{};
        socklen_t l;
        if (h.find(':') != std::string::npos) {
            auto* a = reinterpret_cast<sockaddr_in6*>(&d);
            a->sin6_family = AF_INET6; a->sin6_port = htons(p);
            inet_pton(AF_INET6, h.c_str(), &a->sin6_addr); l = sizeof(sockaddr_in6);
        } else {
            auto* a = reinterpret_cast<sockaddr_in*>(&d);
            a->sin_family = AF_INET; a->sin_port = htons(p);
            inet_pton(AF_INET, h.c_str(), &a->sin_addr); l = sizeof(sockaddr_in);
        }
        if (d.ss_family != dest.ss_family)
            throw fh::Refused("all --dest must share one address family");
        fh::guard_sockaddr(&d, l, "send to");
        extra.emplace_back(d, l);
    }
    bool open(const std::string& dhost, std::uint16_t dport, const std::string& lhost,
              std::uint16_t lport) {
        const bool v6 = dhost.find(':') != std::string::npos;
        fd = ::socket(v6 ? AF_INET6 : AF_INET, SOCK_DGRAM, 0);
        if (fd < 0)
            return false;
        sockaddr_storage la{};
        socklen_t llen;
        if (v6) {
            auto* d = reinterpret_cast<sockaddr_in6*>(&dest);
            d->sin6_family = AF_INET6;
            d->sin6_port = htons(dport);
            inet_pton(AF_INET6, dhost.c_str(), &d->sin6_addr);
            dlen = sizeof(sockaddr_in6);
            auto* l = reinterpret_cast<sockaddr_in6*>(&la);
            l->sin6_family = AF_INET6;
            l->sin6_port = htons(lport);
            inet_pton(AF_INET6, lhost.find(':') != std::string::npos ? lhost.c_str() : "::1",
                      &l->sin6_addr);
            llen = sizeof(sockaddr_in6);
        } else {
            auto* d = reinterpret_cast<sockaddr_in*>(&dest);
            d->sin_family = AF_INET;
            d->sin_port = htons(dport);
            if (inet_pton(AF_INET, dhost.c_str(), &d->sin_addr) != 1)
                return false;
            dlen = sizeof(sockaddr_in);
            auto* l = reinterpret_cast<sockaddr_in*>(&la);
            l->sin_family = AF_INET;
            l->sin_port = htons(lport);
            if (inet_pton(AF_INET, lhost.c_str(), &l->sin_addr) != 1)
                return false;
            llen = sizeof(sockaddr_in);
        }
        fh::guard_sockaddr(&dest, dlen, "send to");
        fh::guard_sockaddr(&la, llen, "bind");
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        if (::bind(fd, reinterpret_cast<sockaddr*>(&la), llen) != 0) {
            std::perror("bind");
            return false;
        }
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
        return true;
    }
    void send(const std::vector<std::uint8_t>& b) {
        fh::guard_sockaddr(&dest, dlen, "send to"); // every datagram
        ::sendto(fd, b.data(), b.size(), 0, reinterpret_cast<sockaddr*>(&dest), dlen);
        for (auto& e : extra) {
            fh::guard_sockaddr(&e.first, e.second, "send to");
            ::sendto(fd, b.data(), b.size(), 0, reinterpret_cast<sockaddr*>(&e.first), e.second);
        }
    }
    int recv(std::uint8_t* buf, std::size_t n) {
        sockaddr_storage from{};
        socklen_t fl = sizeof(from);
        const auto r = ::recvfrom(fd, buf, n, 0, reinterpret_cast<sockaddr*>(&from), &fl);
        if (r <= 0)
            return -1;
        try {
            fh::guard_sockaddr(&from, fl, "reply from");
        } catch (const fh::Refused&) {
            return -1; // ignore anything not from loopback
        }
        return static_cast<int>(r);
    }
    ~Sock() {
        if (fd >= 0)
            ::close(fd);
    }
};

} // namespace

int main(int argc, char** argv) {
    std::string profile, replay, dest = "127.0.0.1:2300", listen = "127.0.0.1:2301";
    std::string start = "6nm", record_out, joystick, encoding = "le/vax", ios_bind, cockpit_io;
    std::vector<std::string> extra_dests;
    bool dest_given = false;
    bool manual = false, six = false, fast = false, dry = false, check = false, quiet = false;
    bool start_given = false;
    double fps = 60, duration = -1, gs = 3.0, vref = 140, hat_tol = 20;
    fh::Environment env;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto need = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << a << " needs an argument\n";
                std::exit(2);
            }
            return argv[++i];
        };
        try {
            if (a == "-h" || a == "--help") { usage(); return 0; }
            else if (a == "--profile") profile = need();
            else if (a == "--replay") replay = need();
            else if (a == "--manual") manual = true;
            else if (a == "--six-mile-final") six = true;
            else if (a == "--start") { start = need(); start_given = true; }
            else if (a == "--gs") gs = std::stod(need());
            else if (a == "--vref") vref = std::stod(need());
            else if (a == "--fps") fps = std::stod(need());
            else if (a == "--duration") duration = std::stod(need());
            else if (a == "--dest") { if (dest_given) extra_dests.push_back(need()); else { dest = need(); dest_given = true; } }
            else if (a == "--ios") ios_bind = need();
            else if (a == "--cockpit-io") cockpit_io = need();
            else if (a == "--listen") listen = need();
            else if (a == "--encoding") encoding = need();
            else if (a == "--tod") env.tod_hours = std::stof(need());
            else if (a == "--vis-sm") env.vis_ft = std::stof(need()) * 5280.f;
            else if (a == "--wind") {
                const std::string w = need();
                const auto sl = w.find('/');
                env.wind_dir = std::stof(w.substr(0, sl));
                env.wind_mph = sl == std::string::npos ? 0 : std::stof(w.substr(sl + 1));
            } else if (a == "--ceiling") env.ceiling_ft_msl = std::stof(need());
            else if (a == "--rvr") { env.rvr_ft = std::stof(need()); env.fog_top_ft = 300; }
            else if (a == "--record-out") record_out = need();
            else if (a == "--joystick") joystick = need();
            else if (a == "--fast") fast = true;
            else if (a == "--dry-run") dry = true;
            else if (a == "--check") check = true;
            else if (a == "--hat-tol") hat_tol = std::stod(need());
            else if (a == "-q" || a == "--quiet") quiet = true;
            else { std::cerr << "unknown option " << a << "\n"; usage(); return 2; }
        } catch (const std::exception& ex) {
            std::cerr << a << ": " << ex.what() << "\n";
            return 2;
        }
    }
    (void)start_given;
    if ((!profile.empty()) + (!replay.empty()) + manual != 1) {
        usage();
        return 2;
    }
    fps = std::max(1.0, std::min(fps, 240.0));

    // Guard first, before any socket exists.
    std::string dhost, lhost;
    std::uint16_t dport = 0, lport = 0;
    try {
        fh::parse_guarded(dest, dhost, dport, "send to");
        fh::parse_guarded(listen, lhost, lport, "bind");
        for (const auto& d : extra_dests) {
            std::string h; std::uint16_t p;
            fh::parse_guarded(d, h, p, "send to");
        }
        if (!ios_bind.empty()) {
            std::string h; std::uint16_t p;
            fh::parse_guarded(ios_bind, h, p, "IOS bind");
        }
    } catch (const fh::Refused& ex) {
        std::cerr << ex.what() << "\n";
        return 4;
    }

    const auto slash = encoding.find('/');
    const auto wo = s376::parse_word_order(encoding.substr(0, slash));
    const auto ff = s376::parse_float_fmt(slash == std::string::npos ? "vax" : encoding.substr(slash + 1));

    fh::Runway rwy;
    std::unique_ptr<fh::ApproachProfile> prof;
    std::unique_ptr<fh::ManualFlight> man;
    std::vector<fh::Frame> traj;
    try {
        double start_nm = fh::parse_distance_nm(start);
        if (!profile.empty() || manual) {
            std::string p = profile.empty() ? "kjfk-04r" : profile;
            const auto dash = p.find('-');
            if (dash == std::string::npos)
                throw std::invalid_argument("profile must look like kjfk-04r");
            rwy = fh::runway_from_faa(p.substr(0, dash), p.substr(dash + 1));
            std::fprintf(stderr,
                         "runway %s %s threshold %.7f %.7f elev %.1f ft brg %.1f T displaced %.0f ft\n"
                         "  source: %s\n",
                         rwy.icao.c_str(), rwy.ident.c_str(), rwy.thr_lat, rwy.thr_lon, rwy.elev_ft,
                         rwy.brg_true, rwy.displaced_ft, rwy.source.c_str());
            std::fprintf(stderr, "start %.2f nm (%s), glidepath %.0f ft AGL, TCH %.1f ft\n", start_nm,
                         start.c_str(), fh::glidepath_height_ft(-start_nm * 6076.115, gs),
                         fh::threshold_crossing_height_ft(gs));
        }
        if (!profile.empty()) {
            fh::ProfileConfig pc;
            pc.rwy = rwy;
            pc.start_nm = start_nm;
            pc.gs_deg = gs;
            pc.vref_kt = vref;
            pc.landing_config_at_start = six;
            prof = std::make_unique<fh::ApproachProfile>(pc);
        } else if (manual) {
            man = std::make_unique<fh::ManualFlight>(rwy, start_nm, gs);
        } else {
            traj = fh::load_trajectory(replay);
            std::fprintf(stderr, "replay %zu points, %.1f s\n", traj.size(), traj.back().t - traj.front().t);
        }
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << "\n";
        return 2;
    }

    std::signal(SIGINT, on_sig);
    std::signal(SIGTERM, on_sig);

    Sock sock;
    if (!dry) {
        try {
            if (!sock.open(dhost, dport, lhost, lport)) {
                std::cerr << "socket setup failed\n";
                return 1;
            }
            for (const auto& d : extra_dests) {
                std::string h; std::uint16_t p;
                fh::parse_guarded(d, h, p, "send to");
                sock.add_dest(h, p);
            }
        } catch (const fh::Refused& ex) {
            std::cerr << ex.what() << "\n";
            return 4;
        }
        std::fprintf(stderr, "sending %s at %.0f Hz to %s:%u, HAT replies on %s:%u\n",
                     encoding.c_str(), fps, dhost.c_str(), dport, lhost.c_str(), lport);
    }

    std::vector<std::unique_ptr<fh::ControlSource>> sources;
#ifndef _WIN32
    RawTerm term;
    if (manual) {
        term.enable();
        sources.push_back(std::make_unique<KeyboardSource>());
#ifdef __linux__
        if (!joystick.empty())
            sources.push_back(std::make_unique<EvdevSource>(joystick));
#endif
    }
#endif
    if (!cockpit_io.empty()) {
        try {
            fh::CockpitIoConfig cc;
            cc.endpoint = cockpit_io;
            sources.push_back(std::make_unique<fh::CockpitIoBridge>(cc));
        } catch (const fh::Refused& ex) {
            std::cerr << ex.what() << "\n";
            return 4;
        }
    }
    for (const auto& src : sources)
        std::fprintf(stderr, "input: %s%s\n", src->name(), src->ok() ? "" : " (not available)");
    fh::IosServer ios;
    fh::IosStatus ist;
    if (!ios_bind.empty()) {
        try {
            ios.start(ios_bind);
        } catch (const std::exception& ex) {
            std::cerr << ex.what() << "\n";
            return 4;
        }
        std::fprintf(stderr, "IOS: http://127.0.0.1:%u/\n", ios.port());
    }
    bool frozen = false;
    ist.mode = manual ? "manual" : prof ? "profile" : "replay";
    ist.preset = profile.empty() ? (manual ? "kjfk-04r-" + start : replay) : profile + "-" + start;
    long tick = 0;
    fh::Controls ctl;
    ctl.flaps = 30;
    ctl.throttle = 0.45;

    const double dt = 1.0 / fps;
    std::vector<fh::Frame> sent;
    fh::Checker chk;
    chk.td_tol_ft = hat_tol;
    fh::Frame last;
    std::string last_phase;
    unsigned count = 0;
    const double t0 = traj.empty() ? 0 : traj.front().t;
    auto next = std::chrono::steady_clock::now();
    std::uint8_t rbuf[2048];
    if (dry)
        std::cout << "t,lat,lon,alt_ft,hdg,pitch,roll,gs_kt,gear,flaps,landing_mask,strobe,wheel_agl,phase\n";

    for (long n = 0; !g_stop; ++tick) {
        if (ios.port()) {
            ist.frozen = frozen;
            ist.t = last.t; ist.phase = last.phase; ist.wheel_agl = last.wheel_agl_ft;
            ist.gs_kt = last.gs_kt; ist.lat = last.lat; ist.lon = last.lon; ist.alt = last.alt_ft;
            ist.hat_replies = chk.replies; ist.contacts = chk.contacts;
            for (const auto& cmd : ios.poll(ist)) {
                try {
                    if (cmd.op == "freeze") frozen = true;
                    else if (cmd.op == "unfreeze") frozen = false;
                    else if (cmd.op == "quit") g_stop = true;
                    else if (cmd.op == "tod") env.tod_hours = std::stof(cmd.arg);
                    else if (cmd.op == "vis") env.vis_ft = std::stof(cmd.arg) * 5280.f;
                    else if (cmd.op == "ceiling") env.ceiling_ft_msl = std::stof(cmd.arg);
                    else if (cmd.op == "rvr") { env.rvr_ft = std::stof(cmd.arg); env.fog_top_ft = 300; }
                    else if (cmd.op == "lead-in") {
                        const int s = std::atoi(cmd.arg.c_str());
                        env.lead_in_step = s < 0 ? 0 : s > 5 ? 5 : s;
                    }
                    else if (cmd.op == "wind") {
                        const auto sl = cmd.arg.find('/');
                        env.wind_dir = std::stof(cmd.arg.substr(0, sl));
                        env.wind_mph = sl == std::string::npos ? 0 : std::stof(cmd.arg.substr(sl + 1));
                    } else if (cmd.op == "reposition") {
                        std::string ic, rw; double snm; bool onr;
                        if (!fh::parse_preset(cmd.arg, ic, rw, snm, onr))
                            throw std::invalid_argument("bad preset " + cmd.arg);
                        rwy = fh::runway_from_faa(ic, rw);
                        if (man || onr) {
                            if (!man) { man = std::make_unique<fh::ManualFlight>(rwy, snm, gs, onr); prof.reset(); traj.clear(); ist.mode = "manual"; }
                            else man->reposition(rwy, snm, gs, onr);
                            ctl = fh::Controls{};
                            if (onr) ctl.throttle = 0;
                        } else {
                            fh::ProfileConfig pc;
                            pc.rwy = rwy; pc.start_nm = snm; pc.gs_deg = gs; pc.vref_kt = vref;
                            pc.landing_config_at_start = true;
                            prof = std::make_unique<fh::ApproachProfile>(pc);
                            traj.clear(); ist.mode = "profile";
                        }
                        chk = fh::Checker{}; chk.td_tol_ft = hat_tol;
                        ist.preset = cmd.arg;
                        n = 0;
                        std::fprintf(stderr, "IOS reposition %s\n", cmd.arg.c_str());
                    }
                    // env changes go out on the next frame
                    count = 0;
                } catch (const std::exception& ex) {
                    std::fprintf(stderr, "IOS %s: %s\n", cmd.op.c_str(), ex.what());
                }
            }
        }
        const double t = n * dt;
        if (duration >= 0 && t > duration)
            break;
        fh::Frame f;
        if (frozen && tick > 0) {
            f = last;
            f.phase = "frozen";
        } else if (prof) {
            if (prof->done()) {
                if (ios.port()) { frozen = true; continue; } // IOS mode: hold at end
                break;
            }
            f = prof->step(dt);
        } else if (man) {
            for (auto& src : sources)
                src->poll(ctl);
            f = man->step(dt, ctl);
        } else {
            const double tt = t0 + t;
            if (tt > traj.back().t)
                break;
            f = fh::interpolate(traj, tt);
            f.t = tt;
            f.wheel_agl_ft = f.alt_ft - fh::kEyeHeightFt - rwy.elev_ft; // rwy unknown: MSL
            f.phase = "replay";
        }
        if (!frozen) ++n;
        count = count % 32767 + 1;
        if (!dry) {
            sock.send(fh::build_frame_datagram(f, count, env, wo, ff));
        } else {
            std::printf("%.3f,%.8f,%.8f,%.2f,%.3f,%.3f,%.3f,%.2f,%d,%d,%u,%d,%.2f,%s\n", f.t, f.lat,
                        f.lon, f.alt_ft, f.hdg, f.pitch, f.roll, f.gs_kt, f.gear_down, f.flaps,
                        f.landing_mask, f.strobe, f.wheel_agl_ft, f.phase.c_str());
        }
        if (!record_out.empty())
            sent.push_back(f);
        if (!quiet && (f.phase != last_phase || n % static_cast<long>(fps * 5) == 0))
            std::fprintf(stderr,
                         "t=%6.1f %-22s wheel=%6.1fft gs=%5.1fkt pitch=%5.2f roll=%5.2f gear=%s flaps=%d\n",
                         f.t, f.phase.c_str(), f.wheel_agl_ft, f.gs_kt, f.pitch, f.roll,
                         f.gear_down ? "dn" : "up", f.flaps);
        last_phase = f.phase;
        last = f;
        if (!dry) {
            const auto until = fast ? std::chrono::steady_clock::now() + std::chrono::milliseconds(2)
                                    : (next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                           std::chrono::duration<double>(dt)));
            do {
                const int r = sock.recv(rbuf, sizeof(rbuf));
                if (r > 0) {
                    fh::HatReply h;
                    if (fh::decode_hat_reply(rbuf, static_cast<std::size_t>(r), h)) {
                        const auto before = chk.events.size();
                        chk.note(f, h, rwy.elev_ft);
                        ist.last_hat = h.hat_ft;
                        for (auto k = before; k < chk.events.size(); ++k)
                            std::fprintf(stderr, "  %s\n", chk.events[k].c_str());
                    }
                    continue;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(300));
            } while (std::chrono::steady_clock::now() < until);
        }
    }
    if (prof && prof->touched_down())
        std::fprintf(stderr, "touchdown %.0f ft past threshold at t=%.1f s\n", prof->touchdown_x_ft(),
                     prof->touchdown_t());
    if (!record_out.empty()) {
        fh::save_trajectory_csv(record_out, sent);
        std::fprintf(stderr, "wrote %zu frames to %s\n", sent.size(), record_out.c_str());
    }
    if (dry)
        return 0;
    std::fprintf(stderr, "%s\n", chk.summary().c_str());
    if (check) {
        if (chk.replies == 0) {
            std::fprintf(stderr, "CHECK FAIL: no HAT replies (renderer running with HAT on?)\n");
            return 3;
        }
        if (prof && !chk.touchdown_ok()) {
            std::fprintf(stderr, "CHECK FAIL: touchdown HAT\n");
            return 3;
        }
    }
    return 0;
}
