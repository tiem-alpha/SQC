#include "sqc/intervals.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace sqc {

namespace {

constexpr int64_t I64_MIN = std::numeric_limits<int64_t>::min();
constexpr int64_t I64_MAX = std::numeric_limits<int64_t>::max();

int64_t clamp_i64(__int128 v) {
    if (v < static_cast<__int128>(I64_MIN)) return I64_MIN;
    if (v > static_cast<__int128>(I64_MAX)) return I64_MAX;
    return static_cast<int64_t>(v);
}

int64_t clamp_to_bitwidth(__int128 v, uint32_t bitwidth) {
    if (bitwidth == 0) return 0;
    if (bitwidth >= 64) return clamp_i64(v);
    __int128 lo = -(static_cast<__int128>(1) << (bitwidth - 1));
    __int128 hi = (static_cast<__int128>(1) << (bitwidth - 1)) - 1;
    if (v < lo) return static_cast<int64_t>(lo);
    if (v > hi) return static_cast<int64_t>(hi);
    return static_cast<int64_t>(v);
}

int64_t all_ones_mask(int64_t v) {
    if (v <= 0) return 0;
    uint64_t u = static_cast<uint64_t>(v);
    u |= u >> 1;
    u |= u >> 2;
    u |= u >> 4;
    u |= u >> 8;
    u |= u >> 16;
    u |= u >> 32;
    if (u > static_cast<uint64_t>(I64_MAX)) return I64_MAX;
    return static_cast<int64_t>(u);
}

DisjointIntervals::CmpOp negate_cmp(DisjointIntervals::CmpOp op) {
    using CmpOp = DisjointIntervals::CmpOp;
    switch (op) {
        case CmpOp::EQ:  return CmpOp::NE;
        case CmpOp::NE:  return CmpOp::EQ;
        case CmpOp::SLT: return CmpOp::SGE;
        case CmpOp::SLE: return CmpOp::SGT;
        case CmpOp::SGT: return CmpOp::SLE;
        case CmpOp::SGE: return CmpOp::SLT;
        case CmpOp::ULT: return CmpOp::UGE;
        case CmpOp::ULE: return CmpOp::UGT;
        case CmpOp::UGT: return CmpOp::ULE;
        case CmpOp::UGE: return CmpOp::ULT;
    }
    return CmpOp::NE;
}

} // namespace

DisjointIntervals::DisjointIntervals(int64_t single_val) {
    ranges_.push_back({single_val, single_val});
}

DisjointIntervals::DisjointIntervals(int64_t lo, int64_t hi) {
    if (lo <= hi) {
        ranges_.push_back({lo, hi});
    }
}

DisjointIntervals::DisjointIntervals(std::vector<Range> ranges)
    : ranges_(std::move(ranges)) {
    normalize();
}

DisjointIntervals DisjointIntervals::empty() {
    return DisjointIntervals();
}

DisjointIntervals DisjointIntervals::full_i32() {
    return DisjointIntervals(MIN_VAL, MAX_VAL);
}

DisjointIntervals DisjointIntervals::full_i64(uint32_t bitwidth, bool is_signed) {
    if (bitwidth == 0) {
        return DisjointIntervals(0);
    }
    if (bitwidth >= 64) {
        if (is_signed) {
            return DisjointIntervals(I64_MIN, I64_MAX);
        }
        return DisjointIntervals(0, I64_MAX);
    }
    if (is_signed) {
        int64_t lo = -(1LL << (bitwidth - 1));
        int64_t hi = (1LL << (bitwidth - 1)) - 1;
        return DisjointIntervals(lo, hi);
    }
    if (bitwidth == 63) {
        return DisjointIntervals(0, I64_MAX);
    }
    int64_t hi = (1LL << bitwidth) - 1;
    return DisjointIntervals(0, hi);
}

DisjointIntervals DisjointIntervals::from_range(int64_t lo, int64_t hi) {
    return DisjointIntervals(lo, hi);
}

