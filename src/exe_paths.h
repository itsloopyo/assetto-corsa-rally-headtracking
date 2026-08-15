#pragma once

#include <string>

namespace acr_ht {

// Everything the mod reads or writes on disk hangs off the directory the game
// EXE sits in, so resolving it is its own concern rather than a step inside the
// bootstrap. Split here so the two sharp edges below can be tested without a
// game: neither may quietly degrade into a relative or root path.

// The directory part of `module_path`. Fails rather than yielding "" on a path
// with no separator: `substr(0, npos)` on a separator-less string turns the INI
// path into "\HeadTracking.ini", which writes the config to the root of the
// current drive. `directory` is left untouched on failure.
bool DirectoryOf(const std::wstring& module_path, std::wstring& directory);

// UTF-16 -> ANSI. The INI layer is ANSI-only (IniReader wraps
// GetPrivateProfile*A) while the log takes UTF-16 straight from Windows, so the
// mod needs the directory in both. Fails rather than best-fit mapping a
// character the code page cannot represent: a substituted character names a
// different directory, which is where the config would then be written.
// `narrow` is left untouched on failure.
bool NarrowToAnsi(const std::wstring& wide, std::string& narrow);

// The directory the running EXE sits in, in both encodings the mod needs.
// Neither output is meaningful unless this returns true, and `narrow` is empty
// when the directory has no ANSI form - the caller must skip its INI, not fall
// back to a relative path.
bool ExeDirectory(std::wstring& wide, std::string& narrow);

}  // namespace acr_ht
