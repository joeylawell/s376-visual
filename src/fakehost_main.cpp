// s376-fakehost — offline 737-300 host stand-in for s376-visual.
// Loopback only. Same xT4 subset as s376-synth (docs/PROTOCOL.md).

#include "s376/config.hpp"
#include "s376/fakehost.hpp"

#include <algorithm>
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

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mmsystem.h>
#include <xinput.h>
#include <conio.h>
#ifdef S376_HAS_SDL2
#include <SDL.h>
#endif
#else
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
#ifdef S376_HAS_SDL2
#include <SDL.h>
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
        "                      p flaps, l lights, q quit)\n"
        "  --joystick SPEC     Linux evdev path, or Windows winmm:N / xinput:N / auto\n"
        "                      (optional sdl:N if built with -DS376_SDL2=ON)\n"
        "  --map TOML          yoke/throttle/pedals axis+button map (see FAKEHOST_API.md)\n"
        "  --calibrate         sample the joystick and write a calibrated map\n"
        "  --map-out TOML      --calibrate destination (default stdout / yoke.toml)\n"
        "  --controls-replay F timestamped control CSV (CI / no hardware)\n"
        "  --fps N             host frame rate (default 60)\n"
        "  --duration SEC      stop after SEC (default: end of profile/replay)\n"
        "  --dest HOST:PORT    renderer host port (default 127.0.0.1:2300); repeat for\n"
        "                      N channels (each renderer --bind 127.0.0.1:23x0)\n"
        "  --ios HOST:PORT     IOS HTML + JSON API on loopback, e.g. 127.0.0.1:8376\n"
        "  --ceiling-top FT    lower cloud top, ft MSL\n"
        "  --upper-cloud BASE/TOP  upper cloud layer, ft MSL (type 126)\n"
        "  --lights STEP       type-112 intensity 0-5 for runway+approach+environment\n"
        "  --cockpit-io EP     future cockpit-IO bridge (stub interface, loopback EP)\n"
        "  --listen HOST:PORT  HAT reply port (default 127.0.0.1:2301)\n"
        "  --encoding O/F      le/vax (default) | be/vax | le/ieee | be/ieee\n"
        "  --tod HOURS         time of day (default 14.5)\n"
        "  --rwy-cond N        type-124 runway condition 0 dry / 1 damp / 2 wet / ≥3 flooded\n"
        "  --wet               same as --rwy-cond 2\n"
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

static void apply_key(char c, fh::Controls& ctl) {
    switch (c) {
    case 'w': ctl.elevator = -1; break; // stick forward
    case 's': ctl.elevator = 1; break;
    case 'a': ctl.aileron = -1; break;
    case 'd': ctl.aileron = 1; break;
    case 'z': ctl.rudder = -1; break;
    case 'x': ctl.rudder = 1; break;
    case 'r': ctl.throttle = std::min(1.0, ctl.throttle + 0.05); break;
    case 'f': ctl.throttle = std::max(0.0, ctl.throttle - 0.05); break;
    case 'g': ctl.gear_down = !ctl.gear_down; break;
    case 'p': ctl.flaps = ctl.flaps == 30 ? 40 : ctl.flaps == 40 ? 0 : ctl.flaps == 0 ? 15 : 30; break;
    case 'l': ctl.lights = !ctl.lights; break;
    case 'q': g_stop = true; break;
    default: break;
    }
}

class KeyboardSource : public fh::ControlSource {
  public:
    const char* name() const override { return "keyboard"; }
    void poll(fh::Controls& ctl) override {
        ctl.elevator *= 0.7;
        ctl.aileron *= 0.7;
        ctl.rudder *= 0.7;
#ifdef _WIN32
        while (_kbhit())
            apply_key(static_cast<char>(_getch()), ctl);
#else
        char c;
        while (::read(0, &c, 1) == 1)
            apply_key(c, ctl);
#endif
    }
};

static double unit_from_range(int v, int lo, int hi) {
    if (hi == lo)
        return 0;
    return 2.0 * (static_cast<double>(v) - lo) / (static_cast<double>(hi) - lo) - 1.0;
}

