#include "s376/wizard.hpp"

#include "s376/log.hpp"
#include "s376/safety.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

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

std::string unquote(const std::string& s) {
    if (s.size() >= 2 &&
        ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'')))
        return s.substr(1, s.size() - 2);
    return s;
}

bool parse_bool(const std::string& s, bool& out) {
    if (s == "true" || s == "1") {
        out = true;
        return true;
    }
    if (s == "false" || s == "0") {
        out = false;
        return true;
    }
    return false;
}

#ifdef _WIN32
bool pid_running(void* handle, unsigned long) {
    if (!handle)
        return false;
    return WaitForSingleObject(static_cast<HANDLE>(handle), 0) == WAIT_TIMEOUT;
}
void kill_child(void* handle, unsigned long pid) {
    if (handle)
        TerminateProcess(static_cast<HANDLE>(handle), 1);
    (void)pid;
}
void close_child(ChildProc& c) {
    if (c.handle) {
        CloseHandle(static_cast<HANDLE>(c.handle));
        c.handle = nullptr;
    }
    c.pid = 0;
}
#else
bool pid_running(int pid) {
    if (pid <= 0)
        return false;
    int st = 0;
    const pid_t w = waitpid(pid, &st, WNOHANG);
    if (w == 0)
        return true;
    return false;
}
void kill_child(int pid) {
    if (pid > 0)
        kill(pid, SIGTERM);
}
#endif

} // namespace

WizardConfig default_wizard_config() {
    WizardConfig c;
    WizardTarget t;
    t.name = "ig";
    t.channel = "all";
    t.control = "127.0.0.1:2376";
    c.targets.push_back(t);
    return c;
}

WizardConfig load_wizard_config(const std::string& path) {
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("wizard config not found: " + path);
    WizardConfig cfg = default_wizard_config();
    cfg.targets.clear();
    std::string line;
    std::string section;
    WizardTarget* cur = nullptr;
    int lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        auto hash = line.find('#');
        if (hash != std::string::npos)
            line = line.substr(0, hash);
        line = trim(line);
        if (line.empty())
            continue;
        if (line.size() >= 4 && line.rfind("[[", 0) == 0 && line.substr(line.size() - 2) == "]]") {
            const std::string name = trim(line.substr(2, line.size() - 4));
            if (name != "targets")
                throw std::runtime_error(path + ":" + std::to_string(lineno) +
                                         ": unknown table array [[" + name + "]]");
            cfg.targets.emplace_back();
            cur = &cfg.targets.back();
            section = "targets";
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = trim(line.substr(1, line.size() - 2));
            cur = nullptr;
            continue;
        }
        auto eq = line.find('=');
        if (eq == std::string::npos)
            throw std::runtime_error(path + ":" + std::to_string(lineno) + ": expected key = value");
        const std::string key = trim(line.substr(0, eq));
        const std::string val = unquote(trim(line.substr(eq + 1)));
        if (section == "targets" && cur) {
            if (key == "name")
                cur->name = val;
            else if (key == "channel")
                cur->channel = val;
            else if (key == "control")
                cur->control = val;
            continue;
        }
        if (key == "visual" || key == "wizard.visual")
            cfg.visual = val;
        else if (key == "ig_config" || key == "wizard.ig_config")
            cfg.ig_config = val;
        else if (key == "log_dir" || key == "wizard.log_dir")
            cfg.log_dir = val;
        else if (key == "allow_lan" || key == "wizard.allow_lan")
            parse_bool(val, cfg.allow_lan);
        else if (key == "allow_sim_lan" || key == "wizard.allow_sim_lan")
            parse_bool(val, cfg.allow_sim_lan);
    }
    if (cfg.targets.empty())
        cfg = default_wizard_config();
    for (const auto& t : cfg.targets) {
        safety::Endpoint ep;
        std::string err;
        if (!safety::parse_host_port(t.control, ep, err))
            throw std::runtime_error(path + ": bad control " + t.control + " (" + err + ")");
        safety::check_destination(ep.host, ep.port, "wizard target", cfg.allow_sim_lan,
                                  cfg.allow_lan);
    }
    return cfg;
}

std::string dump_wizard_config(const WizardConfig& cfg) {
    std::ostringstream o;
    o << "visual=" << cfg.visual << "\n";
    o << "ig_config=" << cfg.ig_config << "\n";
    o << "log_dir=" << cfg.log_dir << "\n";
    o << "allow_lan=" << (cfg.allow_lan ? "true" : "false") << "\n";
    o << "allow_sim_lan=" << (cfg.allow_sim_lan ? "true" : "false") << "\n";
    for (const auto& t : cfg.targets) {
        o << "target " << t.name << " channel=" << t.channel << " control=" << t.control << "\n";
    }
    return o.str();
}

