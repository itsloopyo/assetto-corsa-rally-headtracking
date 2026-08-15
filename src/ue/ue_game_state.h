#pragma once

#include <cstddef>
#include <cstdint>

namespace acr_ht::ue {

// What the camera manager is currently pointed at, which is how this mod
// decides whether the player is driving.
//
// A rally game shows the same camera manager a lot of things that are not
// gameplay: the menu backdrops, the service park, the car in the garage, a
// replay. The one thing that separates driving from all of them is the view
// target - the actor the camera is following. During a stage it is the car;
// everywhere else it is a menu or orbit camera actor.
struct ViewTargetOffsets {
    std::size_t target = 0;  // manager -> ViewTarget.Target (AActor*)
};

// Resolves the offset from UE's property reflection:
// APlayerCameraManager::ViewTarget -> FTViewTarget::Target. Returns false,
// having logged why, if reflection does not have it.
bool ResolveViewTargetOffsets(std::uintptr_t cameraManager, ViewTargetOffsets& out);

// The actor the camera manager is currently following, or 0 if there is none
// or it cannot be read. The detour uses it to tell a recycled camera manager
// from the one whose clean pose it is holding.
std::uintptr_t ReadViewTarget(std::uintptr_t cameraManager, const ViewTargetOffsets& offsets);

// Whether the camera is currently following something the player drives.
//
// Called once per camera update, so it caches on the view target pointer: the
// class name only has to be resolved when the camera changes what it is
// looking at, which happens on a load or a mode change rather than per frame.
bool IsDrivingViewTarget(std::uintptr_t cameraManager, const ViewTargetOffsets& offsets);

// Where the engine keeps the pause flag, reached from the camera manager.
//
// The pause menu leaves the camera pointed at the car, so the view target gate
// cannot see it at all. UE's own flag is AWorldSettings::PauserPlayerState -
// UWorld::IsPaused() is essentially a null check on it, and Blueprint's Set
// Game Paused, UGameplayStatics::SetGamePaused and APlayerController::SetPause
// all end up writing it.
//
// The camera manager is an actor, so its Outer is the level it lives in, and
// the rest of the chain is reflected properties on the objects that chain
// actually reaches:
//   manager -> Outer (ULevel) -> OwningWorld -> PersistentLevel
//           -> WorldSettings -> PauserPlayerState
struct PauseOffsets {
    std::size_t level_owningWorld     = 0;  // ULevel -> UWorld
    std::size_t world_persistentLevel = 0;  // UWorld -> ULevel
    std::size_t level_worldSettings   = 0;  // ULevel -> AWorldSettings
    std::size_t settings_pauser       = 0;  // AWorldSettings -> APlayerState*
    // Diagnostic only. A game that freezes by dilating time rather than by
    // pausing the world leaves the pauser null, and this is what says so in
    // the log instead of leaving us guessing after a test run.
    std::size_t settings_timeDilation = 0;
};

// Resolves that chain against the classes of the objects it walks through, and
// logs what it found. Returns false, having logged why, if any link is missing
// - a mod that cannot tell a paused game from a running one should not hook.
bool ResolvePauseOffsets(std::uintptr_t cameraManager, PauseOffsets& out);

// Whether the game is paused right now. Four pointer reads, no caching: the
// offsets are class layout and the instances are re-walked every call, so a
// stage load needs no invalidation.
//
// `deltaTime` is not part of the verdict. It rides along so the diagnostic
// line can show what the engine is handing the camera, which is what tells an
// engine pause apart from a freeze by time dilation.
bool IsGamePaused(std::uintptr_t cameraManager, const PauseOffsets& offsets, float deltaTime);

}  // namespace acr_ht::ue
