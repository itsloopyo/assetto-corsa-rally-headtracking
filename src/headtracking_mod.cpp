#include "headtracking_mod.h"

#include <windows.h>
#include <psapi.h>

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>

#include "camera_hook.h"
#include "config.h"
#include "exe_paths.h"
#include "logging.h"
#include "startup.h"
#include "ue/ue_globals.h"
#include "ue/ue_probe.h"

#include <cameraunlock/config/defaults_file.h>
#include <cameraunlock/input/hotkey_poller.h>
#include <cameraunlock/input/key_binding_registration.h>
#include <cameraunlock/math/smoothing_utils.h>

namespace acr_ht {
namespace {

// Without IsRemoteConnection() on the receiver the session silently falls back
// to LocalSmoothing forever, with nothing at the call site to show it.
static_assert(Session::kHasRemoteConnection,
              "UdpReceiver must expose IsRemoteConnection() to select Local/RemoteSmoothing");

Config g_config;
cameraunlock::UdpReceiver g_receiver;
Session g_session(g_receiver);
cameraunlock::input::HotkeyPoller g_hotkeys;

std::atomic<bool> g_active{false};
std::atomic<bool> g_trackingEnabled{false};
std::atomic<bool> g_shuttingDown{false};

// The bootstrap thread outlives Initialize by design - it waits on the engine.
// Shutdown has to know when it has finished, because a bootstrap still running
// after the DLL unmaps installs a hook into freed address space.
std::atomic<bool> g_bootstrapRunning{false};

// The engine builds its FName pool and object table during static init, which
// runs after a proxy DLL's DllMain - so the globals are simply not there yet
// when this mod starts, and neither is a world for the camera manager to live
// in. Generous: a cold first launch, with shader compilation and a stage load
// in front of it, is minutes rather than seconds, and giving up early costs the
// user the whole session.
constexpr int   kDiscoveryAttempts = 1200;
constexpr DWORD kDiscoveryIntervalMs = 500;

// The last connection locality the log reported. Only the camera thread touches
// these, and only through LogConnectionLocality below.
bool g_remoteConnection = false;
bool g_remoteConnectionKnown = false;

// The session re-reads the receiver's source-address check every update, so a
// player who switches from a local OpenTrack instance to a phone on WiFi
// mid-session gets the other smoothing parameter without restarting the game.
// This only records the switch, so a bug report can say which of the two values
// was actually in effect.
void LogConnectionLocality() {
    const bool isRemote = g_session.IsRemoteConnection();
    if (g_remoteConnectionKnown && isRemote == g_remoteConnection) return;

    g_remoteConnection = isRemote;
    g_remoteConnectionKnown = true;

    Log::Line("[udp] tracker source is %s - smoothing=%.2f",
              isRemote ? "a remote device" : "on this machine",
              cameraunlock::math::GetEffectiveSmoothing(
                  g_config.local_smoothing, g_config.remote_smoothing, isRemote));
}

void ToggleTracking() {
    const bool on = !g_trackingEnabled.load();
    g_trackingEnabled.store(on);
    Log::Line("[input] tracking %s", on ? "enabled" : "disabled");
}

// The mode the cycle hotkey last chose. SetMode resets the position
// interpolator and processor, which the camera thread is reading mid-Update, so
// the hotkey thread only records the choice here and the camera thread applies
// it at a safe point (ApplyRequestedTrackingMode).
std::atomic<cameraunlock::TrackingMode> g_requestedMode{cameraunlock::TrackingMode::RotationAndPosition};

// Computed from the mode the camera thread last applied, so two presses before
// one camera update are one step, not two.
cameraunlock::TrackingMode NextTrackingMode(cameraunlock::TrackingMode mode) {
    return static_cast<cameraunlock::TrackingMode>((static_cast<int>(mode) + 1) % 3);
}

// Requests the next mode, then saves it, so the choice survives a restart.
// Runs on the hotkey poller's thread.
void CycleTrackingMode() {
    const cameraunlock::TrackingMode mode = NextTrackingMode(g_session.GetMode());
    g_requestedMode.store(mode);
    const char* name = "";
    switch (mode) {
        case cameraunlock::TrackingMode::RotationAndPosition: name = "rotation and position"; break;
        case cameraunlock::TrackingMode::RotationOnly:        name = "rotation only"; break;
        case cameraunlock::TrackingMode::PositionOnly:        name = "position only"; break;
    }
    Log::Line("[input] tracking mode: %s", name);
    config::SaveTrackingMode(mode);
}

void RegisterHotkeys(const Config& config) {
    for (const HotkeyList& list : HotkeyLists(config)) {
        void (*action)() = nullptr;
        switch (list.action) {
            case HotkeyAction::ToggleTracking:    action = ToggleTracking; break;
            case HotkeyAction::CycleTrackingMode: action = CycleTrackingMode; break;
        }
        cameraunlock::input::RegisterKeyBindings(g_hotkeys, list.bindings, action);
    }

    g_hotkeys.Start();
}

bool GameModuleRange(std::uintptr_t& base, std::size_t& size) {
    const HMODULE module = GetModuleHandleW(nullptr);
    if (!module) return false;
    MODULEINFO info{};
    if (!GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info))) return false;
    base = reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll);
    size = info.SizeOfImage;
    return true;
}

