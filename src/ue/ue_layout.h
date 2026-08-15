#pragma once

#include <cstddef>

namespace acr_ht::ue {

// UObject field offsets. These are set by the engine version, not by the
// packaging of a particular game build, which is the whole reason this mod can
// resolve everything else at runtime instead of pinning it: a patch relinks
// acr.exe and moves both engine globals, but it does not move NamePrivate
// within UObject.
//
// They live here rather than beside each use because more than one module
// needs them, and two copies of an engine offset are two chances for one of
// them to be edited alone.
constexpr std::size_t kUObject_ClassPrivate = 0x10;
constexpr std::size_t kUObject_NamePrivate  = 0x18;
constexpr std::size_t kUObject_OuterPrivate = 0x20;

}  // namespace acr_ht::ue
