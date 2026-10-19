#pragma once

#include "s376/control.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace s376 {

// Operator console (s376-wizard). Runs on the Host IO / operator PC.
// Talks to channel IGs over the local control channel — never xT4,
// never the simulator LAN from tests.

struct WizardTarget {
    std::string name;
    std::string channel = "all";
    std::string control = "127.0.0.1:2376";
};

struct WizardConfig {
    std::string visual = "s376-visual";
    std::string ig_config = "config/channels-3.toml";
    std::string log_dir = "logs";
    bool allow_lan = false;
    bool allow_sim_lan = false;
    std::vector<WizardTarget> targets;
};

WizardConfig default_wizard_config();
WizardConfig load_wizard_config(const std::string& path);
std::string dump_wizard_config(const WizardConfig& cfg);

struct ChildProc {
    std::string name;
    std::vector<std::string> argv;
#ifdef _WIN32
    void* handle = nullptr;
    unsigned long pid = 0;
#else
    int pid = -1;
#endif
    bool running() const;
};

class ChannelSupervisor {
  public:
    void configure(const WizardConfig& cfg);
    bool launch(const std::string& name);
    bool launch_argv(const std::string& name, const std::vector<std::string>& argv);
    bool stop(const std::string& name, int wait_ms = 4000);
    // Close + reopen every channel IG. Sends `refresh` first (clears
    // session state including Classic), then SIGTERM / wait / relaunch.
    bool refresh_all(int wait_ms = 4000);
    const std::vector<ChildProc>& children() const { return kids_; }
    const WizardConfig& config() const { return cfg_; }

  private:
    WizardConfig cfg_;
    std::vector<ChildProc> kids_;
    ChildProc* find(const std::string& name);
    std::vector<std::string> default_argv(const WizardTarget& t) const;
};

std::vector<std::string> ig_launch_argv(const WizardConfig& cfg, const WizardTarget& t);
std::string resolve_visual(const std::string& name);

// Poll one or more IGs. Destinations are safety-checked.
ControlStatus poll_target(const WizardTarget& t, const WizardConfig& cfg, int timeout_ms = 400);
std::string merge_logs(const WizardConfig& cfg, int lines = 200);

} // namespace s376
