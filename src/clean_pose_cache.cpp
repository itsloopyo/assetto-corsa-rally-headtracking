#include "clean_pose_cache.h"

namespace acr_ht {

CleanPoseCache::Slot* CleanPoseCache::Acquire(void* manager) {
    // Null is not a manager. Matching it would find the first FREE slot in the
    // identity search below and hand it back without claiming it, so the next
    // real manager would claim that same slot and the two would share a clean
    // pose - the exact aliasing this table exists to prevent.
    if (manager == nullptr) return nullptr;

    ++tick_;

    // The whole table is searched for a match before a free slot is handed out.
    // Stopping at the first free one would give a manager that already has a
    // slot a second one, and its clean pose would then alternate between them.
    for (Slot& slot : slots_) {
        if (slot.manager == manager) {
            slot.last_seen = tick_;
            return &slot;
        }
    }

    for (Slot& slot : slots_) {
        if (slot.manager == nullptr) {
            slot.manager = manager;
            slot.last_seen = tick_;
            return &slot;
        }
    }

    Slot* oldest = &slots_[0];
    for (Slot& slot : slots_) {
        if (slot.last_seen < oldest->last_seen) oldest = &slot;
    }
    ++evicted_;
    *oldest = Slot{};
    oldest->manager = manager;
    oldest->last_seen = tick_;
    return oldest;
}

bool CleanPoseCache::EngineStillHoldsOurPose(const Slot& slot, const CameraPose& live) {
    if (!slot.modified) return false;
    // Exact comparison on purpose. These are the bytes this mod wrote one frame
    // ago, not a computed value, so anything other than an exact match means
    // something else has written the camera since - a cut, a cinematic, or a
    // different manager living at the same address.
    const CameraPose& ours = slot.written;
    return live.location.X == ours.location.X && live.location.Y == ours.location.Y &&
           live.location.Z == ours.location.Z && live.rotation.Pitch == ours.rotation.Pitch &&
           live.rotation.Yaw == ours.rotation.Yaw && live.rotation.Roll == ours.rotation.Roll;
}

}  // namespace acr_ht
