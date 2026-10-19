#include "s376/geo.hpp"
#include "s376/landmarks.hpp"
#include "s376/missiles.hpp"
#include "s376/state.hpp"
#include "s376/terrain.hpp"

#include <cmath>
#include <iostream>

extern int g_fails;
extern int g_passes;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (cond) {                                                                                \
            ++g_passes;                                                                            \
        } else {                                                                                   \
            ++g_fails;                                                                             \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #cond << "\n";          \
        }                                                                                          \
    } while (0)

namespace {
struct FlatZero : s376::HeightSource {
    bool sample_ft(double, double, double& elev_msl_ft) const override {
        elev_msl_ft = 0;
        return true;
    }
};
} // namespace

void test_missiles() {
    s376::refresh_channels();
    CHECK(!s376::session_missiles());
    CHECK(!s376::in_wtc_keepout(40.64, -73.78)); // JFK is outside 10 sm
    CHECK(s376::in_wtc_keepout(40.7115, -74.0134));
    CHECK(s376::in_wtc_keepout(40.72, -74.00));
    CHECK(s376::segment_enters_keepout(40.64, -73.78, 40.7115, -74.0134));
    CHECK(!s376::segment_enters_keepout(47.45, -122.31, 47.46, -122.32));

    // Locked / off: no fire.
    CHECK(s376::try_fire_missile(47.45, -122.31, 2000, 180, 0) == s376::FireResult::Locked);
    s376::set_session_era(s376::SkylineEra::Current, true);
    CHECK(s376::try_fire_missile(47.45, -122.31, 2000, 180, 0) == s376::FireResult::Off);
    s376::set_session_missiles(true);
    CHECK(s376::session_missiles());

    // Far away (KSEA): fire is fine.
    FlatZero ground;
    CHECK(s376::try_fire_missile(47.45, -122.31, 2000, 180, -20) == s376::FireResult::Fired);
    CHECK(s376::missile_world().live.size() == 1);
    for (int i = 0; i < 400 && !s376::missile_world().live.empty(); ++i)
        s376::step_missiles(&ground, 0.05);
    CHECK(s376::missile_world().live.empty());
    CHECK(s376::missile_world().impacts >= 1);
    CHECK(s376::missile_world().culled == 0);

    // Classic active over WTC: no firing, toast, no live missiles.
    s376::reset_missiles();
    s376::tick_session_view(40.7115, -74.0134);
    s376::set_session_era(s376::SkylineEra::Classic, true);
    s376::set_session_missiles(true);
    CHECK(s376::active_skyline_era() == s376::SkylineEra::Classic);
    CHECK(s376::missiles_restricted_at(40.7115, -74.0134));
    CHECK(s376::try_fire_missile(40.7115, -74.0134, 1500, 0, 0) == s376::FireResult::Restricted);
    CHECK(s376::missile_world().live.empty());
    CHECK(s376::missile_world().toast == s376::kMissileKeepoutToast);
    CHECK(s376::missile_world().blocked >= 1);
    CHECK(s376::missile_world().impacts == 0);

    // JFK toward Manhattan, Classic on: missile is removed at the 10 sm
    // boundary. No explosion, never inside the disk.
    s376::reset_missiles();
    s376::tick_session_view(40.64, -73.78);
    s376::set_session_era(s376::SkylineEra::Classic, true);
    s376::set_session_missiles(true);
    CHECK(s376::active_skyline_era() == s376::SkylineEra::Classic);
    CHECK(!s376::in_wtc_keepout(40.64, -73.78));
    double hdg = 0;
    s376::geo::inverse_wgs84(40.64, -73.78, s376::kWtcSiteLat, s376::kWtcSiteLon, &hdg);
    CHECK(s376::try_fire_missile(40.64, -73.78, 4000, hdg, 0) == s376::FireResult::Fired);
    bool ever_inside = false;
    for (int i = 0; i < 800 && !s376::missile_world().live.empty(); ++i) {
        for (const auto& m : s376::missile_world().live) {
            if (s376::in_wtc_keepout(m.lat, m.lon))
                ever_inside = true;
        }
        s376::step_missiles(nullptr, 0.05);
    }
    CHECK(!ever_inside);
    CHECK(s376::missile_world().live.empty());
    CHECK(s376::missile_world().culled >= 1);
    CHECK(s376::missile_world().impacts == 0);
    for (const auto& f : s376::missile_world().fx)
        CHECK(f.kind != s376::MissileFxKind::Fire && f.kind != s376::MissileFxKind::Scorch);

    // Modern skyline: no restriction over WTC.
    s376::reset_missiles();
    s376::tick_session_view(40.7115, -74.0134);
    s376::set_session_era(s376::SkylineEra::Current, true);
    s376::set_session_missiles(true);
    CHECK(s376::active_skyline_era() == s376::SkylineEra::Current);
    CHECK(!s376::missiles_restricted_at(40.7115, -74.0134));
    CHECK(s376::try_fire_missile(40.7115, -74.0134, 2500, 90, -10) == s376::FireResult::Fired);
    CHECK(!s376::missile_world().live.empty());
    for (int i = 0; i < 400 && !s376::missile_world().live.empty(); ++i)
        s376::step_missiles(&ground, 0.05);
    CHECK(s376::missile_world().culled == 0);
    CHECK(s376::missile_world().impacts >= 1);

    // PTT = type 112 switch 255 rising edge (existing packet, not a new type).
    // HostState defaults unused switches to step 3; start released.
    s376::EggConfig egg;
    s376::PttWatch w;
    s376::HostState h;
    h.light_steps.fill(0);
    CHECK(!w.edge(h, egg, false));
    h.light_steps[255] = 5;
    CHECK(w.edge(h, egg, false));
    CHECK(!w.edge(h, egg, false)); // held, not a new press
    h.light_steps[255] = 0;
    CHECK(!w.edge(h, egg, false));
    h.light_steps[255] = 3;
    CHECK(w.edge(h, egg, false));

    egg.ptt_source = "landing_bit";
    egg.ptt_landing_bit = 7;
    s376::PttWatch w2;
    h.lights.landing_mask = 0;
    CHECK(!w2.edge(h, egg, false));
    h.lights.landing_mask = 1u << 7;
    CHECK(w2.edge(h, egg, false));

    egg.ptt_source = "key";
    s376::PttWatch w3;
    CHECK(!w3.edge(h, egg, false));
    CHECK(w3.edge(h, egg, true));

    s376::refresh_channels();
    CHECK(!s376::session_missiles());
    CHECK(s376::try_fire_missile(47.45, -122.31, 2000, 0, 0) == s376::FireResult::Locked);
}
