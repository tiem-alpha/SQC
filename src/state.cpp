#include "sqc/state.h"

#include <atomic>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Operator.h>
#include <llvm/Support/Casting.h>

#include <algorithm>
#include <limits>
#include <queue>
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

__int128 floor_div_pos(__int128 a, __int128 b) {
    __int128 q = a / b;
    __int128 r = a % b;
    if (r != 0 && a < 0) q -= 1;
    return q;
}

__int128 ceil_div_pos(__int128 a, __int128 b) {
    __int128 q = a / b;
    __int128 r = a % b;
    if (r != 0 && a > 0) q += 1;
    return q;
}

uint64_t fnv1a_mix(uint64_t h, uint64_t data) {
    constexpr uint64_t FNV_PRIME = 1099511628211ULL;
    for (int i = 0; i < 8; ++i) {
        h ^= (data >> (i * 8)) & 0xFFULL;
        h *= FNV_PRIME;
    }
    return h;
}

std::string nullability_to_string(Nullability n) {
    switch (n) {
        case Nullability::MustNull:    return "MustNull";
        case Nullability::MustNonNull: return "MustNonNull";
        case Nullability::MayNull:     return "MayNull";
    }
    return "Unknown";
}

void collect_underlying_globals(const llvm::Value* val,
                                std::unordered_set<const llvm::Value*>& visited,
                                std::unordered_set<ValueId>& out_globals) {
    if (!val || !visited.insert(val).second) return;

    if (const auto* gv = llvm::dyn_cast<llvm::GlobalVariable>(val)) {
        out_globals.insert(reinterpret_cast<ValueId>(gv));
        return;
    }
    if (const auto* gep = llvm::dyn_cast<llvm::GEPOperator>(val)) {
        collect_underlying_globals(gep->getPointerOperand(), visited, out_globals);
        return;
    }
    if (const auto* bc = llvm::dyn_cast<llvm::BitCastOperator>(val)) {
        collect_underlying_globals(bc->getOperand(0), visited, out_globals);
        return;
    }
    if (const auto* asc = llvm::dyn_cast<llvm::AddrSpaceCastOperator>(val)) {
        collect_underlying_globals(asc->getOperand(0), visited, out_globals);
        return;
    }
    if (const auto* ce = llvm::dyn_cast<llvm::ConstantExpr>(val)) {
        for (unsigned i = 0; i < ce->getNumOperands(); ++i) {
            collect_underlying_globals(ce->getOperand(i), visited, out_globals);
        }
        return;
    }
    if (const auto* phi = llvm::dyn_cast<llvm::PHINode>(val)) {
        for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i) {
            collect_underlying_globals(phi->getIncomingValue(i), visited, out_globals);
        }
        return;
    }
    if (const auto* sel = llvm::dyn_cast<llvm::SelectInst>(val)) {
        collect_underlying_globals(sel->getTrueValue(), visited, out_globals);
        collect_underlying_globals(sel->getFalseValue(), visited, out_globals);
        return;
    }
}

} // namespace

// ============================================================================
// LinearSymExpr
// ============================================================================

DisjointIntervals LinearSymExpr::invert_range(const DisjointIntervals& target_range) const {
    if (!is_valid || coeff == 0 || target_range.is_empty()) {
        return DisjointIntervals::empty();
    }

    std::vector<DisjointIntervals::Range> res;
    res.reserve(target_range.ranges().size());

    for (const auto& r : target_range.ranges()) {
        __int128 a_lo = 0;
        __int128 a_hi = 0;
        if (coeff > 0) {
            __int128 c = static_cast<__int128>(coeff);
            a_lo = ceil_div_pos(static_cast<__int128>(r.first) - static_cast<__int128>(addend), c);
            a_hi = floor_div_pos(static_cast<__int128>(r.second) - static_cast<__int128>(addend), c);
        } else {
            __int128 c = -static_cast<__int128>(coeff);
            a_lo = ceil_div_pos(static_cast<__int128>(addend) - static_cast<__int128>(r.second), c);
            a_hi = floor_div_pos(static_cast<__int128>(addend) - static_cast<__int128>(r.first), c);
        }
        if (a_lo <= a_hi) {
            res.push_back({clamp_i64(a_lo), clamp_i64(a_hi)});
        }
    }

    return DisjointIntervals(std::move(res));
}

