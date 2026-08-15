#include "ue/ue_probe.h"

#include <string>

#include "logging.h"
#include "ue/ue_layout.h"
#include "ue/ue_properties.h"

#include <cameraunlock/unreal/ue_runtime.h>

namespace acr_ht::ue {
namespace {

namespace cu = cameraunlock::unreal;

// An actor is well under this; the bound only stops a corrupt read from
// walking off into the heap.
constexpr std::size_t kControllerScanBytes = 0x1200;

// Visits (controller, class, offset) for every place a live player controller
// holds `manager`; visit returns true to stop. Shared so the diagnostic and the
// hook installer cannot drift into searching differently and disagreeing about
// which offset is real.
template <typename Fn>
void ForEachControllerReference(std::uintptr_t manager, Fn&& visit) {
    cu::ForEachUObject([&](std::uintptr_t obj) -> bool {
        const std::string cls = cu::ClassName(obj);
        if (cls == "Class" || !cu::ContainsCI(cls, "PlayerController")) return false;
        if (cu::ContainsCI(cu::ObjectName(obj), "Default__")) return false;

        // Every matching offset in this controller, not just the first. A
        // derived BC_*PlayerController_C caching the manager alongside the
        // engine's own PlayerCameraManager field is exactly the ambiguity the
        // caller has to refuse, and stopping at the first match made it
        // invisible - one controller could only ever report one offset.
        for (std::size_t offset = 0; offset < kControllerScanBytes; offset += 8) {
            std::uintptr_t field = 0;
            if (!cu::SafeReadPtr(obj + offset, field)) break;
            if (field == manager && visit(obj, cls, offset)) return true;
        }
        return false;
    });
}

}  // namespace

std::uintptr_t FindPlayerCameraManager() {
    std::uintptr_t found = 0;
    cu::ForEachUObject([&](std::uintptr_t obj) -> bool {
        const std::string cls = cu::ClassName(obj);
        // "Class" would match the UClass describing a camera manager rather
        // than an instance of one, and the CDO is a template that never has a
        // live POV - neither is the object the hook wants.
        if (cls == "Class") return false;
        if (!cu::ContainsCI(cls, "PlayerCameraManager")) return false;
        if (cu::ContainsCI(cu::ObjectName(obj), "Default__")) return false;
        found = obj;
        return true;
    });
    return found;
}

std::size_t FindCameraManagerFieldOffset(std::uintptr_t manager) {
    // A controller can hold the manager in more than one field - a derived
    // BC_*PlayerController_C caching it alongside the engine's own
    // PlayerCameraManager would do it. Taking the first would feed the wrong
    // displacement to the UpdateCamera scan, which then finds nothing and
    // reports that the engine's camera dispatch is a shape this mod does not
    // know: true of the scan, but not the actual problem, and not actionable.
    // Two controllers reporting the SAME offset is not ambiguity - that is the
    // one field, seen twice. Only differing offsets are undecidable.
    std::size_t found = 0;
    bool haveFound = false;
    bool conflict = false;
    std::size_t conflicting = 0;
    ForEachControllerReference(manager,
        [&](std::uintptr_t, const std::string&, std::size_t offset) {
            if (!haveFound) {
                found = offset;
                haveFound = true;
            } else if (offset != found && !conflict) {
                conflict = true;
                conflicting = offset;
            }
            return false;
        });

    if (conflict) {
        // Latched. This runs inside the discovery poll, which retries twice a
        // second for ten minutes, so an unlatched line would put 1200 copies of
        // itself in the log and bury everything else in it.
        static bool logged = false;
        if (!logged) {
            logged = true;
            Log::Line("[probe] camera manager 0x%llX is held at two different offsets (+0x%zX "
                      "and +0x%zX), so the UpdateCamera call site cannot be identified "
                      "unambiguously - not hooking.",
                      static_cast<unsigned long long>(manager), found, conflicting);
        }
        return 0;
    }
    return found;
}

void LogCameraManagerOwner(std::uintptr_t manager) {
    int owners = 0;
    ForEachControllerReference(manager,
        [&](std::uintptr_t obj, const std::string& cls, std::size_t offset) {
            Log::Line("[probe] controller 0x%llX (%s) holds the camera manager at +0x%zX",
                      static_cast<unsigned long long>(obj), cls.c_str(), offset);
            ++owners;
            return false;
        });

    if (owners == 0) {
        Log::Line("[probe] no live player controller points at camera manager 0x%llX - it is "
                  "not the one driving the view",
                  static_cast<unsigned long long>(manager));
    }
}

void LogCameraManagerProperties(std::uintptr_t manager) {
    std::uintptr_t uclass = 0;
    if (!cu::SafeReadPtr(manager + kUObject_ClassPrivate, uclass) || !uclass) {
        Log::Line("[props] camera manager 0x%llX has no readable class",
                  static_cast<unsigned long long>(manager));
        return;
    }
    LogProperties(uclass);
}

}  // namespace acr_ht::ue