#ifdef __linux__
// USB joystick / yoke / throttle quadrant through Linux evdev.
class EvdevSource : public fh::ControlSource {
  public:
    EvdevSource(const std::string& path, fh::InputMap map) : path_(path), map_(std::move(map)) {
        fd_ = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd_ < 0)
            std::perror(path.c_str());
    }
    ~EvdevSource() override { if (fd_ >= 0) ::close(fd_); }
    const char* name() const override { return "evdev"; }
    bool ok() const override { return fd_ >= 0; }
    fh::RawAxes raw() const { return raw_; }
    bool last_raw(fh::RawAxes& out) const override {
        out = raw_;
        return fd_ >= 0;
    }
    void poll(fh::Controls& ctl) override {
        input_event ev;
        while (fd_ >= 0 && ::read(fd_, &ev, sizeof(ev)) == sizeof(ev)) {
            if (ev.type == EV_KEY) {
                int bit = -1;
                if (ev.code == BTN_TRIGGER || ev.code == BTN_SOUTH || ev.code == BTN_0) bit = 0;
                else if (ev.code == BTN_THUMB || ev.code == BTN_EAST || ev.code == BTN_1) bit = 1;
                else if (ev.code == BTN_THUMB2 || ev.code == BTN_WEST || ev.code == BTN_2) bit = 2;
                else if (ev.code >= BTN_JOYSTICK && ev.code < BTN_JOYSTICK + 16)
                    bit = ev.code - BTN_JOYSTICK;
                if (bit >= 0) {
                    if (ev.value)
                        raw_.buttons |= (1u << bit);
                    else
                        raw_.buttons &= ~(1u << bit);
                }
                continue;
            }
            if (ev.type != EV_ABS)
                continue;
            input_absinfo ai{};
            if (ioctl(fd_, EVIOCGABS(ev.code), &ai) != 0 || ai.maximum == ai.minimum)
                continue;
            const double u = unit_from_range(ev.value, ai.minimum, ai.maximum);
            if (ev.code == ABS_X) raw_.x = u;
            else if (ev.code == ABS_Y) raw_.y = u;
            else if (ev.code == ABS_Z) raw_.z = u;
            else if (ev.code == ABS_RX) raw_.rx = u;
            else if (ev.code == ABS_RY) raw_.ry = u;
            else if (ev.code == ABS_RZ) raw_.rz = u;
            else if (ev.code == ABS_THROTTLE) raw_.z = u;
            else if (ev.code == ABS_HAT0X) raw_.hatx = u;
            else if (ev.code == ABS_HAT0Y) raw_.haty = u;
        }
        fh::apply_input_map(map_, raw_, ctl, prev_);
    }
  private:
    std::string path_;
    fh::InputMap map_;
    fh::RawAxes raw_{};
    std::uint32_t prev_ = 0;
    int fd_ = -1;
};
#endif

#ifdef _WIN32
// USB HID yoke / throttle / pedals via winmm joyGetPosEx (DirectInput-class
// devices). Axis names: x y z rx ry rz slider.
class WinmmJoySource : public fh::ControlSource {
  public:
    WinmmJoySource(unsigned id, fh::InputMap map) : id_(id), map_(std::move(map)) {
        JOYINFOEX ji{};
        ji.dwSize = sizeof(ji);
        ji.dwFlags = JOY_RETURNALL;
        ok_ = joyGetPosEx(id_, &ji) == JOYERR_NOERROR;
        if (!ok_)
            std::fprintf(stderr, "winmm:%u not present\n", id_);
    }
    const char* name() const override { return "winmm"; }
    bool ok() const override { return ok_; }
    fh::RawAxes raw() const { return raw_; }
    bool last_raw(fh::RawAxes& out) const override {
        out = raw_;
        return ok_;
    }
    void poll(fh::Controls& ctl) override {
        JOYINFOEX ji{};
        ji.dwSize = sizeof(ji);
        ji.dwFlags = JOY_RETURNALL;
        if (joyGetPosEx(id_, &ji) != JOYERR_NOERROR) {
            ok_ = false;
            return;
        }
        ok_ = true;
        raw_.x = unit_from_range(static_cast<int>(ji.dwXpos), 0, 65535);
        raw_.y = unit_from_range(static_cast<int>(ji.dwYpos), 0, 65535);
        raw_.z = unit_from_range(static_cast<int>(ji.dwZpos), 0, 65535);
        raw_.rx = unit_from_range(static_cast<int>(ji.dwRpos), 0, 65535);
        raw_.ry = unit_from_range(static_cast<int>(ji.dwUpos), 0, 65535);
        raw_.rz = unit_from_range(static_cast<int>(ji.dwVpos), 0, 65535);
        raw_.slider = raw_.z;
        raw_.buttons = ji.dwButtons;
        fh::apply_input_map(map_, raw_, ctl, prev_);
    }
  private:
    unsigned id_ = 0;
    fh::InputMap map_;
    fh::RawAxes raw_{};
    std::uint32_t prev_ = 0;
    bool ok_ = false;
};

