#pragma once

#include <cstdint>
#include <stdexcept>

namespace s376::vax {

// VAX F (32-bit) and D (64-bit) floating point.
// Memory layout is little-endian 16-bit words, most-significant word first:
//   F: word0 = sign | exp(8) | frac(7),  word1 = frac(16)
//   D: word0 = sign | exp(8) | frac(7),  words 1-3 = frac(48)
// value = 0.1ffff * 2^(exp - 128); exp == 0 is zero.

double decode_f(const std::uint8_t bytes[4]);
double decode_d(const std::uint8_t bytes[8]);
void encode_f(double x, std::uint8_t out[4]);
void encode_d(double x, std::uint8_t out[8]);

class Overflow : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

} // namespace s376::vax
