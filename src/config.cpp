#include "config.h"

#include <windows.h>

#include <cctype>
#include <cerrno>
#include <climits>
#include <clocale>
#include <cstdlib>
#include <string>

#include "config_sanitize.h"
#include "logging.h"

#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/protocol/port_utils.h"

namespace acr_ht {
namespace {

constexpr char kIniName[] = "HeadTracking.ini";

// The file a fresh install lands with. Values here must stay in step with the
// Config struct's member initialisers - tests/config_tests.cpp locks that by
// generating this file and loading it back over a poisoned Config.
constexpr char kDefaultIniText[] =
    "; Assetto Corsa Rally Head Tracking - configuration\n"
    "; Edit values, restart the game to apply.\n"
    ";\n"
    "; Controls: Home / Ctrl+Shift+T   recenter        (remappable, see [Hotkeys])\n"
    ";           End  / Ctrl+Shift+Y   toggle tracking (remappable, see [Hotkeys])\n"
    ";           PgUp / Ctrl+Shift+G   cycle tracking mode (rotation and position\n"
    ";                                 / rotation only / position only)\n\n"
    "[Network]\n"
    "UdpPort=4242\n\n"
    "[General]\n"
    "EnableOnStartup=1\n\n"
    "[Hotkeys]\n"
    "; Windows virtual-key codes, in hex. Each action has a nav-cluster key and a\n"
    "; Ctrl+Shift+<key> chord, and both fire it - remap either or both.\n"
    "; Common codes: Home 0x24, End 0x23, Insert 0x2D, Delete 0x2E, PgUp 0x21,\n"
    "; PgDn 0x22, F1-F12 0x70-0x7B, A-Z 0x41-0x5A, numpad 0-9 0x60-0x69.\n"
    "RecenterKey=0x24\n"
    "ToggleKey=0x23\n"
    "ChordRecenterKey=0x54\n"
    "ChordToggleKey=0x59\n\n"
    "[Rotation]\n"
    "YawSensitivity=1.0\n"
    "PitchSensitivity=1.0\n"
    "RollSensitivity=1.0\n"
    "InvertYaw=0\n"
    "InvertPitch=0\n"
    "InvertRoll=0\n"
    "Smoothing=0.0\n\n"
    "[Camera]\n"
    "; Near clip plane in centimetres, applied while head tracking is driving\n"
    "; the view. The game's own 5.0 sits further from your eye than the seat\n"
    "; back behind you, so looking over a shoulder clips the seat away and you\n"
    "; see straight through it. Pulling the plane in renders it instead.\n"
    "; 0 leaves the game's value alone.\n"
    "NearClipCm=1.0\n\n"
    "[Position]\n"
    "Enabled=1\n"
    "SensitivityX=1.0\n"
    "SensitivityY=1.0\n"
    "SensitivityZ=1.0\n"
    "InvertX=0\n"
    "InvertY=0\n"
    "InvertZ=0\n"
    "; How far the camera may travel from where the game put it, in metres.\n"
    "; These are cabin-sized: the headrest is against the back of your head and\n"
    "; the windscreen is an arm's length away, so a head allowed to roam a\n"
    "; room's worth of space ends up inside the seat or out over the bonnet.\n"
    "LimitX=0.15\n"
    "LimitY=0.12\n"
    "LimitZ=0.20\n"
    "; Backward travel. Zero by default: strapped into a rally seat your head\n"
    "; is already touching the headrest, so there is nowhere to go and any\n"
    "; travel here is spent moving your eye into the seat. Raise it only if you\n"
    "; sit forward of the headrest.\n"
    "LimitZBack=0.0\n"
    "Smoothing=0.15\n";

std::string IniPath(const std::string& exe_dir) {
    return exe_dir + "\\" + kIniName;
}

// Windows' INI reader hands back the rest of the line verbatim: it strips a
// whole-line `;` comment but not a trailing one, so `EnableOnStartup=0 ; off`
// arrives as the string "0 ; off". Parsed strictly that is not a boolean, and
// the setting silently keeps its old value - which reads as "the mod ignores
// my INI". The shipped file puts its comments on their own lines, but adding
// one to the end of a value is the obvious edit, so it is handled here.
constexpr char kAbsent[] = "\x01";

// Only `;`. `#` carries no meaning to Windows' INI reader, so treating it as a
// comment would silently truncate the first value that legitimately contains
// one - a path, say - which is the class of bug this layer exists to remove.
std::string StripCommentAndTrim(const std::string& value) {
    const std::size_t comment = value.find(';');
    const std::string body = (comment == std::string::npos) ? value : value.substr(0, comment);

    const std::size_t first = body.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const std::size_t last = body.find_last_not_of(" \t\r\n");
    return body.substr(first, last - first + 1);
}

// False when the key is not in the file at all, which is not worth reporting -
// the caller keeps its default. True with an empty `raw` means the key IS
// there and its value is blank or comment-only, which is a mistake worth
// reporting.
bool ReadRaw(const cameraunlock::IniReader& ini, const char* section, const char* key,
             std::string& raw) {
    const std::string value = ini.ReadString(section, key, kAbsent);
    if (value == kAbsent) return false;
    raw = StripCommentAndTrim(value);
    return true;
}

void LogUnparsed(const char* key, const std::string& raw, const char* expected) {
    Log::Line("[config] %s=%s is not %s; keeping the previous value", key,
              raw.empty() ? "(empty)" : raw.c_str(), expected);
}

// strtod stops at the first character it cannot use and reports success for
// everything before it, so "0,15" parses as 0 and "1.0 metres" as 1.0. A
// decimal comma is the first thing a user on a European keyboard types, and
// silently becoming zero is indistinguishable from the setting working.
//
// The C locale is held once. strtod takes its decimal separator from LC_NUMERIC,
// so under a comma-decimal locale it would reject every number in the shipped
// INI and then advise the user to use the full stop they already used. The mod
// links the static CRT, which starts in "C" and is never moved, so this is
// insurance - but it makes the parser's behaviour a property of this code
// rather than of whichever runtime it happens to be linked against.
_locale_t CNumericLocale() {
    static const _locale_t locale = _create_locale(LC_NUMERIC, "C");
    return locale;
}

bool ParseFloatStrict(const std::string& text, float& out) {
    if (text.empty()) return false;
    const char* start = text.c_str();
    char* end = nullptr;
    const double value = _strtod_l(start, &end, CNumericLocale());
    if (end == start) return false;
    // No trailing-whitespace skip: ReadRaw has already trimmed, so anything
    // left here is a second token and the value is malformed.
    if (*end != '\0') return false;
    out = static_cast<float>(value);
    return true;
}

bool ParseBoolStrict(const std::string& text, bool& out) {
    std::string lower;
    lower.reserve(text.size());
    for (const char c : text) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (lower == "1" || lower == "true" || lower == "yes" || lower == "on") {
        out = true;
        return true;
    }
    if (lower == "0" || lower == "false" || lower == "no" || lower == "off") {
        out = false;
        return true;
    }
    return false;
}

// strtol SATURATES on overflow and reports the saturated value as a success, so
// without the errno check `UdpPort=99999999999999` was refused with a message
// quoting 2147483647 - a number the user never typed, which is a worse
// diagnostic than none at all. long is 32 bits on MSVC, so the int range check
// below could never fire on its own.
bool ParseLongStrict(const char* start, int base, int& out) {
    char* end = nullptr;
    errno = 0;
    const long value = std::strtol(start, &end, base);
    if (end == start) return false;
    if (*end != '\0') return false;
    if (errno == ERANGE) return false;
    if (value < INT_MIN || value > INT_MAX) return false;
    out = static_cast<int>(value);
    return true;
}

bool ParseIntStrict(const std::string& text, int& out) {
    if (text.empty()) return false;
    return ParseLongStrict(text.c_str(), 10, out);
}

// Virtual-key codes are published as hex and that is how the shipped INI writes
// them, but decimal is what a user reading a code off a calculator will type.
// The base is chosen from the prefix rather than handed to strtol as 0, which
// would additionally read a leading zero as octal - so "070" would silently
// mean 56 rather than the F key the user was aiming at.
bool ParseKeyStrict(const std::string& text, int& out) {
    if (text.empty()) return false;
    const bool hex = text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
    return ParseLongStrict(text.c_str(), hex ? 16 : 10, out);
}

void ReadBoolSetting(const cameraunlock::IniReader& ini, const char* section, const char* key,
                     bool& out) {
    std::string raw;
    if (!ReadRaw(ini, section, key, raw)) return;
    bool parsed = false;
    if (!ParseBoolStrict(raw, parsed)) {
        LogUnparsed(key, raw, "0 or 1");
        return;
    }
    out = parsed;
}

// A value the mod refused is exactly what a "my INI setting does nothing" bug
// report needs to show, so every substitution is logged rather than swallowed.
// A NaN raw value compares unequal to everything, including itself, so it
// takes this branch too.
float UseSanitized(const char* name, float raw, float clean) {
    if (raw != clean) {
        Log::Line("[config] %s=%.4f is out of range or not finite; using %.4f",
                  name, raw, clean);
    }
    return clean;
}

// Reads `key` if it is present and parses as a number, then puts it through
// `sanitize`. A key that is absent, or present but unparseable, leaves the
// caller's value alone - the difference being that the second case says so.
template <typename Sanitize>
float ReadFloatSetting(const cameraunlock::IniReader& ini, const char* section, const char* key,
                       float current, Sanitize sanitize) {
    std::string text;
    if (!ReadRaw(ini, section, key, text)) return current;

    float raw = 0.0f;
    if (!ParseFloatStrict(text, raw)) {
        LogUnparsed(key, text, "a number (use a full stop for the decimal point)");
        return current;
    }
    return UseSanitized(key, raw, sanitize(raw));
}

float ReadSensitivity(const cameraunlock::IniReader& ini, const char* section,
                      const char* key, float current) {
    return ReadFloatSetting(ini, section, key, current,
                            [](float v) { return SanitizeSensitivity(v); });
}

float ReadSmoothing(const cameraunlock::IniReader& ini, const char* section,
                    const char* key, float current) {
    return ReadFloatSetting(ini, section, key, current,
                            [](float v) { return SanitizeSmoothing(v); });
}

// A key the mod refused leaves the action on its previous binding rather than
// on nothing, so a mistyped code costs the user that one hotkey and says so.
int ReadKeySetting(const cameraunlock::IniReader& ini, const char* key, int current) {
    std::string text;
    if (!ReadRaw(ini, "Hotkeys", key, text)) return current;

    int parsed = 0;
    if (!ParseKeyStrict(text, parsed)) {
        LogUnparsed(key, text, "a virtual-key code (0x24, or 36)");
        return current;
    }
    if (!IsBindableVirtualKey(parsed)) {
        Log::Line("[config] %s=%s is not a key that can be bound; keeping 0x%02X", key,
                  text.c_str(), current);
        return current;
    }
    return parsed;
}

float ReadLimit(const cameraunlock::IniReader& ini, const char* key, float current) {
    return ReadFloatSetting(ini, "Position", key, current,
                            [current](float v) { return SanitizePositionLimit(v, current); });
}

}  // namespace

void LoadConfig(const std::string& exe_dir, Config& out) {
    const std::string path = IniPath(exe_dir);
    cameraunlock::IniReader ini;
    if (!ini.Open(path)) {
        Log::Line("[config] could not open %s - using built-in defaults", path.c_str());
        return;
    }

    {
        std::string text;
        if (ReadRaw(ini, "Network", "UdpPort", text)) {
            int port = 0;
            if (!ParseIntStrict(text, port)) {
                LogUnparsed("UdpPort", text, "a number");
            } else {
                bool portValid = false;
                out.udp_port = cameraunlock::NormalizeUdpPort(port, out.udp_port, portValid);
                if (!portValid) {
                    Log::Line("[config] UdpPort is outside 1024-65535; using %u",
                              static_cast<unsigned>(out.udp_port));
                }
            }
        }
    }

    ReadBoolSetting(ini, "General", "EnableOnStartup", out.enable_on_startup);

    out.recenter_key       = ReadKeySetting(ini, "RecenterKey",      out.recenter_key);
    out.toggle_key         = ReadKeySetting(ini, "ToggleKey",        out.toggle_key);
    out.chord_recenter_key = ReadKeySetting(ini, "ChordRecenterKey", out.chord_recenter_key);
    out.chord_toggle_key   = ReadKeySetting(ini, "ChordToggleKey",   out.chord_toggle_key);

    out.yaw_sensitivity    = ReadSensitivity(ini, "Rotation", "YawSensitivity",   out.yaw_sensitivity);
    out.pitch_sensitivity  = ReadSensitivity(ini, "Rotation", "PitchSensitivity", out.pitch_sensitivity);
    out.roll_sensitivity   = ReadSensitivity(ini, "Rotation", "RollSensitivity",  out.roll_sensitivity);
    ReadBoolSetting(ini, "Rotation", "InvertYaw",   out.invert_yaw);
    ReadBoolSetting(ini, "Rotation", "InvertPitch", out.invert_pitch);
    ReadBoolSetting(ini, "Rotation", "InvertRoll",  out.invert_roll);
    out.smoothing          = ReadSmoothing(ini, "Rotation", "Smoothing",   out.smoothing);

    out.near_clip_cm = ReadFloatSetting(ini, "Camera", "NearClipCm", out.near_clip_cm,
                                        [](float v) { return SanitizeNearClip(v); });

    ReadBoolSetting(ini, "Position", "Enabled", out.position_enabled);
    out.position_sensitivity_x = ReadSensitivity(ini, "Position", "SensitivityX", out.position_sensitivity_x);
    out.position_sensitivity_y = ReadSensitivity(ini, "Position", "SensitivityY", out.position_sensitivity_y);
    out.position_sensitivity_z = ReadSensitivity(ini, "Position", "SensitivityZ", out.position_sensitivity_z);
    ReadBoolSetting(ini, "Position", "InvertX", out.invert_position_x);
    ReadBoolSetting(ini, "Position", "InvertY", out.invert_position_y);
    ReadBoolSetting(ini, "Position", "InvertZ", out.invert_position_z);
    out.limit_x            = ReadLimit(ini, "LimitX",     out.limit_x);
    out.limit_y            = ReadLimit(ini, "LimitY",     out.limit_y);
    out.limit_z            = ReadLimit(ini, "LimitZ",     out.limit_z);
    out.limit_z_back       = ReadLimit(ini, "LimitZBack", out.limit_z_back);
    out.position_smoothing = ReadSmoothing(ini, "Position", "Smoothing", out.position_smoothing);
}

void WriteDefaultConfigIfMissing(const std::string& exe_dir) {
    const std::string path = IniPath(exe_dir);

    // CREATE_NEW rather than "does it exist?" followed by a truncating open: the
    // two steps can straddle a file the user (or a second launch) writes in
    // between, and never overwriting a user's config is the whole promise here.
    const HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_EXISTS) return;
        Log::Line("[config] could not create %s (%lu) - the game directory is not writable. "
                  "Built-in defaults are in use and edits there will not be read.",
                  path.c_str(), error);
        return;
    }

    // A short write leaves a file that parses as a config but is missing keys,
    // which then reads as "the mod ignores my setting". Say so instead.
    constexpr DWORD kTextBytes = static_cast<DWORD>(sizeof(kDefaultIniText) - 1);
    DWORD written = 0;
    const BOOL ok = WriteFile(file, kDefaultIniText, kTextBytes, &written, nullptr);
    const DWORD writeError = GetLastError();
    CloseHandle(file);
    if (!ok || written != kTextBytes) {
        Log::Line("[config] %s was created but only %lu of %lu bytes could be written (%lu); "
                  "delete it and restart the game for a complete default config.",
                  path.c_str(), written, kTextBytes, writeError);
    }
}

}  // namespace acr_ht
