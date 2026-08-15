// The boundary checks every value read from HeadTracking.ini passes through.
//
// IniReader parses floats with strtod, which accepts "nan" and "inf" and
// overflows a literal like 1e400 to +inf. Anything that gets past here reaches
// the smoothing math, the quaternion, and from there the camera transform this
// mod writes into a live engine - where a NaN is a black screen with nothing in
// the log to explain it.

#include "config_sanitize.h"
#include "test_support.h"

#include <cmath>
#include <limits>

namespace {

using acr_ht_tests::Check;

int g_failures = 0;

constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

void SmoothingStaysInsideZeroToOne() {
    using acr_ht::SanitizeSmoothing;
    // Non-finite values are replaced by the fallback before the clamp is
    // reached, so an infinity lands on the default rather than on a bound.
    Check(g_failures, SanitizeSmoothing(kNan) == 0.0f, "smoothing NaN falls back to 0");
    Check(g_failures, SanitizeSmoothing(kInf) == 0.0f, "smoothing +inf falls back to 0");
    Check(g_failures, SanitizeSmoothing(-kInf) == 0.0f, "smoothing -inf falls back to 0");
    Check(g_failures, SanitizeSmoothing(-5.0f) == 0.0f, "smoothing below 0 clamps to 0");
    // Above 1 the speed lerp goes negative, so the per-frame factor turns
    // negative and the view extrapolates away from the tracker.
    Check(g_failures, SanitizeSmoothing(2.5f) == 1.0f, "smoothing above 1 clamps to 1");
    Check(g_failures, SanitizeSmoothing(0.4f) == 0.4f, "smoothing in range is untouched");
}

void SensitivityKeepsItsSignButNotItsInfinities() {
    using acr_ht::SanitizeSensitivity;
    using acr_ht::kMaxSensitivity;
    // Non-finite values fall back before the clamp, so an infinity lands on
    // 1.0 rather than on the bound.
    Check(g_failures, SanitizeSensitivity(kNan) == 1.0f, "sensitivity NaN falls back to 1");
    Check(g_failures, SanitizeSensitivity(kInf) == 1.0f, "sensitivity +inf falls back to 1");
    Check(g_failures, SanitizeSensitivity(-kInf) == 1.0f, "sensitivity -inf falls back to 1");
    // A negative sensitivity is a legitimate way to invert an axis without
    // touching the Invert flags, so it has to survive.
    Check(g_failures, SanitizeSensitivity(-1.5f) == -1.5f, "a negative sensitivity survives");
    // A finite but absurd value is what the bound is actually for.
    Check(g_failures, SanitizeSensitivity(1e30f) == kMaxSensitivity,
          "an absurd finite sensitivity clamps to the bound");
    Check(g_failures, SanitizeSensitivity(-1e30f) == -kMaxSensitivity,
          "an absurd negative sensitivity clamps to the bound");
    Check(g_failures, std::isfinite(kMaxSensitivity * 180.0f),
          "180 degrees times the maximum sensitivity is still finite");
}

void PositionLimitsCannotInvertTheClamp() {
    using acr_ht::SanitizePositionLimit;
    using acr_ht::kMaxPositionLimit;
    Check(g_failures, SanitizePositionLimit(kNan, 0.2f) == 0.2f,
          "a non-finite position limit falls back");
    // PositionProcessor clamps to [-limit, +limit]; a negative limit swaps the
    // bounds.
    Check(g_failures, SanitizePositionLimit(-0.3f, 0.2f) == 0.0f,
          "a negative position limit clamps to zero travel");
    Check(g_failures, SanitizePositionLimit(10000.0f, 0.2f) == kMaxPositionLimit,
          "a mistyped 10000 m limit clamps to the bound");
    Check(g_failures, SanitizePositionLimit(0.0f, 0.2f) == 0.0f,
          "zero is a real setting - the shipped backward travel - not a missing one");
}

void NearClipIsEitherLeaveAloneOrAUsableDistance() {
    using acr_ht::SanitizeNearClip;
    Check(g_failures, SanitizeNearClip(kNan) == 0.0f, "near clip NaN means leave the game's value");
    Check(g_failures, SanitizeNearClip(-1.0f) == 0.0f, "a negative near clip means leave it alone");
    Check(g_failures, SanitizeNearClip(0.0f) == 0.0f, "zero passes through as leave-alone");
    Check(g_failures, SanitizeNearClip(0.0001f) == 0.1f,
          "a micrometre near clip clamps up rather than wrecking depth precision");
    Check(g_failures, SanitizeNearClip(1000.0f) == 100.0f, "an absurd near clip clamps down");
    Check(g_failures, SanitizeNearClip(1.0f) == 1.0f, "the shipped 1 cm near clip is untouched");
}

}  // namespace

int RunConfigSanitizeTests() {
    std::cout << "\nConfig sanitize\n";
    g_failures = 0;
    SmoothingStaysInsideZeroToOne();
    SensitivityKeepsItsSignButNotItsInfinities();
    PositionLimitsCannotInvertTheClamp();
    NearClipIsEitherLeaveAloneOrAUsableDistance();
    return g_failures;
}
