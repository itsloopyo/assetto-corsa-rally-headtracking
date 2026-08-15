# Assetto Corsa Rally Head Tracking

![Mod GIF](https://raw.githubusercontent.com/itsloopyo/assetto-corsa-rally-headtracking/main/assets/readme-clip.gif)

An unofficial 6DOF head tracking mod for Assetto Corsa Rally that moves the camera with your head while the car and your inputs stay untouched, driven by any OpenTrack-compatible tracker, no VR headset required.

## Requirements

- [Assetto Corsa Rally](https://store.steampowered.com/app/3917090/) on Steam, a legitimately purchased copy.
- A head tracker that speaks the [OpenTrack](https://github.com/opentrack/opentrack) UDP protocol: OpenTrack itself with any of its inputs (webcam, TrackIR, Tobii, SteamVR), or a phone app such as [Headcam](https://headcam.app), which turns any phone into a tracker, for free.
- Windows 10 or 11, 64-bit.

## Installation

1. Download the installer ZIP from the [Releases](https://github.com/itsloopyo/assetto-corsa-rally-headtracking/releases) page.
2. Extract it anywhere.
3. Double-click `install.cmd`. It finds the game and drops the loader and the mod next to `acr.exe`.
4. Point your tracker at UDP port `4242`. OpenTrack on the same PC sends to `127.0.0.1`; a phone app sends to your PC's local network IP instead, because `127.0.0.1` on a phone is the phone.
5. Launch the game. The mod writes a default `HeadTracking.ini` and a log next to the EXE on first run.

If the installer cannot find your game, point it at the install folder yourself. Either set the environment variable:

```powershell
$env:ASSETTO_CORSA_RALLY_PATH = "D:\Games\Assetto Corsa Rally"
```

or pass the path as the first argument:

```powershell
install.cmd "D:\Games\Assetto Corsa Rally"
```

### Manual installation

Copy two files into `<game>\acr\Binaries\Win64\` (the folder containing `acr.exe`, **not** the install root):

- `vendor\ultimate-asi-loader\dinput8.dll` -> `dinput8.dll`
- `plugins\AssettoCorsaRallyHeadTracking.asi` -> `AssettoCorsaRallyHeadTracking.asi`

## OpenTrack setup

1. Install [OpenTrack](https://github.com/opentrack/opentrack/releases).
2. Input: whatever tracker you have (PointTracker, Tobii, neuralnet, …).
3. Output: **UDP over network**, IP `127.0.0.1`, port `4242`.
4. Start tracking before or after launching the game - either order works.

## Phone app setup

If your tracking app filters noise on the device you can point it straight at your PC's local IP on port `4242` and start tracking (I made sure Headcam does this, you can get it from https://headcam.app). Any other app that emits the OpenTrack UDP protocol works the same way. If a phone tracker feels jittery, point it at OpenTrack and ensure it is filtered before being passed to the game - you'll need to choose the OpenTrack UDP input and set the port to something other than 4242 (say, 5252) and the OpenTrack UDP output with port 4242.

## Controls

Two equivalent binding sets - use whichever your keyboard has:

| Action              | Nav-cluster | Chord          |
|---------------------|-------------|----------------|
| Recenter            | `Home`      | `Ctrl+Shift+T` |
| Toggle tracking     | `End`       | `Ctrl+Shift+Y` |
| Cycle tracking mode | `Page Up`   | `Ctrl+Shift+G` |

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay
2. Positional tracking disabled, rotational tracking enabled
3. Rotational tracking disabled, positional tracking enabled
4. Back to normal

The mod recenters itself once, a moment after it first sees tracker data. Sit how you drive when you start it, and use `Home` any time you want to reset the centre.

Recenter and toggle are remappable through `[Hotkeys]` in `HeadTracking.ini` - see below.

## Configuration

`HeadTracking.ini` is written next to `acr.exe` on first run. Edit it and restart the game to apply.

| Setting | Default | Notes |
|---|---|---|
| `[Network] UdpPort` | `4242` | OpenTrack standard. Must be `1024`-`65535` |
| `[General] EnableOnStartup` | `1` | |
| `[Hotkeys] RecenterKey / ToggleKey` | `0x24` / `0x23` | The nav-cluster keys, as [Windows virtual-key codes](https://learn.microsoft.com/windows/win32/inputdev/virtual-key-codes). Hex or decimal |
| `[Hotkeys] ChordRecenterKey / ChordToggleKey` | `0x54` / `0x59` | The letter in the `Ctrl+Shift+` chord for the same two actions |
| `[Rotation] Yaw/Pitch/RollSensitivity` | `1.0` | |
| `[Rotation] InvertYaw/Pitch/Roll` | `0` | Set the one that runs backwards for your tracker |
| `[Rotation] Smoothing` | `0.0` | `0.0`-`1.0`. A baseline of 0.15 is always applied internally; raise this if your tracker is jittery (a phone over WiFi, say) |
| `[Camera] NearClipCm` | `1.0` | Near clip plane in centimetres while tracking is driving the view. The game's own 5.0 sits further from your eye than your seat back, so looking over a shoulder would clip the seat away and show the world through it. `0` turns the adjustment off and leaves the game's value alone, as does a negative value; anything positive is clamped to `0.1`-`100`. |
| `[Position] Enabled` | `1` | |
| `[Position] SensitivityX/Y/Z` | `1.0` | |
| `[Position] InvertX/Y/Z` | `0` | Set the one that runs backwards for your tracker. Separate from the `[Rotation]` inversions |
| `[Position] LimitX / LimitY` | `0.15` / `0.12` m | Sized for a cabin: the door and roll cage are a forearm away |
| `[Position] LimitZ` | `0.20` m | Lean in toward the windscreen. Larger values put your eye out over the bonnet |
| `[Position] LimitZBack` | `0.0` m | Backward travel, off by default: strapped into a rally seat your head is already against the headrest, so every centimetre granted here is spent moving your eye into the seat |
| `[Position] Smoothing` | `0.15` | `0.0`-`1.0` |

Travel limits accept `0`-`10` m and sensitivities `-100`-`100`, far past anything usable; a value outside a setting's range is replaced and the substitution is written to the log, as is a value the mod could not read as a number at all.

Hotkeys are codes, not key names: `RecenterKey=Insert` is refused, `RecenterKey=0x2D` is the same key. Common ones are `Home` `0x24`, `End` `0x23`, `Insert` `0x2D`, `Delete` `0x2E`, `Page Up` `0x21`, `Page Down` `0x22`, `F1`-`F12` `0x70`-`0x7B`, `A`-`Z` `0x41`-`0x5A`, numpad `0`-`9` `0x60`-`0x69`. `Ctrl`, `Shift` and `Alt` cannot be bound - they are what the chord itself is made of. A code the mod refuses leaves that action on its previous key and says so in the log; the log also names every key it ended up bound to, so check there first if a remap did not take.

## Troubleshooting

**Nothing happens in game.** Read `AssettoCorsaRallyHeadTracking.log` next to `acr.exe`. It records every step: whether the loader engaged, whether Unreal's object table was found, whether the camera was hooked, and what the first few frames looked like.

**"the object table never appeared" or "no camera manager appeared".** The mod waits for the engine to build a world before it hooks anything, and stays dormant if that never happens. Load into a session and check the log again.

**The view moves the wrong way on one axis.** Set the matching `Invert…` to `1` in `HeadTracking.ini`. Trackers disagree about axis signs; that is what those settings are for.

**Looking around does not tilt with the car when it is banked or over a crest.** That is deliberate. Head turns are about the world's up axis, so the turn stays level with the horizon however far the car is leaning. Turning about the car's own up axis instead is what a head strapped into a seat physically does, but it tilts the horizon every time you look into an apex, which is worse to drive to.

**The view does not move even though the tracker is running.** Two tracker apps sending to port 4242 at once make the mod ignore the second one - the log says so explicitly. Close whichever you are not using.

**I launched this with another game still running, and that one had the tracker port.** Close the other game and carry on driving; there is no need to restart Assetto Corsa Rally. The mod retries the port every half second for as long as it is running, so tracking comes up about a second after the port frees. The log shows both halves: `Failed to bind UDP port 4242 ... retrying every 500ms`, then `Bound UDP port 4242 after Ns of waiting - tracking is live`.

**The view drifts away from centre.** Press `Home` (or `Ctrl+Shift+T`) while sitting how you drive.

**My head goes into the seat, or out through the windscreen.** The positional limits decide how far the camera may travel from where the game put it, and they ship cabin-sized for exactly this reason: `LimitZBack` is `0` so you cannot reverse into the headrest, and `LimitZ` is `0.20` m so leaning in stops short of the glass. Raise them if you want more room and can live with the intersections.

**Some of the cockpit vanishes when I look at it up close.** That is the near clip plane cutting away geometry nearer to your eye than it allows. `[Camera] NearClipCm` pulls it in to 1 cm; lower it further (`0.5`) if anything still disappears.

**Head tracking does nothing in the menus.** That is deliberate. It follows your head only while the camera is on your car, so the menus, the car showcase, the loading screens and the service park are left as the game renders them.

**The view stops following my head when I pause.** Also deliberate, and handled separately: pausing leaves the camera sitting on your car, so the mod asks the engine whether the game is paused rather than relying on where the camera is pointed. The view holds where it is and resumes when you do.

## Updating and uninstalling

Re-run `install.cmd` to update - it overwrites the mod and leaves your `HeadTracking.ini` alone. Run `uninstall.cmd` to remove the mod and the loader; add `/force` to remove the loader even if something else installed it. Your config file is never deleted.

## Building from source

Requires Visual Studio 2022 or newer (C++ desktop workload), CMake 3.20+, and [pixi](https://pixi.sh).

```powershell
git clone --recurse-submodules https://github.com/itsloopyo/assetto-corsa-rally-headtracking
cd assetto-corsa-rally-headtracking
pixi run build      # -> build/Release/AssettoCorsaRallyHeadTracking.asi
pixi run install    # deploy into the detected game folder
pixi run package    # -> release/AssettoCorsaRallyHeadTracking-v<version>-installer.zip
```

The build needs no game installed: it compiles against no game headers and resolves everything at runtime.

## How it works

The mod loads as an Ultimate ASI Loader plugin next to `acr.exe`. On startup it finds Unreal's FName pool and object table by scanning the module's data for their structure, confirming each by decoding it, and from there uses the engine's own reflection to locate the player camera manager and the exact offsets of its camera cache. It detours `APlayerCameraManager::UpdateCamera`, whose vtable slot it derives at runtime from the engine's own call site.

Each frame it hands the engine back the camera it computed last frame, lets the engine compute the next one untouched, then composes the head pose onto the result. The car's physics, your inputs and the game's own camera logic all run upstream of that and never observe the tracked view.

Because none of this is pinned to addresses in a particular build, a game patch does not require a mod update.

## Community & Support

- [Discord](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch of head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your phone into a head tracker

## License

MIT, copyright itsloopyo / CameraUnlock. See [LICENSE](LICENSE). Both release
ZIPs carry that file, and [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)
lists every third-party component the mod bundles or compiles in, with its own
licence.

## Credits

- **Supernova Games Studios** and **Kunos Simulazioni** for Assetto Corsa Rally.
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by ThirteenAG.
- [MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu.
- [OpenTrack](https://github.com/opentrack/opentrack) for the tracking protocol.

This mod is not affiliated with or endorsed by Supernova Games Studios or Kunos Simulazioni.
