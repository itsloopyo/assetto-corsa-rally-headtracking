// The config_defaults round trip: generate the shipped HeadTracking.ini, load
// it back over a Config whose every member has been poisoned, and require the
// result to be the shipped defaults.
//
// That is what keeps kDefaultIniText and the Config member initialisers in
// step. They are two independent copies of the same numbers, and a default
// changed in one and not the other is invisible - the mod behaves one way on a
// fresh install and another way for anyone whose INI predates the change.

#include "config.h"
#include "config_sanitize.h"
#include "test_support.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

using acr_ht::Config;
using acr_ht_tests::Check;
using acr_ht_tests::Near;

int g_failures = 0;

// A directory of our own per run, so a test never reads or writes a config
// belonging to a real install.
class TempDirectory {
public:
    TempDirectory() {
        char temp[MAX_PATH]{};
        const DWORD length = GetTempPathA(MAX_PATH, temp);
        // PID plus a per-instance counter. On PID alone every TempDirectory in
        // the process shares one path, so a nested instance's destructor would
        // delete the outer test's INI out from under it.
        static int sequence = 0;
        char name[64]{};
        std::snprintf(name, sizeof(name), "acr_ht_test_%lu_%d", GetCurrentProcessId(),
                      ++sequence);
        path_.assign(temp, length);
        path_ += name;
        CreateDirectoryA(path_.c_str(), nullptr);
    }

    ~TempDirectory() {
        DeleteFileA((path_ + "\\HeadTracking.ini").c_str());
        RemoveDirectoryA(path_.c_str());
    }

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

    const std::string& Path() const { return path_; }
    std::string IniPath() const { return path_ + "\\HeadTracking.ini"; }
    void RemoveIni() const { DeleteFileA(IniPath().c_str()); }

private:
    std::string path_;
};

void WriteFileBytes(const std::string& path, const char* bytes, std::size_t length) {
    const HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, bytes, static_cast<DWORD>(length), &written, nullptr);
    CloseHandle(file);
}

std::string ReadWholeFile(const std::string& path) {
    const HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    std::string out;
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(file, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        out.append(buffer, read);
    }
    CloseHandle(file);
    return out;
}

// Every member set to something the shipped default is not, so a key the
// generated INI forgot to write shows up as the poison value rather than
// coincidentally matching.
Config Poisoned() {
    Config c;
    c.udp_port = 5555;
    c.enable_on_startup = false;
    c.yaw_sensitivity = 2.5f;
    c.pitch_sensitivity = 2.6f;
    c.roll_sensitivity = 2.7f;
    c.invert_yaw = true;
    c.invert_pitch = true;
    c.invert_roll = true;
    c.smoothing = 0.7f;
    c.near_clip_cm = 42.0f;
    c.position_enabled = false;
    c.position_sensitivity_x = 3.1f;
    c.position_sensitivity_y = 3.2f;
    c.position_sensitivity_z = 3.3f;
    c.invert_position_x = true;
    c.invert_position_y = true;
    c.invert_position_z = true;
    c.limit_x = 1.1f;
    c.limit_y = 1.2f;
    c.limit_z = 1.3f;
    c.limit_z_back = 1.4f;
    c.position_smoothing = 0.9f;
    return c;
}

