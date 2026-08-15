#include "ue/ue_update_camera.h"

#include <windows.h>

#include <cstring>
#include <vector>

#include "logging.h"

#include <cameraunlock/unreal/ue_runtime.h>

namespace acr_ht::ue {
namespace {

namespace cu = cameraunlock::unreal;

// How far into the candidate to look for the two markers below. UpdateCamera
// reaches its DoUpdateCamera call about 0x60 bytes in on this build; the bound
// only stops the search running into the next function.
constexpr std::size_t kMarkerScanBytes = 0x100;

// The whole forwarder: mov rcx,[rcx+disp32] (7) / test rcx,rcx (3) / je (2) /
// mov rax,[rcx] (3) / jmp [rax+disp32] (7). Every byte of it is matched, so the
// scan must not start within this distance of the end of the section.
constexpr std::size_t kForwarderBytes = 22;

// Ceiling on a vtable displacement taken from a scanned instruction. UE's
// deepest engine vtables run to a few hundred entries; APlayerCameraManager's
// UpdateCamera sits at slot 240 on this build.
constexpr std::uint32_t kMaxVtableBytes = 0x4000;

struct Section {
    std::uintptr_t begin;
    std::uintptr_t end;
};

bool TextSection(std::uintptr_t base, Section& out) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    // By name, not "the first executable section". This EXE also carries
    // Steam's executable `.bind` section, so taking the first would make the
    // scan depend on section ordering: put `.bind` ahead of `.text` in a future
    // repack and the forwarder search runs over the wrong range, finds nothing,
    // and reports that the engine's camera dispatch is an unknown shape - which
    // sends anyone reading the log after the engine rather than the packer.
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
        if (std::memcmp(section->Name, ".text", 6) != 0) continue;
        out.begin = base + section->VirtualAddress;
        out.end   = out.begin + section->Misc.VirtualSize;
        return true;
    }
    return false;
}

// `movaps xmm6, xmm1` - the float argument being parked in a callee-saved
// register because it is needed after a call. A forwarder target that never
// does this is not UpdateCamera: the other three candidates are a
// one-instruction float setter, an array walk and a getter, none of which
// carry a float across a call.
bool LooksLikeUpdateCamera(std::uintptr_t function, const Section& text) {
    if (function < text.begin || function >= text.end) return false;

    const std::size_t available = text.end - function;
    const std::size_t span = available < kMarkerScanBytes ? available : kMarkerScanBytes;
    const auto* code = reinterpret_cast<const std::uint8_t*>(function);

    bool savesFloatArg = false;
    bool callsVirtual = false;
    for (std::size_t i = 0; i + 6 < span; ++i) {
        if (code[i] == 0x0F && code[i + 1] == 0x28 && code[i + 2] == 0xF1)
            savesFloatArg = true;
        // call qword ptr [rax + disp32] - the DoUpdateCamera dispatch.
        if (code[i] == 0xFF && code[i + 1] == 0x90)
            callsVirtual = true;
        if (savesFloatArg && callsVirtual) return true;
    }
    return false;
}

}  // namespace

bool ResolveUpdateCamera(std::uintptr_t moduleBase, std::size_t moduleSize,
                         std::uintptr_t cameraManagerVtable,
                         std::size_t controllerFieldOffset,
                         UpdateCameraTarget& out) {
    (void)moduleSize;

    Section text{};
    if (!TextSection(moduleBase, text)) {
        Log::Line("[camera] could not locate the executable section - cannot resolve UpdateCamera.");
        return false;
    }

    // mov rcx,[rcx+disp32] / test rcx,rcx / je +0x0A / mov rax,[rcx] / jmp [rax+disp32]
    std::uint8_t prologue[7] = {0x48, 0x8B, 0x89, 0, 0, 0, 0};
    // The displacement is encoded as a 32-bit field, and the offset is bounded
    // by the controller scan that produced it - so narrow explicitly rather
    // than copying the low half of a size_t and relying on the byte order.
    const std::uint32_t fieldDisplacement = static_cast<std::uint32_t>(controllerFieldOffset);
    std::memcpy(prologue + 3, &fieldDisplacement, sizeof(fieldDisplacement));

    std::vector<UpdateCameraTarget> matches;
    for (std::uintptr_t p = text.begin; p + kForwarderBytes <= text.end; ++p) {
        const auto* code = reinterpret_cast<const std::uint8_t*>(p);
        if (std::memcmp(code, prologue, sizeof(prologue)) != 0) continue;
        if (code[7] != 0x48 || code[8] != 0x85 || code[9] != 0xC9) continue;   // test rcx,rcx
        if (code[10] != 0x74) continue;                                         // je
        if (code[12] != 0x48 || code[13] != 0x8B || code[14] != 0x01) continue;  // mov rax,[rcx]
        if (code[15] != 0x48 || code[16] != 0xFF || code[17] != 0xA0) continue;  // jmp [rax+disp32]

        std::uint32_t displacement = 0;
        std::memcpy(&displacement, code + 18, 4);
        if (displacement % 8 != 0) continue;
        // A displacement decoded out of a byte pattern is not yet known to be a
        // vtable index. Unbounded, it reads up to 4 GB past the vtable; the
        // guarded read makes that safe but not meaningful. UE's deepest actor
        // vtable is a few hundred slots, so anything past this is a false
        // pattern match rather than a dispatch site.
        if (displacement > kMaxVtableBytes) continue;
        const int slot = static_cast<int>(displacement / 8);

        std::uintptr_t function = 0;
        if (!cu::SafeReadPtr(cameraManagerVtable + displacement, function)) continue;
        if (!LooksLikeUpdateCamera(function, text)) continue;

        matches.push_back({function, slot});
    }

    if (matches.empty()) {
        Log::Line("[camera] no UpdateCamera forwarder found for a camera manager held at "
                  "+0x%zX. The engine's camera dispatch is not the shape this build of the "
                  "mod knows.", controllerFieldOffset);
        return false;
    }
    // More than one survivor means the discriminator stopped discriminating, and
    // guessing which is right would install a hook on an unrelated virtual.
    for (const UpdateCameraTarget& match : matches) {
        if (match.slot != matches.front().slot) {
            Log::Line("[camera] UpdateCamera is ambiguous: %zu candidate slots matched "
                      "(first two: %d and %d). Not hooking.",
                      matches.size(), matches.front().slot, match.slot);
            return false;
        }
    }

    out = matches.front();
    Log::Line("[camera] UpdateCamera resolved: vtable slot %d, function +0x%llX (%zu call site%s)",
              out.slot, static_cast<unsigned long long>(out.function - moduleBase),
              matches.size(), matches.size() == 1 ? "" : "s");
    return true;
}

}  // namespace acr_ht::ue
