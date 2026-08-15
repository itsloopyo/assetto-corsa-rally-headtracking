#include <iostream>

int RunCameraTransformTests();
int RunCleanPoseCacheTests();
int RunConfigSanitizeTests();
int RunConfigTests();
int RunExePathTests();

int main() {
    std::cout << "Assetto Corsa Rally Head Tracking Tests\n";
    std::cout << "======================================\n";

    int failures = 0;
    failures += RunCameraTransformTests();
    failures += RunCleanPoseCacheTests();
    failures += RunConfigSanitizeTests();
    failures += RunConfigTests();
    failures += RunExePathTests();

    if (failures == 0) {
        std::cout << "All tests passed!\n";
        return 0;
    }
    std::cout << failures << " test(s) FAILED\n";
    return 1;
}
