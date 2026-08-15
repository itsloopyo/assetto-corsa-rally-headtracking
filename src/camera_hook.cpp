#include "camera_hook.h"

#include <windows.h>

#include <atomic>

#include "camera_transform.h"
#include "clean_pose_cache.h"
#include "headtracking_mod.h"
#include "logging.h"
#include "ue/ue_camera_layout.h"
#include "ue/ue_game_state.h"
#include "ue/ue_probe.h"
#include "ue/ue_update_camera.h"

#include <MinHook.h>

#include <cameraunlock/unreal/ue_runtime.h>

namespace acr_ht {
namespace {

namespace cu = cameraunlock::unreal;

using UpdateCameraFn = void(__fastcall*)(void* self, float deltaTime);

UpdateCameraFn g_original = nullptr;
void*          g_target = nullptr;
bool           g_installed = false;

CleanPoseCache g_clean;

// Everything the detour reads about where the camera lives. Resolved from
// reflection at install time and committed only once every one of them
// resolved, so the detour never runs against a half-built layout.
ue::CameraCacheOffsets g_offsets{};
ue::ViewTargetOffsets  g_viewTarget{};
ue::PauseOffsets       g_pause{};

// Pulled in only while tracking is driving the view, so a held or menu camera
// keeps whatever the game chose. Zero means "leave it alone" - either the user
// asked for that, or reflection did not find the field.
float g_nearClipCm = 0.0f;

void AbandonPartialInstall() {
    g_offsets = {};
    g_viewTarget = {};
    g_pause = {};
    g_nearClipCm = 0.0f;
    g_target = nullptr;
    g_original = nullptr;
}

// FRotator and FVector are both three doubles, but they are different types
// with differently named fields, so they are moved through an FVector rather
// than aliased: Pitch, Yaw, Roll in declaration order.
bool ReadPov(void* manager, CameraPose& out) {
    const auto base = reinterpret_cast<std::uintptr_t>(manager);
    cu::FVector rotation{};
    if (!cu::SafeReadFVector(base + g_offsets.location, out.location)) return false;
    if (!cu::SafeReadFVector(base + g_offsets.rotation, rotation)) return false;
    out.rotation.Pitch = rotation.X;
    out.rotation.Yaw   = rotation.Y;
    out.rotation.Roll  = rotation.Z;
    return true;
}

bool WritePov(void* manager, const CameraPose& pose) {
    const auto base = reinterpret_cast<std::uintptr_t>(manager);
    const cu::FVector rotation{pose.rotation.Pitch, pose.rotation.Yaw, pose.rotation.Roll};
    return cu::SafeWriteFVector(base + g_offsets.location, pose.location) &&
           cu::SafeWriteFVector(base + g_offsets.rotation, rotation);
}

void ApplyNearClip(void* manager) {
    if (g_nearClipCm <= 0.0f || g_offsets.nearClip == 0) return;
    cu::SafeWriteFloat(reinterpret_cast<std::uintptr_t>(manager) + g_offsets.nearClip,
                       g_nearClipCm);
}

// The first few calls only. Enough for a bug report to show that the detour
// runs, what the engine handed it, and what it composed - and bounded, because
// this is the render path and a log that grows every frame is a log nobody can
// read and a game that stutters.
constexpr long long kTracedCalls = 3;
std::atomic<long long> g_calls{0};

// Calls currently inside the detour. UninstallCameraHook waits for this to
// drain before it frees the trampoline, so a thread already past the entry
// point cannot be left calling into memory MinHook has reclaimed.
std::atomic<int> g_inFlight{0};

// Only ever touched from inside the detour's own guarded body, which the
// engine drives one call at a time per manager, so it needs no atomicity.
bool g_loggedEviction = false;

// Latched by the exception handler. A detour that has faulted once has no
// business touching the camera again, so it passes straight through for the
// rest of the session rather than faulting every frame.
std::atomic<bool> g_faulted{false};

void NoteFault(unsigned long code) {
    if (g_faulted.exchange(true)) return;
    Log::EmergencyLine("[camera] the camera detour faulted (code 0x%lX). Head tracking is now "
                       "inert for this session and the game keeps its own camera. Please "
                       "report this log.",
                       code);
}

void LogWriteFailure(const char* what, unsigned long long manager) {
    static std::atomic<bool> logged{false};
    if (logged.exchange(true)) return;
    Log::Line("[camera] writing the camera for manager 0x%llX failed while %s - the pose was "
              "only partly applied. Head tracking will keep running; report this log.",
              manager, what);
}

void LogPose(const char* label, const CameraPose& pose) {
    Log::Line("[camera]   %-10srot=(P%8.2f Y%8.2f R%8.2f) loc=(%.1f %.1f %.1f)",
              label, pose.rotation.Pitch, pose.rotation.Yaw, pose.rotation.Roll,
              pose.location.X, pose.location.Y, pose.location.Z);
}

// Everything the detour does BEFORE the engine's own UpdateCamera runs: hand
// back the camera the engine last computed, so the modifier stack and the
// last-frame cache it blends against never see a tracked pose.
//
// A pose is taken back out only when this mod put it there and the engine is
// still holding those exact bytes. Anything else - a cut, a cinematic, or a new
// camera manager living on a recycled address - means the value in the engine
// is not ours to take back.
void DetourPre(void* self, CleanPoseCache::Slot* slot, unsigned long long manager, bool trace,
               CameraPose& onEntry, bool& haveOnEntry) {
    haveOnEntry = trace && ReadPov(self, onEntry);
    if (!slot || !slot->modified) return;

    CameraPose live{};
    if (!ReadPov(self, live)) {
        slot->modified = false;
        return;
    }
    if (!CleanPoseCache::EngineStillHoldsOurPose(*slot, live)) {
        slot->modified = false;
        return;
    }
    if (!WritePov(self, slot->clean)) {
        LogWriteFailure("restoring the clean camera", manager);
    }
    slot->modified = false;
}

// Everything after. Reads the camera the engine just computed, decides whether
// the player is driving, and composes the head pose onto it.
void DetourPost(void* self, float deltaTime, CleanPoseCache::Slot* slot,
                unsigned long long manager, bool trace, long long callIndex,
                const CameraPose& onEntry, bool haveOnEntry) {
    // Only a null camera manager gets no slot, and the engine does not call a
    // member function on one. Guarded rather than assumed because the cost is a
    // branch and the alternative is a null dereference on the render path.
    if (!slot) return;

    // More camera managers have been seen than the table holds. Harmless in
    // itself - the stalest entry goes, and a manager that is still updating is
    // never the stalest - but if head tracking ever does go missing after a
    // string of stage loads, this is the line that says the table was churning.
    // Once, not per frame: this is the render path.
    if (g_clean.EvictedManagers() > 0 && !g_loggedEviction) {
        g_loggedEviction = true;
        Log::Line("[camera] more than %d camera managers seen; the clean-pose table is reusing "
                  "the stalest slot.", CleanPoseCache::kSlots);
    }

    CameraPose clean{};
    if (!ReadPov(self, clean)) {
        slot->modified = false;
        return;
    }
    slot->clean = clean;

    CameraPose tracked{};
    // Outside a stage the same camera manager shows the menus, the service park
    // and the garage. Holding there rather than following means the view sits
    // where the game put it, and resuming smooths out of the held pose instead
    // of snapping to wherever the head wandered during a menu.
    //
    // The pause menu is the one thing the view target cannot speak for - it
    // leaves the camera on the car - so the engine's pause flag is asked
    // separately, and only once the view target has said this is a stage.
    const bool driving = ue::IsDrivingViewTarget(
        reinterpret_cast<std::uintptr_t>(self), g_viewTarget);
    const bool applied =
        driving &&
        !ue::IsGamePaused(reinterpret_cast<std::uintptr_t>(self), g_pause, deltaTime) &&
        ComposeTrackedCamera(clean, deltaTime, tracked);

    bool wrote = false;
    if (applied) {
        wrote = WritePov(self, tracked);
        if (wrote) {
            slot->written = tracked;
            slot->modified = true;
            ApplyNearClip(self);
        } else {
            // WritePov writes location then rotation and stops at the first
            // failure, so this can leave a tracked location against a clean
            // rotation - worse than either whole pose, and ours to take back
            // out.
            //
            // What gets recorded is what is ACTUALLY in the engine now, read
            // back, not the pose that was intended. Next frame's guard compares
            // the live camera against this field exactly, so recording the
            // intended pose would fail that comparison and the half-written
            // camera would never be restored at all.
            slot->modified = ReadPov(self, slot->written);
            LogWriteFailure("applying the tracked camera", manager);
        }
    } else {
        slot->modified = false;
    }

    if (trace) {
        Log::Line("[camera] call %lld manager=0x%llX applied=%d", callIndex, manager,
                  wrote ? 1 : 0);
        if (haveOnEntry) LogPose("on entry", onEntry);
        LogPose("clean", clean);
        if (wrote) LogPose("tracked", tracked);
    }
}

// SEH wrappers. A scope carrying __try/__except cannot also hold objects with
// destructors, so each half is called from its own guarded shim.
void GuardedPre(void* self, CleanPoseCache::Slot* slot, unsigned long long manager, bool trace,
                CameraPose& onEntry, bool& haveOnEntry) {
    __try {
        DetourPre(self, slot, manager, trace, onEntry, haveOnEntry);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        NoteFault(GetExceptionCode());
    }
}

void GuardedPost(void* self, float deltaTime, CleanPoseCache::Slot* slot,
                 unsigned long long manager, bool trace, long long callIndex,
                 const CameraPose& onEntry, bool haveOnEntry) {
    __try {
        DetourPost(self, deltaTime, slot, manager, trace, callIndex, onEntry, haveOnEntry);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        NoteFault(GetExceptionCode());
    }
}

// UpdateCamera is called by the engine, which is built without C++ exception
// handling, so anything thrown out of this mod's work would unwind through
// MinHook's trampoline into a frame with no handler. The two guards above catch
// that, say so once, and latch the mod inert for the session.
//
// The engine's own UpdateCamera is deliberately called OUTSIDE both guards. An
// access violation raised inside the game's camera code is the game's crash,
// and swallowing it would let a half-updated engine limp on to fail somewhere
// unrelated - the mod turning a diagnosable crash into an undiagnosable one.
void __fastcall UpdateCameraDetour(void* self, float deltaTime) {
    g_inFlight.fetch_add(1);

    CleanPoseCache::Slot* slot = nullptr;
    const auto manager = static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(self));
    long long callIndex = 0;
    bool trace = false;
    CameraPose onEntry{};
    bool haveOnEntry = false;