class XInputSource : public fh::ControlSource {
  public:
    XInputSource(unsigned id, fh::InputMap map) : id_(id), map_(std::move(map)) {
        XINPUT_STATE st{};
        ok_ = XInputGetState(id_, &st) == ERROR_SUCCESS;
        if (!ok_)
            std::fprintf(stderr, "xinput:%u not present\n", id_);
    }
    const char* name() const override { return "xinput"; }
    bool ok() const override { return ok_; }
    fh::RawAxes raw() const { return raw_; }
    bool last_raw(fh::RawAxes& out) const override {
        out = raw_;
        return ok_;
    }
    void poll(fh::Controls& ctl) override {
        XINPUT_STATE st{};
        if (XInputGetState(id_, &st) != ERROR_SUCCESS) {
            ok_ = false;
            return;
        }
        ok_ = true;
        raw_.lx = st.Gamepad.sThumbLX / 32768.0;
        raw_.ly = st.Gamepad.sThumbLY / 32768.0;
        raw_.xirx = st.Gamepad.sThumbRX / 32768.0;
        raw_.xiry = st.Gamepad.sThumbRY / 32768.0;
        raw_.lt = st.Gamepad.bLeftTrigger / 255.0;
        raw_.rt = st.Gamepad.bRightTrigger / 255.0;
        raw_.x = raw_.lx;
        raw_.y = raw_.ly;
        raw_.rx = raw_.xirx;
        raw_.z = raw_.rt * 2 - 1;
        raw_.buttons = 0;
        static const WORD kBtn[] = {XINPUT_GAMEPAD_A,           XINPUT_GAMEPAD_B,
                                    XINPUT_GAMEPAD_X,           XINPUT_GAMEPAD_Y,
                                    XINPUT_GAMEPAD_LEFT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER,
                                    XINPUT_GAMEPAD_BACK,        XINPUT_GAMEPAD_START,
                                    XINPUT_GAMEPAD_LEFT_THUMB,  XINPUT_GAMEPAD_RIGHT_THUMB};
        for (int i = 0; i < 10; ++i)
            if (st.Gamepad.wButtons & kBtn[i])
                raw_.buttons |= (1u << i);
        fh::apply_input_map(map_, raw_, ctl, prev_);
    }
  private:
    unsigned id_ = 0;
    fh::InputMap map_;
    fh::RawAxes raw_{};
    std::uint32_t prev_ = 0;
    bool ok_ = false;
};
#endif

