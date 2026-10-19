#include "s376/repo_guard.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

extern int g_fails;
extern int g_passes;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (cond) {                                                                                \
            ++g_passes;                                                                            \
        } else {                                                                                   \
            ++g_fails;                                                                             \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #cond << "\n";          \
        }                                                                                          \
    } while (0)

void test_repo_guard() {
    CHECK(s376::is_forbidden_repo_name("foo.nxt"));
    CHECK(s376::is_forbidden_repo_name("BAR.NXB"));
    CHECK(s376::is_forbidden_repo_name("x.nx3"));
    CHECK(s376::is_forbidden_repo_name("nxHostPacket.h"));
    CHECK(s376::is_forbidden_repo_name("RasterXT4.bin"));
    CHECK(!s376::is_forbidden_repo_name("airport.cpp"));
    CHECK(!s376::is_forbidden_repo_name("HANDOFF.md"));
    CHECK(!s376::is_forbidden_repo_name(".gitignore"));

    const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().string();
    const auto hits = s376::scan_repo_for_rsi(root);
    if (!hits.empty())
        std::cerr << s376::format_repo_guard(hits);
    CHECK(hits.empty());
    CHECK(s376::format_repo_guard(hits).find("ok") != std::string::npos);

    const auto tmp = (std::filesystem::temp_directory_path() / "s376-rsi-guard").string();
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp);
    {
        std::ofstream o(tmp + "/evil.nxt");
        o << "nope";
    }
    const auto bad = s376::scan_repo_for_rsi(tmp);
    CHECK(bad.size() == 1);
    CHECK(s376::format_repo_guard(bad).find("FAIL") != std::string::npos);
    std::filesystem::remove_all(tmp);
}
