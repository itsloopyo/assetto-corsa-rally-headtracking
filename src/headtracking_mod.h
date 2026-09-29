#pragma once

#include "camera_transform.h"

namespace acr_ht {

void Initialize();
void Shutdown();

// True once Shutdown has begun. The camera hook checks it immediately before
// patching, because the discovery it waits on can outlast the mod itself.
bool ShuttingDown();

// Applies a tracking mode the cycle hotkey has requested. Called from the camera
// detour on every camera update, driving or not, since that is the thread that
// owns the session's pipeline state.
void ApplyRequestedTrackingMode();

// Advances the tracking pipeline by `deltaTime` and composes the result onto
// `clean`. Returns false - leaving `out` untouched - whenever the camera should
// stay exactly as the engine computed it: no tracker data, tracking toggled
// off, or the mod not fully up. Called from the camera detour, once per camera
// manager per frame.
bool ComposeTrackedCamera(const CameraPose& clean, float deltaTime, CameraPose& out);

}  // namespace acr_ht