#ifdef S376_HAS_SDL2
class Sdl2Source : public fh::ControlSource {
  public:
    Sdl2Source(int index, fh::InputMap map) : map_(std::move(map)) {
        if (SDL_WasInit(SDL_INIT_JOYSTICK) == 0)
            SDL_InitSubSystem(SDL_INIT_JOYSTICK);
        SDL_JoystickEventState(SDL_ENABLE);
        if (index < 0)
            index = 0;
        if (SDL_NumJoysticks() <= index) {
            std::fprintf(stderr, "sdl:%d not present (%d joysticks)\n", index, SDL_NumJoysticks());
            return;
        }
        js_ = SDL_JoystickOpen(index);
    }
    ~Sdl2Source() override {
        if (js_)
            SDL_JoystickClose(js_);
    }
    const char* name() const override { return "sdl2"; }
    bool ok() const override { return js_ != nullptr; }
    fh::RawAxes raw() const { return raw_; }
    bool last_raw(fh::RawAxes& out) const override {
        out = raw_;
        return js_ != nullptr;
    }
    void poll(fh::Controls& ctl) override {
        if (!js_)
            return;
        SDL_JoystickUpdate();
        auto ax = [&](int i) -> double {
            if (i < 0 || i >= SDL_JoystickNumAxes(js_))
                return 0;
            return SDL_JoystickGetAxis(js_, i) / 32768.0;
        };
        raw_.x = ax(0);
        raw_.y = ax(1);
        raw_.z = ax(2);
        raw_.rx = ax(3);
        raw_.ry = ax(4);
        raw_.rz = ax(5);
        raw_.slider = ax(6);
        raw_.buttons = 0;
        const int nb = std::min(32, SDL_JoystickNumButtons(js_));
        for (int i = 0; i < nb; ++i)
            if (SDL_JoystickGetButton(js_, i))
                raw_.buttons |= (1u << i);
        if (SDL_JoystickNumHats(js_) > 0) {
            const Uint8 h = SDL_JoystickGetHat(js_, 0);
            raw_.hatx = (h & SDL_HAT_RIGHT) ? 1 : (h & SDL_HAT_LEFT) ? -1 : 0;
            raw_.haty = (h & SDL_HAT_DOWN) ? 1 : (h & SDL_HAT_UP) ? -1 : 0;
        }
        fh::apply_input_map(map_, raw_, ctl, prev_);
    }
  private:
    fh::InputMap map_;
    fh::RawAxes raw_{};
    std::uint32_t prev_ = 0;
    SDL_Joystick* js_ = nullptr;
};
#endif

static std::unique_ptr<fh::ControlSource> open_joystick(const std::string& spec, const fh::InputMap& map) {
    if (spec.empty())
        return nullptr;
#ifdef __linux__
    if (spec[0] == '/' || spec.rfind("event", 0) == 0 || spec.find("input") != std::string::npos)
        return std::make_unique<EvdevSource>(spec, map);
#endif
#ifdef S376_HAS_SDL2
    if (spec.rfind("sdl", 0) == 0) {
        int idx = 0;
        const auto col = spec.find(':');
        if (col != std::string::npos)
            idx = std::atoi(spec.c_str() + col + 1);
        return std::make_unique<Sdl2Source>(idx, map);
    }
#endif
#ifdef _WIN32
    auto idx_of = [&](const char* pfx) -> int {
        const std::string p = pfx;
        if (spec == p || spec == "auto")
            return 0;
        if (spec.rfind(p + ":", 0) == 0)
            return std::atoi(spec.c_str() + p.size() + 1);
        return -1;
    };
    if (spec == "auto") {
        auto w = std::make_unique<WinmmJoySource>(0, map);
        if (w->ok())
            return w;
        auto x = std::make_unique<XInputSource>(0, map);
        if (x->ok())
            return x;
        std::fprintf(stderr, "joystick auto: no winmm or XInput device\n");
        return w;
    }
    const int wi = idx_of("winmm");
    if (wi >= 0)
        return std::make_unique<WinmmJoySource>(static_cast<unsigned>(wi), map);
    const int xi = idx_of("xinput");
    if (xi >= 0)
        return std::make_unique<XInputSource>(static_cast<unsigned>(xi), map);
    // bare integer = winmm id
    if (spec.find_first_not_of("0123456789") == std::string::npos)
        return std::make_unique<WinmmJoySource>(static_cast<unsigned>(std::atoi(spec.c_str())), map);
#endif
#ifdef __linux__
    return std::make_unique<EvdevSource>(spec, map);
#else
    std::fprintf(stderr, "unknown --joystick %s\n", spec.c_str());
    return nullptr;
#endif
}

