#pragma once

#include "s376/xt4.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace s376 {

struct ChannelConfig {
    std::string name = "front";
    double heading_offset_deg = 0;
    double pitch_offset_deg = 0;
    double roll_offset_deg = 0;
    double hfov_deg = 50.0;
    double vfov_deg = 32.5;
    // Optional seat offset in aircraft body (m). 0 = host eyepoint (true
    // duplicate front). FO mid_right/far_right may set eye_right_m > 0.
    double eye_fwd_m = 0;
    double eye_right_m = 0;
    double eye_up_m = 0;
    int monitor = 0;
    // Raster FOV is 50° H × 32.5° V; aspect tan(25°)/tan(16.25°) ≈ 1.60
    // (16:10). Default output is 1920×1200 until the projector res is confirmed.
    // TOML may set `resolution = "1920x1200"` or width/height.
    int width = 1920;
    int height = 1200;
    bool fullscreen = false;
    bool enabled = true;
    int window_x = -1; // -1 = auto (monitor or tiled)
    int window_y = -1;
    // Original Raster: Front=0, Left=1, Right=2. −1 = not a channel PC.
    int raster_ch = -1;
    // Projector output mirror only (Left Raster). Never a camera
    // orientation; the preview strip and FBO shots stay unflipped.
    bool flip_vertical = false;
    // "channel" draws a collimated view. "host_io" is operator + host I/O.
    std::string role = "channel";
    std::string correction_file = "data/display/correction.toml";
    std::string pattern; // none | grid5 | white70 | black
};

// Local operator control (s376-wizard). Not xT4. Loopback by default.
struct ControlConfig {
    std::string bind = "127.0.0.1:2376";
    bool enabled = true;
};

struct WindowPlacement {
    std::string name;
    int screen = 0;
    int x = 80;
    int y = 80;
    int w = 1920;
    int h = 1200;
    bool fullscreen = false;
};

struct HostConfig {
    std::string bind = "0.0.0.0:2300";
    std::uint16_t reply_port = 2301;
    xt4::WordOrder word_order = xt4::WordOrder::Auto;
    xt4::FloatFmt float_fmt = xt4::FloatFmt::Auto;
    bool hat_responder = true;
    // Empty = this process replies (single-box / laptop). Otherwise only the
    // process whose selected channel list includes this name sends type-200.
    // Four projector PCs share one TOML; set reply_master = "mid_left".
    std::string reply_master = "mid_left";
    bool allow_sim_lan = false;
    bool allow_lan = false;
    // Empty = reply to the sender. Host:port (or host, using reply_port).
    // Never default a simulator-LAN address.
    std::string reply_to;
};

struct RenderConfig {
    std::string engine = "auto"; // auto | osgearth | osg | null
    std::string earth_file = "data/earth/s376.earth";
    std::string cache_dir;
    double frame_rate = 60.0;
    std::string window_title = "s376-visual";
    bool vsync = true;
    int msaa = 0;  // 0: TAA is the 980 Ti AA (GTA V); 4 on `rtx`
    int aniso = 16; // 1 off; 16 is typical
    bool gl_debug = false; // OSG per-attribute GL error checks + notify
    std::string quality = "gtx980ti"; // gtx980ti | rtx | custom
    bool hdr = true;              // ACES tonemap
    bool atmosphere = true;       // analytic sky + aerial perspective
    bool water = true;            // Fresnel water + soft coast mask
    bool markings = true;         // FAA AC 150/5340-1 painted markings
    bool detail_textures = true;  // procedural / CC0 asphalt+grass
    bool buildings = true;        // OSM extruded footprints
    bool ssao = true;             // cheap baked AO; one 980 Ti channel can afford it
    bool shadows = true;          // 2-cascade sun CSM (512+512 on 980 Ti)
    bool bloom = true;            // Raster-style fog halo + city glow
    bool fxaa = true;             // fallback AA if TAA drops
    bool taa = true;              // temporal AA (GTA V); live path, not stills
    bool clustered = true;        // 32 closest lights light the ground
    bool lod_fade = true;         // dither-free fade on page-in (no pop-in)
    bool filmic = true;           // lift/gamma/gain after ACES
    bool solar_astronomy = false; // date/lon sun_ecef; default = visual TOD buckets
    float exposure = 1.0f;
    bool sync_to_host = true;     // draw only on a new type-100 eyepoint
    int ram_mb = 2048;            // CPU resident budget (gtx980ti: 2 GB)
    int vram_mb = 5120;           // GPU texture budget (6 GB Maxwell − headroom)
    int mesh_n = 96;              // DEM mesh samples per tile side
    bool auto_drop = true;        // gtx980ti: drop extras if GPU ms over budget
    float gpu_budget_ms = 12.20f; // live 60 Hz hangar budget (frame = 16.67)
    bool offscreen = false;       // pbuffer / FBO first — no projector window
};