void DisjointIntervals::normalize() {
    if (ranges_.empty()) return;

    std::vector<Range> valid;
    valid.reserve(ranges_.size());
    for (const auto& r : ranges_) {
        if (r.first <= r.second) {
            valid.push_back(r);
        }
    }
    if (valid.empty()) {
        ranges_.clear();
        return;
    }

    std::sort(valid.begin(), valid.end(), [](const Range& a, const Range& b) {
        if (a.first != b.first) return a.first < b.first;
        return a.second < b.second;
    });

    std::vector<Range> merged;
    merged.reserve(valid.size());
    merged.push_back(valid[0]);
    for (size_t i = 1; i < valid.size(); ++i) {
        Range& back = merged.back();
        const Range& cur = valid[i];
        if (static_cast<__int128>(back.second) + 1 >= static_cast<__int128>(cur.first)) {
            if (cur.second > back.second) {
                back.second = cur.second;
            }
        } else {
            merged.push_back(cur);
        }
    }

    while (merged.size() > MAX_RANGES) {
        size_t best_idx = 0;
        __int128 best_gap = static_cast<__int128>(merged[1].first) -
                            static_cast<__int128>(merged[0].second);
        for (size_t i = 1; i + 1 < merged.size(); ++i) {
            __int128 gap = static_cast<__int128>(merged[i + 1].first) -
                           static_cast<__int128>(merged[i].second);
            if (gap < best_gap) {
                best_gap = gap;
                best_idx = i;
            }
        }
        merged[best_idx].second = merged[best_idx + 1].second;
        merged.erase(merged.begin() + static_cast<std::ptrdiff_t>(best_idx + 1));
    }

    ranges_ = std::move(merged);
}

bool DisjointIntervals::contains(int64_t v) const {
    for (const auto& r : ranges_) {
        if (v < r.first) return false;
        if (v <= r.second) return true;
    }
    return false;
}

bool DisjointIntervals::overlaps(const DisjointIntervals& other) const {
    size_t i = 0;
    size_t j = 0;
    while (i < ranges_.size() && j < other.ranges_.size()) {
        const auto& a = ranges_[i];
        const auto& b = other.ranges_[j];
        if (a.first <= b.second && b.first <= a.second) {
            return true;
        }
        if (a.second < b.second) {
            ++i;
        } else {
            ++j;
        }
    }
    return false;
}

bool DisjointIntervals::is_subset_of(const DisjointIntervals& other) const {
    if (ranges_.empty()) return true;
    if (other.ranges_.empty()) return false;

    size_t j = 0;
    for (const auto& a : ranges_) {
        while (j < other.ranges_.size() && other.ranges_[j].second < a.first) {
            ++j;
        }
        if (j == other.ranges_.size()) return false;
        const auto& b = other.ranges_[j];
        if (b.first > a.first || b.second < a.second) {
            return false;
        }
    }
    return true;
}

int64_t DisjointIntervals::sample_witness() const {
    if (ranges_.empty()) return 0;
    if (contains(0)) return 0;
    if (contains(1)) return 1;
    if (contains(-1)) return -1;

    for (const auto& r : ranges_) {
        if (r.first != MIN_VAL && r.first != I64_MIN) {
            return r.first;
        }
        if (r.second != MAX_VAL && r.second != I64_MAX) {
            return r.second;
        }
    }
    return ranges_.front().first;
}

DisjointIntervals DisjointIntervals::unite(const DisjointIntervals& other) const {
    if (ranges_.empty()) return other;
    if (other.ranges_.empty()) return *this;

    std::vector<Range> combined;
    combined.reserve(ranges_.size() + other.ranges_.size());
    combined.insert(combined.end(), ranges_.begin(), ranges_.end());
    combined.insert(combined.end(), other.ranges_.begin(), other.ranges_.end());
    return DisjointIntervals(std::move(combined));
}

DisjointIntervals DisjointIntervals::intersect(const DisjointIntervals& other) const {
    if (ranges_.empty() || other.ranges_.empty()) return empty();

    std::vector<Range> res;
    size_t i = 0;
    size_t j = 0;
    while (i < ranges_.size() && j < other.ranges_.size()) {
        int64_t lo = std::max(ranges_[i].first, other.ranges_[j].first);
        int64_t hi = std::min(ranges_[i].second, other.ranges_[j].second);
        if (lo <= hi) {
            res.push_back({lo, hi});
        }
        if (ranges_[i].second < other.ranges_[j].second) {
            ++i;
        } else {
            ++j;
        }
    }
    return DisjointIntervals(std::move(res));
}

