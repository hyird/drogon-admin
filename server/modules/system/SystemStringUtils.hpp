#pragma once

#include <cctype>
#include <string>

namespace SystemStringUtils {

inline std::string toLowerAscii(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

inline bool containsIgnoreCase(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) {
        return true;
    }

    auto lowerHaystack = toLowerAscii(haystack);
    auto lowerNeedle = toLowerAscii(needle);
    return lowerHaystack.find(lowerNeedle) != std::string::npos;
}

}  // namespace SystemStringUtils
