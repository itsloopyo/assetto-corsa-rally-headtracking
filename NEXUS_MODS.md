# NEXUS_MODS.md

Copy/paste source for the NexusMods page. Everything below describes the
**nexus** zip (`AssettoCorsaRallyHeadTracking-vX.Y.Z-nexus.zip`), which is the
only artifact a NexusMods user ever sees.

---

## Short description

Flatscreen head tracking for Assetto Corsa Rally - no VR headset required. Use a webcam, phone, or any OpenTrack-compatible tracker to look through corners, lean into an apex and peek past the A-pillar by moving your head, while the car, the physics and every one of your inputs stay exactly as the game intended. 

---

## Full description

# Head Tracking for Assetto Corsa Rally

Flatscreen head tracking for Assetto Corsa Rally, driven by any OpenTrack-compatible tracker. Your head moves the driving cameras; the car, the physics and your inputs are never touched by it. The mod pins nothing to a particular game build, so a game patch does not require a mod update.

## Features

- **Decoupled view and driving** - your head moves the camera; steering, throttle, physics and every input stay untouched
- **6DOF tracking** - full rotation (yaw, pitch, roll) plus positional tracking, so you can lean into an apex, look through a corner and peek past the A-pillar
- **Cabin-sized positional limits** - travel is scaled to a rally cockpit rather than a room, so your eye does not end up inside the seat or out over the bonnet
- **Horizon-locked turns** - head yaw turns about the world's up axis, so looking into an apex stays level with the horizon however far the car is banked
- **Near clip plane control** - pulls the clip plane in from the game's 5 cm so the seat back and headrest render when you look over a shoulder instead of being clipped away
- **Gameplay-gated** - tracking drives the view only while the camera is following your car. The menus, the car showcase, the loading screens and the service park are left exactly as the game renders them, and the view holds while the game is paused
- **Patch-resilient** - the engine's own reflection is used to find the camera at runtime, so nothing is pinned to a game build
- **Works with any OpenTrack-compatible source** - webcam, smartphone app, TrackIR, Tobii, or anything that speaks the OpenTrack UDP protocol

## Requirements

- A legitimately purchased copy of **Assetto Corsa Rally** on **Steam** (app 3917090). The mod is built and tested against the Steam release; no other store version has been verified.
- **Windows 10 or 11, 64-bit.**
- **Ultimate ASI Loader** - required, not bundled in this zip. See Dependencies.
- **OpenTrack** or another tracker that speaks the OpenTrack UDP protocol - required, not bundled. See Dependencies.

No Visual C++ redistributable is needed. The mod links the runtime statically.

There is no minimum game patch. The mod resolves the camera through Unreal's own reflection at runtime rather than through pinned addresses, so it is not tied to a specific build.

## Dependencies

### Ultimate ASI Loader (required, not bundled)