void CheckMatchesDefaults(const Config& loaded, const char* label) {
    const Config defaults;
    int mismatches = 0;
    // Names the field and both values. Reporting only a count made the one
    // thing this test exists for - telling you WHICH default drifted - a
    // manual diff of two dozen pairs.
    auto same = [&](bool equal, const char* field, double got, double want) {
        if (equal) return;
        ++mismatches;
        std::cout << "         " << field << ": loaded=" << got << " defaults=" << want << "\n";
    };

    same(loaded.udp_port == defaults.udp_port, "udp_port",
         static_cast<double>(loaded.udp_port), static_cast<double>(defaults.udp_port));
    same(loaded.enable_on_startup == defaults.enable_on_startup, "enable_on_startup",
         static_cast<double>(loaded.enable_on_startup), static_cast<double>(defaults.enable_on_startup));
    same(Near(loaded.yaw_sensitivity, defaults.yaw_sensitivity, 1e-6), "yaw_sensitivity",
         static_cast<double>(loaded.yaw_sensitivity), static_cast<double>(defaults.yaw_sensitivity));
    same(Near(loaded.pitch_sensitivity, defaults.pitch_sensitivity, 1e-6), "pitch_sensitivity",
         static_cast<double>(loaded.pitch_sensitivity), static_cast<double>(defaults.pitch_sensitivity));
    same(Near(loaded.roll_sensitivity, defaults.roll_sensitivity, 1e-6), "roll_sensitivity",
         static_cast<double>(loaded.roll_sensitivity), static_cast<double>(defaults.roll_sensitivity));
    same(loaded.invert_yaw == defaults.invert_yaw, "invert_yaw",
         static_cast<double>(loaded.invert_yaw), static_cast<double>(defaults.invert_yaw));
    same(loaded.invert_pitch == defaults.invert_pitch, "invert_pitch",
         static_cast<double>(loaded.invert_pitch), static_cast<double>(defaults.invert_pitch));
    same(loaded.invert_roll == defaults.invert_roll, "invert_roll",
         static_cast<double>(loaded.invert_roll), static_cast<double>(defaults.invert_roll));
    same(Near(loaded.smoothing, defaults.smoothing, 1e-6), "smoothing",
         static_cast<double>(loaded.smoothing), static_cast<double>(defaults.smoothing));
    same(Near(loaded.near_clip_cm, defaults.near_clip_cm, 1e-6), "near_clip_cm",
         static_cast<double>(loaded.near_clip_cm), static_cast<double>(defaults.near_clip_cm));
    same(loaded.position_enabled == defaults.position_enabled, "position_enabled",
         static_cast<double>(loaded.position_enabled), static_cast<double>(defaults.position_enabled));
    same(Near(loaded.position_sensitivity_x, defaults.position_sensitivity_x, 1e-6), "position_sensitivity_x",
         static_cast<double>(loaded.position_sensitivity_x), static_cast<double>(defaults.position_sensitivity_x));
    same(Near(loaded.position_sensitivity_y, defaults.position_sensitivity_y, 1e-6), "position_sensitivity_y",
         static_cast<double>(loaded.position_sensitivity_y), static_cast<double>(defaults.position_sensitivity_y));
    same(Near(loaded.position_sensitivity_z, defaults.position_sensitivity_z, 1e-6), "position_sensitivity_z",
         static_cast<double>(loaded.position_sensitivity_z), static_cast<double>(defaults.position_sensitivity_z));
    same(loaded.invert_position_x == defaults.invert_position_x, "invert_position_x",
         static_cast<double>(loaded.invert_position_x), static_cast<double>(defaults.invert_position_x));
    same(loaded.invert_position_y == defaults.invert_position_y, "invert_position_y",
         static_cast<double>(loaded.invert_position_y), static_cast<double>(defaults.invert_position_y));
    same(loaded.invert_position_z == defaults.invert_position_z, "invert_position_z",
         static_cast<double>(loaded.invert_position_z), static_cast<double>(defaults.invert_position_z));
    same(Near(loaded.limit_x, defaults.limit_x, 1e-6), "limit_x",
         static_cast<double>(loaded.limit_x), static_cast<double>(defaults.limit_x));
    same(Near(loaded.limit_y, defaults.limit_y, 1e-6), "limit_y",
         static_cast<double>(loaded.limit_y), static_cast<double>(defaults.limit_y));
    same(Near(loaded.limit_z, defaults.limit_z, 1e-6), "limit_z",
         static_cast<double>(loaded.limit_z), static_cast<double>(defaults.limit_z));
    same(Near(loaded.limit_z_back, defaults.limit_z_back, 1e-6), "limit_z_back",
         static_cast<double>(loaded.limit_z_back), static_cast<double>(defaults.limit_z_back));
    same(Near(loaded.position_smoothing, defaults.position_smoothing, 1e-6), "position_smoothing",
         static_cast<double>(loaded.position_smoothing), static_cast<double>(defaults.position_smoothing));

    Check(g_failures, mismatches == 0, label);
    if (mismatches != 0) {
        std::cout << "         " << mismatches << " member(s) differ from the shipped defaults\n";
    }
}