// ============================================================================
// AbstractValue
// ============================================================================

AbstractValue AbstractValue::make_int(const DisjointIntervals& dom, bool init) {
    AbstractValue v;
    v.is_pointer = false;
    v.is_initialized = init;
    v.null_state = Nullability::MustNonNull;
    v.int_domain = dom;
    return v;
}

AbstractValue AbstractValue::make_null_ptr() {
    AbstractValue v;
    v.is_pointer = true;
    v.is_initialized = true;
    v.null_state = Nullability::MustNull;
    v.int_domain = DisjointIntervals(0);
    return v;
}

AbstractValue AbstractValue::make_valid_ptr(RegionId reg, DisjointIntervals off, Nullability ns) {
    AbstractValue v;
    v.is_pointer = true;
    v.is_initialized = true;
    v.null_state = ns;
    v.ptr_targets.push_back(PointerTarget{reg, std::move(off)});
    return v;
}

AbstractValue AbstractValue::join(const AbstractValue& other) const {
    AbstractValue res;
    res.is_pointer = is_pointer || other.is_pointer;
    res.is_initialized = is_initialized && other.is_initialized;
    res.null_state = static_cast<Nullability>(
        static_cast<uint8_t>(null_state) | static_cast<uint8_t>(other.null_state));
    res.int_domain = int_domain.unite(other.int_domain);

    // Merge pointer targets by region_id
    res.ptr_targets = ptr_targets;
    for (const auto& ot : other.ptr_targets) {
        bool found = false;
        for (auto& rt : res.ptr_targets) {
            if (rt.region_id == ot.region_id) {
                rt.offset = rt.offset.unite(ot.offset);
                found = true;
                break;
            }
        }
        if (!found) {
            res.ptr_targets.push_back(ot);
        }
    }

    // Merge function targets uniquely
    res.func_targets = func_targets;
    for (const llvm::Function* fn : other.func_targets) {
        if (std::find(res.func_targets.begin(), res.func_targets.end(), fn) == res.func_targets.end()) {
            res.func_targets.push_back(fn);
        }
    }

    if (sym_expr.is_valid && other.sym_expr.is_valid &&
        sym_expr.root_param_id == other.sym_expr.root_param_id &&
        sym_expr.coeff == other.sym_expr.coeff &&
        sym_expr.addend == other.sym_expr.addend) {
        res.sym_expr = sym_expr;
    }

    return res;
}

bool AbstractValue::is_subset_of(const AbstractValue& other) const {
    // Nullability lattice: MustNull(1) <= MayNull(3), MustNonNull(2) <= MayNull(3)
    if ((static_cast<uint8_t>(null_state) & ~static_cast<uint8_t>(other.null_state)) != 0) {
        return false;
    }
    if (is_initialized != other.is_initialized) {
        return false;
    }
    if (!is_pointer && !other.is_pointer) {
        return int_domain.is_subset_of(other.int_domain);
    }
    if (is_pointer) {
        if (!other.is_pointer && !ptr_targets.empty()) {
            return false;
        }
        for (const auto& t : ptr_targets) {
            bool matched = false;
            for (const auto& ot : other.ptr_targets) {
                if (t.region_id == ot.region_id) {
                    if (!t.offset.is_subset_of(ot.offset)) return false;
                    matched = true;
                    break;
                }
            }
            if (!matched) return false;
        }
        for (const llvm::Function* fn : func_targets) {
            if (std::find(other.func_targets.begin(), other.func_targets.end(), fn) ==
                other.func_targets.end()) {
                return false;
            }
        }
        return true;
    }
    return int_domain.is_subset_of(other.int_domain);
}

