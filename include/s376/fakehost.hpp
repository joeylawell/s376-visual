#pragma once
// s376-fakehost: offline stand-in for the 737-300 host. Speaks the same
// clean-room xT4 subset as s376-synth (docs/PROTOCOL.md) and nothing else.
// Loopback only: see fakehost::guard below.

#include "s376/faa.hpp"
#include "s376/xt4.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace s376::fakehost {

// ---------------------------------------------------------------- guard
// Hard loopback guard. No environment variable or flag relaxes it.
// Accepts 127.0.0.0/8, ::1, ::ffff:127.x and "localhost" only.
class Refused : public std::runtime_error {
  public:
    explicit Refused(const std::string& w) : std::runtime_error(w) {}
};
bool is_loopback_literal(const std::string& host);
// Throws Refused for anything that is not loopback (wildcard binds too).
// Messages call out 10.1.95.x / 192.168.173.x and ports 3340/3341.
void guard(const std::string& host, std::uint16_t port, const char* purpose);
// Same check on a resolved socket address (AF_INET / AF_INET6).
void guard_sockaddr(const void* sa, unsigned salen, const char* purpose);
// "127.0.0.1:2300", "[::1]:2300", "localhost:2300" -> host/port, guarded.
void parse_guarded(const std::string& spec, std::string& host, std::uint16_t& port,
                   const char* purpose);

// ---------------------------------------------------------------- state
struct Frame {
    double t = 0;
    double lat = 0, lon = 0;
    double alt_ft = 0; // pilot eye, MSL
    double hdg = 0, pitch = 0, roll = 0;
    double gs_kt = 0;
    double wheel_agl_ft = 0; // host's own idea of main-gear height (profile/manual)
    bool gear_down = false;
    int flaps = 0;
    std::uint16_t landing_mask = 0;
    bool strobe = false;
    bool on_ground = false;
    std::string phase;
};

constexpr double kEyeHeightFt = 14.0; // eye above main gear, same as s376-synth
constexpr double kAimPointFt = 1000.0; // ILS/visual aim point past threshold

// ---------------------------------------------------------------- distance
// "6nm", "6sm", "10", "11112m", "6km". Bare number = nm.
double parse_distance_nm(const std::string& s);

// ---------------------------------------------------------------- profile
struct Runway {
    std::string icao, ident;
    double thr_lat = 0, thr_lon = 0; // landing threshold (displaced applied)
    double elev_ft = 0;
    double brg_true = 0;
    double length_ft = 0, displaced_ft = 0;
    double ils_tch_ft = 0; // plate TCH; 0 = generic 1000 ft aim
    std::string source;
};
// Aim point past the landing THR: TCH/tan(GPA) when the plate TCH is known.
double aim_point_ft(const Runway& rwy, double gs_deg);
Runway runway_from_faa(const std::string& icao, const std::string& ident);

struct ProfileConfig {
    Runway rwy;
    double start_nm = 6.0;
    double gs_deg = 3.0;
    double vref_kt = 140.0;
    double vapp_add_kt = 5.0;
    double taxi_kt = 20.0;
    bool landing_config_at_start = false; // "six-mile final" IOS-style reposition
    double flare_ft = 30.0;
    double td_sink_fps = 2.0;
};

// Glidepath wheel height above threshold elevation at along-track x
// (ft, negative = before threshold).
double glidepath_height_ft(double x_ft, double gs_deg, double aim_ft = kAimPointFt);
double threshold_crossing_height_ft(double gs_deg, double aim_ft = kAimPointFt);

class ApproachProfile {
  public:
    explicit ApproachProfile(ProfileConfig c);
    Frame step(double dt);
    bool done() const { return done_; }
    double along_ft() const { return x_; }
    double touchdown_x_ft() const { return td_x_; }
    double touchdown_t() const { return td_t_; }
    bool touched_down() const { return td_; }
    const ProfileConfig& config() const { return c_; }

  private:
    void place(Frame& f) const;
    ProfileConfig c_;
    double t_ = 0, x_ = 0, h_ = 0, v_ = 0, pitch_ = 0, roll_ = 0, y_ = 0;
    bool gear_ = false, flare_ = false, td_ = false, done_ = false;
    int flaps_ = 15;
    double td_x_ = 0, td_t_ = 0, taxi_t_ = -1;
    std::string phase_ = "approach";
};

