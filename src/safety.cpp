#include "s376/safety.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

namespace s376::safety {

const char* const kForbiddenNets[] = {"10.1.95.0/24", "192.168.173.0/24", nullptr};

namespace {

bool parse_ipv4(const std::string& host, std::uint32_t& addr) {
    in_addr a{};
    if (inet_pton(AF_INET, host.c_str(), &a) != 1)
        return false;
    addr = ntohl(a.s_addr);
    return true;
}

bool in_cidr(std::uint32_t addr, const char* cidr) {
    unsigned a, b, c, d, bits = 32;
    if (std::sscanf(cidr, "%u.%u.%u.%u/%u", &a, &b, &c, &d, &bits) < 4)
        return false;
    const std::uint32_t net = (a << 24) | (b << 16) | (c << 8) | d;
    const std::uint32_t mask = bits == 0 ? 0 : (~0u << (32 - bits));
    return (addr & mask) == (net & mask);
}

std::string normalize_host(const std::string& host) {
    if (host.empty() || host == "localhost")
        return "127.0.0.1";
    if (host == "*" || host == "any")
        return "0.0.0.0";
    return host;
}

} // namespace

bool parse_host_port(const std::string& spec, Endpoint& out, std::string& err) {
    auto colon = spec.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 == spec.size()) {
        err = "expected host:port, got '" + spec + "'";
        return false;
    }
    out.host = spec.substr(0, colon);
    try {
        int p = std::stoi(spec.substr(colon + 1));
        if (p < 0 || p > 65535)
            throw std::out_of_range("port");
        out.port = static_cast<std::uint16_t>(p);
    } catch (...) {
        err = "bad port in '" + spec + "'";
        return false;
    }
    return true;
}

bool is_loopback(const std::string& host) {
    const std::string h = normalize_host(host);
    std::uint32_t a = 0;
    if (!parse_ipv4(h, a))
        return false;
    return (a & 0xFF000000u) == 0x7F000000u;
}

bool is_forbidden(const std::string& host) {
    const std::string h = normalize_host(host);
    if (h == "0.0.0.0")
        return false;
    std::uint32_t a = 0;
    if (!parse_ipv4(h, a))
        return false;
    for (int i = 0; kForbiddenNets[i]; ++i) {
        if (in_cidr(a, kForbiddenNets[i]))
            return true;
    }
    return false;
}

bool allow_sim_lan_from_env() {
    const char* e = std::getenv("S376_ALLOW_SIM_LAN");
    return e && std::strcmp(e, "1") == 0;
}

static Endpoint check_common(const std::string& host, std::uint16_t port,
                             const std::string& purpose, bool allow_sim_lan, bool allow_lan,
                             bool bind) {
    const std::string h = normalize_host(host);
    if (h.find_first_not_of("0123456789.") != std::string::npos && h != "::") {
        throw UnsafeEndpoint(purpose + ": '" + host +
                             "' is not an IPv4 literal; use an address, not a hostname");
    }
    if (h == "0.0.0.0" && bind)
        return {h, port};

    if (is_forbidden(h) && !allow_sim_lan && !allow_sim_lan_from_env()) {
        throw UnsafeEndpoint(purpose + ": " + h +
                             " is on the real simulator network; refused (set host.allow_sim_lan "
                             "or S376_ALLOW_SIM_LAN=1 only on the IG PC)");
    }
    if (is_loopback(h) || (h == "0.0.0.0" && bind))
        return {h, port};
    const char* lan = std::getenv("S376_ALLOW_LAN");
    const bool lan_ok = allow_lan || (lan && std::strcmp(lan, "1") == 0);
    if (!lan_ok) {
        throw UnsafeEndpoint(
            purpose + ": " + h + ":" + std::to_string(port) +
            " is not loopback. Set S376_ALLOW_LAN=1 only for an isolated bench LAN.");
    }
    return {h, port};
}

Endpoint check_destination(const std::string& host, std::uint16_t port, const std::string& purpose,
                           bool allow_sim_lan, bool allow_lan) {
    return check_common(host, port, purpose, allow_sim_lan, allow_lan, false);
}

Endpoint check_bind(const std::string& host, std::uint16_t port, const std::string& purpose,
                    bool allow_sim_lan, bool allow_lan) {
    return check_common(host, port, purpose, allow_sim_lan, allow_lan, true);
}

bool parse_url_host(const std::string& url, std::string& host) {
    auto scheme = url.find("://");
    if (scheme == std::string::npos)
        return false;
    const auto start = scheme + 3;
    auto slash = url.find('/', start);
    auto colon = url.find(':', start);
    auto end = url.size();
    if (slash != std::string::npos)
        end = slash;
    if (colon != std::string::npos && colon < end)
        end = colon;
    if (end <= start)
        return false;
    host = url.substr(start, end - start);
    return !host.empty();
}

bool is_allowed_data_host(const std::string& host) {
    if (host.empty() || is_forbidden(host))
        return false;
    static const char* kOk[] = {
        "amazonaws.com",       "nationalmap.gov", "usgs.gov",          "arcgis.com",
        "eox.at",              "s2maps-tiles.eu", "overpass-api.de",   "openstreetmap.fr",
        "openstreetmap.org",   "ourairports.com", "ambientcg.com",     "polyhaven.com",
        "polyhaven.org",       "its.ny.gov",      "ny.gov",            "smcgov.org",
        "kingcounty.gov",      "nj.gov",          "mass.gov",          nullptr};
    for (int i = 0; kOk[i]; ++i) {
        const std::string suf = kOk[i];
        if (host == suf)
            return true;
        if (host.size() > suf.size() &&
            host.compare(host.size() - suf.size() - 1, suf.size() + 1, "." + suf) == 0)
            return true;
    }
    return false;
}

void check_data_url(const std::string& url) {
    std::string host;
    if (!parse_url_host(url, host))
        throw UnsafeEndpoint("data url: unparseable '" + url + "'");
    if (is_forbidden(host))
        throw UnsafeEndpoint("data url: " + host + " is a simulator LAN");
    if (!is_allowed_data_host(host))
        throw UnsafeEndpoint("data url: host '" + host + "' is not on the public DEM/imagery list");
}

} // namespace s376::safety
