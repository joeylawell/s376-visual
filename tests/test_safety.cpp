#include "s376/safety.hpp"

#include <iostream>
#include <stdexcept>

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

void test_safety() {
    using namespace s376::safety;
    CHECK(is_loopback("127.0.0.1"));
    CHECK(is_loopback("localhost"));
    CHECK(!is_loopback("8.8.8.8"));
    CHECK(is_forbidden("10.1.95.1"));
    CHECK(is_forbidden("10.1.95.123"));
    CHECK(is_forbidden("192.168.173.5"));
    CHECK(is_forbidden("192.168.173.10"));
    CHECK(!is_forbidden("127.0.0.1"));
    CHECK(!is_forbidden("0.0.0.0"));
    CHECK(!is_forbidden("192.168.1.1"));

    bool threw = false;
    try {
        check_destination("10.1.95.1", 2300, "test", false, false);
    } catch (const UnsafeEndpoint&) {
        threw = true;
    }
    CHECK(threw);

    threw = false;
    try {
        check_destination("192.168.173.10", 2301, "test", false, true);
    } catch (const UnsafeEndpoint&) {
        threw = true;
    }
    CHECK(threw);

    auto ok = check_destination("127.0.0.1", 2300, "test", false, false);
    CHECK(ok.host == "127.0.0.1" && ok.port == 2300);

    auto bind = check_bind("0.0.0.0", 2300, "test", false, false);
    CHECK(bind.host == "0.0.0.0");

    Endpoint ep;
    std::string err;
    CHECK(parse_host_port("127.0.0.1:2300", ep, err));
    CHECK(ep.port == 2300);
    CHECK(!parse_host_port("no-port", ep, err));
}