// ---------------------------------------------------------------- trajectory
// CSV: header row, columns t,lat,lon,alt_ft,hdg,pitch,roll[,gs_kt,gear,flaps,landing_mask,strobe]
// TOML: repeated [[point]] tables with the same keys.
std::vector<Frame> load_trajectory(const std::string& path);
void save_trajectory_csv(const std::string& path, const std::vector<Frame>& frames);
std::string trajectory_csv(const std::vector<Frame>& frames);
std::vector<Frame> parse_trajectory_csv(const std::string& text);
std::vector<Frame> parse_trajectory_toml(const std::string& text);
Frame interpolate(const std::vector<Frame>& tr, double t);

// ---------------------------------------------------------------- manual
struct Controls {
    double elevator = 0; // -1..1, pitch-rate command (nose up +); neutral holds attitude
    double aileron = 0;  // -1..1, roll-rate command (right +); neutral slowly levels
    double rudder = 0;   // -1..1, yaw/heading rate (right +); stronger on the ground
    double throttle = 0.45; // 0..1; 0.45 = trimmed 3 deg descent at Vref+5, flaps 30, gear down
    bool gear_down = true;
    int flaps = 30;
    bool lights = true;
};

// USB yoke / throttle / pedals: raw OS axes are -1..1 (or 0..1 for
// triggers). Mapping + calibration live in TOML so Linux evdev, Windows
// winmm/XInput and optional SDL2 share one file. See docs/FAKEHOST_API.md.
struct AxisCal {
    double min = -1, max = 1, center = 0;
    double deadzone = 0.04;
    bool invert = false;
    bool seeded = false; // set by cal_note
};
// Named raw channels from the OS driver. winmm: x/y/z/rx/ry/rz/slider.
// XInput: lx/ly/rx/ry/lt/rt. evdev: x/y/z/rx/ry/rz/throttle. SDL2: the
// same names plus hatx/haty.
struct RawAxes {
    double x = 0, y = 0, z = 0, rx = 0, ry = 0, rz = 0, slider = 0;
    double lx = 0, ly = 0, xirx = 0, xiry = 0, lt = 0, rt = 0;
    double hatx = 0, haty = 0;
    std::uint32_t buttons = 0;
};
struct InputMap {
    std::string aileron_src = "x";
    std::string elevator_src = "y";
    std::string rudder_src = "rx";
    std::string throttle_src = "z";
    AxisCal aileron, elevator, rudder, throttle;
    int btn_gear = 0, btn_flaps = 1, btn_lights = 2;
};
// raw in the driver's native range is first scaled into [-1, 1] (or
// [0, 1] for throttle sources that are already 0..1) then this applies
// invert / deadzone / min-max / center.
double apply_axis(double raw_unit, const AxisCal& cal, bool unipolar = false);
const double* raw_axis_ptr(const RawAxes& a, const std::string& name);
void apply_input_map(const InputMap& m, const RawAxes& raw, Controls& c, std::uint32_t& prev_buttons);
void cal_note(AxisCal& cal, double raw_unit);
void cal_finish(AxisCal& cal);
InputMap parse_input_map_toml(const std::string& text);
InputMap load_input_map(const std::string& path);
std::string format_input_map_toml(const InputMap& m);
void save_input_map(const std::string& path, const InputMap& m);

// Pluggable control inputs for manual mode. Each source updates the
// fields it owns and leaves the rest alone, so a yoke, a throttle quadrant
// and the keyboard can be stacked.
class ControlSource {
  public:
    virtual ~ControlSource() = default;
    virtual const char* name() const = 0;
    virtual bool ok() const { return true; }
    virtual void poll(Controls& c) = 0;
    virtual bool last_raw(RawAxes& out) const {
        (void)out;
        return false;
    }
};

// Timestamped control CSV for CI / `--controls-replay`.
// Header: t,elevator,aileron,throttle[,rudder,gear,flaps,lights]
class ReplayControlSource : public ControlSource {
  public:
    ReplayControlSource() = default;
    ReplayControlSource(std::vector<double> t, std::vector<Controls> c);
    static ReplayControlSource from_csv(const std::string& text);
    static ReplayControlSource load(const std::string& path);
    const char* name() const override { return "replay"; }
    void set_time(double t) { now_ = t; }
    void poll(Controls& c) override;
    bool done() const;
    double duration() const { return t_.empty() ? 0 : t_.back(); }

  private:
    std::vector<double> t_;
    std::vector<Controls> c_;
    double now_ = 0;
};