bool AbstractValue::operator==(const AbstractValue& o) const {
    if (is_pointer != o.is_pointer ||
        is_initialized != o.is_initialized ||
        null_state != o.null_state ||
        int_domain != o.int_domain) {
        return false;
    }
    if (ptr_targets.size() != o.ptr_targets.size()) return false;
    for (const auto& t : ptr_targets) {
        if (std::find(o.ptr_targets.begin(), o.ptr_targets.end(), t) == o.ptr_targets.end()) {
            return false;
        }
    }
    if (func_targets.size() != o.func_targets.size()) return false;
    for (const llvm::Function* fn : func_targets) {
        if (std::find(o.func_targets.begin(), o.func_targets.end(), fn) == o.func_targets.end()) {
            return false;
        }
    }
    if (sym_expr.is_valid != o.sym_expr.is_valid) return false;
    if (sym_expr.is_valid) {
        if (sym_expr.root_param_id != o.sym_expr.root_param_id ||
            sym_expr.coeff != o.sym_expr.coeff ||
            sym_expr.addend != o.sym_expr.addend) {
            return false;
        }
    }
    return true;
}

std::string AbstractValue::to_string() const {
    std::ostringstream oss;
    if (is_pointer) {
        oss << "ptr(" << nullability_to_string(null_state);
        if (!ptr_targets.empty()) {
            oss << ", {";
            for (size_t i = 0; i < ptr_targets.size(); ++i) {
                if (i > 0) oss << ", ";
                oss << "reg#" << ptr_targets[i].region_id << "+" << ptr_targets[i].offset.to_string();
            }
            oss << "}";
        }
        if (!func_targets.empty()) {
            oss << ", funcs=" << func_targets.size();
        }
        oss << ")";
    } else {
        oss << "int(" << int_domain.to_string() << ")";
    }
    if (!is_initialized) {
        oss << "[uninit]";
    }
    return oss.str();
}

// ============================================================================
// AbstractState (Copy-on-Write Memory Regions)
// ============================================================================

const AbstractState::RegionMap& AbstractState::regions() const {
    static const RegionMap kEmptyRegions;
    return regions_ ? *regions_ : kEmptyRegions;
}

AbstractState::RegionMap& AbstractState::ensure_regions_unique() {
    if (!regions_) {
        regions_ = std::make_shared<RegionMap>();
    } else if (regions_.use_count() > 1) {
        regions_ = std::make_shared<RegionMap>(*regions_);
    }
    return *regions_;
}

RegionId AbstractState::allocate_region(const std::string& name, DisjointIntervals size,
                                        bool is_heap, bool is_global, bool is_stack,
                                        bool initialized, SourceLoc loc) {
    static std::atomic<RegionId> g_global_region_counter{1};
    RegionId rid = g_global_region_counter.fetch_add(1, std::memory_order_relaxed);
    next_region_id_ = std::max(next_region_id_, rid + 1);
    auto reg = std::make_shared<MemoryRegion>();
    reg->id = rid;
    reg->name = name;
    reg->byte_size = std::move(size);
    reg->liveness = RegionLiveness::Alive;
    reg->is_heap = is_heap;
    reg->is_global = is_global;
    reg->is_stack = is_stack;
    reg->default_initialized = initialized;
    reg->alloc_loc = std::move(loc);
    reg->default_element_value = AbstractValue::make_int(DisjointIntervals::full_i32(), initialized);
    ensure_regions_unique()[rid] = std::move(reg);
    return rid;
}

void AbstractState::set_value(ValueId vid, const AbstractValue& val) {
    values_[vid] = val;
}

AbstractValue AbstractState::get_value(ValueId vid) const {
    auto it = values_.find(vid);
    if (it != values_.end()) {
        return it->second;
    }
    return AbstractValue::make_int(DisjointIntervals::full_i32(), true);
}

bool AbstractState::has_value(ValueId vid) const {
    return values_.find(vid) != values_.end();
}

MemoryRegion* AbstractState::get_region(RegionId rid) {
    if (!regions_) return nullptr;
    auto it = regions_->find(rid);
    if (it == regions_->end()) return nullptr;
    if (regions_.use_count() > 1) {
        regions_ = std::make_shared<RegionMap>(*regions_);
        it = regions_->find(rid);
    }
    if (it->second.use_count() > 1) {
        it->second = std::make_shared<MemoryRegion>(*it->second);
    }
    return it->second.get();
}

const MemoryRegion* AbstractState::get_region(RegionId rid) const {
    if (!regions_) return nullptr;
    auto it = regions_->find(rid);
    return (it != regions_->end()) ? it->second.get() : nullptr;
}