// Polls `probe` every kDiscoveryIntervalMs until it yields, or the attempts run
// out, or shutdown starts. Returns 0 on any of the last two.
template <typename Fn>
std::uintptr_t WaitFor(Fn&& probe) {
    for (int attempt = 0; attempt < kDiscoveryAttempts; ++attempt) {
        if (g_shuttingDown.load()) return 0;
        const std::uintptr_t result = probe();
        if (result) return result;
        Sleep(kDiscoveryIntervalMs);
    }
    return 0;
}

void LoadAndApplyConfig(const std::wstring& exeDir) {
    g_config = config::Load(exeDir, cameraunlock::config::DefaultsFile::PerUser());
    Log::Line("[boot] config: port=%u enableOnStartup=%d localSmoothing=%.2f "
              "remoteSmoothing=%.2f rotation=%d position=%d",
              static_cast<unsigned>(g_config.udp_port), g_config.enable_on_startup ? 1 : 0,
              g_config.local_smoothing, g_config.remote_smoothing,
              g_config.rotation_enabled ? 1 : 0, g_config.position_enabled ? 1 : 0);

    ApplyConfigToPipeline(g_config, g_session);
    g_requestedMode.store(g_session.GetMode());
    g_trackingEnabled.store(g_config.enable_on_startup);
}

// Everything from the point the socket is live: waiting for the engine to
// build its object table, then for a camera manager to exist in it, then
// hooking that manager's UpdateCamera. Returns false having logged which of
// the three did not happen.
bool BringUpCameraHook(std::uintptr_t moduleBase, std::size_t moduleSize) {
    Log::Line("[ue] waiting for the engine to bring up its object table...");
    if (!WaitFor([&] { return ue::DiscoverGlobals(moduleBase, moduleSize) ? 1u : 0u; })) {
        Log::Line("[ue] the object table never appeared - mod is dormant, game runs vanilla.");
        return false;
    }
    Log::Line("[ue] globals found: FNamePool=+0x%llX ObjObjects=+0x%llX",
              static_cast<unsigned long long>(ue::FNamePoolRva()),
              static_cast<unsigned long long>(ue::ObjObjectsRva()));

    // The table exists long before the game has a world in it, and the camera
    // manager is only owned by a controller once a level is up - which is also
    // the earliest point the UpdateCamera call site can be identified.
    Log::Line("[ue] waiting for a live player camera manager...");
    const std::uintptr_t manager = WaitFor([] { return ue::FindPlayerCameraManager(); });
    if (!manager) {
        Log::Line("[ue] no camera manager appeared - mod is dormant, game runs vanilla.");
        return false;
    }
    ue::LogCameraManagerOwner(manager);

    if (!InstallCameraHook(moduleBase, moduleSize, manager, g_config.near_clip_cm)) {
        Log::Line("[boot] the camera could not be hooked - mod is inert, game runs vanilla.");
        return false;
    }
    return true;
}

// Clears g_bootstrapRunning on every exit path, so an early return cannot
// leave Shutdown waiting out its whole timeout.
struct BootstrapRunningFlag {
    BootstrapRunningFlag() { g_bootstrapRunning.store(true); }
    ~BootstrapRunningFlag() { g_bootstrapRunning.store(false); }
};

