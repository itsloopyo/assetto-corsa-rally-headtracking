# Third-Party Notices

This mod bundles or links the following third-party components.

## Ultimate ASI Loader

- **Version:** v9.7.2 (commit `ab722befd52581a34449b603926cfab476e66b05`)
- **License:** MIT
- **Upstream:** https://github.com/ThirteenAG/Ultimate-ASI-Loader
- **Usage:** Loads the mod's `.asi` into the game by proxying the `dinput8.dll` import in `acr/Binaries/Win64`.
- **Bundled:** yes. `vendor/ultimate-asi-loader/dinput8.dll` is bundled in the release ZIP and used as the install-time source, with the upstream licence text alongside it.

Copyright (c) 2023 ThirteenAG

---

## MinHook

- **Version:** v1.3.3 (commit `9fbd087432700d73fc571118d6a9697a36443d88`)
- **License:** BSD-2-Clause
- **Upstream:** https://github.com/TsudaKageyu/minhook
- **Usage:** Detours `APlayerCameraManager::UpdateCamera` so the mod can read and adjust the camera the game just calculated.
- **Bundled:** yes. Compiled from source into the mod binary.

Copyright (c) 2009-2017 Tsuda Kageyu

---

## cameraunlock-core

- **Version:** commit `514352d83d3cde2368bdfb937235823d96efe119`
- **License:** MIT
- **Upstream:** https://github.com/itsloopyo/cameraunlock-core
- **Usage:** The shared tracking pipeline (OpenTrack receiver, pose processing, smoothing, Unreal runtime helpers).
- **Bundled:** yes. Compiled from source into the mod binary.

Copyright (c) 2026 itsloopyo

---

## OpenTrack

- **Version:** protocol only, no pinned version
- **License:** ISC
- **Upstream:** https://github.com/opentrack/opentrack
- **Usage:** The mod implements OpenTrack's UDP output protocol so OpenTrack and compatible trackers can drive it.
- **Bundled:** no. No OpenTrack code is bundled or linked.
