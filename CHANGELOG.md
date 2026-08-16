# Changelog

## [1.0.3] - 2026-08-17

### Added

- split smoothing into LocalSmoothing and RemoteSmoothing

## [Unreleased]

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