std::string resolve_visual(const std::string& name) {
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos)
        return name;
    const char* extra[] = {"./", "build/", nullptr};
    for (int i = 0; extra[i]; ++i) {
        const std::string p = std::string(extra[i]) + name;
        std::ifstream in(p);
        if (in)
            return p;
    }
    return name;
}

std::vector<std::string> ig_launch_argv(const WizardConfig& cfg, const WizardTarget& t) {
    std::vector<std::string> a;
    a.push_back(resolve_visual(cfg.visual));
    if (!cfg.ig_config.empty()) {
        a.push_back("--config");
        a.push_back(cfg.ig_config);
    }
    if (!t.channel.empty()) {
        a.push_back("--channel");
        a.push_back(t.channel);
    }
    if (!t.control.empty()) {
        a.push_back("--control");
        a.push_back(t.control);
    }
    return a;
}

bool ChildProc::running() const {
#ifdef _WIN32
    return pid_running(handle, pid);
#else
    return pid_running(pid);
#endif
}

void ChannelSupervisor::configure(const WizardConfig& cfg) {
    cfg_ = cfg;
}

ChildProc* ChannelSupervisor::find(const std::string& name) {
    for (auto& k : kids_) {
        if (k.name == name)
            return &k;
    }
    return nullptr;
}

std::vector<std::string> ChannelSupervisor::default_argv(const WizardTarget& t) const {
    return ig_launch_argv(cfg_, t);
}

