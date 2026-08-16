cmake_minimum_required(VERSION 3.20)
project(AssettoCorsaRallyHeadTracking VERSION 1.0.3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(MSVC)
    add_compile_options(/permissive- /Zc:__cplusplus /MP /W4)
endif()

include(FetchContent)
# Pinned to the commit v1.3.3 points at, not the tag: a tag can be moved at
# any time, and this source is compiled into the binary users install.
FetchContent_Declare(
    minhook_src
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        9fbd087432700d73fc571118d6a9697a36443d88  # v1.3.3
)
FetchContent_MakeAvailable(minhook_src)
add_library(minhook STATIC
    ${minhook_src_SOURCE_DIR}/src/hook.c
    ${minhook_src_SOURCE_DIR}/src/buffer.c
    ${minhook_src_SOURCE_DIR}/src/trampoline.c
    ${minhook_src_SOURCE_DIR}/src/hde/hde64.c
    ${minhook_src_SOURCE_DIR}/src/hde/hde32.c
)
target_include_directories(minhook PUBLIC ${minhook_src_SOURCE_DIR}/include)
if(MSVC)
    target_compile_options(minhook PRIVATE /wd4267 /wd4244 /wd4127 /wd4131)
endif()

# UNREAL carries unreal/ue_runtime.cpp: the fault-guarded reads and the
# GUObjectArray/FName reflection this mod resolves the camera through, instead
# of pinning RVAs that a patch would move.
set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_HOOKS ON CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_UNREAL ON CACHE BOOL "" FORCE)
add_subdirectory(cameraunlock-core/cpp ${CMAKE_BINARY_DIR}/cameraunlock-core)

# The parts of the mod that need no game process: the config boundary, the
# executable-path handling, the camera composition and the detour's clean-pose
# table. Split out so the tests can link them without pulling in DllMain,
# MinHook or the UE reflection walk.
add_library(acr_ht_core STATIC
    src/camera_transform.cpp
    src/clean_pose_cache.cpp
    src/config.cpp
    src/exe_paths.cpp
)

target_include_directories(acr_ht_core PUBLIC
    src
    cameraunlock-core/cpp/include
)

target_link_libraries(acr_ht_core PUBLIC cameraunlock)

# Warnings are errors for the code this repo owns, matching the
# TreatWarningsAsErrors the C# mods carry. Applied per target rather than as a
# directory-wide option because vendored MinHook does not build clean at /W4
# and is not ours to fix.
if(MSVC)
    set(ACR_HT_STRICT_WARNINGS /WX)
endif()
target_compile_options(acr_ht_core PRIVATE ${ACR_HT_STRICT_WARNINGS})

set(MOD_SOURCES
    src/dllmain.cpp
    src/headtracking_mod.cpp
    src/camera_hook.cpp
    src/ue/ue_camera_layout.cpp
    src/ue/ue_game_state.cpp
    src/ue/ue_globals.cpp
    src/ue/ue_probe.cpp
    src/ue/ue_properties.cpp
    src/ue/ue_update_camera.cpp
)

add_library(AssettoCorsaRallyHeadTracking SHARED ${MOD_SOURCES})

target_include_directories(AssettoCorsaRallyHeadTracking PRIVATE
    src
    cameraunlock-core/cpp/include
)

target_compile_options(AssettoCorsaRallyHeadTracking PRIVATE ${ACR_HT_STRICT_WARNINGS})

target_link_libraries(AssettoCorsaRallyHeadTracking PRIVATE
    acr_ht_core
    cameraunlock
    cameraunlock_hooks
    cameraunlock_unreal
    minhook
    ws2_32
    psapi
)

# Output as .asi for Ultimate ASI Loader.
set_target_properties(AssettoCorsaRallyHeadTracking PROPERTIES
    SUFFIX ".asi"
    PREFIX ""
)

option(ACR_HT_BUILD_TESTS "Build the mod's own unit tests" ON)
if(ACR_HT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
