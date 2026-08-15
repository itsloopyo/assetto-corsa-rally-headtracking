# Ultimate ASI Loader (vendored)

Bundled copy of Ultimate ASI Loader, the install-time source of truth.
Refresh manually with `pixi run update-deps`, then commit.

## Snapshot

- Upstream: https://github.com/ThirteenAG/Ultimate-ASI-Loader
- Tag: `v9.7.3`
- Commit: `776f75ca5c92e022f8848550d1ba9e784b9a4d7d`
- Asset: `Ultimate-ASI-Loader_x64.zip`
- dinput8.dll SHA-256: `5dc7d6695e509948df5cd39b780b132d9ab0ca001287803350db282b7214da28`
- Fetched at: 2026-08-15T18:25:28.4914306+01:00

`dinput8.dll` is extracted from the upstream asset untouched. It deploys to the game
EXE directory (`acr/Binaries/Win64`) as `dinput8.dll`: acr.exe statically imports DINPUT8.dll, and the
safe DLL search order resolves the application directory before System32, so the
loader proxies that import without a rename.
