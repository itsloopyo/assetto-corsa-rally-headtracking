#include "ue/ue_globals.h"

#include <windows.h>

#include <string>
#include <vector>

#include "logging.h"
#include "ue/ue_layout.h"

#include <cameraunlock/unreal/ue_runtime.h>

namespace acr_ht::ue {
namespace {

namespace cu = cameraunlock::unreal;

// The rest of the UE5 struct layout this scan needs. Like the UObject offsets
// in ue_layout.h these are set by the engine version rather than by the
// packaging of a particular build; they live here because nothing outside this
// file uses them.
constexpr std::size_t   kFNamePoolCurrentBlock = 0x08;  // FNameEntryAllocator::CurrentBlock
constexpr std::size_t   kFNamePoolCurrentCursor = 0x0C; // FNameEntryAllocator::CurrentByteCursor
constexpr std::size_t   kFNamePoolBlocks   = 0x10;    // FNameEntryAllocator::Blocks
constexpr std::uint32_t kFNameMaxBlocks    = 8192;
constexpr std::size_t   kObjObjectsNum     = 0x14;    // FChunkedFixedUObjectArray::NumElements
constexpr std::size_t   kFUObjectItemSize  = 0x18;
constexpr std::size_t   kChunkNumElems     = 0x10000;

std::uintptr_t g_fNamePoolRva = 0;
std::uintptr_t g_objObjectsRva = 0;

cu::UObjectGlobalsLayout MakeLayout(std::uintptr_t fNamePoolRva,
                                    std::uintptr_t objObjectsRva) {
    cu::UObjectGlobalsLayout layout{};
    layout.kObjObjects       = objObjectsRva;
    layout.kObjObjects_Num   = kObjObjectsNum;
    layout.kFUObjectItemSize = kFUObjectItemSize;
    layout.kChunkNumElems    = kChunkNumElems;
    layout.kFNamePool        = fNamePoolRva;
    layout.kFNamePoolBlocks  = kFNamePoolBlocks;
    layout.kClassPrivate     = kUObject_ClassPrivate;
    layout.kNamePrivate      = kUObject_NamePrivate;
    layout.kOuterPrivate     = kUObject_OuterPrivate;
    return layout;
}

struct Region {
    std::uintptr_t begin;
    std::uintptr_t end;
};

// Committed, readable, writable pages inside the module. Both globals live in
// .data (the pool and the array header are written during engine startup), and
// walking the real page map rather than the section table keeps the scan off
// anything the loader left uncommitted - which is what would otherwise turn a
// scan into a fault on a page that merely exists in the section headers.
std::vector<Region> WritableRegions(std::uintptr_t base, std::size_t size) {
    std::vector<Region> regions;
    const std::uintptr_t end = base + size;
    for (std::uintptr_t addr = base; addr < end;) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) != sizeof(mbi))
            break;
        const std::uintptr_t regionBase = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
        const std::uintptr_t regionEnd  = regionBase + mbi.RegionSize;
        const bool writable = (mbi.State == MEM_COMMIT) &&
            (mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY |
                            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0 &&
            (mbi.Protect & PAGE_GUARD) == 0;
        if (writable) {
            regions.push_back({regionBase < base ? base : regionBase,
                               regionEnd > end ? end : regionEnd});
        }
        addr = regionEnd <= addr ? addr + 0x1000 : regionEnd;
    }
    return regions;
}

// One FNameEntry: uint16 header (bIsWide bit 0, Len in the top 10 bits)
// followed by the characters. Returns the byte stride to the next entry, or 0
// if the header does not decode as a name.
std::size_t DecodeNameEntry(std::uintptr_t entry, std::string& out) {
    std::uint16_t header = 0;
    if (!cu::SafeReadU16(entry, header)) return 0;

    const bool isWide = (header & 1) != 0;
    const int len = header >> 6;
    if (len <= 0 || len > 1024) return 0;

    out.clear();
    out.reserve(static_cast<std::size_t>(len));
    for (int i = 0; i < len; ++i) {
        std::uint16_t unit = 0;
        if (!cu::SafeReadU16(entry + 2 + static_cast<std::size_t>(i) * (isWide ? 2 : 1), unit))
            return 0;
        const char c = static_cast<char>(unit & 0xff);
        // Names are identifiers and paths. A control byte here means the header
        // was not a header, so reject rather than return a string of noise that
        // would pass a bare "non-empty" check.
        if (c < 0x20 || static_cast<unsigned char>(c) > 0x7e) return 0;
        out.push_back(c);
    }

    const std::size_t bytes = 2 + static_cast<std::size_t>(len) * (isWide ? 2 : 1);
    return (bytes + 1) & ~static_cast<std::size_t>(1);  // entries are 2-byte aligned
}

