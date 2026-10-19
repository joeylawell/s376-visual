#pragma once

#include "s376/config.hpp"
#include "s376/display.hpp"
#include "s376/landmarks.hpp"
#include "s376/state.hpp"

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace s376 {

// Local operator ↔ IG control channel. This is NOT xT4 and never uses
// UDP 2300/2301. Default bind is loopback. Tests/CI must not point it
// at 10.1.95.0/24 or 192.168.173.0/24.

constexpr const char* kControlMagic = "S376C1";
constexpr std::uint16_t kControlDefaultPort = 2376;

struct ControlMsg {
    std::string op;
    std::uint32_t id = 0;
    std::map<std::string, std::string> kv;

    std::string get(const char* key, const char* fallback = "") const;
    void set(const char* key, const std::string& value);
    void set(const char* key, double value);
    void set(const char* key, int value);
    bool ok() const;
};

std::string encode_control(const ControlMsg& m);
bool decode_control(const std::string& text, ControlMsg& out);

// Live IG state the wizard polls. One process may own several channels.
struct ChannelLive {
    ChannelConfig cfg;
    DisplayCorrection corr;
    double fps = 0;
    float gpu_temp_c = -1;
    float vram_mb = -1;
    float ram_mb = -1;
    double host_age_ms = -1;
    int errors = 0;
    std::string last_error;
};

struct ControlStatus {
    std::vector<ChannelLive> channels;
    std::string version;
    std::string git;
    std::string build_date;
    std::string gpu;
    std::string driver;
    std::string airports;
    std::string era;
    bool era_unlocked = false;
    bool missiles = false;
    std::string toast;
    std::string log_file;
    int errors = 0;
    std::string last_error;
};

class ControlHub {
  public:
    void attach(const AppConfig& cfg, std::vector<ChannelConfig> selected);
    void note_frame(double now_s);
    void note_host(const HostState& host, double now_s);
    void note_error(const std::string& msg);
    ControlMsg handle(const ControlMsg& req);
    ControlStatus snapshot() const;
    std::vector<ChannelConfig> live_channels() const;
    bool restart_requested() const;
    void clear_restart();
    void reset(); // tests

  private:
    mutable std::mutex mu_;
    AppConfig cfg_;
    std::vector<ChannelLive> chans_;
    double last_frame_s_ = 0;
    double fps_ = 0;
    int frames_ = 0;
    double last_host_s_ = 0;
    bool have_host_ = false;
    int errors_ = 0;
    std::string last_error_;
    bool restart_ = false;
    std::string save_dir_;
    EraUnlock unlock_;

    ChannelLive* find(const std::string& name);
    void fill_status(ControlMsg& r) const;
    void apply_set(const ControlMsg& req);
    bool save_display(std::string& err);
};

ControlHub& control_hub();

class ControlServer {
  public:
    ControlServer();
    ~ControlServer();
    ControlServer(const ControlServer&) = delete;
    ControlServer& operator=(const ControlServer&) = delete;

    // bind e.g. "127.0.0.1:2376" or "127.0.0.1:0" (ephemeral).
    void start(const std::string& bind, bool allow_sim_lan, bool allow_lan);
    void stop();
    std::uint16_t port() const { return port_; }
    bool is_open() const { return fd_ >= 0; }

  private:
    void loop();
    int fd_ = -1;
    std::uint16_t port_ = 0;
    std::thread th_;
    std::atomic<bool> run_{false};
#ifdef _WIN32
    bool wsa_ = false;
#endif
};

// One request/reply. Destination must pass safety::check_destination.
ControlMsg control_request(const std::string& dest, const ControlMsg& req, int timeout_ms,
                           bool allow_sim_lan, bool allow_lan);

struct GpuSample {
    float temp_c = -1;
    float vram_mb = -1;
    std::string name;
    std::string driver;
};

GpuSample sample_gpu();
std::string loaded_airport_list(const std::string& cache_dir);

} // namespace s376
