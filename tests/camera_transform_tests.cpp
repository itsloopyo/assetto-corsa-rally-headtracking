// Pins the camera composition - the one piece of this mod whose output is
// written straight into the engine's view info. Every check here is a
// convention that a plausible-looking simplification could flip: the sign of
// each lean axis, which basis the lean travels along, the roll negation, and
// the fact that the head rotation is rotator addition rather than a quaternion
// compose in the camera frame. A flipped sign renders as "the camera moves the
// wrong way", which is only ever caught in a car on a stage.

#include "camera_transform.h"
#include "test_support.h"

#include <cameraunlock/unreal/ue_math.h>

namespace {

using acr_ht::ApplyHeadPose;
using acr_ht::CameraPose;
using acr_ht::HeadPose;
using acr_ht_tests::Check;
using acr_ht_tests::Near;

int g_failures = 0;

CameraPose MakeClean(double pitch, double yaw, double roll,
                     double x = 0.0, double y = 0.0, double z = 0.0) {
    CameraPose pose{};
    pose.location = {x, y, z};
    pose.rotation = {pitch, yaw, roll};
    return pose;
}

void IdentityPoseLeavesTheCameraAlone() {
    const CameraPose clean = MakeClean(12.0, -34.0, 5.0, 100.0, 200.0, 300.0);
    const HeadPose none{};

    const CameraPose out = ApplyHeadPose(clean, none);
    Check(g_failures,
          Near(out.location.X, clean.location.X, 1e-9) &&
          Near(out.location.Y, clean.location.Y, 1e-9) &&
          Near(out.location.Z, clean.location.Z, 1e-9) &&
          Near(out.rotation.Pitch, clean.rotation.Pitch, 1e-9) &&
          Near(out.rotation.Yaw, clean.rotation.Yaw, 1e-9) &&
          Near(out.rotation.Roll, clean.rotation.Roll, 1e-9),
          "zero pose is a no-op");
}

void WorldSpaceYawIsRotatorAddition() {
    const CameraPose clean = MakeClean(10.0, 20.0, 5.0);
    HeadPose pose{};
    pose.yaw = 15.0f;
    pose.pitch = -7.0f;
    pose.roll = 3.0f;

    const CameraPose out = ApplyHeadPose(clean, pose);
    Check(g_failures, Near(out.rotation.Pitch, 3.0, 1e-6), "world-space pitch adds");
    Check(g_failures, Near(out.rotation.Yaw, 35.0, 1e-6), "world-space yaw adds");
    // Roll is subtracted: the tracker's positive roll tilts the head the
    // opposite way round from FRotator::Roll.
    Check(g_failures, Near(out.rotation.Roll, 2.0, 1e-6), "world-space roll subtracts");
}

// The head rotation adds rotator components directly, so nothing normalises the
// result. An FRotator pitched past 90 is the same orientation as one yawed and
// rolled by 180, which inverts the horizon - and the game's own camera already
// pitches 30-40 degrees over a crest, so a sensitivity above 1 can reach it.
void WorldSpacePitchStopsShortOfVertical() {
    const CameraPose clean = MakeClean(40.0, 0.0, 0.0);
    HeadPose pose{};
    pose.pitch = 80.0f;

    const CameraPose out = ApplyHeadPose(clean, pose);
    Check(g_failures, out.rotation.Pitch < 90.0,
          "a combined pitch past vertical is clamped rather than flipping the horizon");
    Check(g_failures, Near(out.rotation.Pitch, 89.9, 1e-6),
          "and it is clamped to just short of vertical, not zeroed");

    const CameraPose down = MakeClean(-40.0, 0.0, 0.0);
    HeadPose downPose{};
    downPose.pitch = -80.0f;
    const CameraPose outDown = ApplyHeadPose(down, downPose);
    Check(g_failures, Near(outDown.rotation.Pitch, -89.9, 1e-6),
          "the clamp is symmetric about level");

    // Ordinary poses must be untouched by the clamp.
    HeadPose small{};
    small.pitch = 10.0f;
    const CameraPose normal = ApplyHeadPose(MakeClean(5.0, 0.0, 0.0), small);
    Check(g_failures, Near(normal.rotation.Pitch, 15.0, 1e-6),
          "a pitch nowhere near vertical is left exactly as it composed");

    // A clamp that ignored where the engine already was would MOVE an
    // untracked camera the game had pitched past the limit, which would make a
    // centred head stop being a no-op.
    const HeadPose none{};
    const CameraPose steep = ApplyHeadPose(MakeClean(95.0, 0.0, 0.0), none);
    Check(g_failures, Near(steep.rotation.Pitch, 95.0, 1e-6),
          "a camera the game pitched past the limit is left where the game put it");
}

// The reason head yaw is added to the rotator rather than composed in the
// camera's own frame. Composing in the camera frame is the physically truthful
// model of a head strapped into a seat, but it tilts the horizon every time the
// driver looks into an apex: 30 degrees of head yaw against a 45 degree bank
// comes back with over 20 degrees of pitch nobody asked for.
void YawTurnsAboutTheWorldsUpAxisWhenTheCarIsBanked() {
    HeadPose yawOnly{};
    yawOnly.yaw = 30.0f;

    const CameraPose banked = MakeClean(0.0, 0.0, 45.0);
    const CameraPose out = ApplyHeadPose(banked, yawOnly);
    Check(g_failures, Near(out.rotation.Yaw, 30.0, 1e-6),
          "head yaw turns about the world's up axis when the car is banked");
    Check(g_failures,
          Near(out.rotation.Pitch, 0.0, 1e-9) && Near(out.rotation.Roll, 45.0, 1e-9),
          "and leaves the bank the game rendered exactly where it was");
}

void LeanAxesKeepTheirSignsAndScale() {
    const CameraPose clean = MakeClean(0.0, 0.0, 0.0);

    // The pipeline's NEGATIVE z is the forward lean, and UE's camera-local
    // forward is +X, so a forward lean has to come out as +X in centimetres.
    HeadPose forward{};
    forward.lean_z = -0.5f;
    const CameraPose leanedIn = ApplyHeadPose(clean, forward);
    Check(g_failures, Near(leanedIn.location.X, 50.0, 1e-6),
          "negative pipeline z leans forward, in centimetres");

    HeadPose sway{};
    sway.lean_x = 0.1f;
    const CameraPose swayed = ApplyHeadPose(clean, sway);
    Check(g_failures, Near(swayed.location.Y, -10.0, 1e-6), "positive sway moves along -Y");

    HeadPose heave{};
    heave.lean_y = 0.1f;
    const CameraPose heaved = ApplyHeadPose(clean, heave);
    Check(g_failures, Near(heaved.location.Z, 10.0, 1e-6), "positive heave moves along +Z");
}

void LeanTravelsAlongTheCleanBasis() {
    // Car pointing along +Y: the clean camera basis is rotated a quarter turn,
    // so a forward lean has to follow the car, not the world.
    const CameraPose clean = MakeClean(0.0, 90.0, 0.0);
    HeadPose pose{};
    pose.lean_z = -1.0f;

    const CameraPose out = ApplyHeadPose(clean, pose);
    Check(g_failures,
          Near(out.location.X, 0.0, 1e-6) && Near(out.location.Y, 100.0, 1e-6) &&
          Near(out.location.Z, 0.0, 1e-6),
          "lean follows the clean camera basis");

    // Same lean, but the head is also turned. The offset must not swing round
    // with where the driver is looking - it follows where the body points.
    HeadPose turned = pose;
    turned.yaw = 60.0f;
    const CameraPose lookedAway = ApplyHeadPose(clean, turned);
    Check(g_failures,
          Near(lookedAway.location.X, out.location.X, 1e-9) &&
          Near(lookedAway.location.Y, out.location.Y, 1e-9) &&
          Near(lookedAway.location.Z, out.location.Z, 1e-9),
          "head rotation does not swing the lean offset");
}

void LeanIsAddedToTheCleanLocation() {
    const CameraPose clean = MakeClean(0.0, 0.0, 0.0, 1000.0, -2000.0, 300.0);
    HeadPose pose{};
    pose.lean_z = -0.2f;

    const CameraPose out = ApplyHeadPose(clean, pose);
    Check(g_failures,
          Near(out.location.X, 1020.0, 1e-6) && Near(out.location.Y, -2000.0, 1e-6) &&
          Near(out.location.Z, 300.0, 1e-6),
          "lean is an offset from where the game put the camera");
}

}  // namespace

int RunCameraTransformTests() {
    std::cout << "\nCamera transform\n";
    g_failures = 0;
    IdentityPoseLeavesTheCameraAlone();
    WorldSpaceYawIsRotatorAddition();
    WorldSpacePitchStopsShortOfVertical();
    YawTurnsAboutTheWorldsUpAxisWhenTheCarIsBanked();
    LeanAxesKeepTheirSignsAndScale();
    LeanTravelsAlongTheCleanBasis();
    LeanIsAddedToTheCleanLocation();
    return g_failures;
}
