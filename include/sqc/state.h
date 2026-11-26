#pragma once

#include "sqc/types.h"
#include "sqc/intervals.h"
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace llvm {
class Value;
class Function;
class BasicBlock;
class Module;
} // namespace llvm

namespace sqc {

struct PointerTarget {
    RegionId region_id = 0;
    DisjointIntervals offset = DisjointIntervals(0);
    ValueId sym_index_id = 0;

    bool operator==(const PointerTarget& o) const {
        return region_id == o.region_id && offset == o.offset;
    }
};

/**
 * @brief Linear symbolic expression of the form: param_id * coeff + offset
 * Used for backward hazard condition propagation (e.g., y = *a + 10; 100 / (y - 25) -> *a == 15).
 */
struct LinearSymExpr {
    bool is_valid = false;
    ValueId root_param_id = 0;
    std::string root_param_name;
    int64_t coeff = 1;
    int64_t addend = 0;

    // Given target value T for this expression (coeff * alpha + addend == T), solve for alpha
    std::optional<int64_t> solve_for_root(int64_t target_val) const {
        if (!is_valid || coeff == 0) return std::nullopt;
        int64_t diff = target_val - addend;
        if (diff % coeff != 0) return std::nullopt;
        return diff / coeff;
    }

    // Invert interval [lo, hi] back to root parameter interval
    DisjointIntervals invert_range(const DisjointIntervals& target_range) const;
};

struct AbstractValue {
    Nullability null_state = Nullability::MustNonNull;
    std::vector<PointerTarget> ptr_targets;
    std::vector<const llvm::Function*> func_targets;
    DisjointIntervals int_domain = DisjointIntervals::full_i32();
    LinearSymExpr sym_expr;
    bool is_pointer = false;
    bool is_initialized = true;

    static AbstractValue make_int(const DisjointIntervals& dom, bool init = true);
    static AbstractValue make_null_ptr();
    static AbstractValue make_valid_ptr(RegionId reg, DisjointIntervals off = DisjointIntervals(0),
                                        Nullability ns = Nullability::MustNonNull);

    bool may_be_null() const {
        return null_state == Nullability::MustNull || null_state == Nullability::MayNull;
    }
    bool must_be_null() const {
        return null_state == Nullability::MustNull;
    }

    AbstractValue join(const AbstractValue& other) const;
    bool is_subset_of(const AbstractValue& other) const;
    bool operator==(const AbstractValue& o) const;
    std::string to_string() const;
};

struct MemoryRegion {
    RegionId id = 0;
    std::string name;
    DisjointIntervals byte_size = DisjointIntervals(4);
    RegionLiveness liveness = RegionLiveness::Alive;
    bool is_heap = false;
    bool is_global = false;
    bool is_stack = false;
    bool is_symbolic_input = false;
    bool default_initialized = true;
    RegionId cached_elem_pointee_rid = 0;
    SourceLoc alloc_loc;

    // Range of first '\0' byte offset for C-string safety checks (strlen/sscanf/strcpy)
    DisjointIntervals null_term_offset = DisjointIntervals(0, DisjointIntervals::MAX_VAL);

    // Byte offset -> stored AbstractValue (coarse/field-level map)
    std::unordered_map<int64_t, AbstractValue> field_values;
    AbstractValue default_element_value = AbstractValue::make_int(DisjointIntervals::full_i32(), true);
};

class AbstractState {
public:
    using RegionPtr = std::shared_ptr<MemoryRegion>;
    using RegionMap = std::unordered_map<RegionId, RegionPtr>;

    AbstractState() = default;

    RegionId allocate_region(const std::string& name, DisjointIntervals size,
                             bool is_heap, bool is_global, bool is_stack,
                             bool initialized, SourceLoc loc = {});

    void set_value(ValueId vid, const AbstractValue& val);
    AbstractValue get_value(ValueId vid) const;
    bool has_value(ValueId vid) const;

    MemoryRegion* get_region(RegionId rid);
    const MemoryRegion* get_region(RegionId rid) const;

    void store_to_region(RegionId rid, const DisjointIntervals& offset, const AbstractValue& val);
    AbstractValue load_from_region(RegionId rid, const DisjointIntervals& offset, uint32_t byte_width = 4) const;

    void merge_regions_from(const AbstractState& other, bool skip_stack = false);

    void mark_indexed_nonnull(RegionId rid, ValueId idx_vid);
    bool is_indexed_nonnull(RegionId rid, ValueId idx_vid) const;
    void invalidate_index_var(ValueId idx_vid);

    // Liveness projection: keep only variables in live_vars (+ regions reachable from them and globals)
    AbstractState project_live(const std::unordered_set<ValueId>& live_vars) const;

    // Lattice operations
    bool is_subset_of(const AbstractState& other) const;
    AbstractState join(const AbstractState& other) const;

    // Fingerprint for fast equivalence lookup
    uint64_t compute_hash(const std::vector<ValueId>& relevant_keys) const;

    const std::unordered_map<ValueId, AbstractValue>& values() const { return values_; }
    const RegionMap& regions() const;

private:
    RegionMap& ensure_regions_unique();

    std::unordered_map<ValueId, AbstractValue> values_;
    std::shared_ptr<RegionMap> regions_;
    std::set<std::pair<RegionId, ValueId>> indexed_nonnull_;
    RegionId next_region_id_ = 1;
};

/**
 * @brief Pre-pass computing LiveIn sets for BasicBlocks and ReadSet/WriteSet for Functions.
 */
class LivenessAndModRefPass {
public:
    void run(const llvm::Module& mod);

    const std::unordered_set<ValueId>& live_in(const llvm::BasicBlock* bb) const;
    const std::unordered_set<ValueId>& func_read_globals(const llvm::Function* fn) const;
    const std::unordered_set<ValueId>& func_write_globals(const llvm::Function* fn) const;
    bool function_is_pure_or_readonly(const llvm::Function* fn) const;

private:
    std::unordered_map<const llvm::BasicBlock*, std::unordered_set<ValueId>> bb_live_in_;
    std::unordered_map<const llvm::Function*, std::unordered_set<ValueId>> fn_read_globals_;
    std::unordered_map<const llvm::Function*, std::unordered_set<ValueId>> fn_write_globals_;
    std::unordered_set<ValueId> empty_set_;
};

} // namespace sqc