#ifdef _WIN32
using UdpFd = SOCKET;
constexpr UdpFd kUdpInvalid = INVALID_SOCKET;
void udp_close(UdpFd s) {
    if (s != kUdpInvalid)
        closesocket(s);
}
void udp_nonblock(UdpFd s) {
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
}
#else
using UdpFd = int;
constexpr UdpFd kUdpInvalid = -1;
void udp_close(UdpFd s) {
    if (s >= 0)
        ::close(s);
}
void udp_nonblock(UdpFd s) { fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK); }
#endif

struct Sock {
    UdpFd fd = kUdpInvalid;
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
#ifdef _WIN32
        WSADATA wsa{};
        WSAStartup(MAKEWORD(2, 2), &wsa);
        fd = socket(v6 ? AF_INET6 : AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (fd == kUdpInvalid)
            return false;
#else
        fd = ::socket(v6 ? AF_INET6 : AF_INET, SOCK_DGRAM, 0);
        if (fd < 0)
            return false;
#endif
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
#ifdef _WIN32
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
#else
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#endif
        if (::bind(fd, reinterpret_cast<sockaddr*>(&la), llen) != 0) {
            std::perror("bind");
            return false;
        }
        udp_nonblock(fd);
        return true;
    }
    void send(const std::vector<std::uint8_t>& b) {
        fh::guard_sockaddr(&dest, dlen, "send to"); // every datagram
#ifdef _WIN32
        ::sendto(fd, reinterpret_cast<const char*>(b.data()), static_cast<int>(b.size()), 0,
                 reinterpret_cast<sockaddr*>(&dest), dlen);
        for (auto& e : extra) {
            fh::guard_sockaddr(&e.first, e.second, "send to");
            ::sendto(fd, reinterpret_cast<const char*>(b.data()), static_cast<int>(b.size()), 0,
                     reinterpret_cast<sockaddr*>(&e.first), e.second);
        }
#else
        ::sendto(fd, b.data(), b.size(), 0, reinterpret_cast<sockaddr*>(&dest), dlen);
        for (auto& e : extra) {
            fh::guard_sockaddr(&e.first, e.second, "send to");
            ::sendto(fd, b.data(), b.size(), 0, reinterpret_cast<sockaddr*>(&e.first), e.second);
        }
#endif
    }
    int recv(std::uint8_t* buf, std::size_t n) {
        sockaddr_storage from{};
        socklen_t fl = sizeof(from);
#ifdef _WIN32
        const int r = ::recvfrom(fd, reinterpret_cast<char*>(buf), static_cast<int>(n), 0,
                                 reinterpret_cast<sockaddr*>(&from), &fl);
#else
        const auto r = ::recvfrom(fd, buf, n, 0, reinterpret_cast<sockaddr*>(&from), &fl);
#endif
        if (r <= 0)
            return -1;
        try {
            fh::guard_sockaddr(&from, fl, "reply from");
        } catch (const fh::Refused&) {
            return -1; // ignore anything not from loopback
        }
        return static_cast<int>(r);
    }
    ~Sock() { udp_close(fd); }
};

} // namespace