void Bootstrap() {
    const BootstrapRunningFlag running;

    std::wstring exeDirWide;
    std::string exeDir;
    const bool haveExeDir = ExeDirectory(exeDirWide, exeDir);

    // Beside the game EXE, not in the process working directory: a launcher can
    // start the game from anywhere, and a bare relative name then drops the log
    // wherever that happens to be - or fails to create it at all - exactly when
    // a user is being asked to send one.
    Log::Open(haveExeDir ? exeDirWide + L"\\HeadTracking.log"
                         : std::wstring(L"HeadTracking.log"));
    Log::Line("=== Assetto Corsa Rally Head Tracking ===");

    if (!haveExeDir) {
        Log::Line("[boot] could not resolve the game directory - mod is dormant, game runs vanilla.");
        return;
    }
    if (!exeDir.empty()) Log::Line("[boot] game directory: %s", exeDir.c_str());

    std::uintptr_t moduleBase = 0;
    std::size_t moduleSize = 0;
    if (!GameModuleRange(moduleBase, moduleSize)) {
        Log::Line("[boot] could not read the game module range - mod is dormant, game runs vanilla.");
        return;
    }

    LoadAndApplyConfig(exeDirWide);

    g_receiver.SetLog([](const std::string& msg) { Log::Line("[udp] %s", msg.c_str()); });
    if (g_receiver.Start(g_config.udp_port)) {
        Log::Line("[boot] listening for OpenTrack data on UDP %u",
                  static_cast<unsigned>(g_config.udp_port));
    }

    if (!BringUpCameraHook(moduleBase, moduleSize)) {
        // Nothing will ever read the tracker now, so give the port back rather
        // than sitting on 4242 for the rest of the session and blocking
        // whatever else the user points their tracker at.
        g_receiver.Stop();
        return;
    }

    RegisterHotkeys(g_config);
    g_active.store(true);
    Log::Line("[boot] ready. ToggleKey=%s toggles tracking, CycleTrackingModeKey=%s cycles "
              "the tracking mode.",
              g_config.toggle_key.c_str(), g_config.cycle_tracking_mode_key.c_str());
}

}  // namespace

void ApplyRequestedTrackingMode() {
    if (!g_active.load(std::memory_order_acquire)) return;
    const cameraunlock::TrackingMode requested = g_requestedMode.load();
    if (requested != g_session.GetMode()) g_session.SetMode(requested);
}

bool ComposeTrackedCamera(const CameraPose& clean, float deltaTime, CameraPose& out) {
    if (!g_active.load(std::memory_order_acquire)) return false;

    // The caller reaches this only for a camera manager that is following the
    // car, so in practice it runs once per frame: a replay or photo camera
    // updating alongside the driving one is gated out before it gets here.
    // Two managers both following the car in the same frame would advance the
    // pipeline twice, which the smoothing absorbs but the interpolator does
    // not - it estimates the sample interval against wall time and would
    // extrapolate twice as far ahead as it should.
    // Gated on fresh data: the tri-state would otherwise announce a local
    // tracker on the first camera frame, before any packet was classified.
    if (g_session.Update(deltaTime)) {
        LogConnectionLocality();
    }

    HeadPose pose;
    const bool haveRotation = g_session.GetRotation(pose.yaw, pose.pitch, pose.roll);
    g_session.GetPositionOffset(pose.lean_x, pose.lean_y, pose.lean_z);

    if (!haveRotation || !g_trackingEnabled.load(std::memory_order_relaxed)) return false;

    out = ApplyHeadPose(clean, pose);
    return true;
}

bool ShuttingDown() { return g_shuttingDown.load(); }

void Initialize() {
    // Detached: DllMain runs under the loader lock, so the bootstrap - which
    // opens a log, reads an INI, starts a socket and then waits on the engine -
    // cannot run here.
    std::thread(Bootstrap).detach();
}

void Shutdown() {
    g_shuttingDown.store(true);
    g_active.store(false);

    // Let the bootstrap thread notice and unwind before the hook comes out.
    // It re-checks the flag every kDiscoveryIntervalMs, so this is a wait of
    // about one interval in practice. Bounded rather than joined: this runs
    // under the loader lock on FreeLibrary, and a thread that needs the loader
    // lock to finish would deadlock against an unbounded wait.
    constexpr int kBootstrapWaitAttempts = 40;
    for (int attempt = 0; attempt < kBootstrapWaitAttempts && g_bootstrapRunning.load();
         ++attempt) {
        Sleep(50);
    }

    UninstallCameraHook();
    g_hotkeys.Stop();
    g_receiver.Stop();
    Log::Line("[boot] shutdown");
    Log::Close();
}

}  // namespace acr_ht