// The pool's first block holds the hardcoded names the engine registers before
// anything else, starting with "None" at offset 0. Requiring that plus a long
// unbroken run of decodable names is what separates the real pool from the
// handful of unrelated structures that happen to start with two small integers
// and a pointer.
bool ValidateNamePool(std::uintptr_t poolAddr) {
    std::uintptr_t block0 = 0;
    if (!cu::SafeReadPtr(poolAddr + kFNamePoolBlocks, block0) || !block0) return false;

    std::string name;
    if (DecodeNameEntry(block0, name) == 0 || name != "None") return false;

    constexpr int kWanted = 64;
    std::uintptr_t cursor = block0;
    for (int i = 0; i < kWanted; ++i) {
        const std::size_t stride = DecodeNameEntry(cursor, name);
        if (stride == 0) return false;
        cursor += stride;
    }

    // Everything above reads through Blocks[0] alone, so it cannot tell the
    // pool apart from any other structure that happens to hold the same
    // Blocks[0] pointer at the same displacement - which would then supply
    // garbage for every name that lives in a later block. When the pool says
    // it has filled more than one block, the newest one has to decode too.
    std::uint32_t currentBlock = 0;
    std::uint32_t currentCursor = 0;
    if (!cu::SafeReadU32(poolAddr + kFNamePoolCurrentBlock, currentBlock)) return false;
    if (!cu::SafeReadU32(poolAddr + kFNamePoolCurrentCursor, currentCursor)) return false;
    if (currentBlock == 0) return true;
    if (currentBlock >= kFNameMaxBlocks) return false;
    // A fresh block is published into Blocks[] before anything is written into
    // it, and the cursor is zeroed at the same moment. Demanding a decode in
    // that window would reject the genuine pool, and the scan would carry on
    // past it and could settle on a later candidate.
    if (currentCursor == 0) return true;

    std::uintptr_t latestBlock = 0;
    if (!cu::SafeReadPtr(poolAddr + kFNamePoolBlocks + currentBlock * sizeof(std::uintptr_t),
                         latestBlock) ||
        !latestBlock) {
        return false;
    }
    return DecodeNameEntry(latestBlock, name) != 0;
}

bool ScanForNamePool(const std::vector<Region>& regions, std::uintptr_t base,
                     std::uintptr_t& poolRvaOut) {
    for (const Region& region : regions) {
        for (std::uintptr_t p = region.begin; p + 0x40 < region.end; p += 8) {
            // FNameEntryAllocator: FRWLock Lock (8), uint32 CurrentBlock,
            // uint32 CurrentByteCursor, then uint8* Blocks[kFNameMaxBlocks].
            //
            // Read through the guarded helpers, not raw. The region list is a
            // VirtualQuery snapshot taken before a scan that runs over tens of
            // megabytes while the engine is still in static init, so a page
            // that was committed and writable when it was taken can be
            // reprotected or freed before this reaches it.
            std::uint32_t currentBlock = 0;
            std::uint32_t currentCursor = 0;
            // A failed read means the page went away since the snapshot was
            // taken, so the rest of this region is gone too. Stepping on would
            // raise and dispatch an SEH exception every 8 bytes for the length
            // of the region, turning a 1.5 s scan into minutes - and the whole
            // scan is retried twice a second.
            if (!cu::SafeReadU32(p + 8, currentBlock)) break;
            if (!cu::SafeReadU32(p + 12, currentCursor)) break;
            if (currentBlock >= kFNameMaxBlocks) continue;
            if (currentCursor > 0x40000) continue;

            std::uintptr_t block0 = 0;
            if (!cu::SafeReadPtr(p + kFNamePoolBlocks, block0)) break;
            if (!cu::LooksLikePointer(block0)) continue;

            if (!ValidateNamePool(p)) continue;

            poolRvaOut = p - base;
            return true;
        }
    }
    return false;
}

// Every UE process has the CoreUObject script package, and it is reachable
// through nothing but the object table and the name pool - so finding it
// proves both at once, and proves them through exactly the decoder the rest of
// the mod will use rather than through a private copy of it.
bool ValidateObjectArray(std::uintptr_t arrayAddr, std::int32_t numChunks) {
    std::uintptr_t chunks = 0;
    if (!cu::SafeReadPtr(arrayAddr, chunks) || !chunks) return false;

    std::uint32_t num = 0;
    if (!cu::SafeReadU32(arrayAddr + kObjObjectsNum, num)) return false;
    if (num == 0) return false;

    // The walk below only ever reaches chunk 0, because it stops well short of
    // one chunk's worth of elements. Checking the last allocated chunk pointer
    // is what confirms the chunk array is a chunk array at all, rather than a
    // single plausible pointer with three integers behind it that happen to
    // agree with each other.
    std::uintptr_t lastChunk = 0;
    if (!cu::SafeReadPtr(chunks + static_cast<std::uintptr_t>(numChunks - 1) * 8, lastChunk) ||
        !lastChunk) {
        return false;
    }

    const std::uint32_t inspect = num < 4096 ? num : 4096;
    int resolvable = 0;
    for (std::uint32_t i = 0; i < inspect; ++i) {
        std::uintptr_t chunk = 0;
        if (!cu::SafeReadPtr(chunks + (i / kChunkNumElems) * 8, chunk) || !chunk) continue;
        std::uintptr_t obj = 0;
        if (!cu::SafeReadPtr(chunk + (i % kChunkNumElems) * kFUObjectItemSize, obj) || !obj)
            continue;

        const std::string cls = cu::ClassName(obj);
        if (cls.empty()) continue;
        ++resolvable;
        if (cls == "Package" && cu::ObjectName(obj) == "/Script/CoreUObject") return true;
    }

    // Reaching here means the shape held and names decoded but CoreUObject was
    // not among the objects inspected. Report it rather than accepting on the
    // weaker evidence: a partial match is how a wrong candidate gets adopted
    // and then fails much later, somewhere unrelated.
    if (resolvable > 0) {
        Log::Line("[ue] object table candidate at 0x%llX decoded %d class names but no "
                  "/Script/CoreUObject package - rejected",
                  static_cast<unsigned long long>(arrayAddr), resolvable);
    }
    return false;
}

