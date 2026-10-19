#pragma once

#include "s376/airport.hpp"

#include <array>

namespace s376::lights {

// Our packet-112 switch map (not a vendor table). Runway k = 1…7 uses a
// base, then offsets 1…8 for the eight groups on that runway. Taxi groups
// are global. See data/lights/switches.toml.

constexpr std::array<int, 7> kRunwayBases = {0, 8, 16, 24, 32, 84, 92};

constexpr int kOffApproachA = 1;
constexpr int kOffStrobeA = 2;
constexpr int kOffEdge = 3;
constexpr int kOffCenterline = 4;
constexpr int kOffVasi = 5;
constexpr int kOffTdz = 6;
constexpr int kOffApproachB = 7;
constexpr int kOffStrobeB = 8;

constexpr int kSwTaxiEdge = 41;
constexpr int kSwStopBar = 56;
constexpr int kSwTaxiCenterline = 60;

constexpr int kIntensitySteps = 5;

// Step 1…5 (step 0 is off). Approach and TDZ share one curve; strobe/REIL
// another; edge (and everything else) a third.
constexpr std::array<float, kIntensitySteps> kApproachTdz = {0.27f, 0.35f, 0.50f, 0.74f, 1.0f};
constexpr std::array<float, kIntensitySteps> kStrobeReil = {0.70f, 0.75f, 0.80f, 0.85f, 1.0f};
constexpr std::array<float, kIntensitySteps> kEdge = {0.35f, 0.55f, 0.68f, 0.92f, 1.0f};

constexpr float kBaseSizePx = 2.5f;
constexpr float kNoonSizePx = 2.8f;
constexpr float kNightFarSizePx = 5.5f; // Raster halo disc at night; day stays 2.5
constexpr float kSizeMultNear = 12.f; // at 0 ft HAT, *near* lights only
constexpr float kSizeMultFar = 1.f;   // by 500 ft HAT
constexpr float kSizeEaseFt = 500.f;
// HAT boost eases to the Raster 2.5 px floor by this slant range.
// Distant lights (22L ALS on a 04R short-final, taxi on the horizon)
// must stay ~2.5 px and never grow with range.
constexpr float kSizeNearM = 350.f;
constexpr float kMaxRangeFt = 250000.f;

constexpr double kBeaconPeriodS = 1.5;
constexpr double kBeaconOnS = 0.1;
constexpr double kStrobePeriodS = 1.5;
constexpr double kStrobeOnS = 0.066;
// JO 6850.2C §200.b: the whole sequence repeats twice per second (~0.5 s sweep).
constexpr double kSflHz = 2.0;
constexpr int kSflSlots = 15;
// JO 6850.2C Table 5-2, Height Group 4 with a published GPA (arcmin).
// Box 0 is the inboard unit (nearest the runway) and is the HIGHEST.
constexpr std::array<float, 4> kPapiHg4Min = {35.f, 15.f, -15.f, -35.f};

int runway_base(int k); // k = 1…7
int runway_switch(int k, int offset);
bool is_runway_switch(int sw, int offset);

float intensity(LightKind kind, int step); // 0 if step <= 0
// 1 at night; daytime edge/taxi/night-layer barely visible (~0.04).
// Approach / TDZ / PAPI stay readable in daylight (Raster step curve).
float tod_visibility(LightKind kind, float tod_hours);
// 0 until 18:00/06:00; 1 by 21:00/03:00. Drives night halo size.
float night_weight(float tod_hours);
float size_mult_hat(float hat_ft);
float base_size_px(float tod_hours);
// HAT boost applies only inside kSizeNearM. `range_m <= 0` is the near
// size (used as the shader's uPointSize). Far size is always base_size_px.
float point_size_px(float tod_hours, float hat_ft, float range_m = 0.f);
// PAPI stays a bit larger than the 2.5 px floor so 2W2R reads at 0.6 nm
// without letting horizon ALS grow. Other kinds stay at 1.
float type_size_boost(LightKind kind);
float type_range_scale(LightKind kind); // 1 = full kMaxRangeFt
// ALSF-2 / MALSR / SFL / REIL: daylight additive stack over water.
bool is_als_family(LightKind kind);
// Day size cap (px) so 0.6 nm noon ALS is a bar, not a bay flare.
float day_als_size_px(LightKind kind, float night_w, float size_px);
// Day intensity scale (1 at night). ALS family is ~0.18 at noon (PAR-56
// photometry: a readable warm bar, not a cyan bay wash).
float day_als_intensity(LightKind kind, float night_w);
// Daylight ALSF-2 is aviation white, not cyan. Warms and drops blue over water.
void day_als_rgb(LightKind kind, float night_w, float& r, float& g, float& b);
float max_range_m();
// Inverse-square-ish fade; approach/PAPI keep a readable floor at 3 nm.
float range_visibility(LightKind kind, float range_m);

// PAPI 4-box: box 0 inboard (nearest the runway, HIGHEST) … box 3 outboard
// (lowest). HG4 published-GPA aiming ±35′/±15′. White when the eye is
// above that unit's setting. On path: 2 inner red, 2 outer white.
bool papi_box_white(float eye_angle_deg, int box, float gs_deg);
float papi_eye_angle_deg(double eye_lat, double eye_lon, double eye_alt_m, double papi_lat,
                         double papi_lon, double papi_alt_m);
// Signed azimuth of the eye from the extended CL at the PAPI, degrees.
// Positive = right of CL as seen on the approach (looking along rwy heading).
float papi_azimuth_deg(double eye_lat, double eye_lon, double papi_lat, double papi_lon,
                       float rwy_hdg_deg);
// Chart-Supplement coverage: rotate is the optical-axis offset (+ right);
// usable_left / usable_right are CL-relative limits (13L left = 0).
bool papi_in_coverage(double eye_lat, double eye_lon, double papi_lat, double papi_lon,
                      float rwy_hdg_deg, float rotate_deg, float usable_left_deg,
                      float usable_right_deg);
// Flat-runway PAPI distance past the landing THR: TCH / tan(GPA).
double papi_distance_ft(double vgsi_tch_ft, double gs_deg);

bool beacon_on(double t_s);
bool strobe_on(double t_s);
bool sfl_on(double t_s, int index);

} // namespace s376::lights
