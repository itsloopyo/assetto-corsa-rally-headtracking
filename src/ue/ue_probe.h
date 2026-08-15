#pragma once

#include <cstddef>
#include <cstdint>

namespace acr_ht::ue {

// Locating the one camera manager the player looks through, and reporting what
// was found. Everything here walks the live UObject table, so none of it is
// cheap enough for the render path - it runs once, during bootstrap.

// The live camera manager for the local player, or 0 if the engine has not
// built a world yet. Class name is whatever the game subclassed
// APlayerCameraManager as, so this matches on the suffix rather than on an
// exact name.
std::uintptr_t FindPlayerCameraManager();

// Finds which live player controller owns `manager` and at what byte offset it
// holds the pointer. That offset is APlayerController::PlayerCameraManager, and
// it is what identifies the engine's `if (PlayerCameraManager) { ->UpdateCamera
// (dt); }` call site among the ~1000 call sites in the EXE that share its
// instruction shape - which in turn gives the vtable slot to hook.
//
// Returns 0 if no live player controller holds it. That means this camera
// manager is not the one driving a player's view, which is a reason not to
// hook rather than something to work around.
std::size_t FindCameraManagerFieldOffset(std::uintptr_t manager);

// The same search, to the log, including any second owner - which
// FindCameraManagerFieldOffset would silently pick between.
void LogCameraManagerOwner(std::uintptr_t manager);

// Every reflected property on the camera manager's class and its bases. This
// is what turns the camera cache from an offset that looked right into one the
// engine itself reports.
void LogCameraManagerProperties(std::uintptr_t manager);

}  // namespace acr_ht::ue
