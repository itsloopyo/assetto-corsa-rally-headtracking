#pragma once

#include "camera_transform.h"

namespace acr_ht {

// The clean camera the engine computed, per camera manager, carried from one
// frame's post-hook to the next frame's pre-hook. Handing that back before the
// engine computes the next one is what keeps the modifier stack and the
// last-frame cache from ever seeing a tracked pose.
//
// Held apart from the detour so the slot bookkeeping can be exercised without a
// game process: getting it wrong shows up as two camera managers stealing each
// other's clean pose, which on a stage looks like the view snapping once a
// frame and is near-impossible to attribute.
class CleanPoseCache {
public:
    // A rally session has one driving camera manager. The spare slots cover a
    // replay or photo camera coexisting with it without either evicting the
    // other every frame.
    static constexpr int kSlots = 4;

    struct Slot {
        void*      manager = nullptr;
        // The camera as the engine computed it, and the tracked camera this mod
        // put there. Both are needed: the first is what gets handed back, the
        // second is how the next frame recognises that the value in the engine
        // is still the one we wrote.
        CameraPose clean{};
        CameraPose written{};
        // False until a post-hook has written a TRACKED pose over this
        // manager's camera. Only a pose this mod put there needs taking back
        // out, and restoring on a frame we did not touch is not a no-op: the
        // engine seeds the camera cache from outside UpdateCamera too (a fresh
        // manager, a cut, a cinematic), and a pre-call write would stamp a
        // frame-old pose over it.
        bool       modified = false;
        // Acquire order, for eviction. See Acquire.
        unsigned long long last_seen = 0;
    };

    // The slot belonging to `manager`, claiming a free one the first time it is
    // seen, and otherwise evicting the one that has gone longest without being
    // asked for.
    //
    // Eviction rather than refusal because the game respawns its camera manager
    // on every stage load: refusing left a rally session dead after four loads,
    // with head tracking simply gone and nothing but one early log line to say
    // why. The live manager is asked for every frame, so it is never the oldest,
    // and the slot that goes is always one belonging to a manager that has
    // stopped updating - which is to say, a dead one.
    Slot* Acquire(void* manager);

    // How many times a slot has been taken from one manager and given to
    // another. Non-zero means more managers have been seen than the table
    // holds, which is not itself a fault - the stalest entry is the one that
    // goes, and a manager still updating is never the stalest - but it is the
    // one signal that would explain head tracking going missing after a long
    // string of stage loads.
    int EvictedManagers() const { return evicted_; }

    // Whether the pose this mod wrote into `slot`'s manager is still the pose
    // sitting in the engine, and so whether handing the clean one back is
    // taking out something we put there.
    //
    // This is the whole recycled-manager guard. UE reuses freed object memory,
    // so a destroyed camera manager can be replaced by a new one at the same
    // address, which would otherwise inherit this slot and be handed a dead
    // camera's pose. A new manager's camera does not hold the bytes we wrote,
    // and neither does one the game cut to a different view in the meantime.
    static bool EngineStillHoldsOurPose(const Slot& slot, const CameraPose& live);

private:
    Slot slots_[kSlots];
    int  evicted_ = 0;
    unsigned long long tick_ = 0;
};

}  // namespace acr_ht
