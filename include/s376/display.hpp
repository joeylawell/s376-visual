#pragma once

#include "s376/image.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace s376 {

// Per-channel display correction. Written in our own words from a hangar
// survey — not a vendor document. One cheap post pass (skip when identity).
//
// Warp mesh is 7×5 in channel angles: columns every 10 deg from −30 to +30,
// rows at −16.25, −10, 0, +10, +16.25. Identity = output angle = input angle.
// Edge blend: ramp (default 0.5; lower = brighter edge) and pixel width per
// side. Four time-of-day colour sets. 256-entry RGB LUT plus gamma. Optional
// edge mask is multiplied in *before* the warp. Test patterns replace the
// scene: 5 deg spherical grid, 70% white, or black.

constexpr int kWarpCols = 7;
constexpr int kWarpRows = 5;

enum class DisplayPattern { None, Grid5, White70, Black };

enum class TodBucket { Day, Dawn, Dusk, Night };

struct WarpNode {
    float src_h_deg = 0;
    float src_v_deg = 0;
};

struct EdgeBlend {
    float ramp = 0.5f; // lower = brighter remaining edge
    float left_px = 0;
    float right_px = 0;
    float top_px = 0;
    float bottom_px = 0;
};

struct ColorSet {
    float r = 1;
    float g = 1;
    float b = 1;
    float mix = 1; // 1 = apply the tint fully
};

struct DisplayCorrection {
    // [row][col], row 0 = bottom (most negative pitch)
    std::array<std::array<WarpNode, kWarpCols>, kWarpRows> warp{};
    EdgeBlend blend;
    ColorSet day;
    ColorSet dawn;
    ColorSet dusk;
    ColorSet night;
    std::array<std::uint8_t, 256> lut_r{};
    std::array<std::uint8_t, 256> lut_g{};
    std::array<std::uint8_t, 256> lut_b{};
    float gamma = 1.f;
    std::vector<std::uint8_t> mask; // optional, 1 byte/px, applied before warp
    int mask_w = 0;
    int mask_h = 0;
    DisplayPattern pattern = DisplayPattern::None;
    float hfov_deg = 50.f;
    float vfov_deg = 32.5f;
};

DisplayCorrection identity_correction();
bool load_correction(const std::string& path, DisplayCorrection& out, std::string& err);
bool save_correction(const std::string& path, const DisplayCorrection& c, std::string& err);
std::string dump_correction(const DisplayCorrection& c);

// Column headings (−30 … +30) and row pitches (bottom → top).
float warp_h_deg(int col);
float warp_v_deg(int row);

bool warp_is_identity(const DisplayCorrection& c);
bool lut_is_identity(const DisplayCorrection& c);
bool correction_is_identity(const DisplayCorrection& c);
// True when the live view needs the extra fullscreen pass (warp/blend/LUT/
// gamma/mask/pattern). Identity + no pattern → skip (980 Ti).
bool needs_gpu_pass(const DisplayCorrection& c);

DisplayPattern parse_display_pattern(const std::string& s);
const char* display_pattern_name(DisplayPattern p);
TodBucket tod_bucket(float tod_hours, int tod_select);
// Type-124: tod_select 0/1/2/3 snaps to day/dusk/dawn/night hours;
// −1 keeps the continuous clock (wrapped 0..24).
float effective_tod_hours(float tod_hours, int tod_select);
// Visual sun elevation / azimuth from the host TOD. Independent of date
// and longitude so operator "dusk" is dusk at any airport. Astronomy
// (sun_ecef + month/day/lon) is opt-in via render.solar_astronomy.
float visual_sun_elev_deg(float tod_hours, int tod_select);
float visual_sun_az_deg(float tod_hours, int tod_select, double lat_deg);
const ColorSet& color_for_tod(const DisplayCorrection& c, TodBucket b);

// Host type-124 runway_condition → wetness 0..1 (dry / damp / wet / flooded).
float runway_wetness(int runway_condition);

RgbImage make_pattern_rgb(int w, int h, DisplayPattern p, float hfov_deg, float vfov_deg);
RgbImage apply_correction(const RgbImage& src, const DisplayCorrection& c, TodBucket bucket);

} // namespace s376
