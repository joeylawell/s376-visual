#include "s376/tod.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace s376 {
namespace {

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
        ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        --b;
    return s.substr(a, b - a);
}

bool parse_rgb(const std::string& val, float rgb[3]) {
    float a = 0, b = 0, c = 0;
    char sep1 = 0, sep2 = 0;
    if (std::sscanf(val.c_str(), "%f %c %f %c %f", &a, &sep1, &b, &sep2, &c) == 5) {
        rgb[0] = a;
        rgb[1] = b;
        rgb[2] = c;
        return true;
    }
    if (std::sscanf(val.c_str(), "%f %f %f", &a, &b, &c) == 3) {
        rgb[0] = a;
        rgb[1] = b;
        rgb[2] = c;
        return true;
    }
    return false;
}

} // namespace

TodTable default_tod_table() {
    TodTable t;
    auto add = [&](float hour, float ar, float ag, float ab, float dr, float dg, float db, float sr,
                   float sg, float sb, float hr, float hg, float hb, float haze, float stars) {
        TodSample s;
        s.hour = hour;
        s.ambient[0] = ar;
        s.ambient[1] = ag;
        s.ambient[2] = ab;
        s.diffuse[0] = dr;
        s.diffuse[1] = dg;
        s.diffuse[2] = db;
        s.sun[0] = sr;
        s.sun[1] = sg;
        s.sun[2] = sb;
        s.horizon[0] = hr;
        s.horizon[1] = hg;
        s.horizon[2] = hb;
        s.haze = haze;
        s.stars = stars;
        t.samples.push_back(s);
    };
    // Placeholder; replace from the hangar.
    add(0.0f, 0.02f, 0.02f, 0.04f, 0.04f, 0.04f, 0.06f, 0.02f, 0.03f, 0.05f, 0.04f, 0.05f, 0.10f,
        8e-7f, 1.0f);
    add(5.5f, 0.06f, 0.05f, 0.08f, 0.35f, 0.22f, 0.16f, 0.70f, 0.40f, 0.22f, 0.55f, 0.32f, 0.22f,
        1.4e-6f, 0.35f);
    add(6.5f, 0.10f, 0.10f, 0.14f, 0.70f, 0.55f, 0.40f, 0.95f, 0.70f, 0.45f, 0.70f, 0.50f, 0.40f,
        1.6e-6f, 0.05f);
    add(12.0f, 0.16f, 0.16f, 0.18f, 1.00f, 0.95f, 0.85f, 1.00f, 0.96f, 0.88f, 0.55f, 0.72f, 0.95f,
        1.1e-6f, 0.0f);
    add(18.0f, 0.10f, 0.08f, 0.10f, 0.75f, 0.45f, 0.28f, 0.95f, 0.50f, 0.25f, 0.75f, 0.40f, 0.28f,
        1.8e-6f, 0.15f);
    add(19.5f, 0.05f, 0.04f, 0.07f, 0.28f, 0.16f, 0.12f, 0.40f, 0.20f, 0.12f, 0.25f, 0.14f, 0.16f,
        1.2e-6f, 0.55f);
    add(23.0f, 0.02f, 0.02f, 0.04f, 0.05f, 0.05f, 0.07f, 0.02f, 0.03f, 0.05f, 0.04f, 0.05f, 0.10f,
        8e-7f, 1.0f);
    return t;
}

bool load_tod_table(const std::string& path, TodTable& out, std::string& err) {
    std::ifstream in(path);
    if (!in) {
        err = "tod table not found: " + path;
        return false;
    }
    out.samples.clear();
    TodSample cur;
    bool have = false;
    auto flush = [&]() {
        if (have)
            out.samples.push_back(cur);
        cur = TodSample{};
        have = false;
    };
    std::string line;
    while (std::getline(in, line)) {
        auto hash = line.find('#');
        if (hash != std::string::npos)
            line = line.substr(0, hash);
        line = trim(line);
        if (line.empty())
            continue;
        if (line.rfind("[[", 0) == 0) {
            flush();
            have = true;
            continue;
        }
        if (line.front() == '[')
            continue;
        auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        have = true;
        const std::string key = trim(line.substr(0, eq));
        const std::string val = trim(line.substr(eq + 1));
        if (key == "hour")
            cur.hour = std::strtof(val.c_str(), nullptr);
        else if (key == "haze")
            cur.haze = std::strtof(val.c_str(), nullptr);
        else if (key == "stars")
            cur.stars = std::strtof(val.c_str(), nullptr);
        else if (key == "ambient")
            parse_rgb(val, cur.ambient);
        else if (key == "diffuse")
            parse_rgb(val, cur.diffuse);
        else if (key == "sun")
            parse_rgb(val, cur.sun);
        else if (key == "horizon")
            parse_rgb(val, cur.horizon);
    }
    flush();
    if (out.samples.empty()) {
        err = "tod table has no samples: " + path;
        return false;
    }
    std::sort(out.samples.begin(), out.samples.end(),
              [](const TodSample& a, const TodSample& b) { return a.hour < b.hour; });
    return true;
}

TodSample sample_tod(const TodTable& t, float hour) {
    if (t.samples.empty())
        return TodSample{};
    float h = hour;
    if (!std::isfinite(h))
        h = 12.f;
    while (h < 0.f)
        h += 24.f;
    while (h >= 24.f)
        h -= 24.f;
    const auto& s = t.samples;
    if (s.size() == 1)
        return s.front();

    std::size_t i1 = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i].hour > h) {
            i1 = i;
            break;
        }
        i1 = 0;
    }
    const std::size_t i0 = (i1 == 0) ? s.size() - 1 : i1 - 1;
    float h0 = s[i0].hour;
    float h1 = s[i1].hour;
    float span = h1 - h0;
    if (span <= 0.f)
        span += 24.f;
    float x = h - h0;
    if (x < 0.f)
        x += 24.f;
    const float u = std::max(0.f, std::min(1.f, x / span));
    TodSample o;
    o.hour = h;
    auto lerp3 = [&](const float a[3], const float b[3], float* d) {
        d[0] = a[0] + (b[0] - a[0]) * u;
        d[1] = a[1] + (b[1] - a[1]) * u;
        d[2] = a[2] + (b[2] - a[2]) * u;
    };
    lerp3(s[i0].ambient, s[i1].ambient, o.ambient);
    lerp3(s[i0].diffuse, s[i1].diffuse, o.diffuse);
    lerp3(s[i0].sun, s[i1].sun, o.sun);
    lerp3(s[i0].horizon, s[i1].horizon, o.horizon);
    o.haze = s[i0].haze + (s[i1].haze - s[i0].haze) * u;
    o.stars = s[i0].stars + (s[i1].stars - s[i0].stars) * u;
    return o;
}

} // namespace s376