DisjointIntervals DisjointIntervals::subtract(const DisjointIntervals& covered) const {
    if (ranges_.empty() || covered.ranges_.empty()) return *this;

    std::vector<Range> res;
    res.reserve(ranges_.size() + covered.ranges_.size());

    for (const auto& r : ranges_) {
        int64_t cur_lo = r.first;
        int64_t cur_hi = r.second;
        bool exhausted = false;

        for (const auto& c : covered.ranges_) {
            if (c.second < cur_lo) continue;
            if (c.first > cur_hi) break;

            if (c.first > cur_lo) {
                res.push_back({cur_lo, c.first - 1});
            }
            if (c.second >= cur_hi) {
                exhausted = true;
                break;
            }
            cur_lo = c.second + 1;
        }

        if (!exhausted && cur_lo <= cur_hi) {
            res.push_back({cur_lo, cur_hi});
        }
    }

    return DisjointIntervals(std::move(res));
}

DisjointIntervals DisjointIntervals::add(const DisjointIntervals& rhs) const {
    if (is_empty() || rhs.is_empty()) return empty();
    std::vector<Range> res;
    res.reserve(ranges_.size() * rhs.ranges_.size());
    for (const auto& a : ranges_) {
        for (const auto& b : rhs.ranges_) {
            int64_t lo = clamp_i64(static_cast<__int128>(a.first) + static_cast<__int128>(b.first));
            int64_t hi = clamp_i64(static_cast<__int128>(a.second) + static_cast<__int128>(b.second));
            res.push_back({lo, hi});
        }
    }
    return DisjointIntervals(std::move(res));
}

DisjointIntervals DisjointIntervals::sub(const DisjointIntervals& rhs) const {
    if (is_empty() || rhs.is_empty()) return empty();
    std::vector<Range> res;
    res.reserve(ranges_.size() * rhs.ranges_.size());
    for (const auto& a : ranges_) {
        for (const auto& b : rhs.ranges_) {
            int64_t lo = clamp_i64(static_cast<__int128>(a.first) - static_cast<__int128>(b.second));
            int64_t hi = clamp_i64(static_cast<__int128>(a.second) - static_cast<__int128>(b.first));
            res.push_back({lo, hi});
        }
    }
    return DisjointIntervals(std::move(res));
}

DisjointIntervals DisjointIntervals::mul(const DisjointIntervals& rhs) const {
    if (is_empty() || rhs.is_empty()) return empty();
    std::vector<Range> res;
    res.reserve(ranges_.size() * rhs.ranges_.size());
    for (const auto& a : ranges_) {
        for (const auto& b : rhs.ranges_) {
            __int128 p1 = static_cast<__int128>(a.first) * static_cast<__int128>(b.first);
            __int128 p2 = static_cast<__int128>(a.first) * static_cast<__int128>(b.second);
            __int128 p3 = static_cast<__int128>(a.second) * static_cast<__int128>(b.first);
            __int128 p4 = static_cast<__int128>(a.second) * static_cast<__int128>(b.second);
            __int128 lo = std::min({p1, p2, p3, p4});
            __int128 hi = std::max({p1, p2, p3, p4});
            res.push_back({clamp_i64(lo), clamp_i64(hi)});
        }
    }
    return DisjointIntervals(std::move(res));
}

DisjointIntervals DisjointIntervals::sdiv(const DisjointIntervals& rhs) const {
    if (is_empty() || rhs.is_empty()) return empty();
    DisjointIntervals nz = rhs.remove_value(0);
    if (nz.is_empty()) return empty();

    std::vector<Range> res;
    res.reserve(ranges_.size() * nz.ranges_.size());
    for (const auto& a : ranges_) {
        for (const auto& b : nz.ranges_) {
            __int128 q1 = static_cast<__int128>(a.first) / static_cast<__int128>(b.first);
            __int128 q2 = static_cast<__int128>(a.first) / static_cast<__int128>(b.second);
            __int128 q3 = static_cast<__int128>(a.second) / static_cast<__int128>(b.first);
            __int128 q4 = static_cast<__int128>(a.second) / static_cast<__int128>(b.second);
            __int128 lo = std::min({q1, q2, q3, q4});
            __int128 hi = std::max({q1, q2, q3, q4});
            res.push_back({clamp_i64(lo), clamp_i64(hi)});
        }
    }
    return DisjointIntervals(std::move(res));
}

