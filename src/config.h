#pragma once

#include <cstdint>
#include <string>

namespace acr_ht {

struct Config {
    // Held as the socket's own type so an out-of-range INI value cannot reach
    // UdpReceiver::Start by silently truncating to a wrong 16-bit port.
    std::uint16_t udp_port = 4242;
    bool enable_on_startup = true;

    // Windows virtual-key codes. Each action has a nav-cluster key and a
    // Ctrl+Shift+<key> chord, and both fire it - the chord exists for keyboards
    // with no nav cluster. Defaults: Home, End, PgUp, and the T/Y/G chord
    // letters.
    int recenter_key = 0x24;
    int toggle_key = 0x23;
    int cycle_mode_key = 0x21;
    int chord_recenter_key = 0x54;
    int chord_toggle_key = 0x59;
    int chord_cycle_mode_key = 0x47;

    float yaw_sensitivity = 1.0f;
    float pitch_sensitivity = 1.0f;
    float roll_sensitivity = 1.0f;
    bool invert_yaw = false;
    bool invert_pitch = false;
    bool invert_roll = false;

    // Smoothing is chosen per connection from the packet's source address, and
    // both values cover rotation and position alike. A tracker running on this
    // machine is already steady, so local_smoothing is 0.0 and nothing floors
    // it; a phone on WiFi jitters over the network, which is what
    // remote_smoothing is for.
    float local_smoothing = 0.0f;
    float remote_smoothing = 0.15f;

    // Near clip plane, in centimetres, used while head tracking is driving the
    // view. The game ships 5 cm, which is further from the eye than the
    // driver's own seat back and headrest - so looking over a shoulder clips
    // them away and shows the world through the seat. Pulling the plane in
    // renders them instead. 0 leaves whatever the game set.
    float near_clip_cm = 1.0f;

    bool position_enabled = true;
    float position_sensitivity_x = 1.0f;
    float position_sensitivity_y = 1.0f;
    float position_sensitivity_z = 1.0f;
    bool invert_position_x = false;
    bool invert_position_y = false;
    bool invert_position_z = false;
    // Cabin-sized, not the room-sized defaults the shared pipeline ships.
    // The camera starts at a driver's eye point in a rally car: the headrest is
    // touching the back of the head, the windscreen is an arm's length ahead,
    // and the door and roll cage are within a forearm either side. The stock
    // 0.40 m of forward travel puts the eye out over the bonnet and the stock
    // 0.10 m of backward travel puts it inside the seat, which is what a head
    // "clipping through the headrest" actually is.
    //
    // Backward is zero on purpose. A driver strapped into a rally seat has
    // their head against the headrest already - there is nowhere to go, and
    // every centimetre of travel granted here is a centimetre of seat to see
    // the inside of. Raise it if you sit forward of the headrest and want to
    // pull back; anything above a few centimetres will start to intersect.
    float limit_x = 0.15f;
    float limit_y = 0.12f;
    float limit_z = 0.20f;
    float limit_z_back = 0.0f;
};

// Reads HeadTracking.ini from `exe_dir` over `out`. Keys that are absent, or
// whose value the boundary checks in config_sanitize.h reject, leave the
// corresponding member of `out` at whatever it already held - so passing a
// default-constructed Config yields the shipped defaults.
void LoadConfig(const std::string& exe_dir, Config& out);

// Writes the documented default HeadTracking.ini into `exe_dir`, unless one is
// already there. Never overwrites a user's file.
void WriteDefaultConfigIfMissing(const std::string& exe_dir);

}  // namespace acr_ht
