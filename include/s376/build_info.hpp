#pragma once

// Fallback when CMake has not generated a stamp. The build dir copy
// (S376_GIT_DESCRIBE / S376_BUILD_DATE) wins when present.
#ifndef S376_GIT_DESCRIBE
#define S376_GIT_DESCRIBE "unknown"
#endif
#ifndef S376_BUILD_DATE
#define S376_BUILD_DATE "unknown"
#endif

namespace s376 {

inline const char* git_describe() {
    return S376_GIT_DESCRIBE[0] ? S376_GIT_DESCRIBE : "unknown";
}

inline const char* build_date() {
    return S376_BUILD_DATE[0] ? S376_BUILD_DATE : "unknown";
}

} // namespace s376