DisjointIntervals DisjointIntervals::srem(const DisjointIntervals& rhs) const {
    if (is_empty() || rhs.is_empty()) return empty();
    DisjointIntervals nz = rhs.remove_value(0);
    if (nz.is_empty()) return empty();

    std::vector<Range> res;
    res.reserve(ranges_.size() * nz.ranges_.size());
    for (const auto& a : ranges_) {
        for (const auto& b : nz.ranges_) {
            if (a.first == a.second && b.first == b.second) {
                __int128 rem = static_cast<__int128>(a.first) % static_cast<__int128>(b.first);
                int64_t r = clamp_i64(rem);
                res.push_back({r, r});
                continue;
            }
            __int128 abs_b1 = b.first < 0 ? -static_cast<__int128>(b.first) : static_cast<__int128>(b.first);
            __int128 abs_b2 = b.second < 0 ? -static_cast<__int128>(b.second) : static_cast<__int128>(b.second);
            __int128 max_mag = std::max(abs_b1, abs_b2) - 1;
            if (max_mag < 0) max_mag = 0;

            __int128 lo = 0;
            __int128 hi = 0;
            if (a.second < 0) {
                __int128 neg_bound = std::min(max_mag, -static_cast<__int128>(a.first));
                lo = -neg_bound;
                hi = 0;
            } else if (a.first >= 0) {
                lo = 0;
                hi = std::min(max_mag, static_cast<__int128>(a.second));
            } else {
                __int128 neg_bound = std::min(max_mag, -static_cast<__int128>(a.first));
                __int128 pos_bound = std::min(max_mag, static_cast<__int128>(a.second));
                lo = -neg_bound;
                hi = pos_bound;
            }
            res.push_back({clamp_i64(lo), clamp_i64(hi)});
        }
    }
    return DisjointIntervals(std::move(res));
}

DisjointIntervals DisjointIntervals::bitwise_and(const DisjointIntervals& rhs) const {
    if (is_empty() || rhs.is_empty()) return empty();
    if (is_singleton() && rhs.is_singleton()) {
        return DisjointIntervals(singleton_value() & rhs.singleton_value());
    }

    std::vector<Range> res;
    for (const auto& a : ranges_) {
        for (const auto& b : rhs.ranges_) {
            if (a.first == a.second && b.first == b.second) {
                int64_t v = a.first & b.first;
                res.push_back({v, v});
            } else if (a.first >= 0 && b.first >= 0) {
                res.push_back({0, std::min(a.second, b.second)});
            } else if (a.first >= 0) {
                res.push_back({0, a.second});
            } else if (b.first >= 0) {
                res.push_back({0, b.second});
            } else {
                res.push_back({MIN_VAL, MAX_VAL});
            }
        }
    }
    return DisjointIntervals(std::move(res));
}

DisjointIntervals DisjointIntervals::bitwise_or(const DisjointIntervals& rhs) const {
    if (is_empty() || rhs.is_empty()) return empty();
    if (is_singleton() && rhs.is_singleton()) {
        return DisjointIntervals(singleton_value() | rhs.singleton_value());
    }

    std::vector<Range> res;
    for (const auto& a : ranges_) {
        for (const auto& b : rhs.ranges_) {
            if (a.first == a.second && b.first == b.second) {
                int64_t v = a.first | b.first;
                res.push_back({v, v});
            } else if (a.first >= 0 && b.first >= 0) {
                int64_t lo = std::max(a.first, b.first);
                int64_t hi = all_ones_mask(std::max(a.second, b.second));
                res.push_back({lo, std::max(lo, hi)});
            } else {
                res.push_back({MIN_VAL, MAX_VAL});
            }
        }
    }
    return DisjointIntervals(std::move(res));
}