    const bool active = !g_faulted.load();
    if (active) {
        slot = g_clean.Acquire(self);
        // Captured, not re-read below: the counter is shared, so a second
        // camera manager updating in the same frame would make a re-read name
        // the wrong call in the trace.
        callIndex = g_calls.fetch_add(1);
        trace = callIndex < kTracedCalls;
        GuardedPre(self, slot, manager, trace, onEntry, haveOnEntry);
    }

    g_original(self, deltaTime);

    if (active && !g_faulted.load()) {
        GuardedPost(self, deltaTime, slot, manager, trace, callIndex, onEntry, haveOnEntry);
    }

    g_inFlight.fetch_sub(1);
}

}  // namespace

bool InstallCameraHook(std::uintptr_t moduleBase, std::size_t moduleSize,
                       std::uintptr_t cameraManager, float nearClipCm) {
    std::uintptr_t vtable = 0;
    if (!cu::SafeReadPtr(cameraManager, vtable) || !vtable) {
        Log::Line("[camera] camera manager 0x%llX has no readable vtable - not hooking.",
                  static_cast<unsigned long long>(cameraManager));
        return false;
    }

    const std::size_t controllerField = ue::FindCameraManagerFieldOffset(cameraManager);
    if (controllerField == 0) {
        Log::Line("[camera] no live player controller owns camera manager 0x%llX, so the "
                  "UpdateCamera call site cannot be identified - not hooking.",
                  static_cast<unsigned long long>(cameraManager));
        return false;
    }

    // Resolved into locals so a step that fails leaves nothing behind for the
    // detour to run against.
    ue::CameraCacheOffsets offsets{};
    if (!ue::ResolveCameraCacheOffsets(cameraManager, offsets)) return false;

    ue::ViewTargetOffsets viewTarget{};
    if (!ue::ResolveViewTargetOffsets(cameraManager, viewTarget)) return false;

    ue::PauseOffsets pause{};
    if (!ue::ResolvePauseOffsets(cameraManager, pause)) return false;

    ue::UpdateCameraTarget target{};
    if (!ue::ResolveUpdateCamera(moduleBase, moduleSize, vtable, controllerField, target))
        return false;

    const MH_STATUS initStatus = MH_Initialize();
    if (initStatus != MH_OK && initStatus != MH_ERROR_ALREADY_INITIALIZED) {
        Log::Line("[camera] MinHook failed to initialise - not hooking.");
        return false;
    }

    // Committed before the hook exists: the detour can fire on a render thread
    // the instant MH_EnableHook returns, and everything it reads has to already
    // be there.
    g_offsets = offsets;
    g_viewTarget = viewTarget;
    g_pause = pause;
    g_nearClipCm = nearClipCm;

    // The discovery loop ahead of this can sit for minutes, so shutdown may
    // have started while it waited. Patching the game after Shutdown has run
    // means a detour that lives past the DLL being unmapped.
    if (ShuttingDown()) {
        Log::Line("[camera] shutdown started before the hook could be installed - not hooking.");
        AbandonPartialInstall();
        return false;
    }

    g_target = reinterpret_cast<void*>(target.function);
    if (MH_CreateHook(g_target, reinterpret_cast<void*>(&UpdateCameraDetour),
                      reinterpret_cast<void**>(&g_original)) != MH_OK) {
        Log::Line("[camera] MinHook could not detour UpdateCamera at +0x%llX - not hooking.",
                  static_cast<unsigned long long>(target.function - moduleBase));
        AbandonPartialInstall();
        return false;
    }
    if (MH_EnableHook(g_target) != MH_OK) {
        Log::Line("[camera] MinHook could not enable the UpdateCamera detour - not hooking.");
        MH_RemoveHook(g_target);
        AbandonPartialInstall();
        return false;
    }

    g_installed = true;
    Log::Line("[camera] hooked UpdateCamera (+0x%llX, vtable slot %d)",
              static_cast<unsigned long long>(target.function - moduleBase), target.slot);
    return true;
}

