#pragma once

#include <cstddef>
#include <cstdint>

namespace acr_ht::ue {

// Finds the two globals every piece of UE reflection hangs off - the FName
// pool and GUObjectArray's chunked object table - by scanning the game
// module's writable data for their struct shapes and confirming each candidate
// by decoding it.
//
// Deliberately not a per-build offset table. Pinned RVAs would have to be
// rederived by hand for every patch, with the mod dormant in between, and a
// rally game patches often. The shapes below are engine-version-bound rather
// than build-bound, so a patch that relinks the EXE moves nothing this has to
// know, and the scan re-derives both addresses in about a second and a half.
//
// Publishes the result through cameraunlock::unreal::SetRuntime on success, so
// the whole of that namespace's reflection is live afterwards. Safe to call
// before the engine has initialised: it simply finds nothing and returns false,
// which is what makes it pollable.
bool DiscoverGlobals(std::uintptr_t moduleBase, std::size_t moduleSize);

// Where DiscoverGlobals found them, as RVAs, for the log. Zero until it
// succeeds.
std::uintptr_t FNamePoolRva();
std::uintptr_t ObjObjectsRva();

}  // namespace acr_ht::ue