DisjointIntervals DisjointIntervals::bitwise_xor(const DisjointIntervals& rhs) const {
    if (is_empty() || rhs.is_empty()) return empty();
    if (is_singleton() && rhs.is_singleton()) {
        return DisjointIntervals(singleton_value() ^ rhs.singleton_value());
    }

    std::vector<Range> res;
    for (const auto& a : ranges_) {
        for (const auto& b : rhs.ranges_) {
            if (a.first == a.second && b.first == b.second) {
                int64_t v = a.first ^ b.first;
                res.push_back({v, v});
            } else if (a.first >= 0 && b.first >= 0) {
                int64_t hi = all_ones_mask(std::max(a.second, b.second));
                res.push_back({0, hi});
            } else {
                res.push_back({MIN_VAL, MAX_VAL});
            }
        }
    }
    return DisjointIntervals(std::move(res));
}

DisjointIntervals DisjointIntervals::shl(const DisjointIntervals& rhs, uint32_t bitwidth) const {
    if (is_empty() || rhs.is_empty()) return empty();
    if (bitwidth == 0) return DisjointIntervals(0);

    int64_t max_shift = static_cast<int64_t>(std::min<uint32_t>(bitwidth - 1, 62));
    DisjointIntervals valid_rhs = rhs.intersect(DisjointIntervals(0, max_shift));
    if (valid_rhs.is_empty()) return empty();

    std::vector<Range> res;
    for (const auto& a : ranges_) {
        for (const auto& b : valid_rhs.ranges_) {
            __int128 mult_lo = static_cast<__int128>(1) << static_cast<unsigned>(b.first);
            __int128 mult_hi = static_cast<__int128>(1) << static_cast<unsigned>(b.second);
            __int128 p1 = static_cast<__int128>(a.first) * mult_lo;
            __int128 p2 = static_cast<__int128>(a.first) * mult_hi;
            __int128 p3 = static_cast<__int128>(a.second) * mult_lo;
            __int128 p4 = static_cast<__int128>(a.second) * mult_hi;
            __int128 lo = std::min({p1, p2, p3, p4});
            __int128 hi = std::max({p1, p2, p3, p4});
            res.push_back({clamp_to_bitwidth(lo, bitwidth), clamp_to_bitwidth(hi, bitwidth)});
        }
    }
    return DisjointIntervals(std::move(res));
}

DisjointIntervals DisjointIntervals::ashr(const DisjointIntervals& rhs, uint32_t bitwidth) const {
    if (is_empty() || rhs.is_empty()) return empty();
    if (bitwidth == 0) return DisjointIntervals(0);

    int64_t max_shift = static_cast<int64_t>(std::min<uint32_t>(bitwidth - 1, 63));
    DisjointIntervals valid_rhs = rhs.intersect(DisjointIntervals(0, max_shift));
    if (valid_rhs.is_empty()) return empty();

    std::vector<Range> res;
    for (const auto& a : ranges_) {
        for (const auto& b : valid_rhs.ranges_) {
            int64_t r1 = a.first >> static_cast<unsigned>(b.first);
            int64_t r2 = a.first >> static_cast<unsigned>(b.second);
            int64_t r3 = a.second >> static_cast<unsigned>(b.first);
            int64_t r4 = a.second >> static_cast<unsigned>(b.second);
            int64_t lo = std::min({r1, r2, r3, r4});
            int64_t hi = std::max({r1, r2, r3, r4});
            res.push_back({lo, hi});
        }
    }
    return DisjointIntervals(std::move(res));
}

