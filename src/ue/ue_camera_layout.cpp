#include "ue/ue_camera_layout.h"

#include "logging.h"
#include "ue/ue_layout.h"
#include "ue/ue_properties.h"

#include <cameraunlock/unreal/ue_runtime.h>

namespace acr_ht::ue {
namespace {

namespace cu = cameraunlock::unreal;

// Follows one named struct-typed property into the struct it holds, so the
// chain below reads as the chain it is.
bool Descend(std::uintptr_t owner, const char* property, const char* ownerLabel,
             PropertyInfo& info, std::uintptr_t& inner) {
    if (!FindProperty(owner, property, info)) {
        Log::Line("[camera] %s has no reflected %s - the camera cache cannot be located.",
                  ownerLabel, property);
        return false;
    }
    inner = StructOfProperty(info);
    if (!inner) {
        Log::Line("[camera] %s::%s is not a struct property - the reflection does not match "
                  "the engine layout this mod knows.", ownerLabel, property);
        return false;
    }
    return true;
}

bool ResolveVectorField(std::uintptr_t viewInfo, const char* property, std::size_t base,
                        std::size_t& out) {
    PropertyInfo info{};
    if (!FindProperty(viewInfo, property, info)) {
        Log::Line("[camera] FMinimalViewInfo has no reflected %s.", property);
        return false;
    }
    if (info.elementSize != kLwcVectorBytes) {
        Log::Line("[camera] FMinimalViewInfo::%s is %d bytes, not the %d of a Large World "
                  "Coordinates vector. This build uses a different precision than this mod "
                  "writes - refusing rather than corrupting the struct.",
                  property, info.elementSize, kLwcVectorBytes);
        return false;
    }
    out = base + info.offset;
    return true;
}

}  // namespace

bool ResolveCameraCacheOffsets(std::uintptr_t cameraManager, CameraCacheOffsets& out) {
    std::uintptr_t uclass = 0;
    if (!cu::SafeReadPtr(cameraManager + kUObject_ClassPrivate, uclass) || !uclass) {
        Log::Line("[camera] camera manager 0x%llX has no readable class.",
                  static_cast<unsigned long long>(cameraManager));
        return false;
    }

    PropertyInfo cache{};
    std::uintptr_t cacheEntry = 0;
    if (!Descend(uclass, "CameraCachePrivate", "APlayerCameraManager", cache, cacheEntry))
        return false;

    PropertyInfo pov{};
    std::uintptr_t viewInfo = 0;
    if (!Descend(cacheEntry, "POV", "FCameraCacheEntry", pov, viewInfo)) return false;

    const std::size_t povOffset = cache.offset + pov.offset;
    if (!ResolveVectorField(viewInfo, "Location", povOffset, out.location)) return false;
    if (!ResolveVectorField(viewInfo, "Rotation", povOffset, out.rotation)) return false;

    // Optional. Losing it costs the near clip adjustment - the driver's own
    // seat back stays invisible when looking over a shoulder - and nothing
    // else, so it is reported rather than treated as a reason not to hook.
    PropertyInfo nearClip{};
    if (FindProperty(viewInfo, "PerspectiveNearClipPlane", nearClip) &&
        nearClip.elementSize == static_cast<std::int32_t>(sizeof(float))) {
        out.nearClip = povOffset + nearClip.offset;
    } else {
        Log::Line("[camera] FMinimalViewInfo has no float PerspectiveNearClipPlane - the near "
                  "clip plane will be left as the game sets it.");
    }

    Log::Line("[camera] camera cache resolved: CameraCachePrivate+0x%zX -> POV+0x%zX "
              "(manager+0x%zX), Location at +0x%zX, Rotation at +0x%zX, NearClip at +0x%zX",
              cache.offset, pov.offset, povOffset, out.location, out.rotation, out.nearClip);
    return true;
}

}  // namespace acr_ht::ue
