#include "s376/vaxfloat.hpp"

#include <cmath>
#include <cstring>

namespace s376::vax {
namespace {

std::uint64_t words_to_int(const std::uint8_t* b, int nwords) {
    std::uint64_t v = 0;
    for (int i = 0; i < nwords; ++i) {
        std::uint16_t w = static_cast<std::uint16_t>(b[2 * i] | (b[2 * i + 1] << 8));
        v = (v << 16) | w;
    }
    return v;
}

void int_to_words(std::uint64_t v, std::uint8_t* out, int nwords) {
    for (int i = 0; i < nwords; ++i) {
        int shift = 16 * (nwords - 1 - i);
        std::uint16_t w = static_cast<std::uint16_t>((v >> shift) & 0xFFFF);
        out[2 * i] = static_cast<std::uint8_t>(w & 0xFF);
        out[2 * i + 1] = static_cast<std::uint8_t>(w >> 8);
    }
}

double decode(const std::uint8_t* b, int nbits, int expbits, int bias) {
    const int nwords = nbits / 16;
    const std::uint64_t v = words_to_int(b, nwords);
    const int sign = static_cast<int>(v >> (nbits - 1));
    const int exp = static_cast<int>((v >> (nbits - 1 - expbits)) & ((1u << expbits) - 1));
    const int fbits = nbits - 1 - expbits;
    const std::uint64_t frac = v & ((1ull << fbits) - 1);
    if (exp == 0)
        return 0.0;
    const double mant =
        static_cast<double>(frac | (1ull << fbits)) / static_cast<double>(1ull << (fbits + 1));
    double x = std::ldexp(mant, exp - bias);
    return sign ? -x : x;
}

void encode(double x, std::uint8_t* out, int nbits, int expbits, int bias) {
    const int nwords = nbits / 16;
    if (x == 0.0 || std::isnan(x)) {
        std::memset(out, 0, static_cast<std::size_t>(2 * nwords));
        return;
    }
    const int sign = x < 0 ? 1 : 0;
    int e = 0;
    double m = std::frexp(std::fabs(x), &e); // 0.5 <= m < 1
    const int fbits = nbits - 1 - expbits;
    auto frac =
        static_cast<std::uint64_t>(std::llround(m * static_cast<double>(1ull << (fbits + 1))));
    if (frac >> (fbits + 1)) {
        frac >>= 1;
        ++e;
    }
    const int exp = e + bias;
    if (exp <= 0) {
        std::memset(out, 0, static_cast<std::size_t>(2 * nwords));
        return;
    }
    if (exp >= (1 << expbits))
        throw Overflow("value out of VAX range");
    const std::uint64_t v = (static_cast<std::uint64_t>(sign) << (nbits - 1)) |
                            (static_cast<std::uint64_t>(exp) << fbits) |
                            (frac & ((1ull << fbits) - 1));
    int_to_words(v, out, nwords);
}

} // namespace

double decode_f(const std::uint8_t bytes[4]) {
    return decode(bytes, 32, 8, 128);
}
double decode_d(const std::uint8_t bytes[8]) {
    return decode(bytes, 64, 8, 128);
}
void encode_f(double x, std::uint8_t out[4]) {
    encode(x, out, 32, 8, 128);
}
void encode_d(double x, std::uint8_t out[8]) {
    encode(x, out, 64, 8, 128);
}

} // namespace s376::vax