void GeneratedIniLoadsBackAsTheShippedDefaults() {
    const TempDirectory dir;
    dir.RemoveIni();
    acr_ht::WriteDefaultConfigIfMissing(dir.Path());

    Config loaded = Poisoned();
    acr_ht::LoadConfig(dir.Path(), loaded);
    CheckMatchesDefaults(loaded, "the generated INI loads back as the shipped defaults");
}

void AnExistingConfigIsNeverOverwritten() {
    const TempDirectory dir;
    dir.RemoveIni();

    const char kUserIni[] = "[Network]\nUdpPort=5000\n";
    WriteFileBytes(dir.IniPath(), kUserIni, sizeof(kUserIni) - 1);

    acr_ht::WriteDefaultConfigIfMissing(dir.Path());

    // Byte-for-byte, not "the setting I checked survived". Appending the
    // defaults to a user's file leaves the first UdpPort winning, so a
    // value-only assertion stays green while the file has been mangled.
    Check(g_failures, ReadWholeFile(dir.IniPath()) == std::string(kUserIni),
          "an existing config is left exactly as the user wrote it");

    Config loaded;
    acr_ht::LoadConfig(dir.Path(), loaded);
    Check(g_failures, loaded.udp_port == 5000, "an existing config is not overwritten");
}

void AMissingConfigLeavesTheCallersValuesAlone() {
    const TempDirectory dir;
    dir.RemoveIni();

    Config loaded = Poisoned();
    acr_ht::LoadConfig(dir.Path(), loaded);
    Check(g_failures,
          loaded.udp_port == 5555 && Near(loaded.smoothing, 0.7f, 1e-6),
          "a missing config leaves the caller's values untouched");
}

void OutOfRangeValuesFallBackRatherThanReachingTheEngine() {
    const TempDirectory dir;
    dir.RemoveIni();

    const char kHostileIni[] =
        "[Network]\nUdpPort=70000\n"
        "[Rotation]\nSmoothing=nan\nYawSensitivity=1e30\nPitchSensitivity=1e40\n"
        "[Camera]\nNearClipCm=-3.0\n"
        "[Position]\nLimitZ=10000\nLimitZBack=-1\n";
    WriteFileBytes(dir.IniPath(), kHostileIni, sizeof(kHostileIni) - 1);

    Config loaded;
    // Seeded away from the SHIPPED defaults so a refusal is distinguishable
    // from a reset-to-default. It cannot distinguish either from "the key is
    // never read at all", because a refusal and a missing read both leave the
    // caller's value untouched - that is what
    // GeneratedIniLoadsBackAsTheShippedDefaults covers, by requiring every key
    // to round trip.
    loaded.smoothing = 0.5f;
    loaded.udp_port = 5555;
    loaded.limit_z_back = 0.07f;
    loaded.pitch_sensitivity = 2.25f;
    loaded.yaw_sensitivity = 2.75f;
    loaded.near_clip_cm = 3.5f;
    loaded.limit_z = 0.33f;
    acr_ht::LoadConfig(dir.Path(), loaded);

    Check(g_failures, loaded.udp_port == 5555, "an out-of-range port keeps the caller's value");
    Check(g_failures, Near(loaded.smoothing, 0.0f, 1e-6), "a NaN smoothing falls back to 0");
    Check(g_failures, Near(loaded.yaw_sensitivity, acr_ht::kMaxSensitivity, 1e-6),
          "an absurd finite sensitivity is clamped to the bound");
    // 1e40 overflows a float to +inf, which is not finite, so it takes the
    // fallback rather than the clamp.
    Check(g_failures, Near(loaded.pitch_sensitivity, 1.0f, 1e-6),
          "an overflowing sensitivity falls back to 1.0 rather than reaching the engine as Inf");
    Check(g_failures, Near(loaded.near_clip_cm, 0.0f, 1e-6),
          "a negative near clip turns the adjustment off");
    Check(g_failures, Near(loaded.limit_z, acr_ht::kMaxPositionLimit, 1e-6),
          "an absurd travel limit is clamped");
    Check(g_failures, Near(loaded.limit_z_back, 0.0f, 1e-6),
          "a negative travel limit becomes no travel");
}

