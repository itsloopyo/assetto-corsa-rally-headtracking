#include "camera_transform.h"

namespace acr_ht {
namespace {

using cameraunlock::unreal::FQuat4d;
using cameraunlock::unreal::FVector;
using cameraunlock::unreal::QuatFromEulerDeg;
using cameraunlock::unreal::QuatRotateVec;

// UE works in centimetres; the pipeline hands out metres of head travel.
constexpr double kMetresToUnreal = 100.0;

double ClampPitch(double pitch, double limit) {
    if (pitch > limit) return limit;
    if (pitch < -limit) return -limit;
    return pitch;
}

}  // namespace

CameraPose ApplyHeadPose(const CameraPose& clean, const HeadPose& pose) {
    const FQuat4d baseQ = QuatFromEulerDeg(clean.rotation.Pitch, clean.rotation.Yaw,
                                           clean.rotation.Roll);

    // Plain rotator addition: FRotator yaw is about world Z by definition, so
    // adding it there - rather than composing in the camera frame - is exactly
    // what keeps the turn axis level with the horizon while the car is banked.
    // Roll is subtracted because the tracker's positive roll is a head tilt the
    // opposite way round from FRotator::Roll.
    //
    // Pitch is clamped short of vertical because this does not go through a
    // quaternion, so nothing normalises the result. An FRotator pitched past 90
    // is the same orientation as one yawed and rolled by 180, and the horizon
    // inverts. The game's own camera already pitches 30-40 degrees over a
    // crest, so a sensitivity above 1 can reach it.
    //
    // The limit never pulls the camera back from where the engine already had
    // it: a clean pitch beyond the limit stays where it is, so a centred head
    // remains exactly a no-op whatever the game is doing.
    constexpr double kMaxPitchDegrees = 89.9;
    const double cleanPitch = clean.rotation.Pitch;
    const double limit = cleanPitch < 0.0
                             ? (-cleanPitch > kMaxPitchDegrees ? -cleanPitch : kMaxPitchDegrees)
                             : (cleanPitch > kMaxPitchDegrees ? cleanPitch : kMaxPitchDegrees);

    CameraPose out = clean;
    out.rotation.Pitch = ClampPitch(cleanPitch + pose.pitch, limit);
    out.rotation.Yaw   = clean.rotation.Yaw   + pose.yaw;
    out.rotation.Roll  = clean.rotation.Roll  - pose.roll;

    // Lean travels along the CLEAN camera basis, not the head-rotated one, so
    // the offset follows where the driver's body is pointing rather than
    // swinging around with wherever they happen to be looking.
    const FVector forward = QuatRotateVec(baseQ, FVector{1.0, 0.0, 0.0});
    const FVector right   = QuatRotateVec(baseQ, FVector{0.0, 1.0, 0.0});
    const FVector up      = QuatRotateVec(baseQ, FVector{0.0, 0.0, 1.0});

    // Two axes run opposite between the pipeline and UE, and both are flipped
    // here, at the one point the two conventions meet. Doing it here rather
    // than by shipping InvertX/InvertZ pre-set is what keeps those settings
    // meaning "my tracker's axis runs backwards" - a user who does need them
    // gets a working knob instead of one that starts half-used. For z it also
    // matters functionally: inversion is applied before the clamp, and that
    // clamp is asymmetric, so pre-setting InvertZ would move the forward
    // travel allowance onto the lean-back and leave leaning in with the
    // backward one - which this mod ships as zero.
    const double surge = -static_cast<double>(pose.lean_z) * kMetresToUnreal;
    const double sway  = -static_cast<double>(pose.lean_x) * kMetresToUnreal;
    const double heave =  static_cast<double>(pose.lean_y) * kMetresToUnreal;

    out.location.X = clean.location.X + forward.X * surge + right.X * sway + up.X * heave;
    out.location.Y = clean.location.Y + forward.Y * surge + right.Y * sway + up.Y * heave;
    out.location.Z = clean.location.Z + forward.Z * surge + right.Z * sway + up.Z * heave;
    return out;
}

}  // namespace acr_ht
