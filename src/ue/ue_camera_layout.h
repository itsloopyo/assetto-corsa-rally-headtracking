#pragma once

#include <cstddef>
#include <cstdint>

namespace acr_ht::ue {

// Where the camera's view info lives inside a live APlayerCameraManager.
// Every offset here is resolved from UE's own property reflection at runtime -
// nothing is pinned, because a shape-scan guess at these already cost a
// corrupted AActor and a crash. See Resolve().
struct CameraCacheOffsets {
    std::size_t location = 0;  // manager -> FMinimalViewInfo::Location, 3 doubles
    std::size_t rotation = 0;  // manager -> FMinimalViewInfo::Rotation, 3 doubles
    // manager -> FMinimalViewInfo::PerspectiveNearClipPlane, float, world units
    // (centimetres). Zero if reflection did not have it, which costs the near
    // clip adjustment and nothing else.
    std::size_t nearClip = 0;
};

// UE 5 ships Large World Coordinates on, so Location and Rotation are three
// doubles each. Reflection reports the real size, so a build that shipped the
// float variants is refused rather than written to as if it were doubles -
// which is the LWC trap that silently overflows the struct and decodes the
// wrong halves of the wrong fields.
constexpr std::int32_t kLwcVectorBytes = 24;

// Resolves the offsets by walking APlayerCameraManager::CameraCachePrivate ->
// FCameraCacheEntry::POV -> FMinimalViewInfo::{Location, Rotation}. Returns
// false, having logged why, if any link is missing or is not the shape this
// mod knows how to write.
bool ResolveCameraCacheOffsets(std::uintptr_t cameraManager, CameraCacheOffsets& out);

}  // namespace acr_ht::ue
