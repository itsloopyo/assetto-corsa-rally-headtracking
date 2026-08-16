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

// The shipped default of each smoothing key. They are not the same number, and
// that is the whole point of the fallback argument.
constexpr float kLocalDefault  = 0.0f;
constexpr float kRemoteDefault = 0.15f;

void SmoothingStaysInsideZeroToOne() {
    using acr_ht::SanitizeSmoothing;
    // Non-finite values are replaced by the fallback before the clamp is
    // reached, so an infinity lands on the default rather than on a bound - and
    // the default is the one belonging to the key that was read. Answering a
    // malformed RemoteSmoothing with LocalSmoothing's 0.0 would hand a phone on
    // WiFi no smoothing at all on raw network jitter.
    Check(g_failures, SanitizeSmoothing(kNan, kLocalDefault) == 0.0f,
          "a NaN LocalSmoothing falls back to the local default 0.0");
    Check(g_failures, SanitizeSmoothing(kNan, kRemoteDefault) == 0.15f,
          "a NaN RemoteSmoothing falls back to the remote default 0.15, not to 0.0");
    Check(g_failures, SanitizeSmoothing(kInf, kRemoteDefault) == 0.15f,
          "a +inf RemoteSmoothing falls back to the remote default 0.15");
    Check(g_failures, SanitizeSmoothing(-kInf, kRemoteDefault) == 0.15f,
          "a -inf RemoteSmoothing falls back to the remote default 0.15");
    Check(g_failures, SanitizeSmoothing(kInf, kLocalDefault) == 0.0f,
          "a +inf LocalSmoothing falls back to the local default 0.0");
    Check(g_failures, SanitizeSmoothing(-kInf, kLocalDefault) == 0.0f,
          "a -inf LocalSmoothing falls back to the local default 0.0");

    Check(g_failures, SanitizeSmoothing(-5.0f, kRemoteDefault) == 0.0f,
          "smoothing below 0 clamps to the bound, not to the fallback");
    // Out of range saturates. Not because the math breaks - the core clamps its
    // own interpolation speed to [0.1, 50], so a smoothing above 1 no longer
    // drives the per-frame factor negative - but so the value the mod acts on
    // and the value the INI advertises stay the same number.
    Check(g_failures, SanitizeSmoothing(2.5f, kLocalDefault) == 1.0f,
          "smoothing above 1 clamps to 1");
    Check(g_failures, SanitizeSmoothing(0.4f, kLocalDefault) == 0.4f,
          "smoothing in range is untouched");
    // A configured zero is a real setting - track me with no added latency -
    // and it must reach the processor as written even on the remote key, whose
    // fallback is 0.15. This is validation, never a floor.
    Check(g_failures, SanitizeSmoothing(0.0f, kRemoteDefault) == 0.0f,
          "a configured 0 survives verbatim on the remote key, never floored to 0.15");
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

void HotkeyCodesThatCannotFireAreRefused() {
    using acr_ht::IsBindableVirtualKey;
    Check(g_failures, !IsBindableVirtualKey(0), "0 is not a key");
    Check(g_failures, !IsBindableVirtualKey(0xFF), "0xFF is not a key");
    Check(g_failures, !IsBindableVirtualKey(-1), "a negative code is not a key");
    Check(g_failures, !IsBindableVirtualKey(0x100), "a code past the range is not a key");
    // Bound to a modifier, a nav binding is suppressed by the chord guard and a
    // chord binding fires the moment the chord itself is held.
    Check(g_failures, !IsBindableVirtualKey(0x10), "Shift cannot be bound");
    Check(g_failures, !IsBindableVirtualKey(0x11), "Control cannot be bound");
    Check(g_failures, !IsBindableVirtualKey(0x12), "Alt cannot be bound");
    Check(g_failures, !IsBindableVirtualKey(0xA2), "left Control cannot be bound");
    Check(g_failures, IsBindableVirtualKey(0x24), "Home can be bound");
    Check(g_failures, IsBindableVirtualKey(0x59), "a chord letter can be bound");
    Check(g_failures, IsBindableVirtualKey(0x7B), "F12 can be bound");
}

}  // namespace

int RunConfigSanitizeTests() {
    std::cout << "\nConfig sanitize\n";
    g_failures = 0;
    SmoothingStaysInsideZeroToOne();
    SensitivityKeepsItsSignButNotItsInfinities();
    PositionLimitsCannotInvertTheClamp();
    NearClipIsEitherLeaveAloneOrAUsableDistance();
    HotkeyCodesThatCannotFireAreRefused();
    return g_failures;
}
