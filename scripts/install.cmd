@echo off
:: ============================================
:: Assetto Corsa Rally Head Tracking - Install
:: ============================================
:: Thin wrapper - install body lives in cameraunlock-core/scripts/install-body-asi.cmd,
:: staged into the release ZIP's shared/ by Copy-SharedBundle. To change
:: install behaviour edit the body, not this wrapper. Everything below the
:: CONFIG BLOCK is copied verbatim from
:: cameraunlock-core/scripts/templates/install-wrapper-asi.cmd.
:: ============================================

:: --- CONFIG BLOCK ---
set "GAME_ID=assetto-corsa-rally"
set "MOD_DISPLAY_NAME=Assetto Corsa Rally Head Tracking"
set "MOD_DLLS=AssettoCorsaRallyHeadTracking.asi"
set "MOD_INTERNAL_NAME=AssettoCorsaRallyHeadTracking"
set "MOD_VERSION=1.2.0"
set "STATE_FILE=.headtracking-state.json"
set "FRAMEWORK_TYPE=ASILoader"
:: Filename the ASI loader DLL is renamed to: the import the game exe already
:: has. winmm.dll, dinput8.dll, dxgi.dll and xinput1_3.dll are the common ones.
set "ASI_LOADER_NAME=dinput8.dll"
:: Subdirectory below the exe directory to deploy into, for engines that load
:: their proxy DLL from somewhere other than beside the exe. Source engine wants
:: bin\; a copy next to the exe is never loaded. Leave empty for everything
:: else, and set the same value in uninstall.cmd.
set "ASI_SUBDIR="
:: Files copied only when they are not already there, so an upgrade keeps
:: whatever the user tuned. Listing an .ini in MOD_DLLS instead puts it through
:: the unconditional copy and resets every key on every update.
set "MOD_SEED_FILES="
:: Version of the vendored Ultimate ASI Loader, recorded in the state file so
:: the launcher can tell which loader build it is looking at. Leave empty to
:: omit the field. Bump alongside vendor/ via `pixi run update-deps`.
set "ASI_LOADER_VERSION=9.7.2"
:: Post-install help text. `&echo ` starts each further line.
set "MOD_CONTROLS=End / Ctrl+Shift+Y toggle tracking, PgUp / Ctrl+Shift+G cycle tracking mode"
:: --- END CONFIG BLOCK ---

:: Pin delayed expansion off before `%*` is expanded on the `call` below.
:: Under `cmd /V:ON`, or with DelayedExpansion=1 in
:: HKCU\Software\Microsoft\Command Processor, cmd.exe eats a `!` out of the
:: expanded line, and a real game path like C:\Games\Oh! My Game reaches the
:: body already mangled. The body pins expansion off at its own outer scope
:: too, but that is one `call` too late to save the argument it was handed.
setlocal disabledelayedexpansion

set "WRAPPER_DIR=%~dp0"
set "_BODY=%WRAPPER_DIR%shared\install-body-asi.cmd"
if not exist "%_BODY%" set "_BODY=%WRAPPER_DIR%..\cameraunlock-core\scripts\install-body-asi.cmd"
if not exist "%_BODY%" (
    echo ERROR: install-body-asi.cmd not found in shared\ or ..\cameraunlock-core\scripts\.
    echo If this is a release ZIP, re-download it from GitHub ^(corrupt installer^).
    echo If this is the dev tree, run: git submodule update --init --recursive
    exit /b 1
)
call "%_BODY%" %*
exit /b %errorlevel%
