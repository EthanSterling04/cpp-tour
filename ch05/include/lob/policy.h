#pragma once

#include "lob/level.h"
#include "lob/types.h"

namespace lob {

class MatchingPolicy {
public:
    virtual ~MatchingPolicy() = default;
    virtual Fill allocate(Qty incoming, PriceLevel& level) = 0;
protected:
    MatchingPolicy() = default;
    MatchingPolicy(const MatchingPolicy&) = default;
    MatchingPolicy& operator=(const MatchingPolicy&) = default;
};

// Time priority: the oldest order is filled completely before the next
// order gets anything.
class FifoPolicy final : public MatchingPolicy {
public:
    Fill allocate(Qty incoming, PriceLevel& level) override;
};

// Size priority: each order gets a share proportional to its quantity,
// rounded down. The units lost to rounding go one each to the oldest orders
// with room left. (Real pro-rata venues add rules such as minimum
// allocation sizes; this keeps only the rounding rule.)
class ProRataPolicy final : public MatchingPolicy {
public:
    Fill allocate(Qty incoming, PriceLevel& level) override;
};

}  // namespace lob