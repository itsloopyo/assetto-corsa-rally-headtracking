#include "ue/ue_game_state.h"

#include "logging.h"
#include "ue/ue_layout.h"
#include "ue/ue_properties.h"

#include <cameraunlock/unreal/ue_runtime.h>

namespace acr_ht::ue {
namespace {

namespace cu = cameraunlock::unreal;

// Every drivable car derives from this, the native pawn class the physics
// module declares. Matching on it rather than on the view target's own class
// is what makes the gate hold for every car and every future one: the object
// the camera follows during a stage is a per-vehicle blueprint
// (BC_LanciaDeltaHFIntegraleEvo_C ...), whose name changes with the car, but
// whose chain is always
//   <car blueprint> -> BC_CarAvatar_C -> AcrCarAvatar -> CarAvatar -> Pawn.
//
// Everything the camera looks at outside a stage - BC_MainPlayerController_C
// in the menus, BC_OrbitCamera_C in the car showcase, AcrCineCameraActor in
// the transitions, BC_OrbitCameraServicePark_C in the service park - derives
// from none of it.
constexpr const char* kCarPawnBaseClass = "CarAvatar";

// The verdict is a pure function of the view target's CLASS, so the class is
// what it is cached against.
//
// Caching against the instance pointer went stale the moment UE recycled a
// freed actor's memory: a menu actor landing on a retired car's address
// inherited "following" and drove the camera through the menus, and a car
// landing on a retired orbit camera's address inherited "held" and left
// tracking dead for a whole stage with the log insisting it was holding.
//
// Several classes are remembered rather than one, because more than one camera
// manager can update per frame - a replay or photo camera alongside the driving
// one - each following a different class. A single-entry cache thrashes between
// them, and each miss costs a DerivesFrom walk and a log line, on the render
// path, every frame.
constexpr int kVerdictCacheSize = 4;

struct VerdictEntry {
    std::uintptr_t uclass = 0;
    bool           verdict = false;
};

VerdictEntry g_verdicts[kVerdictCacheSize];
int          g_verdictNext = 0;

const VerdictEntry* FindVerdict(std::uintptr_t uclass) {
    for (const VerdictEntry& entry : g_verdicts) {
        if (entry.uclass == uclass) return &entry;
    }
    return nullptr;
}

void RememberVerdict(std::uintptr_t uclass, bool verdict) {
    g_verdicts[g_verdictNext] = VerdictEntry{uclass, verdict};
    g_verdictNext = (g_verdictNext + 1) % kVerdictCacheSize;
}

bool g_haveTarget = false;

}  // namespace

bool ResolveViewTargetOffsets(std::uintptr_t cameraManager, ViewTargetOffsets& out) {
    std::uintptr_t uclass = 0;
    if (!cu::SafeReadPtr(cameraManager + kUObject_ClassPrivate, uclass) || !uclass) {
        Log::Line("[state] camera manager 0x%llX has no readable class.",
                  static_cast<unsigned long long>(cameraManager));
        return false;
    }

    PropertyInfo viewTarget{};
    if (!FindProperty(uclass, "ViewTarget", viewTarget)) {
        Log::Line("[state] APlayerCameraManager has no reflected ViewTarget - gameplay cannot "
                  "be told apart from the menus.");
        return false;
    }
    const std::uintptr_t viewTargetStruct = StructOfProperty(viewTarget);
    if (!viewTargetStruct) {
        Log::Line("[state] APlayerCameraManager::ViewTarget is not a struct property.");
        return false;
    }

    PropertyInfo target{};
    if (!FindProperty(viewTargetStruct, "Target", target)) {
        Log::Line("[state] FTViewTarget has no reflected Target.");
        return false;
    }

    out.target = viewTarget.offset + target.offset;
    Log::Line("[state] view target resolved: ViewTarget+0x%zX -> Target+0x%zX "
              "(manager+0x%zX)", viewTarget.offset, target.offset, out.target);
    return true;
}

std::uintptr_t ReadViewTarget(std::uintptr_t cameraManager, const ViewTargetOffsets& offsets) {
    std::uintptr_t target = 0;
    if (!cu::SafeReadPtr(cameraManager + offsets.target, target)) return 0;
    return target;
}

bool IsDrivingViewTarget(std::uintptr_t cameraManager, const ViewTargetOffsets& offsets) {
    const std::uintptr_t target = ReadViewTarget(cameraManager, offsets);

    // No view target at all is a load screen or a torn-down world, not
    // gameplay.
    if (!target) {
        if (g_haveTarget) {
            g_haveTarget = false;
            Log::Line("[state] no view target - head tracking held");
        }
        return false;
    }
    g_haveTarget = true;

    // The class is re-read every call rather than trusted from a cached
    // instance pointer. That one guarded read is what makes a recycled actor
    // address safe: the pointer can repeat, but a different actor at it reports
    // a different class, and the verdict is re-derived.
    std::uintptr_t targetClass = 0;
    if (!cu::SafeReadPtr(target + kUObject_ClassPrivate, targetClass) || !targetClass) {
        // Failing closed is right; doing it silently is not. This is the one
        // way tracking can stop mid-stage while the last line in the log still
        // says it is following.
        static bool logged = false;
        if (!logged) {
            logged = true;
            Log::Line("[state] view target 0x%llX has no readable class - head tracking held.",
                      static_cast<unsigned long long>(target));
        }
        return false;
    }

    if (const VerdictEntry* known = FindVerdict(targetClass)) return known->verdict;

    // First time this class has been seen, so this is also the only place the
    // class name is resolved and logged. Both allocate, and this is the render
    // path.
    const bool verdict = DerivesFrom(targetClass, kCarPawnBaseClass);
    RememberVerdict(targetClass, verdict);
    Log::Line("[state] view target: %s - head tracking %s",
              cu::ClassName(target).c_str(), verdict ? "following" : "held");
    return verdict;
}

namespace {

// Neither of these decides anything. They are the thresholds the log line
// reports against, so one test run can say whether a game that leaves the
// pauser null is freezing time instead.
constexpr float kFrozenDilation = 0.01f;
constexpr float kFrozenDelta    = 1.0e-4f;

// Follows one reflected object pointer from a live instance to the live
// instance it points at, recording the offset it went through. Reflecting the
// class of the object that is actually there, rather than looking a class up
// by name, is what keeps the chain honest: a link that is not what this
// expects stops the walk instead of yielding a confident wrong offset.
bool Step(std::uintptr_t object, const char* ownerLabel, const char* property,
          std::size_t& offset, std::uintptr_t& next) {
    std::uintptr_t uclass = 0;
    if (!cu::SafeReadPtr(object + kUObject_ClassPrivate, uclass) || !uclass) {
        Log::Line("[state] %s 0x%llX has no readable class - the pause flag cannot be located.",
                  ownerLabel, static_cast<unsigned long long>(object));
        return false;
    }

    PropertyInfo info{};
    if (!FindProperty(uclass, property, info)) {
        Log::Line("[state] %s has no reflected %s - a paused game cannot be told from a "
                  "running one.", ownerLabel, property);
        return false;
    }
    if (!cu::SafeReadPtr(object + info.offset, next) || !next) {
        Log::Line("[state] %s::%s is null, so the chain to the pause flag cannot be walked.",
                  ownerLabel, property);
        return false;
    }

    offset = info.offset;
    return true;
}

struct PauseSample {
    bool readable       = false;
    bool paused         = false;
    bool dilationFrozen = false;
    bool deltaFrozen    = false;
};

bool SameState(const PauseSample& a, const PauseSample& b) {
    return a.readable == b.readable && a.paused == b.paused &&
           a.dilationFrozen == b.dilationFrozen && a.deltaFrozen == b.deltaFrozen;
}

PauseSample g_lastSample{};
bool        g_haveSample = false;

// One line per change of state, never per frame - this runs on the render
// path. Every signal is on the line, not just the one that moved, so a pause
// that leaves the pauser clear still shows what did change.
void LogSample(const PauseSample& sample, float dilation, float deltaTime) {
    if (g_haveSample && SameState(sample, g_lastSample)) return;
    g_lastSample = sample;
    g_haveSample = true;

    if (!sample.readable) {
        Log::Line("[state] the pause flag could not be read - head tracking held");
        return;
    }
    Log::Line("[state] pause flag %s (dilation=%.3f dt=%.4f) - head tracking %s",
              sample.paused ? "set" : "clear", dilation, deltaTime,
              sample.paused ? "held" : "following");
}

}  // namespace

bool ResolvePauseOffsets(std::uintptr_t cameraManager, PauseOffsets& out) {
    std::uintptr_t level = 0;
    if (!cu::SafeReadPtr(cameraManager + kUObject_OuterPrivate, level) || !level) {
        Log::Line("[state] camera manager 0x%llX has no outer - the pause flag cannot be "
                  "located.", static_cast<unsigned long long>(cameraManager));
        return false;
    }

    // An actor is outered to the level it was spawned into, which is where the
    // walk below starts. Checking it rather than assuming it means a build that
    // outers actors somewhere else says so, instead of failing later with a
    // message naming a class it never had.
    std::uintptr_t levelClass = 0;
    if (!cu::SafeReadPtr(level + kUObject_ClassPrivate, levelClass) || !levelClass ||
        !DerivesFrom(levelClass, "Level")) {
        Log::Line("[state] the camera manager is outered to a %s, not a Level - the pause "
                  "flag cannot be located.", cu::ClassName(level).c_str());
        return false;
    }

    PauseOffsets resolved{};
    std::uintptr_t world = 0, persistent = 0, settings = 0;
    if (!Step(level, "ULevel", "OwningWorld", resolved.level_owningWorld, world)) return false;
    if (!Step(world, "UWorld", "PersistentLevel", resolved.world_persistentLevel, persistent))
        return false;
    // The persistent level's settings, not the outer level's: a streaming
    // sublevel carries its own AWorldSettings, and the engine only ever pauses
    // through the persistent one.
    if (!Step(persistent, "ULevel", "WorldSettings", resolved.level_worldSettings, settings))
        return false;

    std::uintptr_t settingsClass = 0;
    if (!cu::SafeReadPtr(settings + kUObject_ClassPrivate, settingsClass) || !settingsClass ||
        !DerivesFrom(settingsClass, "WorldSettings")) {
        Log::Line("[state] ULevel::WorldSettings points at a %s, which is not a WorldSettings "
                  "- refusing to read a pause flag out of it.", cu::ClassName(settings).c_str());
        return false;
    }

    PropertyInfo pauser{};
    if (!FindProperty(settingsClass, "PauserPlayerState", pauser)) {
        Log::Line("[state] AWorldSettings has no reflected PauserPlayerState - a paused game "
                  "cannot be told from a running one.");
        return false;
    }
    resolved.settings_pauser = pauser.offset;

    // Optional, and only ever printed. Losing it costs the log line one field.
    PropertyInfo dilation{};
    if (FindProperty(settingsClass, "TimeDilation", dilation) &&
        dilation.elementSize == static_cast<std::int32_t>(sizeof(float))) {
        resolved.settings_timeDilation = dilation.offset;
    } else {
        Log::Line("[state] AWorldSettings has no float TimeDilation - the log cannot show "
                  "whether a pause froze time.");
    }

    out = resolved;
    Log::Line("[state] pause flag resolved: ULevel::OwningWorld+0x%zX -> "
              "UWorld::PersistentLevel+0x%zX -> ULevel::WorldSettings+0x%zX (%s) -> "
              "PauserPlayerState+0x%zX",
              out.level_owningWorld, out.world_persistentLevel, out.level_worldSettings,
              cu::ClassName(settings).c_str(), out.settings_pauser);
    return true;
}

bool IsGamePaused(std::uintptr_t cameraManager, const PauseOffsets& offsets, float deltaTime) {
    std::uintptr_t level = 0, world = 0, persistent = 0, settings = 0, pauser = 0;
    if (!cu::SafeReadPtr(cameraManager + kUObject_OuterPrivate, level) || !level ||
        !cu::SafeReadPtr(level + offsets.level_owningWorld, world) || !world ||
        !cu::SafeReadPtr(world + offsets.world_persistentLevel, persistent) || !persistent ||
        !cu::SafeReadPtr(persistent + offsets.level_worldSettings, settings) || !settings ||
        !cu::SafeReadPtr(settings + offsets.settings_pauser, pauser)) {
        // Not being able to tell is no reason to keep moving the view. A held
        // camera is the harmless way to be wrong, and the log says it happened.
        LogSample(PauseSample{}, -1.0f, deltaTime);
        return true;
    }

    float dilation = -1.0f;
    if (offsets.settings_timeDilation != 0)
        cu::SafeReadFloat(settings + offsets.settings_timeDilation, dilation);

    PauseSample sample{};
    sample.readable       = true;
    sample.paused         = pauser != 0;
    sample.dilationFrozen = dilation >= 0.0f && dilation < kFrozenDilation;
    sample.deltaFrozen    = deltaTime < kFrozenDelta;
    LogSample(sample, dilation, deltaTime);
    return sample.paused;
}

}  // namespace acr_ht::ue