void UninstallCameraHook() {
    if (!g_installed) return;

    // Stops new calls entering the detour. MinHook suspends threads and
    // rewrites any instruction pointer sitting inside the trampoline, but a
    // thread already in the detour body is in OUR code, which it knows nothing
    // about - it will still reach the g_original call below.
    MH_DisableHook(g_target);

    // Then wait for anything already inside it to leave.
    constexpr int kDrainAttempts = 200;
    for (int attempt = 0; attempt < kDrainAttempts && g_inFlight.load() > 0; ++attempt) {
        Sleep(5);
    }
    if (g_inFlight.load() > 0) {
        Log::Line("[camera] a camera update was still in the detour after 1s.");
    }

    // MH_RemoveHook is deliberately NOT called, and g_original is deliberately
    // left valid. Removing the hook frees MinHook's trampoline, and the drain
    // above cannot prove nobody needs it: MH_DisableHook rewrites instruction
    // pointers sitting inside the trampoline, but a thread stopped at the first
    // instruction of the detour is in this mod's code, has not yet incremented
    // g_inFlight, and will still go on to call through g_original. Disabling
    // has already restored the game's own bytes, so no new call can enter;
    // keeping the trampoline costs a few hundred bytes on an unload that in
    // practice never happens, and removing it costs a crash.
    g_installed = false;
}

}  // namespace acr_ht
