#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace s376::safety {

// Real simulator LANs. Tests and CI must never send to these.
// Production IG sets host.allow_sim_lan (or S376_ALLOW_SIM_LAN=1) so it
// can reply to the emulated VAX on the visual LAN.
extern const char* const kForbiddenNets[]; // NULL-terminated CIDR list

class UnsafeEndpoint : public std::runtime_error {
  public:
    explicit UnsafeEndpoint(const std::string& what) : std::runtime_error(what) {}
};

struct Endpoint {
    std::string host;
    std::uint16_t port = 0;
};

bool parse_host_port(const std::string& spec, Endpoint& out, std::string& err);
bool is_loopback(const std::string& host);
bool is_forbidden(const std::string& host);
bool allow_sim_lan_from_env();

// Throws UnsafeEndpoint if the destination is a simulator LAN and
// allow_sim_lan is false, or if it is a non-loopback address and
// allow_lan is false. Binding 0.0.0.0 / :: is listen-only and is OK.
Endpoint check_destination(const std::string& host, std::uint16_t port, const std::string& purpose,
                           bool allow_sim_lan, bool allow_lan);

// Bind addresses: 0.0.0.0, ::, and loopback are always OK. A concrete
// non-loopback bind must pass the same LAN rules as a destination.
Endpoint check_bind(const std::string& host, std::uint16_t port, const std::string& purpose,
                    bool allow_sim_lan, bool allow_lan);

// Public DEM/imagery hosts (AWS Open Data, USGS, EOX, Overpass, …).
// IPv4 sim-LAN literals are still refused. Hostnames must match the list.
bool is_allowed_data_host(const std::string& host);
void check_data_url(const std::string& url); // throws UnsafeEndpoint
bool parse_url_host(const std::string& url, std::string& host);

} // namespace s376::safety