- **What it is:** a small proxy DLL that loads `.asi` plugins into a game process. It is the standard loader for drop-in native mods.
- **Why it is needed:** this mod ships as `AssettoCorsaRallyHeadTracking.asi`. Without a loader in the game folder, nothing loads it and the game runs completely vanilla.
- **Download:** https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases - grab the **x64** release (`Ultimate-ASI-Loader_x64.zip`). The 32-bit build will not load into this game.
- **Install:**
  1. Open the downloaded zip and take `dinput8.dll` out of it.
  2. Put that `dinput8.dll` in `<Steam library>\steamapps\common\Assetto Corsa Rally\acr\Binaries\Win64\`, next to `acr.exe`.
  3. That is the whole install. `acr.exe` imports `DINPUT8.dll`, and Windows resolves the application directory before `System32`, so the loader takes over that import with no rename and forwards everything on to the real system DLL.
- **Its own dependencies:** none.
- **Note on the file name:** it must be `dinput8.dll` for this game. Do not rename it to `winmm.dll` or `version.dll`; those work for other titles, not this one.
- **If you already have it:** if `dinput8.dll` is already in `acr\Binaries\Win64\` from another mod, leave it alone. One loader serves every `.asi` in the folder.

### OpenTrack, or any OpenTrack-compatible tracker (required, not bundled)

- **What it is:** open-source head tracking software that turns a webcam, TrackIR, Tobii or SteamVR device into head pose data and sends it over UDP.
- **Why it is needed:** the mod is a receiver only. It does no face or head detection itself; something has to send it a head pose.
- **Download:** https://github.com/opentrack/opentrack/releases - the latest Windows installer.
- **Install:** run the installer with defaults. Configuration is covered under Setup below.
- **Its own dependencies:** none for the standard webcam inputs. Hardware-specific inputs (TrackIR, Tobii) need their vendor drivers installed.
- **Alternative:** any phone app or tracker that sends OpenTrack UDP packets to port `4242` works just as well. [Headcam](https://headcam.app) is a free phone app that does this. OpenTrack is simply the most common choice.

## Installation

1. **Install the dependencies first**, in this order: Ultimate ASI Loader, then OpenTrack (or your phone app). Both are covered in the Dependencies section above. Order matters only in that the loader must be in place before you launch the game.
2. **Download this mod's zip** from the Files tab.
3. **Extract the contents of the zip into your game's install directory**, that is `<Steam library>\steamapps\common\Assetto Corsa Rally\`, so that `AssettoCorsaRallyHeadTracking.asi` ends up at `acr\Binaries\Win64\AssettoCorsaRallyHeadTracking.asi`, next to `acr.exe`. The zip mirrors the game folder (it contains a single `acr\Binaries\Win64\` path), so extracting at the install root is all that is required. Do not extract it inside `Win64` itself or you will end up with `acr\Binaries\Win64\acr\Binaries\Win64\`.

   After extraction, `acr\Binaries\Win64\` should contain at least:

   ```
   acr.exe
   dinput8.dll                            (from Ultimate ASI Loader)
   AssettoCorsaRallyHeadTracking.asi      (from this zip)
   ```

   **Mod manager note:** this is a plain drop-in `.asi` plus a proxy DLL. Vortex and MO2 are not needed and are not supported for this mod; install manually as above.
4. **Configure your tracker** to send OpenTrack UDP to port `4242` (see Setup below), then **launch the game**.
5. **Confirm it worked.** On first launch the mod writes two files into `acr\Binaries\Win64\`:
   - `HeadTracking.ini` - a self-documenting config file with every default in it
   - `AssettoCorsaRallyHeadTracking.log` - a log of everything the mod did

   If those two files appear, the loader engaged and the mod is running. Load into a stage and move your head to confirm tracking.

## Setup / Configuration

### Setting up a tracker

**OpenTrack (PC webcam or hardware tracker):**

1. Set **Input** to whatever tracker you have: `neuralnet tracker` or `PointTracker` for a webcam, or your TrackIR / Tobii / SteamVR input.
2. Set **Output** to `UDP over network`, IP `127.0.0.1`, port `4242`.
3. Click **Start**. Starting before or after launching the game both work.

**Phone app:**

1. Install an OpenTrack-compatible head tracking app such as [Headcam](https://headcam.app).
2. Point it at your PC's local IP address (run `ipconfig` to find it) on port `4242`.
3. Start tracking on the phone.

If a phone app over WiFi is jittery, route it through OpenTrack for smoothing: send from the phone to OpenTrack on a different port (`5252`, say, opened in your firewall), then have OpenTrack output to `127.0.0.1:4242`. Raising `[Rotation] RemoteSmoothing` in the INI works too - that is the value a phone on the network gets.

The mod never picks a centre on its own. It uses whatever your tracker sends, so centre it in your tracker app once you are sitting how you drive.

### Configuration file

`HeadTracking.ini` is written next to `acr.exe` in `acr\Binaries\Win64\` on first launch. Edit it and restart the game to apply. Every setting is commented in the file itself.

| Setting | Default | Notes |
|---|---|---|
| `[Network] UdpPort` | `4242` | OpenTrack standard. Must be `1024`-`65535` |
| `[General] EnableOnStartup` | `1` | |
| `[Hotkeys] ToggleKey` / `CycleModeKey` | `0x23` / `0x21` | The nav-cluster keys, as Windows virtual key codes in hex |
| `[Hotkeys] ChordToggleKey` / `ChordCycleModeKey` | `0x59` / `0x47` | The letter in the `Ctrl+Shift+` chord for the same two actions |
| `[Rotation] YawSensitivity` / `PitchSensitivity` / `RollSensitivity` | `1.0` | |
| `[Rotation] InvertYaw` / `InvertPitch` / `InvertRoll` | `0` | Set whichever axis runs backwards for your tracker |
| `[Rotation] LocalSmoothing` | `0.0` | `0.0` to `1.0`, used when the tracker runs on this PC. Covers rotation and position, and nothing floors it |
| `[Rotation] RemoteSmoothing` | `0.15` | `0.0` to `1.0`, used when the tracker is a device on the network. Covers rotation and position. Raise this if that feed is jittery |
| `[Camera] NearClipCm` | `1.0` | Near clip plane in centimetres while tracking is driving the view. The game's own 5.0 sits further from your eye than the seat back, so looking over a shoulder would clip the seat away and show the world through it. `0` turns the adjustment off and leaves the game's value alone, as does a negative value; anything positive is clamped to `0.1`-`100` |
| `[Position] Enabled` | `1` | |
| `[Position] SensitivityX` / `Y` / `Z` | `1.0` | |
| `[Position] InvertX` / `Y` / `Z` | `0` | |
| `[Position] LimitX` / `LimitY` | `0.15` / `0.12` m | Cabin-sized, not room-sized: the door and the roll cage are a forearm away |
| `[Position] LimitZ` | `0.20` m | How far you may lean in toward the windscreen. Larger values put your eye out over the bonnet |
| `[Position] LimitZBack` | `0.0` m | Backward travel, off by default: strapped into a rally seat your head is already against the headrest, so every centimetre granted here is spent moving your eye into the seat. Raise it only if you sit forward of the headrest |

Travel limits accept `0`-`10` m and sensitivities `-100`-`100`, far past anything usable. A value outside a setting's range is refused and the substitution written to the log, as is a value the mod could not read as a number at all - so a setting that appears to do nothing is explained there rather than silently ignored. Write decimals with a full stop (`0.15`, not `0,15`), and note that a comment placed at the end of a value line is fine.

## Controls

Two equivalent binding sets. Use whichever your keyboard has.

| Action | Nav-cluster | Chord |
|---|---|---|
| Toggle tracking | `End` | `Ctrl+Shift+Y` |
| Cycle tracking mode | `Page Up` | `Ctrl+Shift+G` |

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked driving (rotation and position)
2. Position off, rotation only
3. Rotation off, position only
4. Back to normal

The chord letters Y/G sit in the middle of the keyboard, easy to find by touch. `Ctrl+Shift+<letter>` is avoided by games, so the chords work whether or not your keyboard has a nav cluster.

Both halves of both actions are remappable through `[Hotkeys]` in `HeadTracking.ini`, which is worth doing if a button box or a wheel plugin already sits on one of them. They are Windows virtual key codes in hex, not key names: `ToggleKey=Insert` is refused, `ToggleKey=0x2D` is the same key, and a bare `24` is read as `0x24`. `Ctrl`, `Shift` and `Alt` cannot be bound, being what the chord is made of. A code the mod refuses leaves that action on its previous key and says so in the log, which also names every key it ended up bound to.

## Troubleshooting

**Nothing happens in game, and no log file appears.**
The loader is not engaging. Check all three of these:
- `dinput8.dll` (Ultimate ASI Loader, x64 build) is in `acr\Binaries\Win64\`, next to `acr.exe`, not at the install root and not in `acr\`.
- `AssettoCorsaRallyHeadTracking.asi` is in that same folder.
- You extracted the mod zip at the install root, not inside `Win64`. Look for a stray `acr\Binaries\Win64\acr\` folder, which is the sign of a double-nested extraction.

Verify the game files through Steam if you are unsure which folder is which; `acr.exe` is the landmark.

**The log appears but says the object table never appeared, or no camera manager appeared.**
The mod waits for the engine to build a world before hooking anything, and stays dormant if that never happens. Load into a stage, then check `AssettoCorsaRallyHeadTracking.log` again.

**Tracking does not respond even though the tracker is running.**
- Confirm your tracker's output is UDP to `127.0.0.1` port `4242` (or your PC's local IP on `4242` from a phone).
- Press `End` (or `Ctrl+Shift+Y`) to make sure tracking is toggled on.
- Centre the view in your tracker app. The mod applies the pose it is sent as absolute and keeps no centre of its own.
- Check that your firewall is not blocking UDP port `4242`.
- Two tracker apps sending to `4242` at once makes the mod ignore the second one. The log says so explicitly. Close whichever one you are not using.
- Read `acr\Binaries\Win64\AssettoCorsaRallyHeadTracking.log`. It records every step: whether the loader engaged, whether Unreal's object table was found, whether the camera was hooked, and what the first frames looked like.

**Another game had the tracker port when I launched.**
Close the other game and carry on driving; there is no need to restart Assetto Corsa Rally. The mod retries the port every half second for as long as it is running, so tracking comes up about a second after the port frees. The log shows both halves: `Failed to bind UDP port 4242 ... retrying every 500ms`, then `Bound UDP port 4242 after Ns of waiting - tracking is live`.

**My INI edit does nothing.**
- Edit the `HeadTracking.ini` in `acr\Binaries\Win64\`, the one written next to `acr.exe`. That is the only file the mod reads.
- Restart the game. The config is read once at startup.
- If the value was out of range it was refused and replaced with a safe one. That substitution is logged, so search `AssettoCorsaRallyHeadTracking.log` for the setting name.

**The view moves the wrong way on one axis.**
Set the matching `Invert...` to `1` in the INI (`[Rotation]` for rotation axes, `[Position]` for positional ones). Trackers disagree about axis signs; that is what those settings exist for.

**Looking around does not tilt with the car when it is banked or over a crest.**
That is deliberate. Head turns are about the world's up axis, so the turn stays level with the horizon however far the car is leaning. Turning about the car's own up axis instead is what a head strapped into a seat physically does, but it tilts the horizon every time you look into an apex, which is worse to drive to.

**My head goes into the seat, or out through the windscreen.**
The positional limits decide how far the camera may travel from where the game put it, and they ship cabin-sized for exactly this reason. Raise `[Position] LimitZ` / `LimitZBack` / `LimitX` / `LimitY` if you want more room and can live with the intersections.

**Some of the cockpit vanishes when I look at it up close.**
That is the near clip plane cutting away geometry nearer to your eye than it allows. `[Camera] NearClipCm` pulls it in to 1 cm by default; lower it further (`0.5`) if anything still disappears.

**Head tracking does nothing in the menus.**
That is deliberate. It follows your head only while the camera is on your car, so the menus, the car showcase, the loading screens and the service park are left as the game renders them. Pausing is handled separately - it leaves the camera on your car, so the mod asks the engine whether the game is paused - and the view holds until you resume.

**The camera jitters.**
- Raise the smoothing value your tracker actually uses: `[Rotation] LocalSmoothing` if it runs on this PC, `[Rotation] RemoteSmoothing` if it is a phone or other device on the network. The log says which of the two is in effect.
- Add smoothing in your tracking source. OpenTrack has a smoothing filter.
- A phone app over WiFi will always show some jitter. Route it through OpenTrack for smoothing.
- For webcam tracking, improve your lighting.

## Uninstalling

Everything the mod touches lives in `<Steam library>\steamapps\common\Assetto Corsa Rally\acr\Binaries\Win64\`. Delete from that folder:

- `AssettoCorsaRallyHeadTracking.asi` - the mod itself
- `HeadTracking.ini` - the config the mod wrote (optional; keep it if you plan to reinstall, it will be reused)
- `AssettoCorsaRallyHeadTracking.log` - the log the mod wrote (optional)

That is a complete revert; the mod never modifies a game file.

Removing the dependencies is optional:

- `dinput8.dll` (Ultimate ASI Loader) can stay. It is harmless with no `.asi` files present, and other mods may rely on it. Delete it only if this was the only `.asi` mod you had.
- OpenTrack is a separate application and is not coupled to the mod in any way. Uninstall it independently or leave it installed.

## Credits

- **Supernova Games Studios** and **Kunos Simulazioni** for Assetto Corsa Rally
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by ThirteenAG, MIT
- [MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu, BSD-2-Clause
- [OpenTrack](https://github.com/opentrack/opentrack) for the tracking protocol, ISC
- [cameraunlock-core](https://github.com/itsloopyo/cameraunlock-core), the shared tracking pipeline, MIT

This mod is not affiliated with or endorsed by Supernova Games Studios or Kunos Simulazioni.

## License

MIT, copyright itsloopyo / CameraUnlock. The full licence text ships in the zip
as `LICENSE`, and `THIRD-PARTY-NOTICES.md` alongside it lists every third-party
component compiled into the mod with its own licence.

## Source Code

https://github.com/itsloopyo/assetto-corsa-rally-headtracking - MIT License
