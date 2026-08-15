// Pins the detour's per-manager clean-pose table. A slot handed to the wrong
// manager writes one camera's pose into the other every frame, which on a
// stage reads as the view snapping and is nearly impossible to attribute back
// to here.
//
// Two rules carry the weight. A pose is only ever taken back out if this mod
// put it there AND the engine is still holding those exact bytes, which is what
// stops a recycled camera manager inheriting a dead one's camera. And a full
// table evicts its stalest entry rather than refusing, because the game
// respawns its camera manager on every stage load.

#include "clean_pose_cache.h"
#include "test_support.h"

#include <cstdint>

namespace {

using acr_ht::CameraPose;
using acr_ht::CleanPoseCache;
using acr_ht_tests::Check;
using acr_ht_tests::Near;

int g_failures = 0;

// Stand-ins for camera manager pointers. Never dereferenced - the cache only
// ever compares them.
void* FakeManager(std::uintptr_t id) { return reinterpret_cast<void*>(id * 0x1000); }

CameraPose MakePose(double x, double pitch) {
    CameraPose pose{};
    pose.location = {x, 0.0, 0.0};
    pose.rotation = {pitch, 0.0, 0.0};
    return pose;
}

// A fresh cache always has a slot free, so a null here is a regression in
// Acquire. Failing the assertion beats crashing the runner, which would take
// the remaining test files' results down with it.
CleanPoseCache::Slot* AcquireOrFail(CleanPoseCache& cache, void* manager, const char* what) {
    CleanPoseCache::Slot* slot = cache.Acquire(manager);
    Check(g_failures, slot != nullptr, what);
    return slot;
}

void AFreshCacheHandsOutASlot() {
    CleanPoseCache cache;
    CleanPoseCache::Slot* slot = cache.Acquire(FakeManager(1));
    Check(g_failures, slot != nullptr, "a fresh cache hands out a slot");
    Check(g_failures, slot != nullptr && slot->manager == FakeManager(1),
          "the slot records the manager that claimed it");
    Check(g_failures, slot != nullptr && !slot->modified,
          "a newly claimed slot has nothing to take back out");
}

void TheSameManagerKeepsTheSameSlot() {
    CleanPoseCache cache;
    CleanPoseCache::Slot* first = cache.Acquire(FakeManager(1));
    CleanPoseCache::Slot* again = cache.Acquire(FakeManager(1));
    Check(g_failures, first != nullptr && first == again,
          "the same manager gets the same slot back");
}

void AKnownManagerIsMatchedBeforeAFreeSlotIsTaken() {
    CleanPoseCache cache;
    // Claim two, then release nothing: the second manager's re-request must
    // find its own slot rather than the first free one it walks past.
    CleanPoseCache::Slot* first = cache.Acquire(FakeManager(1));
    CleanPoseCache::Slot* second = cache.Acquire(FakeManager(2));
    Check(g_failures, first != second, "two managers do not share a slot");
    Check(g_failures, cache.Acquire(FakeManager(2)) == second,
          "a known manager is matched across the whole table, not given a second slot");
}

void DistinctManagersGetDistinctSlots() {
    CleanPoseCache cache;
    CleanPoseCache::Slot* slots[CleanPoseCache::kSlots]{};
    bool allDistinct = true;
    for (int i = 0; i < CleanPoseCache::kSlots; ++i) {
        slots[i] = cache.Acquire(FakeManager(static_cast<std::uintptr_t>(i) + 1));
        if (slots[i] == nullptr) allDistinct = false;
        for (int j = 0; j < i; ++j) {
            if (slots[i] == slots[j]) allDistinct = false;
        }
    }
    Check(g_failures, allDistinct, "every manager up to the slot count gets its own slot");
}

// The game respawns its camera manager on every stage load. Refusing once the
// table filled left head tracking dead for the rest of a rally after four
// loads, with one early log line as the only evidence.
void AFullTableEvictsTheStalestManagerRatherThanRefusing() {
    CleanPoseCache cache;
    for (int i = 0; i < CleanPoseCache::kSlots; ++i) {
        cache.Acquire(FakeManager(static_cast<std::uintptr_t>(i) + 1));
    }

    // Manager 1 is the stalest: every other one has been touched since.
    CleanPoseCache::Slot* fresh = cache.Acquire(FakeManager(99));
    Check(g_failures, fresh != nullptr, "a manager past the slot count is still served");
    Check(g_failures, fresh != nullptr && fresh->manager == FakeManager(99),
          "the evicted slot is handed to the new manager");
    Check(g_failures, cache.EvictedManagers() == 1, "the eviction is counted");
    Check(g_failures, fresh != nullptr && !fresh->modified,
          "an evicted slot carries none of the previous manager's state");
}

void EvictionTakesTheStalestNotTheLiveOne() {
    CleanPoseCache cache;
    for (int i = 0; i < CleanPoseCache::kSlots; ++i) {
        cache.Acquire(FakeManager(static_cast<std::uintptr_t>(i) + 1));
    }

    // Manager 1 is the driving one, asked for every frame. Managers 2-4 are
    // dead. Whatever gets evicted, it must not be the live one.
    CleanPoseCache::Slot* live = cache.Acquire(FakeManager(1));
    cache.Acquire(FakeManager(99));
    Check(g_failures, cache.Acquire(FakeManager(1)) == live,
          "the manager still updating every frame is never the one evicted");
}

void AStoredPoseSurvivesAnotherManagersUpdate() {
    CleanPoseCache cache;
    CleanPoseCache::Slot* slot = AcquireOrFail(cache, FakeManager(7), "a slot to store a pose in");
    if (!slot) return;
    slot->clean = MakePose(10.0, 1.0);
    slot->modified = true;

    // Another manager in between, exactly as a replay camera would be.
    cache.Acquire(FakeManager(8));

    CleanPoseCache::Slot* again = cache.Acquire(FakeManager(7));
    Check(g_failures,
          again != nullptr && again->modified && Near(again->clean.location.X, 10.0, 1e-9) &&
              Near(again->clean.rotation.Pitch, 1.0, 1e-9),
          "a stored clean pose survives another manager's update");
}

// The restore rule.
void NothingIsRestoredUntilSomethingWasWritten() {
    CleanPoseCache cache;
    CleanPoseCache::Slot* slot = AcquireOrFail(cache, FakeManager(1), "a slot for the restore rule");
    if (!slot) return;

    const CameraPose live = MakePose(5.0, 2.0);
    slot->written = live;
    Check(g_failures, !CleanPoseCache::EngineStillHoldsOurPose(*slot, live),
          "a manager whose camera this mod never wrote gets nothing written back");

    slot->modified = true;
    Check(g_failures, CleanPoseCache::EngineStillHoldsOurPose(*slot, live),
          "a pose this mod wrote, still sitting in the engine, is taken back out");
}

// The recycled-manager guard. UE hands a freed camera manager's address to a
// new one; the new one's camera does not hold the bytes we wrote.
void ACameraHoldingSomethingElseIsLeftAlone() {
    CleanPoseCache cache;
    CleanPoseCache::Slot* slot = AcquireOrFail(cache, FakeManager(1), "a slot for the recycle guard");
    if (!slot) return;
    slot->written = MakePose(5.0, 2.0);
    slot->modified = true;

    Check(g_failures, !CleanPoseCache::EngineStillHoldsOurPose(*slot, MakePose(999.0, 2.0)),
          "a camera whose location is not the one we wrote is left alone");
    Check(g_failures, !CleanPoseCache::EngineStillHoldsOurPose(*slot, MakePose(5.0, 88.0)),
          "a camera whose rotation is not the one we wrote is left alone");

    // Every component matters: a guard that compared only location would let a
    // cut that kept the position through.
    Check(g_failures, CleanPoseCache::EngineStillHoldsOurPose(*slot, MakePose(5.0, 2.0)),
          "an exact match is still recognised as ours");
}

// Acquire compares raw pointers, and a null one used to match the first FREE
// slot without claiming it - so the next real manager claimed the same slot and
// the two aliased, which is the precise failure this file exists to catch.
void ANullManagerNeverAliasesARealOne() {
    CleanPoseCache cache;
    Check(g_failures, cache.Acquire(nullptr) == nullptr,
          "a null manager is refused rather than handed a free slot");

    CleanPoseCache::Slot* real = cache.Acquire(FakeManager(1));
    Check(g_failures, real != nullptr && cache.Acquire(nullptr) == nullptr,
          "and it still cannot claim a slot once the table has entries");
    Check(g_failures, cache.Acquire(FakeManager(1)) == real,
          "a real manager keeps its own slot regardless");
}

}  // namespace

int RunCleanPoseCacheTests() {
    std::cout << "\nClean pose cache\n";
    g_failures = 0;
    AFreshCacheHandsOutASlot();
    TheSameManagerKeepsTheSameSlot();
    AKnownManagerIsMatchedBeforeAFreeSlotIsTaken();
    DistinctManagersGetDistinctSlots();
    AFullTableEvictsTheStalestManagerRatherThanRefusing();
    EvictionTakesTheStalestNotTheLiveOne();
    AStoredPoseSurvivesAnotherManagersUpdate();
    NothingIsRestoredUntilSomethingWasWritten();
    ACameraHoldingSomethingElseIsLeftAlone();
    ANullManagerNeverAliasesARealOne();
    return g_failures;
}