// Future cockpit-IO bridge (the real cockpit switches/yoke via some IO
// box). Interface only: no CAE or vendor protocol is implemented here.
// A bridge would open a *loopback* endpoint fed by a local IO daemon and
// map its channels onto Controls.
struct CockpitIoConfig {
    std::string endpoint = "127.0.0.1:0"; // must pass guard()
};
class CockpitIoBridge : public ControlSource {
  public:
    explicit CockpitIoBridge(CockpitIoConfig cfg);
    const char* name() const override { return "cockpit-io (stub)"; }
    bool ok() const override { return false; }
    void poll(Controls&) override {} // not implemented
  private:
    CockpitIoConfig cfg_;
};

// Simple, stable 737-300-ish point-mass model: attitude-hold pitch,
// speed-stable lift/drag, coordinated turns. Trimmed hands-off at
// Vref+5 (145 kt) on a 3 deg path with flaps 30, gear down, throttle 0.45.
class ManualFlight {
  public:
    ManualFlight(const Runway& rwy, double start_nm, double gs_deg, bool on_runway = false);
    void reposition(const Runway& rwy, double start_nm, double gs_deg, bool on_runway);
    // Pilot-eye WGS-84 (same as the type-100 wire).
    void reposition_lla(double eye_lat, double eye_lon, double eye_alt_ft, double hdg_deg);
    Frame step(double dt, const Controls& c);
    const Runway& runway() const { return rwy_; }

  private:
    Runway rwy_;
    double t_ = 0, lat_ = 0, lon_ = 0, alt_ = 0, hdg_ = 0, pitch_ = 2.5, roll_ = 0, v_ = 145,
           vs_ = 0;
};

// ---------------------------------------------------------------- IOS
// Instructor station: HTML page + documented JSON-over-HTTP API on
// loopback only (docs/FAKEHOST_API.md). Guarded the same way as UDP.
struct IosCommand {
    std::string op; // reposition | freeze | unfreeze | tod | vis | wind |
                    // ceiling | rvr | weather | lights | cloud | state |
                    // rwy-cond | quit
    std::string arg;
    bool have_lla = false;
    double lat = 0, lon = 0, alt_ft = 0, hdg = 0;
    bool have_runway = false;
    std::string icao, runway;
    double distance_nm = 0;
    bool on_runway = false;
    bool have_tod = false;
    float tod_hours = 0;
    bool have_vis = false;
    float vis_sm = 0;
    bool have_wind = false;
    float wind_dir = 0, wind_mph = 0;
    bool have_ceiling = false, have_cloud_top = false;
    float ceiling_ft = -1, cloud_top_ft = -1;
    bool have_upper = false;
    float upper_ceiling_ft = -1, upper_top_ft = -1;
    bool have_rvr = false;
    float rvr_ft = -1;
    bool have_precip = false;
    int precip_type = 0, precip_intensity = 0;
    bool have_rwy_cond = false;
    int rwy_cond = 0; // type-124: 0 dry, 1 damp, 2 wet, 3+ flooded
    bool have_lights = false;
    int light_step = -1, rwy_light = -1, apch_light = -1, env_light = -1;
    bool have_state = false;
    double pitch = 0, roll = 0, gs_kt = 0;
    bool have_gear = false, gear_down = false;
    int flaps = -1;
    double timeout_s = -1;
    bool have_frozen = false, frozen = false;
};
struct IosStatus {
    std::string mode, phase, preset;
    double t = 0, wheel_agl = 0, gs_kt = 0, lat = 0, lon = 0, alt = 0;
    double hdg = 0, pitch = 0, roll = 0;
    bool frozen = false;
    bool gear_down = false;
    int flaps = 0;
    int hat_replies = 0, contacts = 0;
    float last_hat = 0;
    float tod_hours = 14.5f, vis_sm = 10, wind_dir = 0, wind_mph = 0;
    float ceiling_ft = -1, cloud_top_ft = -1, upper_ceiling_ft = -1, upper_top_ft = -1;
    float rvr_ft = -1;
    int light_step = 5, rwy_light = 5, apch_light = 5, env_light = 5;
    int rwy_cond = 0; // type-124 runway_condition
};

// Live 60 Hz adapter feed. Stops updating (caller should freeze) when
// no POST /api/v1/state arrives within timeout_s (default 250 ms).
class LiveStateFeed {
  public:
    static constexpr double kDefaultTimeoutS = 0.25;
    void set_timeout(double s) { timeout_s_ = s > 0 ? s : kDefaultTimeoutS; }
    void push(const Frame& f);
    bool active() const { return active_; }
    bool stale() const;
    const Frame& frame() const { return f_; }
    void clear();

