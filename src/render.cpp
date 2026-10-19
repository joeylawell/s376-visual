#include "s376/image.hpp"
#include "s376/render.hpp"

#include <algorithm>
#include <cctype>

namespace s376 {

GlDisplayPlan plan_gl_display(bool linux_host, bool gl_debug) {
    GlDisplayPlan p;
    p.check_errors = gl_debug;
    if (linux_host) {
        // Leave version empty: glXCreateContext (legacy) → 4.6 compatibility
        // on NVIDIA. Do not set "3.3" without a profile mask.
        p.version.clear();
        p.profile_mask = 0;
    } else {
        p.version = "3.3";
        p.profile_mask = kGlCompatProfileBit;
    }
    return p;
}

bool Renderer::capture_self_test(const std::string& dir, std::string& err) {
    return self_test_shot_files(dir, err);
}

std::unique_ptr<Renderer> make_renderer(const std::string& engine) {
    std::string e = engine;
    std::transform(e.begin(), e.end(), e.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#ifdef S376_HAS_OSG
    if (e == "auto" || e == "osg" || e == "osgearth")
        return make_osg_renderer();
#endif
    return std::make_unique<NullRenderer>();
}

} // namespace s376
