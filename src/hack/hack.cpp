#include "hack.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace ddvk {
namespace hack {

static bool EndsWithIgnoreCaseImpl(const std::string& text, const char* suffix) {
    const size_t suffixLen = std::strlen(suffix);
    if (text.size() < suffixLen) return false;
    const size_t start = text.size() - suffixLen;
    for (size_t i = 0; i < suffixLen; ++i) {
        const unsigned char a = static_cast<unsigned char>(text[start + i]);
        const unsigned char b = static_cast<unsigned char>(suffix[i]);
        if (std::tolower(a) != std::tolower(b)) {
            return false;
        }
    }
    return true;
}

static bool PathContainsIgnoreCaseImpl(std::string text, const char* needle) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::string pattern(needle);
    std::transform(pattern.begin(), pattern.end(), pattern.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text.find(pattern) != std::string::npos;
}

bool EndsWithIgnoreCase(const std::string& text, const char* suffix) {
    return EndsWithIgnoreCaseImpl(text, suffix);
}

bool PathContainsIgnoreCase(std::string text, const char* needle) {
    return PathContainsIgnoreCaseImpl(text, needle);
}

bool IsWestwoodProcess() {
    static bool initialized = false;
    static bool isWestwood = false;
    if (!initialized) {
        initialized = true;

        char exePath[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);

        const char* exeName = std::strrchr(exePath, '\\');
        const char* altSlash = std::strrchr(exePath, '/');
        if (!exeName || (altSlash && altSlash > exeName)) {
            exeName = altSlash;
        }
        exeName = exeName ? (exeName + 1) : exePath;

        std::string fullPath(exePath);
        std::string exeNameStr(exeName);

        const bool looksLikeDune2000 =
            EndsWithIgnoreCaseImpl(exeNameStr, "dune2000.exe") ||
            EndsWithIgnoreCaseImpl(exeNameStr, "dune2000.dat") ||
            EndsWithIgnoreCaseImpl(exeNameStr, "dune2000-spawn.exe") ||
            EndsWithIgnoreCaseImpl(exeNameStr, "dune2k.exe") ||
            (EndsWithIgnoreCaseImpl(exeNameStr, "game.exe") &&
             (PathContainsIgnoreCaseImpl(fullPath, "dune2000") ||
              PathContainsIgnoreCaseImpl(fullPath, "dune 2000") ||
              PathContainsIgnoreCaseImpl(fullPath, "games\\dune") ||
              PathContainsIgnoreCaseImpl(fullPath, "/games/dune/")));

        const bool looksLikeTiberianSun =
            EndsWithIgnoreCaseImpl(exeNameStr, "sun.exe") ||
            EndsWithIgnoreCaseImpl(exeNameStr, "sunmd.exe") ||
            (EndsWithIgnoreCaseImpl(exeNameStr, "game.exe") &&
             (PathContainsIgnoreCaseImpl(fullPath, "tiberian") ||
              PathContainsIgnoreCaseImpl(fullPath, "sun")));

        const bool looksLikeRedAlert2 =
            EndsWithIgnoreCaseImpl(exeNameStr, "ra2.exe") ||
            EndsWithIgnoreCaseImpl(exeNameStr, "ra2md.exe") ||
            EndsWithIgnoreCaseImpl(exeNameStr, "yuri.exe") ||
            EndsWithIgnoreCaseImpl(exeNameStr, "gamemd.exe") ||
            (EndsWithIgnoreCaseImpl(exeNameStr, "game.exe") &&
             (PathContainsIgnoreCaseImpl(fullPath, "red alert") ||
              PathContainsIgnoreCaseImpl(fullPath, "ra2") ||
              PathContainsIgnoreCaseImpl(fullPath, "yuri")));

        isWestwood = looksLikeDune2000 ||
                     looksLikeTiberianSun ||
                     looksLikeRedAlert2 ||
                     PathContainsIgnoreCaseImpl(fullPath, "dune2000") ||
                     PathContainsIgnoreCaseImpl(fullPath, "dune 2000") ||
                     PathContainsIgnoreCaseImpl(fullPath, "westwood");
    }
    return isWestwood;
}

// Dune 2000 и Tiberian Dawn используют BGR565, в отличие от Tiberian Sun (RGB565)
bool IsBGR565Game() {
    static bool initialized = false;
    static bool isBGR565 = false;
    if (!initialized) {
        initialized = true;

        char exePath[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);

        const char* exeName = std::strrchr(exePath, '\\');
        const char* altSlash = std::strrchr(exePath, '/');
        if (!exeName || (altSlash && altSlash > exeName)) {
            exeName = altSlash;
        }
        exeName = exeName ? (exeName + 1) : exePath;

        std::string fullPath(exePath);
        std::string exeNameStr(exeName);

        // Dune 2000
        const bool isDune2000 =
            EndsWithIgnoreCaseImpl(exeNameStr, "dune2000.exe") ||
            EndsWithIgnoreCaseImpl(exeNameStr, "dune2000.dat") ||
            EndsWithIgnoreCaseImpl(exeNameStr, "dune2000-spawn.exe") ||
            EndsWithIgnoreCaseImpl(exeNameStr, "dune2k.exe") ||
            (EndsWithIgnoreCaseImpl(exeNameStr, "game.exe") &&
             (PathContainsIgnoreCaseImpl(fullPath, "dune2000") ||
              PathContainsIgnoreCaseImpl(fullPath, "dune 2000") ||
              PathContainsIgnoreCaseImpl(fullPath, "games\\dune") ||
              PathContainsIgnoreCaseImpl(fullPath, "/games/dune/")));

        // Tiberian Dawn (Command & Conquer 1)
        const bool isTiberianDawn =
            EndsWithIgnoreCaseImpl(exeNameStr, "cnc.exe") ||
            EndsWithIgnoreCaseImpl(exeNameStr, "cnc95.exe") ||
            EndsWithIgnoreCaseImpl(exeNameStr, "conquer.exe") ||
            (EndsWithIgnoreCaseImpl(exeNameStr, "game.exe") &&
             (PathContainsIgnoreCaseImpl(fullPath, "tiberian dawn") ||
              PathContainsIgnoreCaseImpl(fullPath, "command & conquer") ||
              PathContainsIgnoreCaseImpl(fullPath, "command and conquer") ||
              PathContainsIgnoreCaseImpl(fullPath, "cnc") ||
              PathContainsIgnoreCaseImpl(fullPath, "conquer")));

        // Red Alert 1
        const bool isRedAlert1 =
            EndsWithIgnoreCaseImpl(exeNameStr, "ra.exe") ||
            EndsWithIgnoreCaseImpl(exeNameStr, "redalert.exe") ||
            (EndsWithIgnoreCaseImpl(exeNameStr, "game.exe") &&
             (PathContainsIgnoreCaseImpl(fullPath, "red alert") ||
              PathContainsIgnoreCaseImpl(fullPath, "command & conquer")));

        isBGR565 = isDune2000 || isTiberianDawn || isRedAlert1;
    }
    return isBGR565;
}

} // namespace hack
} // namespace ddvk
