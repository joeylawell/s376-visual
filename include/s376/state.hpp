#pragma once

#include "s376/xt4.hpp"

#include <array>
#include <cstdint>
#include <mutex>

namespace s376 {

struct HostState {
    bool have_eyepoint = false;
    xt4::Dcs eye;
    xt4::Env env;
    xt4::Fog fog;
    xt4::Lights lights;
    xt4::OwnshipConfig ownship;
    xt4::DisplayControl display;
    std::array<int, xt4::kMaxLightSwitches> light_steps{};
    bool have_env = false;
    bool have_fog = false;
    bool have_ownship = false;
    bool have_cloud = false;
    std::array<xt4::CloudLayer, xt4::kMaxCloudLayers> clouds{};
    std::array<xt4::StormCell, xt4::kMaxActiveStorms> storms{};
    bool have_hat = false;
    float hat_ft = 0;
    float terrain_elev_ft = 0;
    std::uint64_t datagrams = 0;
    std::uint64_t eyepoints = 0;
    // >= 0: renderer uses this dt (scripted record). < 0: internal 1/60.
    float script_dt = -1.f;
    std::uint64_t last_rx_ns = 0;
    std::string last_peer;
    xt4::WordOrder word_order = xt4::WordOrder::Le;
    xt4::FloatFmt float_fmt = xt4::FloatFmt::Vax;

    HostState() {
        light_steps.fill(3);
        light_steps[70] = 0; // 13L RLLS lead-in (kSwRllsLeadIn); ILS default off
    }
};

class SharedState {
  public:
    void store(const HostState& s) {
        std::lock_guard<std::mutex> g(mu_);
        s_ = s;
    }
    HostState load() const {
        std::lock_guard<std::mutex> g(mu_);
        return s_;
    }
    void apply(const xt4::Datagram& dg, xt4::Decoder& dec, const std::string& peer,
               std::uint64_t now_ns, HostState& out_snapshot);
    void set_hat(float hat_ft, float elev_ft);

  private:
    mutable std::mutex mu_;
    HostState s_;
};

} // namespace s376
