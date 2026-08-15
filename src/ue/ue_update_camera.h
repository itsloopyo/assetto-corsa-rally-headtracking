#pragma once

#include <cstddef>
#include <cstdint>

namespace acr_ht::ue {

// Resolves APlayerCameraManager::UpdateCamera in the running EXE, without
// pinning its address.
//
// The route in is the engine's own APlayerController::UpdateCameraManager,
// whose entire body is a null check and a tail call through the camera
// manager's vtable:
//
//     mov  rcx, [rcx + PlayerCameraManager]
//     test rcx, rcx
//     je   ret
//     mov  rax, [rcx]
//     jmp  qword ptr [rax + slot*8]
//     ret
//
// so the slot number falls out of the displacement. `controllerFieldOffset` is
// what makes the scan specific - it is derived at runtime by finding which
// live player controller points at the live camera manager, so this cannot
// latch onto the same shape in an unrelated class.
//
// Three other forwarders on APlayerController share the shape exactly, so a
// match is only accepted once the function it points at also looks like
// UpdateCamera: a float argument preserved across a call (`movaps xmm6, xmm1`)
// and an indirect call to DoUpdateCamera. Ambiguity is reported as failure
// rather than resolved by picking the first hit.
struct UpdateCameraTarget {
    std::uintptr_t function = 0;  // absolute address of UpdateCamera
    int            slot     = 0;  // its index in the camera manager's vtable
};

bool ResolveUpdateCamera(std::uintptr_t moduleBase, std::size_t moduleSize,
                         std::uintptr_t cameraManagerVtable,
                         std::size_t controllerFieldOffset,
                         UpdateCameraTarget& out);

}  // namespace acr_ht::ue
