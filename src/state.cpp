#include "s376/state.hpp"

#include <chrono>

namespace s376 {

void SharedState::apply(const xt4::Datagram& dg, xt4::Decoder& dec, const std::string& peer,
                        std::uint64_t now_ns, HostState& out_snapshot) {
    std::lock_guard<std::mutex> g(mu_);
    s_.datagrams += 1;
    s_.last_rx_ns = now_ns;
    s_.last_peer = peer;
    s_.word_order = dg.word_order;
    s_.float_fmt = dg.float_fmt;

    xt4::Datagram mut = dg;
    if (auto eye = dec.eyepoint(mut)) {
        s_.eye = *eye;
        s_.have_eyepoint = true;
        s_.eyepoints += 1;
        s_.float_fmt = mut.float_fmt;
    }
    const xt4::FloatFmt fmt = s_.float_fmt;
    for (const auto& p : dg.packets) {
        switch (p.type) {
        case xt4::kTypeEnv:
            s_.env = xt4::decode_env(p, fmt);
            s_.have_env = true;
            break;
        case xt4::kTypeFog:
            s_.fog = xt4::decode_fog(p, fmt);
            s_.have_fog = true;
            break;
        case xt4::kTypeLowerCloud:
        case xt4::kTypeUpperCloud:
        case xt4::kTypeTopCloud: {
            xt4::CloudLayer L = (p.type == xt4::kTypeTopCloud) ? xt4::decode_top_cloud(p)
                                                              : xt4::decode_cloud(p, fmt);
            int slot = (p.type == xt4::kTypeLowerCloud) ? 0
                       : (p.type == xt4::kTypeUpperCloud) ? 1
                                                          : 2;
            if (L.region >= 0 && L.region < xt4::kMaxCloudLayers)
                slot = L.region;
            s_.clouds[static_cast<std::size_t>(slot)] = L;
            s_.have_cloud = true;
            break;
        }
        case xt4::kTypeStormSelect: {
            const auto sel = xt4::decode_storm(p);
            int slot = -1;
            if (sel.csn >= xt4::kStormCsn0 &&
                sel.csn < xt4::kStormCsn0 + xt4::kMaxActiveStorms)
                slot = sel.csn - xt4::kStormCsn0;
            if (slot < 0) {
                for (int i = 0; i < xt4::kMaxActiveStorms; ++i) {
                    if (s_.storms[static_cast<std::size_t>(i)].cfg.select == sel.select) {
                        slot = i;
                        break;
                    }
                }
            }
            if (slot < 0) {
                for (int i = 0; i < xt4::kMaxActiveStorms; ++i) {
                    if (!s_.storms[static_cast<std::size_t>(i)].cfg.active) {
                        slot = i;
                        break;
                    }
                }
            }
            if (slot >= 0)
                s_.storms[static_cast<std::size_t>(slot)].cfg = sel;
            break;
        }
        case xt4::kTypeDcs: {
            const xt4::Dcs d = xt4::decode_dcs(p, fmt);
            if (d.csn >= xt4::kStormCsn0 && d.csn < xt4::kStormCsn0 + xt4::kMaxActiveStorms) {
                auto& cell = s_.storms[static_cast<std::size_t>(d.csn - xt4::kStormCsn0)];
                cell.pose = d;
                cell.have_pose = true;
                if (cell.cfg.csn == 0)
                    cell.cfg.csn = d.csn;
            }
            break;
        }
        case xt4::kTypeAcLights:
            s_.lights = xt4::decode_lights(p);
            break;
        case xt4::kTypeOwnship:
            s_.ownship = xt4::decode_ownship(p);
            s_.have_ownship = true;
            break;
        case xt4::kTypeDisplay:
            s_.display = xt4::decode_display(p);
            break;
        case xt4::kTypeLightSwitch: {
            auto sw = xt4::decode_light_switch(p);
            if (sw.switch_num >= 0 && sw.switch_num < xt4::kMaxLightSwitches)
                s_.light_steps[static_cast<std::size_t>(sw.switch_num)] = sw.step;
            break;
        }
        default:
            break;
        }
    }
    out_snapshot = s_;
}

void SharedState::set_hat(float hat_ft, float elev_ft) {
    std::lock_guard<std::mutex> g(mu_);
    s_.have_hat = true;
    s_.hat_ft = hat_ft;
    s_.terrain_elev_ft = elev_ft;
}

} // namespace s376
