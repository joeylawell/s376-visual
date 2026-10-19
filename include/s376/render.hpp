#pragma once

#include "s376/config.hpp"
#include "s376/image.hpp"
#include "s376/state.hpp"

#include <memory>
#include <string>
#include <vector>

namespace s376 {

// GLX_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB / WGL equivalent.
constexpr unsigned kGlCompatProfileBit = 0x2;

// How we ask the window system for a GL context. On Linux we leave the
// version unset so NVIDIA gives a 4.6 *compatibility* (legacy) context.
// setGLContextVersion("3.3") with no profile mask is a CORE profile on
// NVIDIA 595, and apt OSG 3.6's fixed-function path is illegal there.
struct GlDisplayPlan {
    std::string version; // empty = do not call setGLContextVersion
    unsigned profile_mask = 0;
    bool check_errors = false; // OSG ONCE_PER_ATTRIBUTE; off in Release
};

GlDisplayPlan plan_gl_display(bool linux_host, bool gl_debug);

class Renderer {
  public:
    virtual ~Renderer() = default;
    // `channels` is the already-selected list (one window, or all enabled).
    virtual bool begin(const AppConfig& cfg, const std::vector<ChannelConfig>& channels) = 0;
    virtual void frame(const HostState& host) = 0;
    virtual bool done() const = 0;
    virtual void request_stop() {}
    virtual bool capture(const std::string&) { return false; }
    // Offscreen FBO of the first channel into `im` (top-down RGB). No files.
    // When more than one channel is selected (e.g. --channel all --record),
    // a Left|Front|Right horizontal strip at each channel's WxH.
    virtual bool capture_rgb(RgbImage&) { return false; }
    // True when the OSG DatabasePager has nothing left to fetch/compile.
    virtual bool pager_idle() const { return true; }
    // CPU (and, when a GL context exists, FBO) solid/gradient pixel check.
    virtual bool capture_self_test(const std::string& dir, std::string& err);
    virtual void end() = 0;
    virtual std::string name() const = 0;
    virtual std::size_t view_count() const { return 0; }
    // Desk/demo PTT when egg.ptt_source = "key".
    virtual bool ptt_key_down() const { return false; }
    // Last GPU frame time in ms from GL_TIME_ELAPSED. <0 if unavailable.
    virtual float last_gpu_ms() const { return -1.f; }
    virtual float last_cpu_ms() const { return -1.f; }
    // Comma-separated features dropped this session (gtx980ti auto-drop).
    virtual std::string autodrop_log() const { return {}; }
    // Sync one timer-query result (stalls). Used by --bench.
    virtual float measure_gpu_ms() { return last_gpu_ms(); }
    virtual void set_render_toggles(const RenderConfig& r) { (void)r; }
};

class NullRenderer : public Renderer {
  public:
    bool begin(const AppConfig&, const std::vector<ChannelConfig>& channels) override {
        n_ = channels.size();
        return true;
    }
    void frame(const HostState&) override {}
    bool done() const override { return false; }
    void end() override { n_ = 0; }
    std::string name() const override { return "null"; }
    std::size_t view_count() const override { return n_; }

  private:
    std::size_t n_ = 0;
};

#ifdef S376_HAS_OSG
std::unique_ptr<Renderer> make_osg_renderer();
#endif

std::unique_ptr<Renderer> make_renderer(const std::string& engine);

} // namespace s376
