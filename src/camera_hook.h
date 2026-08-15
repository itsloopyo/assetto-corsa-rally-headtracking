#pragma once

#include <cstddef>
#include <cstdint>

namespace acr_ht {

// Detours APlayerCameraManager::UpdateCamera so the mod can compose the head
// pose onto the camera the engine just computed. Everything per-build is
// resolved before anything is written: the manager, the controller field it
// hangs off, and the vtable slot. Returns false having patched nothing if any
// of that could not be established.
// `nearClipCm` is written into the view while tracking is driving it; 0 leaves
// the game's own near plane alone.
bool InstallCameraHook(std::uintptr_t moduleBase, std::size_t moduleSize,
                       std::uintptr_t cameraManager, float nearClipCm);
void UninstallCameraHook();

}  // namespace acr_ht