void apply_quality_preset(RenderConfig& r, const std::string& name);

// GTX 980 Ti / 1920×1200 / one channel. 16.67 ms is 60 Hz. Cheap
// 2-cascade CSM stays on until autodrop. Values are Maxwell ALU
// budgets (hangar 980 Ti not on this box); llvmpipe is a relative proxy.
struct QualityGpuBudget {
    const char* name;
    const char* toggle;
    float gpu_ms;
};
const QualityGpuBudget* quality_gpu_budgets(int& n);
float quality_gpu_budget_total_ms(bool shadows_on);
const QualityGpuBudget* quality_autodrop_order(int& n);
// Turn off the next droppable feature when gpu_ms exceeds budget.
// Never drops water / markings / buildings / atmosphere (accuracy).
// Returns true if something changed. Appends the name to `dropped`.
bool apply_quality_autodrop(RenderConfig& r, float gpu_ms, float budget_ms, std::string* dropped);

struct LogConfig {
    bool verbose = false;
    bool debug = false;
    std::string dir;
};

// Hidden developer egg. Ignored until version×7 unlock. PTT is a rising
// edge on an *existing* host field — never a new xT4 type.
struct EggConfig {
    // switch = type 112 (default switch 255, unused by airport lights 1–4)
    // landing_bit = unused bit of type 109 landing_mask (default bit 7)
    // key = desk/demo keyboard (default M)
    std::string ptt_source = "switch";
    int ptt_switch = 255;
    int ptt_landing_bit = 7;
    std::string ptt_key = "m";
};

struct AppConfig {
    HostConfig host;
    RenderConfig render;
    LogConfig log;
    ControlConfig control;
    EggConfig egg;
    std::vector<ChannelConfig> channels;
};

AppConfig default_config();
AppConfig load_config(const std::string& path); // throws std::runtime_error
std::string dump_config(const AppConfig& cfg);
// When `selected` is non-empty, only those channels are printed and a
// `selected=name` line is added (so --channel mid_left does not look like far_right).
std::string dump_config(const AppConfig& cfg, const std::vector<std::string>& selected);
// Rewrite heading / FOV / flip / resolution / pattern for one [[channels]] block.
bool patch_channel_display(const std::string& config_path, const ChannelConfig& ch,
                           std::string& err);

// `resolution = "1920x1200"` (also 1920X1200 / 1920×1200). False if out of range.
bool parse_resolution(const std::string& s, int& w, int& h);
std::string format_resolution(int w, int h);

// name empty or "all" → every enabled channel; otherwise the named one.
std::vector<ChannelConfig> select_channels(const AppConfig& cfg, const std::string& name);

// Unique Raster wrap Left | Front | Right. Skips the FO front duplicate
// (mid_right) and non-channel roles (host_io / ios).
std::vector<ChannelConfig> wrap_channels(const std::vector<ChannelConfig>& channels);
// Four projector frusta (far_left | mid_left | mid_right | far_right).
// Keeps the duplicated front so a --channel strip shot is 4 × 50°×32.5°.
std::vector<ChannelConfig> strip4_channels(const std::vector<ChannelConfig>& channels);
void wrap_strip_size(const std::vector<ChannelConfig>& channels, int& w, int& h);

// Desk-tile order: left, captain front, FO front, right. Independent of
// heading-offset sign (Left is −50° of front, not “largest heading”).
int channel_tile_order(const ChannelConfig& ch);

// Type-200 HAT: hat_responder must be on, and reply_master empty or selected.
bool hat_reply_enabled(const HostConfig& h, const std::vector<ChannelConfig>& selected);

// First configured name in `prefer`, else the first enabled channel.
std::string default_shot_channel(const AppConfig& cfg);

// Place windows: explicit window_x/y win; else one channel per monitor when
// num_screens > 1; else tile by heading (far_left … far_right) on one display.
std::vector<WindowPlacement> layout_channels(const std::vector<ChannelConfig>& channels,
                                             int num_screens, int screen_w, int screen_h);

xt4::WordOrder parse_word_order(const std::string& s);
xt4::FloatFmt parse_float_fmt(const std::string& s);

} // namespace s376
