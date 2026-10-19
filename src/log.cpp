#include "s376/log.hpp"
#include "s376/version.hpp"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <unordered_map>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace s376::log {
namespace {

std::mutex g_mu;
Options g_opt;
std::ofstream g_file;
std::string g_path;
bool g_ready = false;
std::deque<std::string> g_recent;
constexpr int kRecentMax = 256;

const char* level_name(Level l) {
    switch (l) {
    case Debug:
        return "DEBUG";
    case Info:
        return "INFO";
    case Warn:
        return "WARNING";
    case Error:
        return "ERROR";
    }
    return "?";
}

std::string now_stamp() {
    using namespace std::chrono;
    const auto tp = system_clock::now();
    const auto t = system_clock::to_time_t(tp);
    const auto ms = duration_cast<milliseconds>(tp.time_since_epoch()) % 1000;
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d", tm.tm_year + 1900,
                  tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
                  static_cast<int>(ms.count()));
    return buf;
}

std::string default_dir() {
    if (const char* e = std::getenv("S376_LOG_DIR"))
        return e;
    return "logs";
}

void ensure_dir(const std::string& path) {
    std::string d = path;
    auto sl = d.find_last_of("/\\");
    if (sl == std::string::npos)
        return;
    d.resize(sl);
    if (d.empty())
        return;
#ifdef _WIN32
    CreateDirectoryA(d.c_str(), nullptr);
#else
    mkdir(d.c_str(), 0755);
#endif
}

struct Rate {
    int count = 0;
    int suppressed = 0;
    double last = 0;
};

std::unordered_map<std::string, Rate> g_rates;

double mono() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

} // namespace

void setup(const Options& opt, const char* command) {
#ifdef _WIN32
    const int pid = static_cast<int>(GetCurrentProcessId());
#else
    const int pid = static_cast<int>(getpid());
#endif
    {
        std::lock_guard<std::mutex> g(g_mu);
        g_opt = opt;
        g_file.close();
        g_path.clear();
        g_ready = true;
        if (!opt.no_file) {
            std::string dir = opt.log_dir.empty() ? default_dir() : opt.log_dir;
            g_path = opt.log_file.empty() ? dir + "/s376-visual.log" : opt.log_file;
            ensure_dir(g_path);
            g_file.open(g_path, std::ios::app);
            if (!g_file)
                g_path.clear();
        }
    }
    write(Info, "ig", "==== s376-visual %s %s | pid %d", version(), command ? command : "", pid);
    if (session_file())
        write(Debug, "ig", "log file: %s", session_file());
}

void shutdown() {
    std::lock_guard<std::mutex> g(g_mu);
    g_file.close();
    g_ready = false;
}

bool packets() {
    return g_opt.debug;
}

const char* session_file() {
    return g_path.empty() ? nullptr : g_path.c_str();
}

std::string recent_lines(int max) {
    std::lock_guard<std::mutex> g(g_mu);
    if (max < 1)
        max = 1;
    std::string out;
    const int n = static_cast<int>(g_recent.size());
    const int start = n > max ? n - max : 0;
    for (int i = start; i < n; ++i) {
        out += g_recent[static_cast<std::size_t>(i)];
        out += '\n';
    }
    return out;
}

std::string tail_file(const std::string& path, int max) {
    std::ifstream in(path);
    if (!in)
        return {};
    std::deque<std::string> q;
    std::string line;
    while (std::getline(in, line)) {
        q.push_back(line);
        if (static_cast<int>(q.size()) > max && max > 0)
            q.pop_front();
    }
    std::string out;
    for (const auto& l : q) {
        out += l;
        out += '\n';
    }
    return out;
}

void writev(Level level, const char* subsystem, const char* fmt, va_list ap) {
    char msg[2048];
    va_list copy;
    va_copy(copy, ap);
    std::vsnprintf(msg, sizeof(msg), fmt, copy);
    va_end(copy);

    const Level console_min = (g_opt.verbose || g_opt.debug) ? Debug : Info;
    std::ostringstream line;
    line << now_stamp() << ' ' << level_name(level);
    for (int i = static_cast<int>(std::strlen(level_name(level))); i < 7; ++i)
        line << ' ';
    line << ' ' << (subsystem ? subsystem : "ig");
    const std::string sub = subsystem ? subsystem : "ig";
    for (int i = static_cast<int>(sub.size()); i < 10; ++i)
        line << ' ';
    line << ' ' << msg;
    const std::string s = line.str();

    std::lock_guard<std::mutex> g(g_mu);
    g_recent.push_back(s);
    while (static_cast<int>(g_recent.size()) > kRecentMax)
        g_recent.pop_front();
    if (level >= console_min)
        std::cerr << s << '\n';
    if (g_file)
        g_file << s << '\n' << std::flush;
}

void write(Level level, const char* subsystem, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    writev(level, subsystem, fmt, ap);
    va_end(ap);
}

void limited(Level level, const char* subsystem, const char* key, const char* fmt, ...) {
    bool emit = false;
    int suppressed = 0;
    {
        std::lock_guard<std::mutex> g(g_mu);
        Rate& r = g_rates[key ? key : ""];
        ++r.count;
        const double t = mono();
        if (r.count <= 3 || t - r.last >= 5.0) {
            emit = true;
            suppressed = r.suppressed;
            r.suppressed = 0;
            r.last = t;
        } else {
            ++r.suppressed;
        }
    }
    if (!emit)
        return;
    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    if (suppressed)
        write(level, subsystem, "%s (%d similar suppressed)", msg, suppressed);
    else
        write(level, subsystem, "%s", msg);
}

std::string hexdump(const std::uint8_t* data, std::size_t n, std::size_t limit) {
    std::string out;
    const std::size_t show = n < limit ? n : limit;
    for (std::size_t off = 0; off < show; off += 16) {
        char line[80];
        std::snprintf(line, sizeof(line), "%04zx  ", off);
        out += line;
        const std::size_t chunk = (show - off) < 16 ? (show - off) : 16;
        for (std::size_t i = 0; i < 16; ++i) {
            if (i < chunk) {
                std::snprintf(line, sizeof(line), "%02x ", data[off + i]);
                out += line;
            } else {
                out += "   ";
            }
        }
        out += ' ';
        for (std::size_t i = 0; i < chunk; ++i) {
            const unsigned c = data[off + i];
            out += (c >= 32 && c < 127) ? static_cast<char>(c) : '.';
        }
        out += '\n';
    }
    if (n > limit) {
        out += "... (";
        out += std::to_string(n - limit);
        out += " more bytes)\n";
    }
    return out;
}

} // namespace s376::log
