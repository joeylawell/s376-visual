#include "s376/vaxfloat.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

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
#define CHECK_NEAR(a, b, eps)                                                                      \
    do {                                                                                           \
        const double _a = static_cast<double>(a);                                                  \
        const double _b = static_cast<double>(b);                                                  \
        if (std::fabs(_a - _b) <= (eps)) {                                                         \
            ++g_passes;                                                                            \
        } else {                                                                                   \
            ++g_fails;                                                                             \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #a << "=" << _a << " "  \
                      << #b << "=" << _b << " eps=" << (eps) << "\n";                              \
        }                                                                                          \
    } while (0)

#include <iostream>

void test_vaxfloat() {
    std::uint8_t one[4];
    s376::vax::encode_f(1.0, one);
    // VAX F 1.0 = words 0x4080, 0x0000 little-endian -> 80 40 00 00
    CHECK(one[0] == 0x80 && one[1] == 0x40 && one[2] == 0x00 && one[3] == 0x00);
    CHECK_NEAR(s376::vax::decode_f(one), 1.0, 1e-9);

    const double samples[] = {
        0.0, 1.0, -1.0, 0.5, 3.14159, 47.54414417813255, -122.30703738740388, 433.0, 29.92};
    for (double x : samples) {
        std::uint8_t f[4], d[8];
        s376::vax::encode_f(x, f);
        s376::vax::encode_d(x, d);
        CHECK_NEAR(s376::vax::decode_f(f), x, std::max(1e-6, std::fabs(x) * 1e-6));
        CHECK_NEAR(s376::vax::decode_d(d), x, std::max(1e-12, std::fabs(x) * 1e-14));
    }

    std::uint8_t z[4] = {};
    CHECK(s376::vax::decode_f(z) == 0.0);

    bool overflow = false;
    try {
        std::uint8_t o[4];
        s376::vax::encode_f(1e39, o);
    } catch (const s376::vax::Overflow&) {
        overflow = true;
    }
    CHECK(overflow);
}