void AbstractState::merge_regions_from(const AbstractState& other, bool skip_stack) {
    if (!other.regions_ || other.regions_->empty()) return;
    if (regions_.get() == other.regions_.get()) return;
    if (!skip_stack && (!regions_ || regions_->empty())) {
        regions_ = other.regions_;
        next_region_id_ = std::max(next_region_id_, other.next_region_id_);
        return;
    }
    RegionMap* dst = nullptr;
    const RegionMap* cur = regions_.get();
    for (const auto& kv : *other.regions_) {
        if (skip_stack && kv.second->is_stack) continue;
        if (cur) {
            auto it = cur->find(kv.first);
            if (it != cur->end() && it->second.get() == kv.second.get()) {
                continue;
            }
        }
        if (!dst) {
            dst = &ensure_regions_unique();
            cur = dst;
        }
        (*dst)[kv.first] = kv.second;
    }
    next_region_id_ = std::max(next_region_id_, other.next_region_id_);
}

void AbstractState::mark_indexed_nonnull(RegionId rid, ValueId idx_vid) {
    if (rid != 0 && idx_vid != 0) {
        indexed_nonnull_.insert({rid, idx_vid});
    }
}

bool AbstractState::is_indexed_nonnull(RegionId rid, ValueId idx_vid) const {
    return rid != 0 && idx_vid != 0 && indexed_nonnull_.find({rid, idx_vid}) != indexed_nonnull_.end();
}

void AbstractState::invalidate_index_var(ValueId idx_vid) {
    if (idx_vid == 0) return;
    for (auto it = indexed_nonnull_.begin(); it != indexed_nonnull_.end(); ) {
        if (it->second == idx_vid) {
            it = indexed_nonnull_.erase(it);
        } else {
            ++it;
        }
    }
}

void AbstractState::store_to_region(RegionId rid, const DisjointIntervals& offset,
                                    const AbstractValue& val) {
    MemoryRegion* reg = get_region(rid);
    if (!reg || offset.is_empty()) return;

    if (val.is_pointer && val.may_be_null()) {
        for (auto it = indexed_nonnull_.begin(); it != indexed_nonnull_.end(); ) {
            if (it->first == rid) {
                it = indexed_nonnull_.erase(it);
            } else {
                ++it;
            }
        }
    }

    if (offset.is_singleton()) {
        reg->field_values[offset.singleton_value()] = val;
        return;
    }

    // Weak update over all overlapping known offsets and default element
    for (auto& kv : reg->field_values) {
        if (offset.contains(kv.first)) {
            kv.second = kv.second.is_initialized ? kv.second.join(val) : val;
        }
    }
    if (!reg->default_element_value.is_initialized) {
        reg->default_element_value = val;
        reg->default_initialized = val.is_initialized;
    } else {
        reg->default_element_value = reg->default_element_value.join(val);
    }
}

AbstractValue AbstractState::load_from_region(RegionId rid, const DisjointIntervals& offset,
                                              uint32_t /*byte_width*/) const {
    const MemoryRegion* reg = get_region(rid);
    if (!reg) {
        return AbstractValue::make_int(DisjointIntervals::full_i32(), true);
    }

    if (offset.is_singleton()) {
        auto it = reg->field_values.find(offset.singleton_value());
        if (it != reg->field_values.end()) {
            return it->second;
        }
        return reg->default_element_value;
    }

    bool has_any = false;
    AbstractValue acc;
    size_t matched_fields = 0;
    for (const auto& kv : reg->field_values) {
        if (offset.contains(kv.first)) {
            if (!has_any) {
                acc = kv.second;
                has_any = true;
            } else {
                acc = acc.join(kv.second);
            }
            ++matched_fields;
        }
    }

    // Check if the offset could hit unmapped slots
    __int128 total_slots = 0;
    for (const auto& r : offset.ranges()) {
        total_slots += (static_cast<__int128>(r.second) - static_cast<__int128>(r.first) + 1);
        if (total_slots > static_cast<__int128>(matched_fields)) break;
    }
    if (!has_any) {
        return reg->default_element_value;
    }
    if (total_slots > static_cast<__int128>(matched_fields)) {
        acc = acc.join(reg->default_element_value);
    }
    return acc;
}