// Loads `ini` over a Config seeded by `seed` and hands the result to `check`.
template <typename Seed, typename Verify>
void WithIni(const char* ini, Seed seed, Verify check) {
    const TempDirectory dir;
    dir.RemoveIni();
    WriteFileBytes(dir.IniPath(), ini, std::strlen(ini));

    Config loaded;
    seed(loaded);
    acr_ht::LoadConfig(dir.Path(), loaded);
    check(loaded);
}

// The malformed values a real user actually produces. Each one used to be
// accepted as a silently wrong number, or ignored with nothing in the log,
// which from the player's seat is identical to "the mod ignores my INI".
void MalformedValuesAreRefusedRatherThanMisread() {
    // Windows hands back the rest of the line, comment and all.
    WithIni("[General]\nEnableOnStartup=0 ; press End to switch it on\n",
            [](Config& c) { c.enable_on_startup = true; },
            [](const Config& c) {
                Check(g_failures, !c.enable_on_startup,
                      "a boolean with a trailing comment is still read");
            });

    WithIni("[Position]\nLimitX=0.15 ; metres\n", [](Config& c) { c.limit_x = 0.0f; },
            [](const Config& c) {
                Check(g_failures, Near(c.limit_x, 0.15f, 1e-6),
                      "a number with a trailing comment is still read");
            });

    // strtod stops at the comma and reports success, so this used to become a
    // silent zero - and zero is a legitimate limit, so nothing complained.
    WithIni("[Position]\nLimitX=0,15\n", [](Config& c) { c.limit_x = 0.11f; },
            [](const Config& c) {
                Check(g_failures, Near(c.limit_x, 0.11f, 1e-6),
                      "a decimal comma is refused rather than read as zero");
            });

    WithIni("[Rotation]\nSmoothing=1,5\n", [](Config& c) { c.smoothing = 0.3f; },
            [](const Config& c) {
                Check(g_failures, Near(c.smoothing, 0.3f, 1e-6),
                      "a decimal comma in smoothing keeps the previous value");
            });

    WithIni("[Position]\nLimitX=abc\n", [](Config& c) { c.limit_x = 0.11f; },
            [](const Config& c) {
                Check(g_failures, Near(c.limit_x, 0.11f, 1e-6),
                      "a value that is not a number at all keeps the previous value");
            });

    WithIni("[Rotation]\nSmoothing=\n", [](Config& c) { c.smoothing = 0.3f; },
            [](const Config& c) {
                Check(g_failures, Near(c.smoothing, 0.3f, 1e-6),
                      "an empty value keeps the previous value");
            });

    WithIni("[General]\nEnableOnStartup=maybe\n", [](Config& c) { c.enable_on_startup = true; },
            [](const Config& c) {
                Check(g_failures, c.enable_on_startup,
                      "a boolean that is neither true nor false keeps the previous value");
            });

    // Other spellings a user reasonably reaches for.
    WithIni("[General]\nEnableOnStartup=false\n", [](Config& c) { c.enable_on_startup = true; },
            [](const Config& c) {
                Check(g_failures, !c.enable_on_startup, "\"false\" is accepted as a boolean");
            });

    WithIni("[Position]\nEnabled=Off\n", [](Config& c) { c.position_enabled = true; },
            [](const Config& c) {
                Check(g_failures, !c.position_enabled,
                      "boolean spellings are matched without regard to case");
            });

    // CRLF is what every Windows editor writes.
    WithIni("[Position]\r\nLimitY=0.09\r\n", [](Config& c) { c.limit_y = 0.0f; },
            [](const Config& c) {
                Check(g_failures, Near(c.limit_y, 0.09f, 1e-6),
                      "a file with CRLF line endings reads the same as one with LF");
            });

    WithIni("", [](Config& c) { c.limit_y = 0.09f; },
            [](const Config& c) {
                Check(g_failures, Near(c.limit_y, 0.09f, 1e-6),
                      "an empty file leaves every value alone");
            });

    // A section the file never mentions must not disturb anything.
    WithIni("[Network]\nUdpPort=4500\n", [](Config& c) { c.limit_y = 0.09f; },
            [](const Config& c) {
                Check(g_failures, c.udp_port == 4500 && Near(c.limit_y, 0.09f, 1e-6),
                      "a missing section leaves its settings at the caller's values");
            });

    // Trailing whitespace survives an editor round trip more often than not.
    WithIni("[Position]\nLimitZ=0.25   \n", [](Config& c) { c.limit_z = 0.0f; },
            [](const Config& c) {
                Check(g_failures, Near(c.limit_z, 0.25f, 1e-6),
                      "trailing whitespace does not stop a value being read");
            });
}