int main(int argc, char** argv) {
    std::string profile, replay, dest = "127.0.0.1:2300", listen = "127.0.0.1:2301";
    std::string start = "6nm", record_out, joystick, encoding = "le/vax", ios_bind, cockpit_io;
    std::string map_path, map_out, controls_replay;
    std::vector<std::string> extra_dests;
    bool dest_given = false;
    bool manual = false, six = false, fast = false, dry = false, check = false, quiet = false;
    bool start_given = false, calibrate = false;
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
            else if (a == "--rwy-cond") {
                env.runway_condition = std::stoi(need());
                if (env.runway_condition < 0)
                    env.runway_condition = 0;
            } else if (a == "--wet")
                env.runway_condition = 2;
            else if (a == "--vis-sm") env.vis_ft = std::stof(need()) * 5280.f;
            else if (a == "--wind") {
                const std::string w = need();
                const auto sl = w.find('/');
                env.wind_dir = std::stof(w.substr(0, sl));
                env.wind_mph = sl == std::string::npos ? 0 : std::stof(w.substr(sl + 1));
            } else if (a == "--ceiling") env.ceiling_ft_msl = std::stof(need());
            else if (a == "--ceiling-top") env.cloud_top_ft_msl = std::stof(need());
            else if (a == "--upper-cloud") {
                const std::string u = need();
                const auto sl = u.find('/');
                env.upper_ceiling_ft_msl = std::stof(u.substr(0, sl));
                env.upper_top_ft_msl = sl == std::string::npos
                                           ? env.upper_ceiling_ft_msl + 2000.f
                                           : std::stof(u.substr(sl + 1));
            } else if (a == "--lights") {
                const int s = std::max(0, std::min(5, std::stoi(need())));
                env.light_step = env.rwy_light_step = env.apch_light_step = env.env_light_step = s;
            } else if (a == "--rvr") { env.rvr_ft = std::stof(need()); env.fog_top_ft = 300; }
            else if (a == "--record-out") record_out = need();
            else if (a == "--joystick") joystick = need();
            else if (a == "--map") map_path = need();
            else if (a == "--map-out") map_out = need();
            else if (a == "--calibrate") calibrate = true;
            else if (a == "--controls-replay") controls_replay = need();
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
    if (!controls_replay.empty())
        manual = true;
    if (calibrate) {
        dry = true;
        if (!manual && profile.empty() && replay.empty())
            manual = true;
    }
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

    fh::InputMap imap;
    if (!map_path.empty()) {
        try {
            imap = fh::load_input_map(map_path);
        } catch (const std::exception& ex) {
            std::cerr << ex.what() << "\n";
            return 2;
        }
    }
    std::unique_ptr<fh::ReplayControlSource> replay_ctl;
    if (!controls_replay.empty()) {
        try {
            replay_ctl = std::make_unique<fh::ReplayControlSource>(fh::ReplayControlSource::load(controls_replay));
            if (duration < 0)
                duration = replay_ctl->duration();
            manual = true;
        } catch (const std::exception& ex) {
            std::cerr << ex.what() << "\n";
            return 2;
        }
    }
    std::vector<std::unique_ptr<fh::ControlSource>> sources;
#ifndef _WIN32
    RawTerm term;
#endif
    if (manual) {
#ifndef _WIN32
        term.enable();
#endif
        if (!replay_ctl)
            sources.push_back(std::make_unique<KeyboardSource>());
        if (!joystick.empty()) {
            if (auto js = open_joystick(joystick, imap))
                sources.push_back(std::move(js));
        }
        if (replay_ctl)
            sources.push_back(std::move(replay_ctl));
    }
    if (calibrate) {
        fh::InputMap cal = imap;
        const bool have_js = !joystick.empty();
        const auto t1 = std::chrono::steady_clock::now() + std::chrono::seconds(have_js ? 8 : 0);
        fh::Controls dummy;
        int samples = 0;
        do {
            for (auto& src : sources)
                src->poll(dummy);
            for (auto& src : sources) {
                fh::RawAxes raw{};
                if (!src->last_raw(raw))
                    continue;
                auto note = [&](const std::string& srcn, fh::AxisCal& c) {
                    const double* p = fh::raw_axis_ptr(raw, srcn);
                    if (p)
                        fh::cal_note(c, *p);
                };
                note(cal.aileron_src, cal.aileron);
                note(cal.elevator_src, cal.elevator);
                note(cal.rudder_src, cal.rudder);
                note(cal.throttle_src, cal.throttle);
                ++samples;
            }
            if (!have_js)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        } while (std::chrono::steady_clock::now() < t1 && !g_stop);
        fh::cal_finish(cal.aileron);
        fh::cal_finish(cal.elevator);
        fh::cal_finish(cal.rudder);
        fh::cal_finish(cal.throttle);
        std::fprintf(stderr, "calibrate: %d samples%s\n", samples,
                     have_js ? "" : " (no --joystick; writing current map)");
        if (!map_out.empty()) {
            fh::save_input_map(map_out, cal);
            std::fprintf(stderr, "wrote calibrated map to %s\n", map_out.c_str());
        } else {
            std::cout << fh::format_input_map_toml(cal);
        }
        return 0;
    }
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
    fh::LiveStateFeed live;
    ist.mode = manual ? "manual" : prof ? "profile" : "replay";
    ist.preset = profile.empty() ? (manual ? "kjfk-04r-" + start : replay) : profile + "-" + start;
    long tick = 0;
    fh::Controls ctl;
    ctl.flaps = 30;
    ctl.throttle = 0.45;
    fh::ReplayControlSource* replay_src = nullptr;
    for (auto& src : sources) {
        if (std::strcmp(src->name(), "replay") == 0)
            replay_src = static_cast<fh::ReplayControlSource*>(src.get());
    }

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
            ist.hdg = last.hdg; ist.pitch = last.pitch; ist.roll = last.roll;
            ist.gear_down = last.gear_down; ist.flaps = last.flaps;
            ist.hat_replies = chk.replies; ist.contacts = chk.contacts;
            ist.tod_hours = env.tod_hours;
            ist.vis_sm = env.vis_ft / 5280.f;
            ist.wind_dir = env.wind_dir; ist.wind_mph = env.wind_mph;
            ist.ceiling_ft = env.ceiling_ft_msl; ist.cloud_top_ft = env.cloud_top_ft_msl;
            ist.upper_ceiling_ft = env.upper_ceiling_ft_msl; ist.upper_top_ft = env.upper_top_ft_msl;
            ist.rvr_ft = env.rvr_ft;
            ist.rwy_cond = env.runway_condition;
            ist.light_step = env.light_step; ist.rwy_light = env.rwy_light_step;
            ist.apch_light = env.apch_light_step; ist.env_light = env.env_light_step;
            for (const auto& cmd : ios.poll(ist)) {
                try {
                    const auto eff = fh::apply_ios_command(cmd, env, &live);
                    if (eff.quit) g_stop = true;
                    if (eff.set_frozen) frozen = eff.frozen;
                    if (eff.reposition_lla) {
                        if (!man)
                            man = std::make_unique<fh::ManualFlight>(rwy, 0, gs, true);
                        man->reposition_lla(eff.lat, eff.lon, eff.alt_ft, eff.hdg);
                        prof.reset();
                        traj.clear();
                        ist.mode = "manual";
                        ist.preset = "lla";
                        n = 0;
                        std::fprintf(stderr, "IOS reposition lla %.6f %.6f alt=%.0f hdg=%.1f\n",
                                     eff.lat, eff.lon, eff.alt_ft, eff.hdg);
                    } else if (eff.reposition) {
                        std::string ic = eff.icao, rw = eff.runway;
                        double snm = eff.start_nm;
                        bool onr = eff.on_runway;
                        if (ic.empty() && !eff.preset.empty()) {
                            if (!fh::parse_preset(eff.preset, ic, rw, snm, onr))
                                throw std::invalid_argument("bad preset " + eff.preset);
                        }
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
                        ist.preset = eff.preset.empty() ? (ic + "-" + rw) : eff.preset;
                        n = 0;
                        std::fprintf(stderr, "IOS reposition %s\n", ist.preset.c_str());
                    }
                    if (eff.have_state)
                        ist.mode = "adapter";
                    count = 0;
                } catch (const std::exception& ex) {
                    std::fprintf(stderr, "IOS %s: %s\n", cmd.op.c_str(), ex.what());
                }
            }
            if (live.active() && live.stale()) {
                frozen = true;
                live.clear();
                std::fprintf(stderr, "IOS live-state timeout — frozen\n");
            }
        }
        const double t = n * dt;
        if (duration >= 0 && tick * dt > duration)
            break;
        fh::Frame f;
        if (replay_src)
            replay_src->set_time(n * dt);
        if (live.active() && !live.stale() && !(frozen && tick > 0)) {
            f = live.frame();
            f.t = n * dt;
            f.phase = "adapter";
            f.wheel_agl_ft = f.alt_ft - fh::kEyeHeightFt - rwy.elev_ft;
        } else if (frozen && tick > 0) {
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
