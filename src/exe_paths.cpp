#include "exe_paths.h"

#include <windows.h>

#include <cstddef>

namespace acr_ht {
namespace {

// No path can exceed this, so the doubling below terminates.
constexpr std::size_t kMaxNtPathChars = 32767;

// GetModuleFileNameW truncates instead of failing, and reports the buffer size
// rather than the required one, so the only way to know the name fitted is that
// it came back shorter than the buffer. A fixed MAX_PATH buffer therefore turns
// a game installed under a long path into "could not resolve the game
// directory" and a dormant mod, on a machine where nothing is actually wrong.
std::wstring ModulePath() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(),
                                                static_cast<DWORD>(path.size()));
        if (length == 0) return {};
        if (length < path.size()) {
            path.resize(length);
            return path;
        }
        if (path.size() >= kMaxNtPathChars) return {};
        path.resize(path.size() * 2);
    }
}

}  // namespace

bool DirectoryOf(const std::wstring& module_path, std::wstring& directory) {
    const size_t separator = module_path.find_last_of(L'\\');
    if (separator == std::wstring::npos) return false;

    directory = module_path.substr(0, separator);
    return true;
}

bool NarrowToAnsi(const std::wstring& wide, std::string& narrow) {
    if (wide.empty()) return false;

    // Best-fit mapping is on by default for the ANSI code page: a character
    // with no encoding is quietly replaced by one that looks similar, so a
    // directory can narrow to the name of a DIFFERENT directory that exists -
    // and the config is then read from, and written to, the wrong one.
    // WC_NO_BEST_FIT_CHARS plus the used-default flag turns that into a
    // refusal.
    //
    // The Unicode transformations need neither, because they can represent
    // every character - and WideCharToMultiByte rejects both outright for
    // them, so passing them anyway would fail every conversion rather than
    // just the lossy ones.
    const UINT codePage = GetACP();
    const bool unicodeCodePage =
        (codePage == CP_UTF8 || codePage == CP_UTF7 || codePage == 54936 /* GB18030 */);
    const DWORD flags = unicodeCodePage ? 0 : WC_NO_BEST_FIT_CHARS;
    BOOL usedDefault = FALSE;
    BOOL* const usedDefaultOut = unicodeCodePage ? nullptr : &usedDefault;

    const int bytes = WideCharToMultiByte(codePage, flags, wide.c_str(),
                                          static_cast<int>(wide.size()),
                                          nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return false;

    std::string converted(static_cast<size_t>(bytes), '\0');
    if (WideCharToMultiByte(codePage, flags, wide.c_str(), static_cast<int>(wide.size()),
                            converted.data(), bytes, nullptr, usedDefaultOut) != bytes) {
        return false;
    }
    if (usedDefault) return false;

    narrow = converted;
    return true;
}

bool ExeDirectory(std::wstring& wide, std::string& narrow) {
    const std::wstring path = ModulePath();
    if (path.empty()) return false;

    std::wstring directory;
    if (!DirectoryOf(path, directory)) return false;

    wide = directory;
    // The log takes the UTF-16 form and always works. Only the ANSI INI layer
    // needs the narrow one, so a directory the code page cannot represent
    // costs the config file and nothing else - which is a far better outcome
    // than a mod that refuses to run, and a far safer one than a best-fit
    // approximation naming somebody else's folder.
    if (!NarrowToAnsi(directory, narrow)) narrow.clear();
    return true;
}

}  // namespace acr_ht
