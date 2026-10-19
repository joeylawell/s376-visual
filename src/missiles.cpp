#include "s376/missiles.hpp"

#include "s376/geo.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace s376 {
namespace {

MissileWorld g_w;
std::mutex g_mu;
bool g_on = false;

void offset_enu(double& lat, double& lon, double& alt_m, double e, double n, double u) {
    const geo::Ecef p = geo::lla_to_ecef(lat, lon, alt_m);
    geo::Ecef east, north, up;
    geo::ecef_to_enu_axes(lat, lon, east, north, up);
    geo::Ecef q;
    q.x = p.x + east.x * e + north.x * n + up.x * u;
    q.y = p.y + east.y * e + north.y * n + up.y * u;
    q.z = p.z + east.z * e + north.z * n + up.z * u;
    geo::ecef_to_lla(q, lat, lon, alt_m);
}

void add_fx(MissileFxKind k, double lat, double lon, double alt_m, float r, float g, float b,
            float a, float size, float life) {
    if (g_w.fx.size() >= 256)
        g_w.fx.erase(g_w.fx.begin(), g_w.fx.begin() + 32);
    MissileFx f;
    f.kind = k;
    f.lat = lat;
    f.lon = lon;
    f.alt_m = alt_m;
    f.r = r;
    f.g = g;
    f.b = b;
    f.a = a;
    f.size = size;
    f.life = life;
    g_w.fx.push_back(f);
}

void explode(double lat, double lon, double alt_m) {
    if (missiles_restricted_at(lat, lon))
        return; // never render impact in the Classic keep-out
    ++g_w.impacts;
    for (int i = 0; i < 20; ++i) {
        const float t = static_cast<float>(i);
        add_fx(MissileFxKind::Fire, lat, lon, alt_m + 2.0 + i * 0.8, 1.f, 0.45f + 0.02f * t, 0.08f,
               0.95f, 8.f + i * 0.4f, 1.1f);
    }
    for (int i = 0; i < 16; ++i)
        add_fx(MissileFxKind::Smoke, lat, lon, alt_m + 4.0 + i * 1.4, 0.35f, 0.35f, 0.36f, 0.7f,
               10.f + i * 0.5f, 2.4f);
    add_fx(MissileFxKind::Scorch, lat, lon, alt_m + 0.4, 0.08f, 0.07f, 0.06f, 0.85f, 14.f, 20.f);
}

} // namespace

bool in_wtc_keepout(double lat, double lon) {
    return geo::ground_distance_m(lat, lon, kWtcSiteLat, kWtcSiteLon) <= kMissileKeepOutM;
}

bool missiles_restricted_at(double lat, double lon) {
    return active_skyline_era() == SkylineEra::Classic && in_wtc_keepout(lat, lon);
}

bool segment_enters_keepout(double lat0, double lon0, double lat1, double lon1) {
    if (in_wtc_keepout(lat0, lon0) || in_wtc_keepout(lat1, lon1))
        return true;
    for (int i = 1; i < 10; ++i) {
        const double t = static_cast<double>(i) / 10.0;
        const double la = lat0 + (lat1 - lat0) * t;
        const double lo = lon0 + (lon1 - lon0) * t;
        if (in_wtc_keepout(la, lo))
            return true;
    }
    return false;
}

bool session_missiles() {
    return g_on && session_era_unlocked();
}

void set_session_missiles(bool on) {
    const bool unlocked = session_era_unlocked();
    std::lock_guard<std::mutex> g(g_mu);
    g_on = on && unlocked;
    if (!g_on) {
        g_w.live.clear();
        g_w.toast.clear();
    }
}

void reset_missiles() {
    std::lock_guard<std::mutex> g(g_mu);
    g_on = false;
    g_w = MissileWorld{};
}