AbstractState AbstractState::project_live(const std::unordered_set<ValueId>& live_vars) const {
    AbstractState proj;
    proj.next_region_id_ = next_region_id_;
    proj.values_.reserve(live_vars.size());

    std::queue<RegionId> worklist;
    std::unordered_set<RegionId> visited_regions;

    auto enqueue_value_targets = [&](const AbstractValue& v) {
        for (const auto& pt : v.ptr_targets) {
            if (visited_regions.insert(pt.region_id).second) {
                worklist.push(pt.region_id);
            }
        }
    };

    for (ValueId vid : live_vars) {
        auto it = values_.find(vid);
        if (it != values_.end()) {
            proj.values_.emplace(vid, it->second);
            enqueue_value_targets(it->second);
        }
    }

    if (regions_ && !regions_->empty()) {
        for (const auto& kv : *regions_) {
            if (kv.second->is_global) {
                if (visited_regions.insert(kv.first).second) {
                    worklist.push(kv.first);
                }
            }
        }

        if (!worklist.empty()) {
            proj.regions_ = std::make_shared<RegionMap>();
            proj.regions_->reserve(std::min(regions_->size(), worklist.size() + 16));
            while (!worklist.empty()) {
                RegionId rid = worklist.front();
                worklist.pop();
                auto rit = regions_->find(rid);
                if (rit == regions_->end()) continue;

                const MemoryRegion& reg = *rit->second;
                proj.regions_->emplace(rid, rit->second); // CoW share — zero deep copy
                for (const auto& fkv : reg.field_values) {
                    enqueue_value_targets(fkv.second);
                }
                enqueue_value_targets(reg.default_element_value);
                if (reg.cached_elem_pointee_rid != 0) {
                    if (visited_regions.insert(reg.cached_elem_pointee_rid).second) {
                        worklist.push(reg.cached_elem_pointee_rid);
                    }
                }
            }
        }
    }

    if (proj.regions_) {
        for (const auto& p : indexed_nonnull_) {
            if (proj.regions_->find(p.first) != proj.regions_->end()) {
                proj.indexed_nonnull_.insert(p);
            }
        }
    }

    return proj;
}

bool AbstractState::is_subset_of(const AbstractState& other) const {
    for (const auto& p : other.indexed_nonnull_) {
        if (indexed_nonnull_.find(p) == indexed_nonnull_.end()) {
            return false;
        }
    }
    for (const auto& kv : values_) {
        auto oit = other.values_.find(kv.first);
        if (oit == other.values_.end()) {
            return false;
        }
        if (!kv.second.is_subset_of(oit->second)) {
            return false;
        }
    }
    if (regions_.get() != other.regions_.get() && regions_ && !regions_->empty()) {
        if (!other.regions_) return false;
        for (const auto& kv : *regions_) {
            auto oit = other.regions_->find(kv.first);
            if (oit == other.regions_->end()) return false;
            if (kv.second.get() == oit->second.get()) continue; // O(1) CoW fast path!

            const MemoryRegion& r1 = *kv.second;
            const MemoryRegion& r2 = *oit->second;
            if (r1.liveness != r2.liveness) return false;
            if (!r1.byte_size.is_subset_of(r2.byte_size)) return false;
            if (!r1.default_element_value.is_subset_of(r2.default_element_value)) return false;
            for (const auto& fkv : r1.field_values) {
                auto fit = r2.field_values.find(fkv.first);
                const AbstractValue& oval = (fit != r2.field_values.end())
                                                ? fit->second
                                                : r2.default_element_value;
                if (!fkv.second.is_subset_of(oval)) {
                    return false;
                }
            }
        }
    }
    return true;
}

