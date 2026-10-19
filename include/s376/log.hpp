#pragma once

#include <cstdarg>
#include <cstdint>
#include <string>

namespace s376::log {

enum Level { Debug = 0, Info = 1, Warn = 2, Error = 3 };

struct Options {
    bool verbose = false; // console at Debug
    bool debug = false;   // verbose plus packet dumps / full traces
    bool no_file = false;
    std::string log_file; // empty -> <log_dir>/s376-visual.log
    std::string log_dir;  // empty -> $S376_LOG_DIR or ./logs
};

void setup(const Options& opt, const char* command = "");
void shutdown();

bool packets(); // true when --debug is on
const char* session_file();
// Newest-last ring, up to `max` lines from this process.
std::string recent_lines(int max = 80);
// Last `max` lines of a log file (wizard Logs tab). Empty if missing.
std::string tail_file(const std::string& path, int max = 200);

void write(Level level, const char* subsystem, const char* fmt, ...);
void writev(Level level, const char* subsystem, const char* fmt, va_list ap);

// Rate-limited: at most a few lines per key, then one every `period_s`.
void limited(Level level, const char* subsystem, const char* key, const char* fmt, ...);

std::string hexdump(const std::uint8_t* data, std::size_t n, std::size_t limit = 64);

} // namespace s376::log

#define S376_LOG_DEBUG(sub, ...) ::s376::log::write(::s376::log::Debug, sub, __VA_ARGS__)
#define S376_LOG_INFO(sub, ...) ::s376::log::write(::s376::log::Info, sub, __VA_ARGS__)
#define S376_LOG_WARN(sub, ...) ::s376::log::write(::s376::log::Warn, sub, __VA_ARGS__)
#define S376_LOG_ERROR(sub, ...) ::s376::log::write(::s376::log::Error, sub, __VA_ARGS__)
