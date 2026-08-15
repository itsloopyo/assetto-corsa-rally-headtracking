#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace acr_ht::ue {

// Field offsets read out of UE's own reflection rather than deduced from what
// the memory happens to look like.
//
// The camera cache offset was originally inferred from a shape scan - two
// FMinimalViewInfo-looking structs exactly one struct-size apart. That is a
// good hypothesis and a terrible thing to write through: it landed 0x1260
// bytes early, inside AActor, where writing back the same bytes read fine and
// writing a head-tracked pose corrupted the actor and took the game down a
// second later. UE marks all of these members UPROPERTY, so the engine knows
// their real offsets and can simply be asked.

struct PropertyInfo {
    std::string  name;
    std::size_t  offset = 0;
    std::int32_t elementSize = 0;
    // The FField itself, so a struct property can be followed into the struct
    // it holds without a second lookup.
    std::uintptr_t field = 0;
};

// Looks up a property by name on a UClass or UScriptStruct, walking up through
// SuperStruct. Returns false if reflection could not be decoded or the name is
// not there - never a guess.
bool FindProperty(std::uintptr_t structOrClass, const char* name, PropertyInfo& out);

// For a struct-typed property, the UScriptStruct it holds - which can be
// walked with FindProperty in turn. Returns 0 if `prop` is not a struct
// property, which is what stops a rename or a type change from being followed
// into unrelated memory.
std::uintptr_t StructOfProperty(const PropertyInfo& prop);

// Every reflected property on `structOrClass` and its bases, to the log.
void LogProperties(std::uintptr_t structOrClass);

// True if `structOrClass` or any of its bases is named `name`. The way to ask
// "is this a car" about an object whose own class is a per-vehicle blueprint:
// the blueprint name changes with every car, the native base it derives from
// does not.
bool DerivesFrom(std::uintptr_t structOrClass, const char* name);

}  // namespace acr_ht::ue