bool ChannelSupervisor::launch_argv(const std::string& name, const std::vector<std::string>& argv) {
    stop(name, 500);
    ChildProc c;
    c.name = name;
    c.argv = argv;
    if (argv.empty())
        return false;
#ifdef _WIN32
    std::string cmd;
    for (std::size_t i = 0; i < argv.size(); ++i) {
        if (i)
            cmd += ' ';
        cmd += '"';
        cmd += argv[i];
        cmd += '"';
    }
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<char> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    if (!CreateProcessA(nullptr, buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    c.handle = pi.hProcess;
    c.pid = pi.dwProcessId;
#else
    const pid_t pid = fork();
    if (pid < 0)
        return false;
    if (pid == 0) {
        std::vector<char*> args;
        for (auto& s : const_cast<std::vector<std::string>&>(argv))
            args.push_back(s.data());
        args.push_back(nullptr);
        execvp(args[0], args.data());
        _exit(127);
    }
    c.pid = pid;
#endif
    if (auto* ex = find(name))
        *ex = c;
    else
        kids_.push_back(c);
#ifdef _WIN32
    const int shown = static_cast<int>(c.pid);
#else
    const int shown = c.pid;
#endif
    S376_LOG_INFO("wizard", "launched %s pid %d", name.c_str(), shown);
    return true;
}

bool ChannelSupervisor::launch(const std::string& name) {
    for (const auto& t : cfg_.targets) {
        if (t.name == name || name.empty()) {
            if (!launch_argv(t.name, default_argv(t)))
                return false;
            if (!name.empty())
                return true;
        }
    }
    return name.empty() && !cfg_.targets.empty();
}

bool ChannelSupervisor::stop(const std::string& name, int wait_ms) {
    ChildProc* c = find(name);
    if (!c)
        return true;
#ifdef _WIN32
    if (!c->running()) {
        close_child(*c);
        return true;
    }
    kill_child(c->handle, c->pid);
    const auto t0 = std::chrono::steady_clock::now();
    while (c->running()) {
        const int elapsed = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                  t0)
                .count());
        if (elapsed >= wait_ms)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    close_child(*c);
    return true;
#else
    if (c->pid <= 0)
        return true;
    if (c->running())
        kill_child(c->pid);
    const auto t0 = std::chrono::steady_clock::now();
    while (c->running()) {
        const int elapsed = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                  t0)
                .count());
        if (elapsed >= wait_ms) {
            kill(c->pid, SIGKILL);
            waitpid(c->pid, nullptr, 0);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    c->pid = -1;
    return true;
#endif
}

bool ChannelSupervisor::refresh_all(int wait_ms) {
    // 1. Ask each live IG to clear session state (Classic egg) and exit.
    for (const auto& t : cfg_.targets) {
        ControlMsg req;
        req.op = "refresh";
        try {
            (void)control_request(t.control, req, 400, cfg_.allow_sim_lan, cfg_.allow_lan);
        } catch (...) {
        }
    }
    // 2. Close any children we spawned.
    std::vector<std::string> names;
    for (const auto& k : kids_)
        names.push_back(k.name);
    for (const auto& n : names)
        stop(n, wait_ms);
    // 3. Reopen.
    bool ok = true;
    if (kids_.empty() && !cfg_.targets.empty()) {
        for (const auto& t : cfg_.targets)
            ok = launch(t.name) && ok;
        return ok;
    }
    // Relaunch whoever we used to own. If we had custom argv, reuse it.
    std::vector<ChildProc> prev = kids_;
    kids_.clear();
    for (const auto& p : prev) {
        if (!p.argv.empty())
            ok = launch_argv(p.name, p.argv) && ok;
        else
            ok = launch(p.name) && ok;
    }
    return ok;
}

ControlStatus poll_target(const WizardTarget& t, const WizardConfig& cfg, int timeout_ms) {
    ControlMsg req;
    req.op = "status";
    const auto r = control_request(t.control, req, timeout_ms, cfg.allow_sim_lan, cfg.allow_lan);
    ControlStatus s;
    s.version = r.get("version");
    s.git = r.get("git");
    s.build_date = r.get("build_date");
    s.gpu = r.get("gpu");
    s.driver = r.get("driver");
    s.airports = r.get("airports");
    s.era = r.get("era");
    s.era_unlocked = r.get("era_unlocked") == "1";
    s.missiles = r.get("missiles") == "1";
    s.log_file = r.get("log_file");
    try {
        s.errors = std::stoi(r.get("errors", "0"));
    } catch (...) {
    }
    s.last_error = r.get("last_error");
    int n = 0;
    try {
        n = std::stoi(r.get("n", "0"));
    } catch (...) {
    }
    for (int i = 0; i < n; ++i) {
        const std::string p = "ch." + std::to_string(i) + ".";
        ChannelLive live;
        live.cfg.name = r.get((p + "name").c_str(), t.name.c_str());
        try {
            live.cfg.heading_offset_deg = std::stod(r.get((p + "hdg").c_str(), "0"));
            live.cfg.hfov_deg = std::stod(r.get((p + "hfov").c_str(), "50"));
            live.cfg.vfov_deg = std::stod(r.get((p + "vfov").c_str(), "32.5"));
            live.cfg.flip_vertical = r.get((p + "flip").c_str()) == "1";
            live.cfg.width = std::stoi(r.get((p + "width").c_str(), "1920"));
            live.cfg.height = std::stoi(r.get((p + "height").c_str(), "1200"));
            int rw = live.cfg.width, rh = live.cfg.height;
            if (parse_resolution(r.get((p + "resolution").c_str()), rw, rh)) {
                live.cfg.width = rw;
                live.cfg.height = rh;
            }
            live.corr.gamma = std::stof(r.get((p + "gamma").c_str(), "1"));
            live.corr.blend.ramp = std::stof(r.get((p + "blend_ramp").c_str(), "0.5"));
            live.fps = std::stod(r.get("fps", "0"));
            live.gpu_temp_c = std::stof(r.get("gpu_c", "-1"));
            live.vram_mb = std::stof(r.get("vram_mb", "-1"));
            live.ram_mb = std::stof(r.get("ram_mb", "-1"));
            live.host_age_ms = std::stod(r.get("host_age_ms", "-1"));
        } catch (...) {
        }
        live.corr.pattern = parse_display_pattern(r.get((p + "pattern").c_str(), "none"));
        live.cfg.pattern = display_pattern_name(live.corr.pattern);
        live.cfg.correction_file = r.get((p + "corr").c_str());
        live.errors = s.errors;
        live.last_error = s.last_error;
        s.channels.push_back(std::move(live));
    }
    if (s.channels.empty()) {
        ChannelLive live;
        live.cfg.name = t.name;
        live.errors = s.errors;
        live.last_error = s.last_error;
        s.channels.push_back(live);
    }
    return s;
}

std::string merge_logs(const WizardConfig& cfg, int lines) {
    std::string out;
    for (const auto& t : cfg.targets) {
        ControlMsg req;
        req.op = "logs";
        req.set("n", lines);
        try {
            const auto r = control_request(t.control, req, 300, cfg.allow_sim_lan, cfg.allow_lan);
            if (!r.get("text").empty()) {
                if (!out.empty())
                    out += "\n";
                out += "# " + t.name + "\n" + r.get("text");
            }
        } catch (...) {
        }
    }
    if (out.empty() && !cfg.log_dir.empty()) {
        const std::string path = cfg.log_dir + "/s376-visual.log";
        out = log::tail_file(path, lines);
    }
    return out;
}

} // namespace s376