AbstractState AbstractState::join(const AbstractState& other) const {
    AbstractState res;
    res.next_region_id_ = std::max(next_region_id_, other.next_region_id_);

    res.values_.reserve(std::max(values_.size(), other.values_.size()));
    for (const auto& kv : values_) {
        auto oit = other.values_.find(kv.first);
        if (oit != other.values_.end()) {
            res.values_.emplace(kv.first, kv.second.join(oit->second));
        } else {
            res.values_.emplace(kv.first, kv.second);
        }
    }
    for (const auto& kv : other.values_) {
        if (res.values_.find(kv.first) == res.values_.end()) {
            res.values_.emplace(kv.first, kv.second);
        }
    }

    if (regions_.get() == other.regions_.get()) {
        res.regions_ = regions_; // O(1) CoW fast path!
    } else if (!regions_ || regions_->empty()) {
        res.regions_ = other.regions_;
    } else if (!other.regions_ || other.regions_->empty()) {
        res.regions_ = regions_;
    } else {
        res.regions_ = std::make_shared<RegionMap>();
        res.regions_->reserve(std::max(regions_->size(), other.regions_->size()));
        for (const auto& kv : *regions_) {
            auto oit = other.regions_->find(kv.first);
            if (oit == other.regions_->end()) {
                res.regions_->emplace(kv.first, kv.second);
                continue;
            }
            if (kv.second.get() == oit->second.get()) {
                res.regions_->emplace(kv.first, kv.second); // O(1) CoW fast path!
                continue;
            }
            const MemoryRegion& r1 = *kv.second;
            const MemoryRegion& r2 = *oit->second;
            auto merged = std::make_shared<MemoryRegion>();
            merged->id = r1.id;
            merged->name = r1.name;
            merged->is_heap = r1.is_heap;
            merged->is_global = r1.is_global;
            merged->is_stack = r1.is_stack;
            merged->is_symbolic_input = r1.is_symbolic_input || r2.is_symbolic_input;
            merged->alloc_loc = r1.alloc_loc;
            merged->byte_size = r1.byte_size.unite(r2.byte_size);
            merged->null_term_offset = r1.null_term_offset.unite(r2.null_term_offset);
            merged->default_initialized = r1.default_initialized || r2.default_initialized;
            merged->cached_elem_pointee_rid = r1.cached_elem_pointee_rid != 0
                ? r1.cached_elem_pointee_rid
                : r2.cached_elem_pointee_rid;
            if (r1.default_element_value.is_initialized && !r2.default_element_value.is_initialized) {
                merged->default_element_value = r1.default_element_value;
            } else if (!r1.default_element_value.is_initialized && r2.default_element_value.is_initialized) {
                merged->default_element_value = r2.default_element_value;
            } else {
                merged->default_element_value = r1.default_element_value.join(r2.default_element_value);
            }

            if (r1.liveness == RegionLiveness::Freed || r2.liveness == RegionLiveness::Freed) {
                merged->liveness = RegionLiveness::Freed;
            } else if (r1.liveness == RegionLiveness::EscapedOrOutOfScope ||
                       r2.liveness == RegionLiveness::EscapedOrOutOfScope) {
                merged->liveness = RegionLiveness::EscapedOrOutOfScope;
            } else {
                merged->liveness = RegionLiveness::Alive;
            }

            merged->field_values.reserve(std::max(r1.field_values.size(), r2.field_values.size()));
            for (const auto& fkv : r1.field_values) {
                auto f2 = r2.field_values.find(fkv.first);
                if (f2 != r2.field_values.end()) {
                    merged->field_values.emplace(fkv.first, fkv.second.join(f2->second));
                } else {
                    merged->field_values.emplace(fkv.first, fkv.second.join(r2.default_element_value));
                }
            }
            for (const auto& fkv : r2.field_values) {
                if (merged->field_values.find(fkv.first) == merged->field_values.end()) {
                    merged->field_values.emplace(fkv.first, r1.default_element_value.join(fkv.second));
                }
            }
            res.regions_->emplace(kv.first, std::move(merged));
        }
        for (const auto& kv : *other.regions_) {
            if (res.regions_->find(kv.first) == res.regions_->end()) {
                res.regions_->emplace(kv.first, kv.second);
            }
        }
    }

    for (const auto& p : indexed_nonnull_) {
        if (other.indexed_nonnull_.find(p) != other.indexed_nonnull_.end()) {
            res.indexed_nonnull_.insert(p);
        }
    }

    return res;
}

