#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <utility>
#include <limits>

namespace sqc {

/**
 * @brief Union of up to MAX_DISJOINT_RANGES sorted, non-overlapping closed intervals [lo, hi].
 * Enables O(1) hazard splitting (e.g., removing 0 from [-100, 100] -> [-100, -1] U [1, 100])
 * and exact remainder slicing for GraphEquivalenceCache.
 */
class DisjointIntervals {
public:
    static constexpr size_t MAX_RANGES = 8;
    static constexpr int64_t MIN_VAL = -2147483648LL;
    static constexpr int64_t MAX_VAL = 2147483647LL;

    using Range = std::pair<int64_t, int64_t>;

    DisjointIntervals() = default;
    explicit DisjointIntervals(int64_t single_val);
    DisjointIntervals(int64_t lo, int64_t hi);
    explicit DisjointIntervals(std::vector<Range> ranges);

    static DisjointIntervals empty();
    static DisjointIntervals full_i32();
    static DisjointIntervals full_i64(uint32_t bitwidth = 32, bool is_signed = true);
    static DisjointIntervals from_range(int64_t lo, int64_t hi);

    bool is_empty() const { return ranges_.empty(); }
    bool is_singleton() const { return ranges_.size() == 1 && ranges_[0].first == ranges_[0].second; }
    int64_t singleton_value() const { return ranges_.empty() ? 0 : ranges_[0].first; }
    int64_t min_value() const { return ranges_.empty() ? 0 : ranges_.front().first; }
    int64_t max_value() const { return ranges_.empty() ? -1 : ranges_.back().second; }
    const std::vector<Range>& ranges() const { return ranges_; }

    bool contains(int64_t v) const;
    bool overlaps(const DisjointIntervals& other) const;
    bool is_subset_of(const DisjointIntervals& other) const;
    int64_t sample_witness() const;

    // Set operations
    DisjointIntervals unite(const DisjointIntervals& other) const;
    DisjointIntervals intersect(const DisjointIntervals& other) const;
    DisjointIntervals subtract(const DisjointIntervals& covered) const;
    DisjointIntervals remove_value(int64_t v) const { return subtract(DisjointIntervals(v)); }

    // Arithmetic abstract transformers
    DisjointIntervals add(const DisjointIntervals& rhs) const;
    DisjointIntervals sub(const DisjointIntervals& rhs) const;
    DisjointIntervals mul(const DisjointIntervals& rhs) const;
    DisjointIntervals sdiv(const DisjointIntervals& rhs) const;
    DisjointIntervals srem(const DisjointIntervals& rhs) const;
    DisjointIntervals bitwise_and(const DisjointIntervals& rhs) const;
    DisjointIntervals bitwise_or(const DisjointIntervals& rhs) const;
    DisjointIntervals bitwise_xor(const DisjointIntervals& rhs) const;
    DisjointIntervals shl(const DisjointIntervals& rhs, uint32_t bitwidth = 32) const;
    DisjointIntervals ashr(const DisjointIntervals& rhs, uint32_t bitwidth = 32) const;

    // Relational filtering: returns subset of *this satisfying (*this <op> rhs)
    enum class CmpOp { EQ, NE, SLT, SLE, SGT, SGE, ULT, ULE, UGT, UGE };
    DisjointIntervals filter_by_cmp(CmpOp op, const DisjointIntervals& rhs) const;

    // Evaluate whether (*this <op> rhs) can be True and/or False
    struct CmpOutcome {
        bool can_be_true = false;
        bool can_be_false = false;
    };
    CmpOutcome eval_cmp(CmpOp op, const DisjointIntervals& rhs) const;

    std::string to_string() const;
    bool operator==(const DisjointIntervals& o) const { return ranges_ == o.ranges_; }
    bool operator!=(const DisjointIntervals& o) const { return ranges_ != o.ranges_; }

private:
    std::vector<Range> ranges_;
    void normalize();
};

} // namespace sqc
