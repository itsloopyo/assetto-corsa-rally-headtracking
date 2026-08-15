#pragma once

#include <cameraunlock/unreal/ue_math.h>

namespace acr_ht {

// A processed head pose in the core pipeline's own convention: degrees of
// rotation, and metres of lean where x is sway (right), y is heave (up) and
// z is surge - with NEGATIVE z the forward lean, which is what the position
// processor's asymmetric clamp is built around. Everything the engine's
// conventions differ on is handled inside ApplyHeadPose.
struct HeadPose {
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    float lean_x = 0.0f;
    float lean_y = 0.0f;
    float lean_z = 0.0f;
};

// The camera as the engine computed it, and as this returns it: UE's
// FMinimalViewInfo pair, degrees and centimetres.
struct CameraPose {
    cameraunlock::unreal::FVector  location{};
    cameraunlock::unreal::FRotator rotation{};
};

// Composes `pose` onto the engine's freshly computed camera, returning the
// camera the player should see. Head yaw turns about the world's up axis, so
// the axis the head turns about stays level with the horizon through a banked
// corner or over a crest - which is the whole point in a car, where the
// alternative tilts the horizon every time the driver looks into an apex.
// Pure: no engine state, no clock, no logging - the hook decides whether to
// call this; this decides only what the camera becomes.
CameraPose ApplyHeadPose(const CameraPose& clean, const HeadPose& pose);

}  // namespace acr_ht