uint64_t AbstractState::compute_hash(const std::vector<ValueId>& relevant_keys) const {
    uint64_t h = 14695981039346656037ULL;
    for (ValueId vid : relevant_keys) {
        h = fnv1a_mix(h, vid);
        auto it = values_.find(vid);
        if (it == values_.end()) {
            h = fnv1a_mix(h, 0);
            continue;
        }
        const AbstractValue& v = it->second;
        h = fnv1a_mix(h, 1);
        uint64_t flags = (static_cast<uint64_t>(v.null_state) << 2) |
                         (v.is_pointer ? 2ULL : 0ULL) |
                         (v.is_initialized ? 1ULL : 0ULL);
        h = fnv1a_mix(h, flags);
        for (const auto& r : v.int_domain.ranges()) {
            h = fnv1a_mix(h, static_cast<uint64_t>(r.first));
            h = fnv1a_mix(h, static_cast<uint64_t>(r.second));
        }
        for (const auto& pt : v.ptr_targets) {
            h = fnv1a_mix(h, static_cast<uint64_t>(pt.region_id));
            for (const auto& r : pt.offset.ranges()) {
                h = fnv1a_mix(h, static_cast<uint64_t>(r.first));
                h = fnv1a_mix(h, static_cast<uint64_t>(r.second));
            }
        }
    }
    return h;
}

// ============================================================================
// LivenessAndModRefPass
// ============================================================================

