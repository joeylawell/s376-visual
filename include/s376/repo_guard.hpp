#pragma once

#include <string>
#include <vector>

namespace s376 {

// Fail CI if a Raster / RSI / CAE database file lands in the public tree.
// Extensions and names only — we do not open those files.

struct ForbiddenHit {
    std::string path;
    std::string reason;
};

bool is_forbidden_repo_name(const std::string& filename);
std::vector<ForbiddenHit> scan_repo_for_rsi(const std::string& root);
std::string format_repo_guard(const std::vector<ForbiddenHit>& hits);

} // namespace s376