FireResult try_fire_missile(double lat, double lon, double alt_ft, double true_hdg_deg,
                            double pitch_deg) {
    // Session flags first — do not nest g_mu with sess().mu.
    if (!session_era_unlocked())
        return FireResult::Locked;
    const bool restricted = missiles_restricted_at(lat, lon);
    std::lock_guard<std::mutex> g(g_mu);
    if (!g_on)
        return FireResult::Off;
    if (restricted) {
        g_w.toast = kMissileKeepoutToast;
        ++g_w.blocked;
        return FireResult::Restricted;
    }
    int alive = 0;
    for (const auto& m : g_w.live) {
        if (m.alive)
            ++alive;
    }
    if (alive >= kMaxMissiles)
        return FireResult::Busy;
    Missile m;
    m.lat = lat;
    m.lon = lon;
    m.alt_m = alt_ft * geo::kFtToM;
    m.hdg_deg = true_hdg_deg;
    m.pitch_deg = pitch_deg;
    m.life_s = 0;
    m.alive = true;
    g_w.live.push_back(m);
    ++g_w.fired;
    g_w.toast.clear();
    return FireResult::Fired;
}

void step_missiles(const HeightSource* src, double dt) {
    if (dt < 1e-4)
        dt = 1e-4;
    const bool restrict = active_skyline_era() == SkylineEra::Classic;
    std::lock_guard<std::mutex> g(g_mu);
    constexpr double kSpeed = 420.0;
    for (auto& m : g_w.live) {
        if (!m.alive)
            continue;
        m.life_s += dt;
        if (m.life_s > 45.0) { // long enough for a JFK→Manhattan keep-out cull
            m.alive = false;
            continue;
        }
        geo::Enu fwd, right, down;
        geo::body_to_enu(m.hdg_deg, m.pitch_deg, 0, fwd, right, down);
        const double ds = kSpeed * dt;
        const double lat0 = m.lat, lon0 = m.lon, alt0 = m.alt_m;
        double nlat = m.lat, nlon = m.lon, nalt = m.alt_m;
        offset_enu(nlat, nlon, nalt, fwd.e * ds, fwd.n * ds, fwd.u * ds);
        if (restrict && segment_enters_keepout(lat0, lon0, nlat, nlon)) {
            m.alive = false;
            ++g_w.culled;
            continue;
        }
        const double alt0_ft = alt0 * geo::kMToFt;
        const double nalt_ft = nalt * geo::kMToFt;
        HatSample hat = sample_session_hat(src, nlat, nlon, nalt_ft, &lat0, &lon0, &alt0_ft);
        if ((hat.covered && hat.hat_ft <= 0.f) || hat.intersect || nalt <= 1.0) {
            if (restrict && in_wtc_keepout(nlat, nlon)) {
                m.alive = false;
                ++g_w.culled;
                continue;
            }
            explode(nlat, nlon, std::max(nalt, hat.elev_ft * geo::kFtToM));
            m.alive = false;
            continue;
        }
        add_fx(MissileFxKind::Trail, lat0, lon0, alt0, 0.55f, 0.55f, 0.58f, 0.55f, 4.f, 1.0f);
        add_fx(MissileFxKind::Body, nlat, nlon, nalt, 0.75f, 0.22f, 0.10f, 1.f, 5.f, 0.08f);
        m.lat = nlat;
        m.lon = nlon;
        m.alt_m = nalt;
    }
    g_w.live.erase(std::remove_if(g_w.live.begin(), g_w.live.end(),
                                  [](const Missile& m) { return !m.alive; }),
                   g_w.live.end());
    for (auto& f : g_w.fx)
        f.life -= static_cast<float>(dt);
    g_w.fx.erase(std::remove_if(g_w.fx.begin(), g_w.fx.end(),
                                [](const MissileFx& f) { return f.life <= 0.f; }),
                 g_w.fx.end());
}

const MissileWorld& missile_world() {
    return g_w;
}

bool ptt_held(const HostState& host, const EggConfig& egg, bool key_down) {
    if (egg.ptt_source == "key")
        return key_down;
    if (egg.ptt_source == "landing_bit") {
        const int bit = std::max(0, std::min(15, egg.ptt_landing_bit));
        return host.lights.landing_mask & (1u << bit);
    }
    // type 112 switch rising level: step > 0
    const int sw = std::max(0, std::min(xt4::kMaxLightSwitches - 1, egg.ptt_switch));
    return host.light_steps[static_cast<std::size_t>(sw)] > 0;
}

bool PttWatch::edge(const HostState& host, const EggConfig& egg, bool key_down) {
    const bool now = ptt_held(host, egg, key_down);
    const bool fire = have_ && now && !prev_;
    prev_ = now;
    have_ = true;
    return fire;
}

} // namespace s376
