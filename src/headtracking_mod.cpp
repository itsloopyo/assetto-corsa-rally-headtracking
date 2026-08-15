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
#include "ue/ue_globals.h"
#include "ue/ue_probe.h"

#include <cameraunlock/input/chord_hotkeys.h>
#include <cameraunlock/input/hotkey_poller.h>
#include <cameraunlock/protocol/udp_receiver.h>
#include <cameraunlock/tracking/head_tracking_session.h>

namespace acr_ht {
namespace {

using Session = cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver>;
static_assert(Session::kHasRemoteRecenter,
              "UdpReceiver must forward TryConsumeRecenterRequest for tracker-app recentering");

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

void ApplyConfigToPipeline(const Config& config, Session& session) {
    cameraunlock::SensitivitySettings sensitivity;
    sensitivity.yaw = config.yaw_sensitivity;
    sensitivity.pitch = config.pitch_sensitivity;
    sensitivity.roll = config.roll_sensitivity;
    sensitivity.invert_yaw = config.invert_yaw;
    sensitivity.invert_pitch = config.invert_pitch;
    sensitivity.invert_roll = config.invert_roll;

    auto& proc = session.GetProcessor();
    proc.SetSensitivity(sensitivity);
    proc.SetSmoothing(config.smoothing);

    cameraunlock::PositionSettings position(
        config.position_sensitivity_x,
        config.position_sensitivity_y,
        config.position_sensitivity_z,
        config.limit_x, config.limit_y, config.limit_z, config.limit_z_back,
        config.position_smoothing,
        config.invert_position_x, config.invert_position_y, config.invert_position_z);
    session.GetPositionProcessor().SetSettings(position);

    session.SetMode(config.position_enabled ? cameraunlock::TrackingMode::RotationAndPosition
                                            : cameraunlock::TrackingMode::RotationOnly);
}

void Recenter() {
    g_session.Recenter();
    Log::Line("[input] recenter");
}

void ToggleTracking() {
    const bool on = !g_trackingEnabled.load();
    g_trackingEnabled.store(on);
    Log::Line("[input] tracking %s", on ? "enabled" : "disabled");
}

void CycleTrackingMode() {
    const char* name = "";
    switch (g_session.CycleMode()) {
        case cameraunlock::TrackingMode::RotationAndPosition: name = "rotation and position"; break;
        case cameraunlock::TrackingMode::RotationOnly:        name = "rotation only"; break;
        case cameraunlock::TrackingMode::PositionOnly:        name = "position only"; break;
    }
    Log::Line("[input] tracking mode: %s", name);
}

// Every action is reachable two ways: its nav-cluster key, and the
// Ctrl+Shift+<letter> chord for keyboards without a nav cluster. Pairing them
// in one row is what keeps the two lists from drifting apart - a new action
// cannot pick up a nav key and silently miss its chord.
struct HotkeyBinding {
    int nav_key;
    int chord_key;
    void (*action)();
};

// GetKeyNameText wants the scan code in bits 16-23 and the extended-key flag in
// bit 24. The nav cluster, the arrows and a few others are extended keys, and
// without that bit they name their numpad twins - a recenter left on Home would
// report itself in the log as "Num 7", which is the one thing this line exists
// to get right once the key is the user's choice.
bool IsExtendedKey(int vk) {
    switch (vk) {
        case VK_PRIOR: case VK_NEXT: case VK_END: case VK_HOME:
        case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN:
        case VK_INSERT: case VK_DELETE:
        case VK_DIVIDE: case VK_NUMLOCK: case VK_SNAPSHOT:
            return true;
        default:
            return false;
    }
}

std::string HotkeyName(int vk) {
    const UINT scan = MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);
    if (scan) {
        LONG lparam = static_cast<LONG>(scan) << 16;
        if (IsExtendedKey(vk)) lparam |= 1L << 24;
        char name[64]{};
        if (GetKeyNameTextA(lparam, name, sizeof(name)) > 0) return name;
    }
    // A key this layout has no name for still has to be identifiable, and the
    // code is what the user typed into the INI.
    char code[8]{};
    std::snprintf(code, sizeof(code), "0x%02X", vk);
    return code;
}

void RegisterHotkeys(const Config& config) {
    using namespace cameraunlock::input;

    const HotkeyBinding bindings[] = {
        { config.recenter_key, config.chord_recenter_key, Recenter },
        { config.toggle_key,   config.chord_toggle_key,   ToggleTracking },
        { VK_PRIOR,            'G',                       CycleTrackingMode },
    };

    for (const HotkeyBinding& binding : bindings) {
        g_hotkeys.AddHotkey(binding.nav_key, NavGuarded(binding.action));
        g_hotkeys.AddHotkey(binding.chord_key, ChordGuarded(binding.action));
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

// `exeDir` is the ANSI form of the game directory, and is empty when the
// directory has no ANSI form at all. The INI layer is ANSI-only, so there is
// nothing to read or write in that case - but everything else still works, so
// the mod comes up on its built-in defaults rather than going dormant.
void LoadAndApplyConfig(const std::string& exeDir) {
    if (exeDir.empty()) {
        Log::Line("[config] the game directory has no ANSI form on this system's code page, "
                  "so HeadTracking.ini cannot be read or written beside the game. Built-in "
                  "defaults are in use.");
    } else {
        WriteDefaultConfigIfMissing(exeDir);
        LoadConfig(exeDir, g_config);
    }
    Log::Line("[boot] config: port=%u enableOnStartup=%d smoothing=%.2f position=%d",
              static_cast<unsigned>(g_config.udp_port), g_config.enable_on_startup ? 1 : 0,
              g_config.smoothing, g_config.position_enabled ? 1 : 0);

    ApplyConfigToPipeline(g_config, g_session);
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
    const std::uintptr_t manager = WaitFor([] {
        const std::uintptr_t candidate = ue::FindPlayerCameraManager();
        return (candidate && ue::FindCameraManagerFieldOffset(candidate)) ? candidate : 0;
    });
    if (!manager) {
        Log::Line("[ue] no camera manager appeared - mod is dormant, game runs vanilla.");
        return false;
    }
    ue::LogCameraManagerOwner(manager);
    ue::LogCameraManagerProperties(manager);

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
    Log::Open(haveExeDir ? exeDirWide + L"\\AssettoCorsaRallyHeadTracking.log"
                         : std::wstring(L"AssettoCorsaRallyHeadTracking.log"));
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

    LoadAndApplyConfig(exeDir);

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
    Log::Line("[boot] ready. %s/Ctrl+Shift+%s recenter, %s/Ctrl+Shift+%s toggle tracking, "
              "PgUp/Ctrl+Shift+G cycle tracking mode.",
              HotkeyName(g_config.recenter_key).c_str(),
              HotkeyName(g_config.chord_recenter_key).c_str(),
              HotkeyName(g_config.toggle_key).c_str(),
              HotkeyName(g_config.chord_toggle_key).c_str());
}

}  // namespace

bool ComposeTrackedCamera(const CameraPose& clean, float deltaTime, CameraPose& out) {
    if (!g_active.load(std::memory_order_relaxed)) return false;

    // The caller reaches this only for a camera manager that is following the
    // car, so in practice it runs once per frame: a replay or photo camera
    // updating alongside the driving one is gated out before it gets here.
    // Two managers both following the car in the same frame would advance the
    // pipeline twice, which the smoothing absorbs but the interpolator does
    // not - it estimates the sample interval against wall time and would
    // extrapolate twice as far ahead as it should.
    g_session.Update(deltaTime);

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
