// Pins the two sharp edges in resolving the game directory. Both fail closed,
// and both have to: a path that degrades to "" turns the INI path into
// "\HeadTracking.ini" and writes the config to the root of the current drive,
// and the caller reads a false return as "stay dormant", which is the correct
// outcome rather than a guess at where the game lives.

#include "exe_paths.h"
#include "test_support.h"

#include <windows.h>

#include <iostream>

#include <string>

namespace {

using acr_ht_tests::Check;

int g_failures = 0;

void DirectoryOfKeepsTheDirectoryPart() {
    std::wstring directory = L"untouched";
    Check(g_failures,
          acr_ht::DirectoryOf(L"C:\\Games\\Assetto Corsa Rally\\acr\\Binaries\\Win64\\acr.exe",
                              directory) &&
              directory == L"C:\\Games\\Assetto Corsa Rally\\acr\\Binaries\\Win64",
          "DirectoryOf returns the directory part of a full path");

    directory = L"untouched";
    Check(g_failures, acr_ht::DirectoryOf(L"C:\\acr.exe", directory) && directory == L"C:",
          "DirectoryOf handles a path directly under a drive root");
}

void DirectoryOfRefusesAPathWithNoDirectory() {
    std::wstring directory = L"untouched";
    Check(g_failures, !acr_ht::DirectoryOf(L"acr.exe", directory) && directory == L"untouched",
          "a separator-less path fails rather than yielding the drive root");

    directory = L"untouched";
    Check(g_failures, !acr_ht::DirectoryOf(L"", directory) && directory == L"untouched",
          "an empty path fails rather than yielding the drive root");
}

void NarrowToAnsiConvertsAndFailsClosed() {
    std::string narrow = "untouched";
    Check(g_failures,
          acr_ht::NarrowToAnsi(L"C:\\Games\\acr", narrow) && narrow == "C:\\Games\\acr",
          "NarrowToAnsi converts an ordinary path");

    // The one input that yields no bytes. It is reachable - DirectoryOf on
    // "\\acr.exe" returns an empty directory - and failing here is what keeps
    // the mod dormant instead of logging to the drive root.
    narrow = "untouched";
    Check(g_failures, !acr_ht::NarrowToAnsi(L"", narrow) && narrow == "untouched",
          "an empty wide string fails rather than producing an empty path");

    // The reason the function exists. Without WC_NO_BEST_FIT_CHARS and the
    // used-default check, a character the code page cannot encode is quietly
    // swapped for one that looks similar, so the narrowed path can name a
    // DIFFERENT directory that exists - and the config is then read from and
    // written to somebody else's folder. Strip either guard and this is the
    // only assertion in the suite that notices.
    //
    // Which character is unrepresentable depends on the code page: a kanji is
    // encodable in Shift-JIS and GBK, so hardcoding one turns this into a red
    // suite on Japanese or Chinese Windows for a non-bug. Ask the code page
    // itself, and skip when it can represent the character.
    const wchar_t* const kUnmappable = L"\u5C71";  // a CJK ideograph
    BOOL usedDefault = FALSE;
    const int probe = WideCharToMultiByte(GetACP(), 0, kUnmappable, 1, nullptr, 0, nullptr,
                                          nullptr);
    char probeBytes[8]{};
    if (probe > 0 && probe < static_cast<int>(sizeof(probeBytes))) {
        WideCharToMultiByte(GetACP(), 0, kUnmappable, 1, probeBytes, probe, nullptr,
                            &usedDefault);
    }

    if (!usedDefault) {
        std::cout << "  [SKIP] this code page can represent the test character\n";
        return;
    }

    narrow = "untouched";
    Check(g_failures, !acr_ht::NarrowToAnsi(L"C:\\Games\\\u5C71\\acr", narrow) &&
                          narrow == "untouched",
          "a path the code page cannot represent is refused, not best-fit mapped");
}

void ExeDirectoryResolvesTheRunningProcess() {
    std::wstring wide;
    std::string narrow;
    const bool resolved = acr_ht::ExeDirectory(wide, narrow);
    Check(g_failures, resolved && !wide.empty() && !narrow.empty(),
          "ExeDirectory resolves the running process's directory");
    Check(g_failures, resolved && wide.find(L'\\') != std::wstring::npos &&
                          wide.back() != L'\\',
          "ExeDirectory yields a directory with no trailing separator");
}

}  // namespace

int RunExePathTests() {
    std::cout << "\nExecutable paths\n";
    g_failures = 0;
    DirectoryOfKeepsTheDirectoryPart();
    DirectoryOfRefusesAPathWithNoDirectory();
    NarrowToAnsiConvertsAndFailsClosed();
    ExeDirectoryResolvesTheRunningProcess();
    return g_failures;
}