void LivenessAndModRefPass::run(const llvm::Module& mod) {
    bb_live_in_.clear();
    fn_read_globals_.clear();
    fn_write_globals_.clear();

    std::unordered_map<const llvm::Function*, std::unordered_set<const llvm::Function*>> call_graph;

    for (const llvm::Function& fn : mod) {
        if (fn.isDeclaration()) continue;

        auto& reads = fn_read_globals_[&fn];
        auto& writes = fn_write_globals_[&fn];
        auto& callees = call_graph[&fn];

        std::unordered_map<const llvm::BasicBlock*, std::unordered_set<ValueId>> bb_use;
        std::unordered_map<const llvm::BasicBlock*, std::unordered_set<ValueId>> bb_def;
        std::unordered_map<const llvm::BasicBlock*, std::unordered_set<ValueId>> bb_live_out;
        std::vector<const llvm::BasicBlock*> blocks;

        for (const llvm::BasicBlock& bb : fn) {
            blocks.push_back(&bb);
            auto& use_set = bb_use[&bb];
            auto& def_set = bb_def[&bb];

            for (const llvm::Instruction& inst : bb) {
                if (const auto* phi = llvm::dyn_cast<llvm::PHINode>(&inst)) {
                    def_set.insert(reinterpret_cast<ValueId>(phi));
                    continue;
                }

                for (const llvm::Use& u : inst.operands()) {
                    const llvm::Value* op = u.get();
                    if (llvm::isa<llvm::Instruction>(op) || llvm::isa<llvm::Argument>(op)) {
                        ValueId oid = reinterpret_cast<ValueId>(op);
                        if (def_set.find(oid) == def_set.end()) {
                            use_set.insert(oid);
                        }
                    }
                }
                if (!inst.getType()->isVoidTy()) {
                    def_set.insert(reinterpret_cast<ValueId>(&inst));
                }

                // ModRef collection
                if (const auto* ld = llvm::dyn_cast<llvm::LoadInst>(&inst)) {
                    std::unordered_set<const llvm::Value*> visited;
                    collect_underlying_globals(ld->getPointerOperand(), visited, reads);
                } else if (const auto* st = llvm::dyn_cast<llvm::StoreInst>(&inst)) {
                    std::unordered_set<const llvm::Value*> visited;
                    collect_underlying_globals(st->getPointerOperand(), visited, writes);
                } else if (const auto* rmw = llvm::dyn_cast<llvm::AtomicRMWInst>(&inst)) {
                    std::unordered_set<const llvm::Value*> visited;
                    collect_underlying_globals(rmw->getPointerOperand(), visited, reads);
                    visited.clear();
                    collect_underlying_globals(rmw->getPointerOperand(), visited, writes);
                } else if (const auto* cmpx = llvm::dyn_cast<llvm::AtomicCmpXchgInst>(&inst)) {
                    std::unordered_set<const llvm::Value*> visited;
                    collect_underlying_globals(cmpx->getPointerOperand(), visited, reads);
                    visited.clear();
                    collect_underlying_globals(cmpx->getPointerOperand(), visited, writes);
                } else if (const auto* call = llvm::dyn_cast<llvm::CallBase>(&inst)) {
                    if (const llvm::Function* callee = call->getCalledFunction()) {
                        callees.insert(callee);
                    }
                }
            }
        }

        // Backward dataflow fixed point for liveness
        bool changed = true;
        while (changed) {
            changed = false;
            for (auto it = blocks.rbegin(); it != blocks.rend(); ++it) {
                const llvm::BasicBlock* bb = *it;
                auto& live_out = bb_live_out[bb];
                auto& live_in = bb_live_in_[bb];

                std::unordered_set<ValueId> new_out;
                for (const llvm::BasicBlock* succ : llvm::successors(bb)) {
                    auto sit = bb_live_in_.find(succ);
                    if (sit != bb_live_in_.end()) {
                        new_out.insert(sit->second.begin(), sit->second.end());
                    }
                    // Add PHI incoming values coming from bb
                    for (const llvm::Instruction& inst : *succ) {
                        const auto* phi = llvm::dyn_cast<llvm::PHINode>(&inst);
                        if (!phi) break;
                        const llvm::Value* incoming = phi->getIncomingValueForBlock(bb);
                        if (llvm::isa<llvm::Instruction>(incoming) ||
                            llvm::isa<llvm::Argument>(incoming)) {
                            new_out.insert(reinterpret_cast<ValueId>(incoming));
                        }
                    }
                }

                std::unordered_set<ValueId> new_in = bb_use[bb];
                const auto& def_set = bb_def[bb];
                for (ValueId vid : new_out) {
                    if (def_set.find(vid) == def_set.end()) {
                        new_in.insert(vid);
                    }
                }

                if (new_in.size() != live_in.size() || new_out.size() != live_out.size()) {
                    live_out = std::move(new_out);
                    live_in = std::move(new_in);
                    changed = true;
                }
            }
        }

        // Ensure PHI nodes defined at the top of each BasicBlock are retained in live_in(bb)
        for (const llvm::BasicBlock* bb : blocks) {
            auto& live_in = bb_live_in_[bb];
            for (const llvm::PHINode& phi : bb->phis()) {
                live_in.insert(reinterpret_cast<ValueId>(&phi));
            }
        }
    }

    // Bottom-up propagation of global ModRef across call graph
    bool cg_changed = true;
    while (cg_changed) {
        cg_changed = false;
        for (const auto& kv : call_graph) {
            const llvm::Function* caller = kv.first;
            auto& caller_reads = fn_read_globals_[caller];
            auto& caller_writes = fn_write_globals_[caller];

            for (const llvm::Function* callee : kv.second) {
                auto rit = fn_read_globals_.find(callee);
                if (rit != fn_read_globals_.end()) {
                    size_t before = caller_reads.size();
                    caller_reads.insert(rit->second.begin(), rit->second.end());
                    if (caller_reads.size() != before) cg_changed = true;
                }
                auto wit = fn_write_globals_.find(callee);
                if (wit != fn_write_globals_.end()) {
                    size_t before = caller_writes.size();
                    caller_writes.insert(wit->second.begin(), wit->second.end());
                    if (caller_writes.size() != before) cg_changed = true;
                }
            }
        }
    }
}

const std::unordered_set<ValueId>& LivenessAndModRefPass::live_in(const llvm::BasicBlock* bb) const {
    auto it = bb_live_in_.find(bb);
    return (it != bb_live_in_.end()) ? it->second : empty_set_;
}

const std::unordered_set<ValueId>& LivenessAndModRefPass::func_read_globals(const llvm::Function* fn) const {
    auto it = fn_read_globals_.find(fn);
    return (it != fn_read_globals_.end()) ? it->second : empty_set_;
}

const std::unordered_set<ValueId>& LivenessAndModRefPass::func_write_globals(const llvm::Function* fn) const {
    auto it = fn_write_globals_.find(fn);
    return (it != fn_write_globals_.end()) ? it->second : empty_set_;
}

bool LivenessAndModRefPass::function_is_pure_or_readonly(const llvm::Function* fn) const {
    if (!fn) return false;
    if (fn->doesNotAccessMemory() || fn->onlyReadsMemory()) return true;
    if (fn->isDeclaration()) return false;
    return func_write_globals(fn).empty();
}

} // namespace sqc
