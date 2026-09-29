# Changelog

## [1.2.0] - 2026-09-29

### Added

- A setting set to `default` in `CameraUnlock.ini` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it, and neither do earlier versions of this mod. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.
- `Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.
- When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that.

### Changed

- Settings move to `acr\Binaries\Win64\CameraUnlock.ini`. Earlier versions of the mod kept these settings in `HeadTracking.ini`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `HeadTracking.ini` and writes them into `CameraUnlock.ini`. It never changes `HeadTracking.ini`, and does not read it again while `CameraUnlock.ini` exists.
- A first start with no `HeadTracking.ini` no longer writes one. It creates `CameraUnlock.ini` instead.
- A setting that the defaults the README shows set to `default` is written as `default` when you never changed it from the default earlier versions used, because `HeadTracking.ini` does not hold it or holds that default. It then follows `Defaults.ini`, so it takes the value `Defaults.ini` gives it, or the built-in value where `Defaults.ini` gives none, which can differ from the default earlier versions used. A setting you changed is written with the value imported for it, or as `default` where that value equals its default at that start.
- `RotationEnabled` and `PositionEnabled` are one setting here, the tracking mode, so both are written as `default` or neither is.
- Comments, and keys the mod never read, are not carried over. Nor is this, where your old file had it:
  - A sensitivity or axis inversion you changed from its default. Set these in your tracker instead.
- An older version of the mod reads `HeadTracking.ini` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `HeadTracking.ini`.
- Deleting only `CameraUnlock.ini` makes the next start read `HeadTracking.ini` again. To go back to the defaults, replace everything in `CameraUnlock.ini` with the defaults the README shows. Every setting they set to `default` then follows `Defaults.ini`.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`. `[Hotkeys] ToggleKey` and `ChordToggleKey` become the one list `ToggleKey`, and `CycleModeKey` and `ChordCycleModeKey` become `CycleTrackingModeKey`; the codes in your old file are imported into them.
- The tracking mode (Page Up / Ctrl+Shift+G) is saved to `CameraUnlock.ini` when you change it, and the game starts in the mode you left it in. Turning tracking on or off (End / Ctrl+Shift+Y) is still not saved; the game starts with head tracking on or off as `EnableOnStartup` says.
- `[Position] Enabled = false` is imported as the startup tracking mode, rotation only.
- `UdpPort` in `CameraUnlock.ini` takes any port from 1 to 65535. Earlier versions refused a port below 1024 and ran on 4242, their default, so a `HeadTracking.ini` holding one is imported as `default`, like a port you never changed: the game runs on the port `Defaults.ini` gives, or 4242 where it gives none.
- The keys are renamed to the names every head tracking mod on `CameraUnlock.ini` uses: `LocalSmoothing` and `RemoteSmoothing` move from `[Rotation]` to `[Smoothing]`, and the lean limits are `PositionLimitX`, `PositionLimitY`, `PositionLimitYDown`, `PositionLimitZ` (forward) and `PositionLimitZBack` (back). `LimitY` limited lowering your head as well as raising it, so it is imported into both `PositionLimitY` and `PositionLimitYDown`.
- A new `CameraUnlock.ini` sets the lean limits to `default`, whose built-in values are 0.3 m to either side, 0.2 m up and down, 0.4 m forward and 0.1 m back. Earlier versions started with 0.15 m, 0.12 m, 0.20 m forward and none back. A limit your `HeadTracking.ini` did not hold, or held at that old default, is one you never changed, so it is imported as `default` and after the update runs on the built-in value, or the value `Defaults.ini` gives it, as on a new install. A limit you changed is imported with the value you set. The README says how to set the smaller limits.

### Removed

- The sensitivity and axis inversion settings: `[Rotation] YawSensitivity`, `PitchSensitivity`, `RollSensitivity`, `InvertYaw`, `InvertPitch` and `InvertRoll`, and `[Position] SensitivityX`, `SensitivityY`, `SensitivityZ`, `InvertX`, `InvertY` and `InvertZ`. Set these in your tracker app instead.
- With these settings at their shipped defaults the camera moves as it did before.

## [1.1.0] - 2026-08-20

### Added

- drop the recenter hotkey and let the tracker own the centre

### Changed

- The tracker owns the centre. The recenter hotkeys (`Home` / `Ctrl+Shift+T`)
  and their `RecenterKey` / `ChordRecenterKey` settings are gone, along with the
  mod-side centre capture; the tracker pose is applied as absolute. Centre the
  view in your tracker app instead.

### Fixed

- migrate PositionSettings to the Symmetric factory

## [1.0.3] - 2026-08-17

### Added

- split smoothing into LocalSmoothing and RemoteSmoothing

### Changed

- Smoothing is now two settings instead of one: `[Rotation] LocalSmoothing`
  (default `0.0`) applies when the tracker runs on this PC, `[Rotation]
  RemoteSmoothing` (default `0.15`) applies when it is a phone or other device
  on the network. Which one is used is decided per connection from the packet's
  source address and is re-evaluated when the source changes, so switching
  between a local OpenTrack instance and a phone takes effect without a restart.
- Removed `[Rotation] Smoothing` and `[Position] Smoothing`. Both new values
  cover rotation and position alike, so there is no separate position smoothing
  setting.
- Removed the hidden 0.15 smoothing floor. It silently overrode whatever the
  user set, so a tracker on the same machine now gets zero-latency tracking by
  default.

## [1.0.2] - 2026-08-15

### Added

- make the mode cycle hotkey remappable and read key codes as hex only

## [1.0.1] - 2026-08-15

### Added

- make recenter and toggle hotkeys remappable via [Hotkeys] in the INI

## [1.0.0] - 2026-08-15
### Added

- Head tracking for Assetto Corsa Rally (Unreal Engine 5.4), covering rotation
  and 6DOF position on the driving cameras.
- Runtime discovery of everything the camera hook needs - Unreal's FName pool
  and object table, the player camera manager, the camera cache offsets and the
  `UpdateCamera` vtable slot - so no address is pinned to a game build and a
  game patch does not require a mod update.
- Hotkeys on the nav cluster and on `Ctrl+Shift` chords: recenter, toggle
  tracking, cycle tracking mode.
- Head turns about the world's up axis, so looking into an apex stays level
  with the horizon however far the car is banked or pitched.
- `HeadTracking.ini` next to the game EXE for port, sensitivity, inversion,
  smoothing and positional limits.
- Gameplay gating: the head only drives the view while the camera is following
  the car. The menus, the car showcase, the loading screens and the service
  park are left exactly as the game renders them, and the view holds while the
  game is paused.
- Cabin-sized positional limits. The shared pipeline's room-sized defaults let
  the camera travel 0.40 m forward and 0.10 m back from where the game put it,
  which in a rally cockpit means out over the bonnet or inside the seat.
  Backward travel is now off by default - a driver's head is against the
  headrest already - and the other axes are scaled to the cabin.
- `[Camera] NearClipCm`, pulling the near clip plane in from the game's 5 cm
  while tracking is driving the view, so the cockpit right beside your head -
  the seat back and headrest - renders when you look over a shoulder instead of
  being clipped away and showing the world through it.
