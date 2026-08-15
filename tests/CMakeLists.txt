# Same shape as cameraunlock-core/cpp/tests: one executable, one Run*Tests()
# per file. Only the game-process-free half of the mod is under test - the
# reflection walk and the detour need a live UE process, and are covered by
# `pixi run probe` against the game instead.
add_executable(acr_ht_tests
    test_main.cpp
    camera_transform_tests.cpp
    clean_pose_cache_tests.cpp
    config_sanitize_tests.cpp
    config_tests.cpp
    exe_path_tests.cpp
)

target_compile_options(acr_ht_tests PRIVATE ${ACR_HT_STRICT_WARNINGS})

target_link_libraries(acr_ht_tests PRIVATE acr_ht_core)

add_test(NAME acr_ht_tests COMMAND acr_ht_tests)