bool ScanForObjectArray(const std::vector<Region>& regions, std::uintptr_t base,
                        std::uintptr_t moduleEnd, std::uintptr_t namePoolRva,
                        std::uintptr_t& arrayRvaOut) {
    for (const Region& region : regions) {
        for (std::uintptr_t p = region.begin; p + 0x20 <= region.end; p += 8) {
            // FChunkedFixedUObjectArray: FUObjectItem** Objects,
            // FUObjectItem* PreAllocatedObjects (null in a shipping build),
            // then int32 MaxElements, NumElements, MaxChunks, NumChunks.
            // Guarded for the same reason as the name-pool scan above.
            std::uintptr_t objects = 0;
            std::uintptr_t preAllocated = 0;
            // break, not continue - see the name pool scan above.
            if (!cu::SafeReadPtr(p, objects)) break;
            if (!cu::SafeReadPtr(p + 8, preAllocated)) break;
            if (!cu::LooksLikePointer(objects) || preAllocated != 0) continue;

            std::uint32_t maxElementsRaw = 0, numElementsRaw = 0;
            std::uint32_t maxChunksRaw = 0, numChunksRaw = 0;
            if (!cu::SafeReadU32(p + 0x10, maxElementsRaw)) break;
            if (!cu::SafeReadU32(p + 0x14, numElementsRaw)) break;
            if (!cu::SafeReadU32(p + 0x18, maxChunksRaw)) break;
            if (!cu::SafeReadU32(p + 0x1C, numChunksRaw)) break;
            const auto maxElements = static_cast<std::int32_t>(maxElementsRaw);
            const auto numElements = static_cast<std::int32_t>(numElementsRaw);
            const auto maxChunks   = static_cast<std::int32_t>(maxChunksRaw);
            const auto numChunks   = static_cast<std::int32_t>(numChunksRaw);

            if (maxElements <= 0 || maxElements > 0x4000000) continue;
            if (numElements <= 0 || numElements > maxElements) continue;
            // UE rounds the chunk count up. The stock 2,162,688 objects divide
            // exactly, so a floor here matched by luck and would have rejected
            // the real table on any build whose object cap is not a multiple of
            // the chunk size - leaving the mod permanently dormant, blaming a
            // missing object table.
            const auto chunkElems = static_cast<std::int32_t>(kChunkNumElems);
            if (maxChunks != (maxElements + chunkElems - 1) / chunkElems) continue;
            if (numChunks <= 0 || numChunks > maxChunks) continue;
            // NumElements has to land inside the chunks actually allocated.
            if (numElements > numChunks * static_cast<std::int32_t>(kChunkNumElems)) continue;

            // Reflection has to be live against this candidate to validate it,
            // so publish it first; a rejection is followed by the next
            // candidate's SetRuntime, and the caller only keeps what validated.
            cu::SetRuntime(base, moduleEnd, MakeLayout(namePoolRva, p - base));
            if (!ValidateObjectArray(p, numChunks)) continue;

            arrayRvaOut = p - base;
            return true;
        }
    }
    return false;
}

}  // namespace

std::uintptr_t FNamePoolRva()  { return g_fNamePoolRva; }
std::uintptr_t ObjObjectsRva() { return g_objObjectsRva; }

bool DiscoverGlobals(std::uintptr_t moduleBase, std::size_t moduleSize) {
    const std::vector<Region> regions = WritableRegions(moduleBase, moduleSize);
    if (regions.empty()) return false;

    // The pool first: validating an object-table candidate means resolving
    // names through it, so there is nothing to check the table against until
    // the pool is known.
    std::uintptr_t namePoolRva = 0;
    if (!ScanForNamePool(regions, moduleBase, namePoolRva)) return false;

    std::uintptr_t objObjectsRva = 0;
    if (!ScanForObjectArray(regions, moduleBase, moduleBase + moduleSize,
                            namePoolRva, objObjectsRva)) {
        // ScanForObjectArray leaves the last rejected candidate published.
        cu::SetRuntime(0, 0, cu::UObjectGlobalsLayout{});
        return false;
    }

    cu::SetRuntime(moduleBase, moduleBase + moduleSize,
                   MakeLayout(namePoolRva, objObjectsRva));
    g_fNamePoolRva  = namePoolRva;
    g_objObjectsRva = objObjectsRva;
    return true;
}

}  // namespace acr_ht::ue
