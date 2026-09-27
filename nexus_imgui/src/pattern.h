// ============================================================================
//  pattern.h — tiny IDA-style signature scanner.
//
//  Notation: space separated hex bytes, "??" (or "?") is a wildcard:
//      "48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 50 ??"
//
//  Both functions work on a raw [base, base+size) range, so they are fully
//  testable against a fake in-memory buffer (see tools/logic_test.cpp).
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace game {
namespace pattern {

// Compiled signature: one byte per token, plus a parallel wildcard flag
// (std::vector<std::uint8_t>, not <bool>: we need a contiguous buffer).
struct Signature {
    std::vector<std::uint8_t> bytes;
    std::vector<std::uint8_t> wild;

    bool   empty() const { return bytes.empty(); }
    size_t size() const { return bytes.size(); }
};

// Parses an IDA-style string. Returns false when the string is malformed.
bool Compile(const char* ida, Signature& out);

// First match of 'ida' inside [base, base+size), 0 when not found.
std::uintptr_t Find(std::uintptr_t base, std::size_t size, const char* ida);

// Same, but takes a pre-compiled signature (avoids re-parsing every call).
std::uintptr_t Find(const void* base, std::size_t size, const Signature& sig);

// RIP-relative helper: the match is an instruction of 'instrLen' bytes whose
// 32-bit displacement lives at 'dispRel'. The returned address is the place
// the instruction points at:
//      addr = match + instrLen + (int32_t)*(match + dispRel)
// Returns 0 when the pattern is not found.
//
// Defaults match "48 8B 0D ?? ?? ?? ??" (mov rcx, [rip+disp32]).
std::uintptr_t FindRip(std::uintptr_t base, std::size_t size, const char* ida,
                       int dispRel = 3, int instrLen = 7);

} // namespace pattern
} // namespace game
