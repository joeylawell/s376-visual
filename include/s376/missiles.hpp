#pragma once

#include "s376/config.hpp"
#include "s376/landmarks.hpp"
#include "s376/state.hpp"
#include "s376/terrain.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace s376 {

// Hidden visual-only missiles (same 7-click unlock / session as Classic).
// Off by default. No host damage, no flight-model effects, no new xT4 types.
// PTT is a rising edge on a config-mapped host discrete (default: type 112
// switch 255) or an unused type-109 landing-mask bit, or a desk key.

constexpr double kWtcSiteLat = 40.7115;
constexpr double kWtcSiteLon = -74.0134;
constexpr double kStatuteMileM = 1609.344;
constexpr double kMissileKeepOutM = 10.0 * kStatuteMileM; // 10 statute miles
constexpr int kMaxMissiles = 4;
constexpr const char* kMissileKeepoutToast = "Missiles unavailable in this area";

enum class FireResult { Fired, Off, Locked, Restricted, Busy };

enum class MissileFxKind { Body, Trail, Fire, Smoke, Scorch };

struct MissileFx {
    MissileFxKind kind = MissileFxKind::Trail;
    double lat = 0;
    double lon = 0;
    double alt_m = 0;
    float r = 1, g = 1, b = 1, a = 1;
    float size = 2;
    float life = 1;
};

struct Missile {
    double lat = 0;
    double lon = 0;
    double alt_m = 0;
    double hdg_deg = 0;
    double pitch_deg = 0;
    double life_s = 0;
    bool alive = false;
};

struct MissileWorld {
    std::vector<Missile> live;
    std::vector<MissileFx> fx;
    std::string toast;
    int fired = 0;
    int blocked = 0;
    int culled = 0;
    int impacts = 0;
};

// Geographic keep-out disk (always). Restriction applies only when Classic
// skyline is the *active* draw (chosen Classic AND eye in the NYC box).
bool in_wtc_keepout(double lat, double lon);
bool missiles_restricted_at(double lat, double lon);
bool segment_enters_keepout(double lat0, double lon0, double lat1, double lon1);

bool session_missiles();
void set_session_missiles(bool on); // ignored when locked
void reset_missiles();              // refresh_channels / tests

FireResult try_fire_missile(double lat, double lon, double alt_ft, double true_hdg_deg,
                            double pitch_deg);
void step_missiles(const HeightSource* src, double dt);
const MissileWorld& missile_world();

// Rising-edge PTT from a host discrete or desk key. Not a new packet type.
class PttWatch {
  public:
    bool edge(const HostState& host, const EggConfig& egg, bool key_down);

  private:
    bool prev_ = false;
    bool have_ = false;
};

bool ptt_held(const HostState& host, const EggConfig& egg, bool key_down);

} // namespace s376