  private:
    Frame f_{};
    bool active_ = false;
    double timeout_s_ = kDefaultTimeoutS;
    std::chrono::steady_clock::time_point last_{};
};

class IosServer {
  public:
    ~IosServer();
    void start(const std::string& bind); // guard()ed; port 0 = ephemeral
    std::uint16_t port() const { return port_; }
    // Non-blocking: accept/serve pending requests, return parsed commands.
    std::vector<IosCommand> poll(const IosStatus& st);
  private:
    std::intptr_t fd_ = -1; // SOCKET on Windows, int elsewhere
    std::uint16_t port_ = 0;
};

struct Environment;

struct IosEffect {
    bool quit = false;
    bool set_frozen = false;
    bool frozen = false;
    bool reposition = false;
    bool reposition_lla = false;
    bool have_state = false;
    Frame state;
    std::string preset;
    std::string icao, runway;
    double start_nm = 0;
    bool on_runway = false;
    double lat = 0, lon = 0, alt_ft = 0, hdg = 0;
};

// Apply env / lights / cloud / live-state fields. Reposition and freeze
// are returned for the caller (manual / profile / live) to enact.
IosEffect apply_ios_command(const IosCommand& cmd, Environment& env, LiveStateFeed* live);

// Presets the IOS page offers: kjfk-04r-6nm, kjfk-04r-6sm, kjfk-04r-rwy,
// kjfk-13l-6nm, kjfk-13l-6sm, kjfk-13l-rwy (any <icao>-<rwy>-<dist|rwy>).
bool parse_preset(const std::string& p, std::string& icao, std::string& rwy, double& start_nm,
                  bool& on_runway);
IosCommand parse_ios_query(const std::string& target); // "/cmd?op=tod&arg=19"
IosCommand parse_ios_json(const std::string& path, const std::string& body);
std::string ios_status_json(const IosStatus& st);
std::string ios_page();

// ---------------------------------------------------------------- wire
struct Environment {
    float tod_hours = 14.5f;
    float vis_ft = 10 * 5280.f;
    int month = 10, day = 25, year = 2026;
    float wind_mph = 0, wind_dir = 0;
    int precip_type = 0, precip_intensity = 0;
    float ceiling_ft_msl = -1, cloud_top_ft_msl = -1; // <0 = no layer
    float upper_ceiling_ft_msl = -1, upper_top_ft_msl = -1;
    float rvr_ft = -1, fog_top_ft = 0;                // <0 = no fog
    int runway_condition = 0;                         // type-124: 0 dry / 1 damp / 2 wet / 3+ flooded
    int light_step = 5;                               // default for unspecified groups
    int rwy_light_step = 5;                           // type-112 switches 1, 2, 4
    int apch_light_step = 5;                          // type-112 switch 3
    int env_light_step = 5;                           // taxi / stop-bar (41, 56, 60)
};
// One host datagram for frame n (1-based count), same packet mix as s376-synth.
std::vector<std::uint8_t> build_frame_datagram(const Frame& f, unsigned count,
                                               const Environment& env, xt4::WordOrder wo,
                                               xt4::FloatFmt ff, bool hat_query = true);

// Loopback UDP driver used by `--demo kjfk`. dest/listen must pass guard().
// Stops when the profile ends, duration_s elapses (<0 = until done), or *stop.
void run_approach_sender(const std::string& dest, const std::string& listen,
                         const ProfileConfig& pc, const Environment& env, double fps,
                         double duration_s, std::atomic<bool>* stop);

// ---------------------------------------------------------------- checks
struct HatReply {
    double t = 0;
    bool valid = false;
    float hat_ft = 0, elev_ft = 0;
};
bool decode_hat_reply(const std::uint8_t* d, std::size_t n, HatReply& out);

struct Checker {
    double td_tol_ft = 20.0;
    int replies = 0, invalid = 0, contacts = 0;
    bool have_td = false;
    double td_hat_wheel_ft = 0; // reply HAT minus eye height at touchdown
    double max_air_err_ft = 0;  // |reply - host expectation| while airborne
    std::vector<std::string> events;
    void note(const Frame& sent, const HatReply& r, double rwy_elev_ft);
    bool touchdown_ok() const;
    std::string summary() const;
};

} // namespace s376::fakehost
