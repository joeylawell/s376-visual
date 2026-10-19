#pragma once

#define S376_VERSION_MAJOR 0
#define S376_VERSION_MINOR 1
#define S376_VERSION_PATCH 0
#define S376_VERSION_STRING "0.1.0"

namespace s376 {

inline const char* version() {
    return S376_VERSION_STRING;
}

} // namespace s376
