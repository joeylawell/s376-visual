#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

int g_fails = 0;
int g_passes = 0;

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

void test_vaxfloat();
void test_xt4();
void test_safety();
void test_config();
void test_geo();
void test_synth_loop();
void test_channels();
void test_terrain();
void test_airport();
void test_hat();
void test_fetch();
void test_imagery();
void test_diagnostics();
void test_image();
void test_display();
void test_lights();
void test_accuracy();
void test_faa_audit();
void test_correct();
void test_parity();
void test_repo_guard();
void test_traffic();
void test_landmarks();
void test_missiles();
void test_wizard();
void test_region();
void test_pilot_view();

int main() {
    test_vaxfloat();
    test_xt4();
    test_safety();
    test_config();
    test_geo();
    test_synth_loop();
    test_channels();
    test_terrain();
    test_airport();
    test_hat();
    test_fetch();
    test_imagery();
    test_diagnostics();
    test_image();
    test_display();
    test_lights();
    test_accuracy();
    test_faa_audit();
    test_correct();
    test_parity();
    test_repo_guard();
    test_traffic();
    test_landmarks();
    test_missiles();
    test_wizard();
    test_region();
    test_pilot_view();
    std::cout << g_passes << " passed, " << g_fails << " failed\n";
    return g_fails ? 1 : 0;
}
