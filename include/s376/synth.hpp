#pragma once

#include "s376/xt4.hpp"

#include <atomic>
#include <string>

namespace s376 {

struct SynthConfig {
    double rate_hz = 20.0;
    xt4::WordOrder word_order = xt4::WordOrder::Le;
    xt4::FloatFmt float_fmt = xt4::FloatFmt::Vax;
    bool hat_queries = true;
    double start_lat = 47.463684; // KSEA 16L threshold
    double start_lon = -122.307751;
    double field_elev_ft = 433.0;
    double start_hdg = 180.34;
    bool weather = false; // --demo-weather: 124/125/126/127/128 + storm 105
};

struct SynthPose {
    double t = 0;
    double lat_deg = 0;
    double lon_deg = 0;
    double alt_ft = 0;
    double hdg_true_deg = 0;
    double pitch_deg = 0;
    double roll_deg = 0;
    double gs_kt = 0;
    bool on_ground = true;
    unsigned count = 0;
};

class SyntheticFlight {
  public:
    explicit SyntheticFlight(SynthConfig cfg = {});

    SynthPose step(double dt);
    std::vector<std::uint8_t> datagram(const SynthPose& s) const;

    const SynthConfig& config() const { return cfg_; }
    double field_elev_ft() const { return cfg_.field_elev_ft; }

  private:
    void targets(double t, double& accel_kts, double& pitch, double& roll, double& vs_fpm) const;

    SynthConfig cfg_;
    double lat_, lon_, alt_, hdg_, pitch_, roll_, v_;
    double elev_;
    double t_ = 0;
    unsigned count_ = 0;
    bool on_ground_ = true;
};

// Send `duration_s` seconds of traffic to dest (must pass the safety check).
// Returns datagrams sent. allow_sim_lan / allow_lan default off.
int run_synth_sender(const std::string& dest, const SynthConfig& cfg, double duration_s,
                     bool allow_sim_lan, bool allow_lan, std::atomic<bool>* stop = nullptr);

} // namespace s376
