# Ultimate ASI Loader (vendored)

Bundled copy of Ultimate ASI Loader, the install-time source of truth.
Refresh manually with `pixi run update-deps`, then commit.

## Snapshot

- Upstream: https://github.com/ThirteenAG/Ultimate-ASI-Loader
- Tag: `v9.7.2`
- Commit: `ab722befd52581a34449b603926cfab476e66b05`
- Asset: `Ultimate-ASI-Loader_x64.zip`
- dinput8.dll SHA-256: `22fda9c71eaae02460f311bf3441638340ab591586d78f1de213c4819dcb883c`
- Fetched at: 2026-08-17T00:38:54.6454070+01:00

`dinput8.dll` is extracted from the upstream asset untouched. It deploys to the game
EXE directory (`acr/Binaries/Win64`) as `dinput8.dll`: acr.exe statically imports DINPUT8.dll, and the
safe DLL search order resolves the application directory before System32, so the
loader proxies that import without a rename.
