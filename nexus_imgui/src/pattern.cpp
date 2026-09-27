// ============================================================================
//  pattern.cpp — implementation of the IDA-style signature scanner.
// ============================================================================
#include "pattern.h"

#include <cctype>
#include <cstring>

namespace game {
namespace pattern {
namespace {

inline bool IsHex(int c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }

inline int HexVal(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

} // namespace

bool Compile(const char* ida, Signature& out)
{
    out.bytes.clear();
    out.wild.clear();
    if (!ida)
        return false;

    const char* p = ida;
    while (*p) {
        while (*p && std::isspace(static_cast<unsigned char>(*p)))
            ++p;
        if (!*p)
            break;

        if (*p == '?') {
            ++p;
            if (*p == '?')
                ++p;
            out.bytes.push_back(0);
            out.wild.push_back(1);
            continue;
        }

        if (!IsHex(p[0]) || !IsHex(p[1]))
            return false; // malformed token

        out.bytes.push_back(static_cast<std::uint8_t>((HexVal(p[0]) << 4) |
                                                      HexVal(p[1])));
        out.wild.push_back(0);
        p += 2;
    }

    return !out.bytes.empty();
}

std::uintptr_t Find(const void* base, std::size_t size, const Signature& sig)
{
    const std::size_t n = sig.size();
    if (!base || n == 0 || size < n)
        return 0;

    const std::uint8_t* data  = static_cast<const std::uint8_t*>(base);
    const std::uint8_t* first = sig.bytes.data();
    const std::uint8_t* wild  = sig.wild.data();
    const std::size_t   last  = size - n;

    for (std::size_t i = 0; i <= last; ++i) {
        // cheap first-byte filter before the full comparison
        if (!wild[0] && data[i] != first[0])
            continue;

        std::size_t j = 0;
        for (; j < n; ++j) {
            if (wild[j])
                continue;
            if (data[i + j] != first[j])
                break;
        }
        if (j == n)
            return reinterpret_cast<std::uintptr_t>(data + i);
    }
    return 0;
}

std::uintptr_t Find(std::uintptr_t base, std::size_t size, const char* ida)
{
    Signature sig;
    if (!Compile(ida, sig))
        return 0;
    return Find(reinterpret_cast<const void*>(base), size, sig);
}

std::uintptr_t FindRip(std::uintptr_t base, std::size_t size, const char* ida,
                       int dispRel, int instrLen)
{
    Signature sig;
    if (!Compile(ida, sig))
        return 0;

    const std::uintptr_t match =
        Find(reinterpret_cast<const void*>(base), size, sig);
    if (!match)
        return 0;

    // the whole instruction (and therefore the displacement) has to be inside
    // the scanned range
    const std::size_t need = static_cast<std::size_t>(instrLen);
    if (instrLen < dispRel + 4 || (match - base) + need > size)
        return 0;

    std::int32_t disp = 0;
    std::memcpy(&disp, reinterpret_cast<const void*>(match + dispRel),
                sizeof(disp));

    return match + need + static_cast<std::uintptr_t>(
                              static_cast<std::intptr_t>(disp));
}

} // namespace pattern
} // namespace game
