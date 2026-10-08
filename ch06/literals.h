// Ch. 6 -- user-defined literal.
#pragma once

#include "lob/types.h"
#include <limits>
#include <stdexcept>

namespace lob::literals {

// Stage 5. `10005_ticks` is a Price of 10005 ticks.
//
// Decide, and write down in NOTES.md: what should happen for a literal too
// large to fit in a Price? (unsigned long long can hold values that int64_t
// cannot.) A constexpr function that throws can never be evaluated at
// compile time on that path -- which might be exactly what you want.
constexpr Price operator""_ticks(unsigned long long n) {
    if (n > static_cast<unsigned long long>(std::numeric_limits<Price>::max())) {
        throw std::out_of_range{"_ticks: literal too large for Price"};
    }
    return static_cast<Price>(n);
}

}  // namespace lob::literals