// The keys deliberately allowed to be zero. LimitZBack ships at zero and means
// it - a driver against a headrest has nowhere to go - so a sanitizer that
// treated zero as "unset" would silently hand back the travel the mod exists
// to take away.
void ZeroIsAValueNotAnUnsetMarker() {
    WithIni("[Position]\nLimitZBack=0\n", [](Config& c) { c.limit_z_back = 0.1f; },
            [](const Config& c) {
                Check(g_failures, Near(c.limit_z_back, 0.0f, 1e-6),
                      "an explicit zero backward limit is kept, not replaced by a default");
            });

    WithIni("[Camera]\nNearClipCm=0\n", [](Config& c) { c.near_clip_cm = 1.0f; },
            [](const Config& c) {
                Check(g_failures, Near(c.near_clip_cm, 0.0f, 1e-6),
                      "an explicit zero near clip means leave the game's value alone");
            });
}

// strtol saturates at LONG_MAX and reports success, so without an errno check
// this was accepted as 2147483647 and refused with a message quoting a number
// the user never wrote.
void OverflowingIntegersAreRefusedNotSaturated() {
    WithIni("[Network]\nUdpPort=99999999999999\n", [](Config& c) { c.udp_port = 5555; },
            [](const Config& c) {
                Check(g_failures, c.udp_port == 5555,
                      "an integer too large for the type keeps the previous value");
            });
}

// `#` means nothing to Windows' INI reader, so a value containing one must
// survive intact rather than being truncated by a rule this mod invented.
void HashIsNotAComment() {
    WithIni("[Position]\nLimitX=0.25 # not a comment\n", [](Config& c) { c.limit_x = 0.11f; },
            [](const Config& c) {
                Check(g_failures, Near(c.limit_x, 0.11f, 1e-6),
                      "a value with a trailing hash is refused, not silently truncated");
            });
}

}  // namespace

int RunConfigTests() {
    std::cout << "\nConfig defaults\n";
    g_failures = 0;
    GeneratedIniLoadsBackAsTheShippedDefaults();
    AnExistingConfigIsNeverOverwritten();
    AMissingConfigLeavesTheCallersValuesAlone();
    OutOfRangeValuesFallBackRatherThanReachingTheEngine();
    MalformedValuesAreRefusedRatherThanMisread();
    OverflowingIntegersAreRefusedNotSaturated();
    HashIsNotAComment();
    ZeroIsAValueNotAnUnsetMarker();
    return g_failures;
}