DisjointIntervals DisjointIntervals::filter_by_cmp(CmpOp op, const DisjointIntervals& rhs) const {
    if (is_empty() || rhs.is_empty()) return empty();

    int64_t lo_bound = std::min(MIN_VAL, min_value());
    int64_t hi_bound = std::max(MAX_VAL, max_value());

    switch (op) {
        case CmpOp::EQ:
            return intersect(rhs);
        case CmpOp::NE:
            if (rhs.is_singleton()) {
                return subtract(rhs);
            }
            return *this;
        case CmpOp::SLT: {
            int64_t rhs_max = rhs.max_value();
            if (rhs_max == I64_MIN) return empty();
            return intersect(DisjointIntervals(lo_bound, rhs_max - 1));
        }
        case CmpOp::ULT: {
            DisjointIntervals rhs_neg = rhs.intersect(DisjointIntervals(lo_bound, -1));
            if (!rhs_neg.is_empty()) {
                int64_t rmax = rhs_neg.max_value();
                DisjointIntervals non_neg = intersect(DisjointIntervals(0, hi_bound));
                if (rmax <= lo_bound) return non_neg;
                return non_neg.unite(intersect(DisjointIntervals(lo_bound, rmax - 1)));
            }
            int64_t rhs_max = rhs.max_value();
            if (rhs_max <= 0) return empty();
            return intersect(DisjointIntervals(0, rhs_max - 1));
        }
        case CmpOp::SLE:
            return intersect(DisjointIntervals(lo_bound, rhs.max_value()));
        case CmpOp::ULE: {
            DisjointIntervals rhs_neg = rhs.intersect(DisjointIntervals(lo_bound, -1));
            if (!rhs_neg.is_empty()) {
                int64_t rmax = rhs_neg.max_value();
                return intersect(DisjointIntervals(0, hi_bound))
                    .unite(intersect(DisjointIntervals(lo_bound, rmax)));
            }
            int64_t rhs_max = rhs.max_value();
            if (rhs_max < 0) return empty();
            return intersect(DisjointIntervals(0, rhs_max));
        }
        case CmpOp::SGT: {
            int64_t rhs_min = rhs.min_value();
            if (rhs_min == I64_MAX) return empty();
            return intersect(DisjointIntervals(rhs_min + 1, hi_bound));
        }
        case CmpOp::UGT: {
            DisjointIntervals rhs_non_neg = rhs.intersect(DisjointIntervals(0, hi_bound));
            if (!rhs_non_neg.is_empty()) {
                int64_t u_min = rhs_non_neg.min_value();
                return subtract(DisjointIntervals(0, u_min));
            }
            int64_t rmin = rhs.min_value();
            if (rmin >= -1) return empty();
            return intersect(DisjointIntervals(rmin + 1, -1));
        }
        case CmpOp::SGE:
            return intersect(DisjointIntervals(rhs.min_value(), hi_bound));
        case CmpOp::UGE: {
            DisjointIntervals rhs_non_neg = rhs.intersect(DisjointIntervals(0, hi_bound));
            if (!rhs_non_neg.is_empty()) {
                int64_t u_min = rhs_non_neg.min_value();
                if (u_min <= 0) return *this;
                return subtract(DisjointIntervals(0, u_min - 1));
            }
            int64_t rmin = rhs.min_value();
            return intersect(DisjointIntervals(rmin, -1));
        }
    }
    return *this;
}

DisjointIntervals::CmpOutcome DisjointIntervals::eval_cmp(CmpOp op, const DisjointIntervals& rhs) const {
    CmpOutcome out;
    if (is_empty() || rhs.is_empty()) {
        return out;
    }

    if (op == CmpOp::EQ) {
        out.can_be_true = overlaps(rhs);
        out.can_be_false = !(is_singleton() && rhs.is_singleton() &&
                             singleton_value() == rhs.singleton_value());
        return out;
    }
    if (op == CmpOp::NE) {
        out.can_be_true = !(is_singleton() && rhs.is_singleton() &&
                            singleton_value() == rhs.singleton_value());
        out.can_be_false = overlaps(rhs);
        return out;
    }

    out.can_be_true = !filter_by_cmp(op, rhs).is_empty();
    out.can_be_false = !filter_by_cmp(negate_cmp(op), rhs).is_empty();
    return out;
}

std::string DisjointIntervals::to_string() const {
    if (ranges_.empty()) return "{}";
    std::ostringstream oss;
    for (size_t i = 0; i < ranges_.size(); ++i) {
        if (i > 0) oss << " U ";
        if (ranges_[i].first == ranges_[i].second) {
            oss << "[" << ranges_[i].first << "]";
        } else {
            oss << "[" << ranges_[i].first << ", " << ranges_[i].second << "]";
        }
    }
    return oss.str();
}

} // namespace sqc
