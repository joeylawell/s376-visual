#include "s376/lights.hpp"

#include "s376/geo.hpp"

#include <algorithm>
#include <cmath>

namespace s376::lights {

int runway_base(int k) {
    if (k < 1)
        k = 1;
    if (k > 7)
        k = 7;
    return kRunwayBases[static_cast<std::size_t>(k - 1)];
}

int runway_switch(int k, int offset) {
    return runway_base(k) + offset;
}

bool is_runway_switch(int sw, int offset) {
    for (int k = 1; k <= 7; ++k) {
        if (runway_switch(k, offset) == sw)
            return true;
    }
    return false;
}

float tod_visibility(LightKind kind, float tod_hours) {
    float h = tod_hours;
    if (!std::isfinite(h))
        h = 12.f;
    // 1 at night; ramps down through dawn/dusk; noon is the floor.
    const float night = std::min(1.f, std::max(0.f, std::fabs(h - 12.f) - 5.f) / 4.f);
    const float day = 1.f - night;
    float floor = 0.04f;
    switch (kind) {
    case LightKind::Approach:
    case LightKind::Strobe:
    case LightKind::Flasher:
    case LightKind::Reil:
        floor = 0.62f; // Raster ALS is built to read in daylight
        break;
    case LightKind::Threshold:
    case LightKind::End:
        floor = 0.45f;
        break;
    case LightKind::Vasi:
        floor = 0.88f; // 2W2R must read on a 3° path at noon
        break;
    case LightKind::Night:
    case LightKind::SignGlow:
    case LightKind::TaxiEdge:
    case LightKind::TaxiCenterline:
    case LightKind::LeadOn:
        floor = 0.015f; // street/taxi wash off in day
        break;
    case LightKind::Edge:
    case LightKind::Centerline:
        floor = 0.035f; // daytime runway edge barely visible
        break;
    default:
        floor = 0.05f;
        break;
    }
    return night + day * floor;
}

float intensity(LightKind kind, int step) {
    if (step <= 0)
        return 0.f;
    const int i = std::max(0, std::min(kIntensitySteps - 1, step - 1));
    switch (kind) {
    case LightKind::Approach:
    case LightKind::Threshold:
        return kApproachTdz[static_cast<std::size_t>(i)];
    case LightKind::Strobe:
    case LightKind::Flasher:
    case LightKind::Reil:
        return kStrobeReil[static_cast<std::size_t>(i)];
    case LightKind::Edge:
    case LightKind::Centerline:
    case LightKind::Vasi:
    case LightKind::TaxiEdge:
    case LightKind::StopBar:
    case LightKind::TaxiCenterline:
    case LightKind::Rwsl:
    case LightKind::End:
    case LightKind::LeadOn:
    case LightKind::SignGlow:
    case LightKind::Beacon:
    case LightKind::Night:
        return kEdge[static_cast<std::size_t>(i)];
    }
    return kEdge[static_cast<std::size_t>(i)];
}

float size_mult_hat(float hat_ft) {
    if (!std::isfinite(hat_ft) || hat_ft <= 0.f)
        return kSizeMultNear;
    if (hat_ft >= kSizeEaseFt)
        return kSizeMultFar;
    float t = hat_ft / kSizeEaseFt;
    t = t * t * (3.f - 2.f * t);
    return kSizeMultNear + (kSizeMultFar - kSizeMultNear) * t;
}

float night_weight(float tod_hours) {
    float h = tod_hours;
    if (!std::isfinite(h))
        h = 12.f;
    return std::min(1.f, std::max(0.f, std::fabs(h - 12.f) - 6.f) / 3.f);
}

float base_size_px(float tod_hours) {
    float h = tod_hours;
    if (!std::isfinite(h))
        h = 12.f;
    const float noon = 1.f - std::min(1.f, std::fabs(h - 12.f) / 4.f);
    const float night = night_weight(h);
    const float day_sz = kBaseSizePx + (kNoonSizePx - kBaseSizePx) * noon;
    return day_sz + (kNightFarSizePx - day_sz) * night;
}

float point_size_px(float tod_hours, float hat_ft, float range_m) {
    const float base = base_size_px(tod_hours);
    const float near = base * size_mult_hat(hat_ft);
    if (!std::isfinite(range_m) || range_m <= 0.f)
        return near;
    float t = std::min(1.f, range_m / kSizeNearM);
    t = t * t * (3.f - 2.f * t);
    return near + (base - near) * t;
}

float range_visibility(LightKind kind, float range_m) {
    if (!std::isfinite(range_m) || range_m < 0.f)
        range_m = 0.f;
    const float maxr = max_range_m() * type_range_scale(kind);
    if (!(maxr > 1.f) || range_m >= maxr)
        return 0.f;
    // 50 % at ~2 km; approach / PAPI keep a floor so 3 nm ALS still reads.
    const float d0 = 2000.f;
    const float inv = 1.f / (1.f + (range_m / d0) * (range_m / d0));
    float floor = 0.05f;
    switch (kind) {
    case LightKind::Approach:
    case LightKind::Strobe:
    case LightKind::Flasher:
    case LightKind::Reil:
    case LightKind::Vasi:
        floor = 0.78f;
        break;
    case LightKind::Edge:
    case LightKind::Centerline:
        floor = 0.18f; // HIRL + CL must hold at 0.6 nm
        break;
    case LightKind::Threshold:
    case LightKind::End:
        floor = 0.22f;
        break;
    default:
        floor = 0.05f;
        break;
    }
    const float tail = 1.f - range_m / maxr;
    return std::max(inv, floor * tail);
}

bool papi_box_white(float eye_angle_deg, int box, float gs_deg) {
    if (box < 0)
        box = 0;
    if (box > 3)
        box = 3;
    if (!std::isfinite(eye_angle_deg))
        eye_angle_deg = gs_deg;
    if (!std::isfinite(gs_deg) || gs_deg < 0.5f)
        gs_deg = 3.f;
    // JO 6850.2C Table 5-2 HG4 published GPA: inboard unit is the highest.
    const float setting = gs_deg + kPapiHg4Min[static_cast<std::size_t>(box)] / 60.f;
    return eye_angle_deg > setting;
}

float papi_eye_angle_deg(double eye_lat, double eye_lon, double eye_alt_m, double papi_lat,
                         double papi_lon, double papi_alt_m) {
    const double dist = geo::ground_distance_m(eye_lat, eye_lon, papi_lat, papi_lon);
    if (!(dist > 1.0) || !std::isfinite(dist))
        return 90.f;
    return static_cast<float>(geo::rad2deg(std::atan2(eye_alt_m - papi_alt_m, dist)));
}

float papi_azimuth_deg(double eye_lat, double eye_lon, double papi_lat, double papi_lon,
                       float rwy_hdg_deg) {
    double brg = 0;
    const double dist = geo::inverse_wgs84(papi_lat, papi_lon, eye_lat, eye_lon, &brg);
    if (!(dist > 1.0) || !std::isfinite(dist))
        return 0.f;
    // From the PAPI, the approach is back along rwy_hdg + 180.
    // +az = right of the extended CL as seen on the approach (looking
    // along the landing heading). brg is PAPI→eye; negate so an on-CL
    // aircraft to the right of a left-side P4L is +az, not unusable-left.
    const double rel = geo::wrap180((static_cast<double>(rwy_hdg_deg) + 180.0) - brg);
    return static_cast<float>(rel);
}

bool papi_in_coverage(double eye_lat, double eye_lon, double papi_lat, double papi_lon,
                      float rwy_hdg_deg, float rotate_deg, float usable_left_deg,
                      float usable_right_deg) {
    const float az = papi_azimuth_deg(eye_lat, eye_lon, papi_lat, papi_lon, rwy_hdg_deg);
    if (az < -usable_left_deg || az > usable_right_deg)
        return false;
    const float az_axis = az - rotate_deg;
    return az_axis >= -10.f && az_axis <= 10.f;
}

double papi_distance_ft(double vgsi_tch_ft, double gs_deg) {
    if (!(vgsi_tch_ft > 0.5) || !std::isfinite(vgsi_tch_ft))
        vgsi_tch_ft = 50.0;
    if (!(gs_deg > 0.25) || !std::isfinite(gs_deg))
        gs_deg = 3.0;
    // Flat-runway lamp station: TCH = optical_ht + D·tan(GPA). A 2 ft
    // optical center (JO 6850.2C HG4) must be subtracted or TCH reads
    // official+height (the 5.7 ft bug).
    const double rise = std::max(1.0, vgsi_tch_ft - kPapiOpticalHtFt);
    return rise / std::tan(geo::deg2rad(gs_deg));
}

float type_size_boost(LightKind kind) {
    if (kind == LightKind::Vasi)
        return 2.3f; // ~6.4 px at the Raster floor; 4 boxes stay distinct
    if (kind == LightKind::Edge)
        return 1.28f; // HIRL both sides at 0.6 nm; still < 8 px far
    if (kind == LightKind::Centerline || kind == LightKind::Threshold)
        return 1.18f; // CAT II/III CL + TDZ
    return 1.f;
}

bool is_als_family(LightKind kind) {
    return kind == LightKind::Approach || kind == LightKind::Strobe ||
           kind == LightKind::Flasher || kind == LightKind::Reil;
}

float day_als_size_px(LightKind kind, float night_w, float size_px) {
    if (!std::isfinite(size_px) || size_px < 0.f)
        size_px = 0.f;
    if (!is_als_family(kind) || night_w >= 0.20f)
        return size_px;
    // Noon 0.6 nm ALSF-2 over Jamaica Bay: hundreds of 8 px additive
    // discs became a lens flare. Keep the bar, kill the bloom.
    const float cap = 3.2f - 0.6f * (1.f - night_w / 0.20f);
    return std::min(size_px, std::max(2.4f, cap));
}

float day_als_intensity(LightKind kind, float night_w) {
    if (!is_als_family(kind) || night_w >= 0.20f)
        return 1.f;
    const float t = night_w / 0.20f;
    // Noon floor = published approach step 3 (0.50), not step 1 (0.27).
    // Step 1 is too faint for day ops; the size cap still kills the flare.
    return kApproachTdz[2] + (1.f - kApproachTdz[2]) * t;
}

float close_als_size_px(LightKind kind, float range_m, float size_px) {
    if (!std::isfinite(size_px) || size_px < 0.f)
        size_px = 0.f;
    const bool end = kind == LightKind::End;
    if (!is_als_family(kind) && !end)
        return size_px;
    if (!std::isfinite(range_m) || range_m >= 800.f)
        return size_px;
    const float cap = end ? 4.5f : 3.0f;
    const float t = std::max(0.f, std::min(1.f, range_m / 800.f));
    const float c = cap + (size_px - cap) * t * t;
    return std::min(size_px, std::max(2.2f, c));
}

float close_als_intensity(LightKind kind, float range_m) {
    if (!is_als_family(kind))
        return 1.f;
    if (!std::isfinite(range_m) || range_m >= 700.f)
        return 1.f;
    const float t = std::max(0.f, std::min(1.f, range_m / 700.f));
    return 0.40f + 0.60f * t * t;
}

float type_range_scale(LightKind kind) {
    switch (kind) {
    case LightKind::Approach:
    case LightKind::Strobe:
    case LightKind::Flasher:
    case LightKind::Reil:
    case LightKind::Beacon:
        return 1.f;
    case LightKind::Edge:
    case LightKind::Centerline:
    case LightKind::Threshold:
    case LightKind::Vasi:
    case LightKind::End:
        return 0.70f;
    case LightKind::TaxiEdge:
    case LightKind::StopBar:
    case LightKind::TaxiCenterline:
    case LightKind::Rwsl:
    case LightKind::LeadOn:
    case LightKind::SignGlow:
    case LightKind::Night:
        return 0.35f;
    }
    return 0.70f;
}

float max_range_m() {
    return kMaxRangeFt * static_cast<float>(geo::kFtToM);
}

bool beacon_on(double t_s) {
    if (!std::isfinite(t_s) || t_s < 0.0)
        t_s = 0.0;
    const double p = std::fmod(t_s, kBeaconPeriodS);
    return p >= 0.0 && p < kBeaconOnS;
}

bool strobe_on(double t_s) {
    if (!std::isfinite(t_s) || t_s < 0.0)
        t_s = 0.0;
    const double p = std::fmod(t_s, kStrobePeriodS);
    return p >= 0.0 && p < kStrobeOnS;
}

bool sfl_on(double t_s, int index) {
    if (index < 0)
        return true;
    if (!std::isfinite(t_s) || t_s < 0.0)
        t_s = 0.0;
    // Whole sequence at kSflHz (2 Hz → 0.5 s sweep), not 2 Hz per slot.
    const double phase = std::fmod(t_s * kSflHz * static_cast<double>(kSflSlots),
                                   static_cast<double>(kSflSlots));
    const int slot = static_cast<int>(std::floor(phase));
    return slot == (index % kSflSlots);
}

} // namespace s376::lights
