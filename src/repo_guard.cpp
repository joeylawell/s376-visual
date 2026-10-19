#include "s376/repo_guard.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>

namespace s376 {
namespace {

std::string lower(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool skip_dir(const std::string& name) {
    return name == ".git" || name == "build" || name == "cache" || name == "downloads" ||
           name == "logs" || name == ".idea" || name == ".vscode";
}

} // namespace

bool is_forbidden_repo_name(const std::string& filename) {
    const std::string n = lower(filename);
    if (n == "nxhostpacket.h")
        return true;
    if (n.rfind("rasterxt", 0) == 0)
        return true;
    if (n.rfind("certs", 0) == 0 && n.find('.') != std::string::npos)
        return true;
    auto dot = n.rfind('.');
    if (dot == std::string::npos)
        return false;
    const std::string ext = n.substr(dot);
    return ext == ".nxt" || ext == ".nxb" || ext == ".nx3" || ext == ".nxs" || ext == ".nxo";
}

std::vector<ForbiddenHit> scan_repo_for_rsi(const std::string& root) {
    std::vector<ForbiddenHit> out;
    std::error_code ec;
    if (root.empty() || !std::filesystem::is_directory(root))
        return out;
    for (auto it = std::filesystem::recursive_directory_iterator(
             root, std::filesystem::directory_options::skip_permission_denied, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec)
            continue;
        if (it->is_directory()) {
            const auto name = it->path().filename().string();
            if (skip_dir(name))
                it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file())
            continue;
        const auto name = it->path().filename().string();
        if (name == ".gitignore" || name == "HANDOFF.md")
            continue;
        if (!is_forbidden_repo_name(name))
            continue;
        ForbiddenHit h;
        h.path = it->path().lexically_relative(root).string();
        if (h.path.empty())
            h.path = it->path().string();
        h.reason = "Raster/RSI database file must not enter the public repo";
        out.push_back(std::move(h));
    }
    return out;
}

std::string format_repo_guard(const std::vector<ForbiddenHit>& hits) {
    std::ostringstream o;
    if (hits.empty()) {
        o << "repo-guard  ok  no .nxt/.nxb/.nx3/RSI files\n";
        return o.str();
    }
    o << "repo-guard  FAIL  " << hits.size() << " forbidden file(s)\n";
    for (const auto& h : hits)
        o << "  " << h.path << "  " << h.reason << "\n";
    return o.str();
}

} // namespace s376
