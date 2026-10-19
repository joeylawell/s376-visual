#pragma once

#include <string>
#include <vector>

namespace s376 {

// Time-of-day lighting table. Placeholder numbers live in data/env/tod.toml
// and can be replaced with hangar-tuned values. Hours are local solar.

struct TodSample {
    float hour = 12.f;
    float ambient[3] = {0.16f, 0.16f, 0.18f};
    float diffuse[3] = {1.0f, 0.95f, 0.85f};
    float sun[3] = {1.0f, 0.96f, 0.88f};
    float horizon[3] = {0.55f, 0.72f, 0.95f};
    float haze = 1.1e-6f;
    float stars = 0.f;
};

struct TodTable {
    std::vector<TodSample> samples;
};

TodTable default_tod_table();
bool load_tod_table(const std::string& path, TodTable& out, std::string& err);
TodSample sample_tod(const TodTable& t, float hour);

} // namespace s376
