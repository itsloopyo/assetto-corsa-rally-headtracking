#include "ue/ue_properties.h"

#include "logging.h"

#include <cameraunlock/unreal/ue_runtime.h>

namespace acr_ht::ue {
namespace {

namespace cu = cameraunlock::unreal;

// UE 4.25+ moved properties out of UObject into the lighter FField hierarchy.
// These are engine-version layout - the same kind of constant as the UObject
// offsets in ue_globals.cpp - and they were read off the live process rather
// than taken from a dumper's table, because the widely published values put
// Next and NamePrivate 8 bytes late for this build and decoded fragments of
// the word "Property" as property names.
//
// FField:     vtable(0) ClassPrivate(8) Owner(0x10) Next(0x18) NamePrivate(0x20)
// FProperty:  ArrayDim(0x30) ElementSize(0x34) PropertyFlags(0x38) Offset(0x44)
// FStructProperty: Struct(0x70)
constexpr std::size_t kUStruct_SuperStruct     = 0x40;
constexpr std::size_t kUStruct_ChildProperties = 0x50;
constexpr std::size_t kUStruct_PropertiesSize  = 0x58;

constexpr std::size_t kFField_Next        = 0x18;
constexpr std::size_t kFField_NamePrivate = 0x20;

constexpr std::size_t kFProperty_ArrayDim        = 0x30;
constexpr std::size_t kFProperty_ElementSize     = 0x34;
constexpr std::size_t kFProperty_Offset_Internal = 0x44;

constexpr std::size_t kFStructProperty_Struct = 0x70;

// A class chain is a handful of links and a struct has tens of properties;
// these only stop a corrupted pointer turning into an unbounded walk.
constexpr int kMaxSupers    = 32;
constexpr int kMaxPerStruct = 512;

bool ReadProperty(std::uintptr_t field, std::uint32_t propertiesSize, PropertyInfo& out) {
    std::uint32_t nameId = 0;
    if (!cu::SafeReadU32(field + kFField_NamePrivate, nameId)) return false;
    std::string name = cu::ResolveFName(nameId);
    if (name.empty()) return false;

    std::uint32_t offset = 0;
    std::uint32_t elementSize = 0;
    if (!cu::SafeReadU32(field + kFProperty_Offset_Internal, offset)) return false;
    if (!cu::SafeReadU32(field + kFProperty_ElementSize, elementSize)) return false;

    // A property has to fit ENTIRELY inside the struct it belongs to. This is
    // the guard between a wrong layout constant and a write through a
    // confident-looking offset, so it checks the whole extent rather than the
    // start: a 24-byte Location reflecting at PropertiesSize-8 would otherwise
    // pass and put 16 of those bytes into the neighbouring field.
    //
    // A struct whose size could not be read is refused outright. Treating an
    // unreadable size as "no opinion" made this fail open in exactly the case
    // it exists for - a layout wrong enough that PropertiesSize does not read.
    std::uint32_t arrayDim = 0;
    if (!cu::SafeReadU32(field + kFProperty_ArrayDim, arrayDim)) return false;
    if (arrayDim == 0) return false;
    if (propertiesSize == 0) return false;
    const std::uint64_t extent = static_cast<std::uint64_t>(offset) +
                                 static_cast<std::uint64_t>(elementSize) * arrayDim;
    if (extent > propertiesSize) return false;

    out.name = std::move(name);
    out.offset = offset;
    out.elementSize = static_cast<std::int32_t>(elementSize);
    out.field = field;
    return true;
}

// Calls visit(owner, property) for every reflected property on
// `structOrClass` and each of its bases. visit returns true to stop.
template <typename Fn>
void ForEachProperty(std::uintptr_t structOrClass, Fn&& visit) {
    std::uintptr_t current = structOrClass;
    for (int depth = 0; depth < kMaxSupers && current; ++depth) {
        std::uint32_t propertiesSize = 0;
        if (!cu::SafeReadU32(current + kUStruct_PropertiesSize, propertiesSize)) return;

        std::uintptr_t field = 0;
        if (cu::SafeReadPtr(current + kUStruct_ChildProperties, field)) {
            for (int i = 0; i < kMaxPerStruct && field; ++i) {
                PropertyInfo info{};
                if (ReadProperty(field, propertiesSize, info) && visit(current, info)) return;
                if (!cu::SafeReadPtr(field + kFField_Next, field)) break;
            }
        }

        std::uintptr_t super = 0;
        if (!cu::SafeReadPtr(current + kUStruct_SuperStruct, super)) break;
        current = super;
    }
}

}  // namespace

bool FindProperty(std::uintptr_t structOrClass, const char* name, PropertyInfo& out) {
    bool found = false;
    ForEachProperty(structOrClass, [&](std::uintptr_t, const PropertyInfo& info) {
        if (info.name != name) return false;
        out = info;
        found = true;
        return true;
    });
    return found;
}

std::uintptr_t StructOfProperty(const PropertyInfo& prop) {
    if (!prop.field) return 0;
    std::uintptr_t inner = 0;
    if (!cu::SafeReadPtr(prop.field + kFStructProperty_Struct, inner) || !inner) return 0;
    // Only an FStructProperty has a UScriptStruct there; on any other property
    // that slot holds something else entirely, and following it would be the
    // same class of mistake this whole module exists to avoid.
    if (cu::ClassName(inner) != "ScriptStruct") return 0;
    return inner;
}

bool DerivesFrom(std::uintptr_t structOrClass, const char* name) {
    std::uintptr_t current = structOrClass;
    for (int depth = 0; depth < kMaxSupers && current; ++depth) {
        if (cu::ObjectName(current) == name) return true;
        std::uintptr_t super = 0;
        if (!cu::SafeReadPtr(current + kUStruct_SuperStruct, super)) break;
        current = super;
    }
    return false;
}

void LogProperties(std::uintptr_t structOrClass) {
    int count = 0;
    std::uintptr_t lastOwner = 0;
    ForEachProperty(structOrClass, [&](std::uintptr_t owner, const PropertyInfo& info) {
        if (owner != lastOwner) {
            lastOwner = owner;
            Log::Line("[props] %s:", cu::ObjectName(owner).c_str());
        }
        Log::Line("[props]   +0x%04zX  size %-5d  %s", info.offset, info.elementSize,
                  info.name.c_str());
        ++count;
        return false;
    });
    Log::Line("[props] %d reflected properties on %s and its bases", count,
              cu::ObjectName(structOrClass).c_str());
}

}  // namespace acr_ht::ue
