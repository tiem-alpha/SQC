#include "sqc/summaries.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Operator.h>

#include <algorithm>
#include <unordered_set>

namespace sqc {

namespace {

bool extract_string_literal(const llvm::Value* v, std::string& out) {
    if (!v) return false;
    const llvm::Value* cur = v->stripPointerCasts();
    if (const auto* gep = llvm::dyn_cast<llvm::GEPOperator>(cur)) {
        cur = gep->getPointerOperand()->stripPointerCasts();
    }
    if (const auto* gv = llvm::dyn_cast<llvm::GlobalVariable>(cur)) {
        if (gv->hasInitializer()) {
            if (const auto* cda = llvm::dyn_cast<llvm::ConstantDataArray>(gv->getInitializer())) {
                if (cda->isCString()) {
                    out = cda->getAsCString().str();
                    return true;
                }
                if (cda->isString()) {
                    out = cda->getAsString().str();
                    auto nul = out.find('\0');
                    if (nul != std::string::npos) out.resize(nul);
                    return true;
                }
            }
        }
    }
    return false;
}

AbstractValue eval_arg(const llvm::Value* v, const AbstractState& state, const llvm::Module& mod) {
    if (!v) {
        return AbstractValue::make_int(DisjointIntervals::full_i32(), false);
    }
    ValueId vid = reinterpret_cast<ValueId>(v);
    if (state.has_value(vid)) {
        return state.get_value(vid);
    }
    if (const auto* ci = llvm::dyn_cast<llvm::ConstantInt>(v)) {
        int64_t val = ci->getBitWidth() <= 64 ? ci->getSExtValue() : 0;
        return AbstractValue::make_int(DisjointIntervals(val), true);
    }
    if (llvm::isa<llvm::ConstantPointerNull>(v)) {
        return AbstractValue::make_null_ptr();
    }
    if (const auto* fn = llvm::dyn_cast<llvm::Function>(v)) {
        AbstractValue av = AbstractValue::make_valid_ptr(0, DisjointIntervals(0), Nullability::MustNonNull);
        av.func_targets.push_back(fn);
        return av;
    }
    if (const auto* gep = llvm::dyn_cast<llvm::GEPOperator>(v)) {
        AbstractValue base = eval_arg(gep->getPointerOperand(), state, mod);
        const llvm::DataLayout& dl = mod.getDataLayout();
        llvm::APInt const_off(dl.getPointerSizeInBits(0), 0);
        if (gep->accumulateConstantOffset(dl, const_off)) {
            int64_t delta = const_off.getSExtValue();
            for (auto& t : base.ptr_targets) {
                t.offset = t.offset.add(DisjointIntervals(delta));
            }
        }
        return base;
    }
    if (const auto* op = llvm::dyn_cast<llvm::Operator>(v)) {
        unsigned opc = op->getOpcode();
        if (opc == llvm::Instruction::BitCast ||
            opc == llvm::Instruction::AddrSpaceCast ||
            opc == llvm::Instruction::PtrToInt ||
            opc == llvm::Instruction::IntToPtr) {
            return eval_arg(op->getOperand(0), state, mod);
        }
    }
    if (v->getType()->isPointerTy()) {
        return AbstractValue::make_valid_ptr(0, DisjointIntervals(0), Nullability::MayNull);
    }
    return AbstractValue::make_int(DisjointIntervals::full_i32(), true);
}

HazardReport make_hazard(
    HazardKind kind,
    const SourceLoc& loc,
    const std::string& caller,
    const std::string& analysis,
    const std::string& predicate,
    const std::string& witness,
    const std::string& msg) {
    HazardReport rep;
    rep.kind = kind;
    rep.certainty = HazardCertainty::DefiniteBug;
    rep.loc = loc;
    rep.function_name = caller;
    rep.analysis_name = analysis;
    rep.predicate = predicate;
    rep.concrete_witness = witness;
    rep.message = msg;
    return rep;
}

std::string value_display_name(const llvm::Value* v, const std::string& fallback) {
    if (v && v->hasName()) return v->getName().str();
    return fallback;
}

void check_ptr_valid_and_bounds(
    const AbstractValue& ptr,
    const std::string& ptr_name,
    const DisjointIntervals& access_len,
    bool check_bounds,
    const AbstractState& state,
    const SourceLoc& loc,
    const std::string& caller,
    const std::string& analysis,
    const std::string& api_name,
    std::vector<HazardReport>& hazards) {
    if (ptr.may_be_null()) {
        hazards.push_back(make_hazard(
            HazardKind::NullPointerDeref, loc, caller, analysis,
            ptr_name + " == NULL",
            ptr_name + " = NULL",
            "Null pointer passed to " + api_name + " (" + ptr_name + ")"));
    }
    int64_t len_max = access_len.is_empty() ? 0 : std::max<int64_t>(0, access_len.max_value());
    for (const auto& target : ptr.ptr_targets) {
        const MemoryRegion* reg = state.get_region(target.region_id);
        if (!reg) continue;
        if (reg->liveness == RegionLiveness::Freed) {
            hazards.push_back(make_hazard(
                HazardKind::UseAfterFree, loc, caller, analysis,
                "region(" + reg->name + ") == Freed",
                ptr_name + " -> freed region " + reg->name,
                "Use-after-free in " + api_name + " on " + reg->name));
            continue;
        }
        if (check_bounds && len_max > 0) {
            int64_t reg_max = reg->byte_size.max_value();
            if (target.offset.min_value() < 0) {
                hazards.push_back(make_hazard(
                    HazardKind::OutOfBoundsAccess, loc, caller, analysis,
                    "offset < 0",
                    ptr_name + " offset = " + std::to_string(target.offset.min_value()),
                    "Negative buffer offset in " + api_name + " on " + reg->name));
            } else if (reg_max >= 0 && target.offset.max_value() + len_max > reg_max) {
                hazards.push_back(make_hazard(
                    HazardKind::OutOfBoundsAccess, loc, caller, analysis,
                    "offset + size > " + std::to_string(reg_max),
                    "offset=" + std::to_string(target.offset.max_value()) +
                        ", size=" + std::to_string(len_max) +
                        " > sizeof(" + reg->name + ")=" + std::to_string(reg_max),
                    "Out-of-bounds access in " + api_name + " on " + reg->name));
            }
        }
    }
}

void check_cstring_termination(
    const AbstractValue& ptr,
    const std::string& ptr_name,
    const AbstractState& state,
    const SourceLoc& loc,
    const std::string& caller,
    const std::string& analysis,
    const std::string& api_name,
    std::vector<HazardReport>& hazards) {
    for (const auto& target : ptr.ptr_targets) {
        const MemoryRegion* reg = state.get_region(target.region_id);
        if (!reg || reg->liveness == RegionLiveness::Freed) continue;
        if (!reg->default_initialized && reg->field_values.empty()) {
            hazards.push_back(make_hazard(
                HazardKind::UninitializedRead, loc, caller, analysis,
                "uninitialized(" + reg->name + ")",
                ptr_name + " points to uninitialized " + reg->name,
                "Reading uninitialized string buffer in " + api_name + " (" + reg->name + ")"));
        }
        int64_t reg_max = reg->byte_size.max_value();
        if (reg_max > 0 && reg->null_term_offset.max_value() >= reg_max) {
            hazards.push_back(make_hazard(
                HazardKind::StringNotNullTerminated, loc, caller, analysis,
                "null_term_offset >= sizeof(" + reg->name + ")",
                reg->name + " may lack '\\0' within " + std::to_string(reg_max) + " bytes",
                "Potentially non-null-terminated string passed to " + api_name + " (" + reg->name + ")"));
        }
    }
}

bool is_llvm_mem_intrinsic(const std::string& fn_name, const char* prefix) {
    return fn_name.rfind(prefix, 0) == 0;
}

} // namespace

bool BuiltinSummaryRegistry::is_summarized_function(const std::string& fn_name) const {
    static const std::unordered_set<std::string> kKnown = {
        // SQC builtins
        "__sqc_interval_i64",
        "__sqc_interval_u64",
        "__sqc_interval_split_i64",
        "__sqc_make_unknown",
        "__sqc_make_cstring",
        "__sqc_nondet_i64",
        "__sqc_nondet_ptr2",
        "__sqc_nondet_ptr3",
        "__sqc_nondet_ptr5",
        "__sqc_find_variable",
        "__sqc_assert",
        "__sqc_assume",
        "__sqc_ub",
        "__sqc_requires_valid",
        // Libc O(1) semantic models
        "strlen",
        "sscanf",
        "__isoc99_sscanf",
        "sscanf_make_unknown",
        "memcpy",
        "memmove",
        "memset",
        "strcpy",
        "strncpy",
        "strcmp",
        "strncmp",
        "memcmp",
        "snprintf",
        "sprintf",
        "strtol",
        "strtoul",
        "malloc",
        "calloc",
        "free"
    };

    if (kKnown.count(fn_name) > 0) return true;
    if (is_llvm_mem_intrinsic(fn_name, "llvm.memcpy.") ||
        is_llvm_mem_intrinsic(fn_name, "llvm.memmove.") ||
        is_llvm_mem_intrinsic(fn_name, "llvm.memset.")) {
        return true;
    }
    return false;
}

SummaryCallResult BuiltinSummaryRegistry::evaluate_call(
    const std::string& fn_name,
    const llvm::CallBase& call_inst,
    const AbstractState& current_state,
    const llvm::Module& mod,
    const SourceLoc& call_loc,
    const std::string& caller_name,
    const std::string& analysis_name) const {
    SummaryCallResult res;
    res.updated_state = current_state;

    if (!is_summarized_function(fn_name)) {
        res.handled = false;
        return res;
    }
    res.handled = true;

    const unsigned nargs = call_inst.arg_size();

    // 1. Interval primitives
    if (fn_name == "__sqc_interval_i64" ||
        fn_name == "__sqc_interval_u64" ||
        fn_name == "__sqc_interval_split_i64") {
        if (nargs >= 2) {
            AbstractValue lo = eval_arg(call_inst.getArgOperand(0), current_state, mod);
            AbstractValue hi = eval_arg(call_inst.getArgOperand(1), current_state, mod);
            int64_t lo_v = lo.int_domain.min_value();
            int64_t hi_v = hi.int_domain.max_value();
            if (lo_v > hi_v) std::swap(lo_v, hi_v);
            res.return_value = AbstractValue::make_int(DisjointIntervals(lo_v, hi_v), true);
            if (lo_v < hi_v) {
                ValueId cid = reinterpret_cast<ValueId>(&call_inst);
                std::string sname = call_inst.hasName()
                    ? call_inst.getName().str()
                    : ("in_L" + std::to_string(call_loc.line) + "_" + std::to_string(cid & 0xFFFF));
                res.return_value.sym_expr = LinearSymExpr{true, cid, sname, 1, 0};
            }
            res.input_spec = SymbolicInputSpec{
                "sqc_interval",
                call_inst.hasName() ? call_inst.getName().str() : "interval_val",
                "[" + std::to_string(lo_v) + ", " + std::to_string(hi_v) + "]",
                caller_name,
                call_loc};
        } else {
            res.return_value = AbstractValue::make_int(DisjointIntervals::full_i32(), true);
        }
        return res;
    }

    // 2. __sqc_make_unknown
    if (fn_name == "__sqc_make_unknown" || fn_name == "sscanf_make_unknown") {
        if (nargs >= 2) {
            AbstractValue ptr = eval_arg(call_inst.getArgOperand(0), current_state, mod);
            AbstractValue sz = eval_arg(call_inst.getArgOperand(1), current_state, mod);
            std::string pname = value_display_name(call_inst.getArgOperand(0), "ptr");
            check_ptr_valid_and_bounds(ptr, pname, sz.int_domain, true, current_state,
                                       call_loc, caller_name, analysis_name, fn_name, res.hazards);
            std::string reg_names;
            for (const auto& t : ptr.ptr_targets) {
                if (auto* reg = res.updated_state.get_region(t.region_id)) {
                    reg->default_initialized = true;
                    reg->default_element_value = AbstractValue::make_int(DisjointIntervals::full_i32(), true);
                    reg->field_values.clear();
                    reg->null_term_offset = DisjointIntervals(0, DisjointIntervals::MAX_VAL);
                    if (!reg_names.empty()) reg_names += ", ";
                    reg_names += reg->name;
                }
            }
            res.input_spec = SymbolicInputSpec{
                "sqc_make_unknown",
                reg_names.empty() ? pname : reg_names,
                "unknown bytes (" + sz.int_domain.to_string() + " B, may lack '\\0')",
                caller_name,
                call_loc};
        }
        return res;
    }

    // 3. __sqc_make_cstring
    if (fn_name == "__sqc_make_cstring") {
        if (nargs >= 2) {
            AbstractValue ptr = eval_arg(call_inst.getArgOperand(0), current_state, mod);
            AbstractValue sz = eval_arg(call_inst.getArgOperand(1), current_state, mod);
            std::string pname = value_display_name(call_inst.getArgOperand(0), "ptr");
            check_ptr_valid_and_bounds(ptr, pname, sz.int_domain, true, current_state,
                                       call_loc, caller_name, analysis_name, fn_name, res.hazards);
            int64_t max_sz = std::max<int64_t>(1, sz.int_domain.max_value());
            std::string reg_names;
            for (const auto& t : ptr.ptr_targets) {
                if (auto* reg = res.updated_state.get_region(t.region_id)) {
                    reg->default_initialized = true;
                    reg->default_element_value = AbstractValue::make_int(DisjointIntervals(0, 255), true);
                    reg->field_values.clear();
                    reg->null_term_offset = DisjointIntervals(0, max_sz - 1);
                    if (!reg_names.empty()) reg_names += ", ";
                    reg_names += reg->name;
                }
            }
            res.input_spec = SymbolicInputSpec{
                "sqc_make_cstring",
                reg_names.empty() ? pname : reg_names,
                "C-string (" + std::to_string(max_sz) + " B, '\\0'-terminated)",
                caller_name,
                call_loc};
        }
        return res;
    }

    // 4. Nondeterministic integer choice
    if (fn_name == "__sqc_nondet_i64") {
        if (nargs >= 2) {
            AbstractValue a = eval_arg(call_inst.getArgOperand(0), current_state, mod);
            AbstractValue b = eval_arg(call_inst.getArgOperand(1), current_state, mod);
            res.return_value = a.join(b);
            res.input_spec = SymbolicInputSpec{
                "sqc_nondet",
                call_inst.hasName() ? call_inst.getName().str() : "nondet_val",
                res.return_value.int_domain.to_string(),
                caller_name,
                call_loc};
        } else {
            res.return_value = AbstractValue::make_int(DisjointIntervals::full_i32(), true);
        }
        return res;
    }

    // 5. Nondeterministic pointer choice
    if (fn_name == "__sqc_nondet_ptr2" ||
        fn_name == "__sqc_nondet_ptr3" ||
        fn_name == "__sqc_nondet_ptr5") {
        if (nargs > 0) {
            AbstractValue joined = eval_arg(call_inst.getArgOperand(0), current_state, mod);
            joined.is_pointer = true;
            for (unsigned i = 1; i < nargs; ++i) {
                AbstractValue arg_v = eval_arg(call_inst.getArgOperand(i), current_state, mod);
                arg_v.is_pointer = true;
                joined = joined.join(arg_v);
            }
            res.return_value = joined;
            std::string desc = "{";
            bool first_d = true;
            for (const auto& t : joined.ptr_targets) {
                if (const auto* r = current_state.get_region(t.region_id)) {
                    if (!first_d) desc += ", ";
                    desc += "&" + r->name;
                    first_d = false;
                }
            }
            for (const auto* ft : joined.func_targets) {
                if (ft) {
                    if (!first_d) desc += ", ";
                    desc += ft->getName().str() + "()";
                    first_d = false;
                }
            }
            if (joined.may_be_null()) {
                if (!first_d) desc += ", ";
                desc += "NULL";
            }
            desc += "}";
            res.input_spec = SymbolicInputSpec{
                "sqc_nondet_ptr",
                call_inst.hasName() ? call_inst.getName().str() : "ptr_choice",
                desc,
                caller_name,
                call_loc};
        } else {
            res.return_value = AbstractValue::make_null_ptr();
        }
        return res;
    }

    // 6. White-box static variable lookup
    if (fn_name == "__sqc_find_variable") {
        std::string var_name;
        if (nargs >= 1 && extract_string_literal(call_inst.getArgOperand(0), var_name)) {
            res.input_spec = SymbolicInputSpec{
                "sqc_find_variable",
                var_name,
                "white-box state target '" + var_name + "'",
                caller_name,
                call_loc};
            const llvm::GlobalVariable* found_gv = nullptr;
            for (const llvm::GlobalVariable& gv : mod.globals()) {
                std::string gname = gv.getName().str();
                if (gname == var_name || gname.rfind(var_name + ".", 0) == 0) {
                    found_gv = &gv;
                    if (gname == var_name) break;
                }
            }
            if (found_gv) {
                ValueId gvid = reinterpret_cast<ValueId>(found_gv);
                if (current_state.has_value(gvid)) {
                    res.return_value = current_state.get_value(gvid);
                    return res;
                }
                const llvm::DataLayout& dl = mod.getDataLayout();
                int64_t gsz = found_gv->getValueType()->isSized()
                    ? static_cast<int64_t>(dl.getTypeAllocSize(found_gv->getValueType()))
                    : 4;
                RegionId rid = res.updated_state.allocate_region(
                    found_gv->getName().str(), DisjointIntervals(std::max<int64_t>(1, gsz)),
                    false, true, false, true, call_loc);
                AbstractValue gv_ptr = AbstractValue::make_valid_ptr(rid, DisjointIntervals(0), Nullability::MustNonNull);
                res.updated_state.set_value(gvid, gv_ptr);
                res.return_value = gv_ptr;
                return res;
            }
        }
        // Fallback if symbol not found
        res.return_value = AbstractValue::make_null_ptr();
        return res;
    }

    // 7. Assertions, Assumptions, UB, Requires-valid
    if (fn_name == "__sqc_assert") {
        if (nargs >= 1) {
            AbstractValue cond = eval_arg(call_inst.getArgOperand(0), current_state, mod);
            if (cond.int_domain.contains(0)) {
                std::string expr_str;
                if (nargs >= 2) extract_string_literal(call_inst.getArgOperand(1), expr_str);
                std::string pred = expr_str.empty() ? "assertion_cond == 0" : ("!(" + expr_str + ")");
                res.hazards.push_back(make_hazard(
                    HazardKind::AssertionViolation, call_loc, caller_name, analysis_name,
                    pred, "cond = 0",
                    "Assertion violation: " + (expr_str.empty() ? "condition may be false" : expr_str)));
            }
        }
        return res;
    }

    if (fn_name == "__sqc_assume") {
        return res;
    }

    if (fn_name == "__sqc_ub") {
        std::string msg;
        if (nargs >= 1) extract_string_literal(call_inst.getArgOperand(0), msg);
        bool triggered = true;
        if (nargs >= 2) {
            AbstractValue trig = eval_arg(call_inst.getArgOperand(1), current_state, mod);
            triggered = !trig.int_domain.remove_value(0).is_empty();
        }
        if (triggered) {
            res.hazards.push_back(make_hazard(
                HazardKind::CustomUndefinedBehavior, call_loc, caller_name, analysis_name,
                "trigger != 0", "trigger != 0",
                msg.empty() ? "Custom undefined behavior triggered" : msg));
        }
        return res;
    }

    if (fn_name == "__sqc_requires_valid") {
        if (nargs >= 2) {
            AbstractValue ptr = eval_arg(call_inst.getArgOperand(0), current_state, mod);
            AbstractValue sz = eval_arg(call_inst.getArgOperand(1), current_state, mod);
            std::string pname = value_display_name(call_inst.getArgOperand(0), "ptr");
            check_ptr_valid_and_bounds(ptr, pname, sz.int_domain, true, current_state,
                                       call_loc, caller_name, analysis_name, fn_name, res.hazards);
        }
        return res;
    }

    // 8. strlen
    if (fn_name == "strlen") {
        int64_t max_len = DisjointIntervals::MAX_VAL;
        if (nargs >= 1) {
            AbstractValue ptr = eval_arg(call_inst.getArgOperand(0), current_state, mod);
            std::string pname = value_display_name(call_inst.getArgOperand(0), "s");
            check_ptr_valid_and_bounds(ptr, pname, DisjointIntervals(1), false, current_state,
                                       call_loc, caller_name, analysis_name, fn_name, res.hazards);
            check_cstring_termination(ptr, pname, current_state,
                                      call_loc, caller_name, analysis_name, fn_name, res.hazards);
            int64_t computed_max = -1;
            for (const auto& t : ptr.ptr_targets) {
                if (const auto* reg = current_state.get_region(t.region_id)) {
                    int64_t rem = std::max<int64_t>(0, reg->byte_size.max_value() - t.offset.min_value() - 1);
                    if (reg->null_term_offset.max_value() < reg->byte_size.max_value()) {
                        int64_t nrem = std::max<int64_t>(0, reg->null_term_offset.max_value() - t.offset.min_value());
                        rem = std::min(rem, nrem);
                    }
                    computed_max = std::max(computed_max, rem);
                }
            }
            if (computed_max >= 0) max_len = computed_max;
        }
        res.return_value = AbstractValue::make_int(DisjointIntervals(0, max_len), true);
        return res;
    }

    // 9. sscanf family
    if (fn_name == "sscanf" || fn_name == "__isoc99_sscanf") {
        if (nargs >= 1) {
            AbstractValue s = eval_arg(call_inst.getArgOperand(0), current_state, mod);
            std::string sname = value_display_name(call_inst.getArgOperand(0), "str");
            check_ptr_valid_and_bounds(s, sname, DisjointIntervals(1), false, current_state,
                                       call_loc, caller_name, analysis_name, fn_name, res.hazards);
            check_cstring_termination(s, sname, current_state,
                                      call_loc, caller_name, analysis_name, fn_name, res.hazards);
        }
        if (nargs >= 2) {
            AbstractValue fmt = eval_arg(call_inst.getArgOperand(1), current_state, mod);
            std::string fname = value_display_name(call_inst.getArgOperand(1), "format");
            check_ptr_valid_and_bounds(fmt, fname, DisjointIntervals(1), false, current_state,
                                       call_loc, caller_name, analysis_name, fn_name, res.hazards);
        }
        int64_t num_out = 0;
        for (unsigned i = 2; i < nargs; ++i) {
            ++num_out;
            AbstractValue out_ptr = eval_arg(call_inst.getArgOperand(i), current_state, mod);
            std::string oname = value_display_name(call_inst.getArgOperand(i), "out" + std::to_string(i - 2));
            check_ptr_valid_and_bounds(out_ptr, oname, DisjointIntervals(1), false, current_state,
                                       call_loc, caller_name, analysis_name, fn_name, res.hazards);
            for (const auto& t : out_ptr.ptr_targets) {
                if (auto* reg = res.updated_state.get_region(t.region_id)) {
                    reg->default_initialized = true;
                    res.updated_state.store_to_region(
                        t.region_id, t.offset,
                        AbstractValue::make_int(DisjointIntervals::full_i32(), true));
                }
            }
        }
        res.return_value = AbstractValue::make_int(DisjointIntervals(-1, num_out), true);
        return res;
    }

    // 10. memcpy / memmove / llvm.memcpy.* / llvm.memmove.*
    if (fn_name == "memcpy" || fn_name == "memmove" ||
        is_llvm_mem_intrinsic(fn_name, "llvm.memcpy.") ||
        is_llvm_mem_intrinsic(fn_name, "llvm.memmove.")) {
        if (nargs >= 3) {
            AbstractValue dst = eval_arg(call_inst.getArgOperand(0), current_state, mod);
            AbstractValue src = eval_arg(call_inst.getArgOperand(1), current_state, mod);
            AbstractValue len = eval_arg(call_inst.getArgOperand(2), current_state, mod);
            check_ptr_valid_and_bounds(dst, value_display_name(call_inst.getArgOperand(0), "dst"),
                                       len.int_domain, true, current_state,
                                       call_loc, caller_name, analysis_name, fn_name, res.hazards);
            check_ptr_valid_and_bounds(src, value_display_name(call_inst.getArgOperand(1), "src"),
                                       len.int_domain, true, current_state,
                                       call_loc, caller_name, analysis_name, fn_name, res.hazards);

            // Propagate initialization & known fields when copying from a single source region
            const MemoryRegion* single_src = (src.ptr_targets.size() == 1)
                ? current_state.get_region(src.ptr_targets[0].region_id)
                : nullptr;
            int64_t src_off = (src.ptr_targets.size() == 1 && src.ptr_targets[0].offset.is_singleton())
                ? src.ptr_targets[0].offset.singleton_value()
                : -1;

            for (const auto& t : dst.ptr_targets) {
                if (auto* dreg = res.updated_state.get_region(t.region_id)) {
                    dreg->default_initialized = true;
                    if (single_src) {
                        dreg->default_element_value = single_src->default_element_value;
                        if (src_off == 0 && t.offset.is_singleton() && t.offset.singleton_value() == 0) {
                            dreg->field_values = single_src->field_values;
                            dreg->null_term_offset = single_src->null_term_offset;
                        }
                    } else {
                        dreg->default_element_value = AbstractValue::make_int(DisjointIntervals::full_i32(), true);
                    }
                }
            }
            res.return_value = dst;
        }
        return res;
    }

    // 11. memset / llvm.memset.*
    if (fn_name == "memset" || is_llvm_mem_intrinsic(fn_name, "llvm.memset.")) {
        if (nargs >= 3) {
            AbstractValue dst = eval_arg(call_inst.getArgOperand(0), current_state, mod);
            AbstractValue val = eval_arg(call_inst.getArgOperand(1), current_state, mod);
            AbstractValue len = eval_arg(call_inst.getArgOperand(2), current_state, mod);
            check_ptr_valid_and_bounds(dst, value_display_name(call_inst.getArgOperand(0), "dst"),
                                       len.int_domain, true, current_state,
                                       call_loc, caller_name, analysis_name, fn_name, res.hazards);
            bool is_zero = val.int_domain.is_singleton() && val.int_domain.singleton_value() == 0;
            for (const auto& t : dst.ptr_targets) {
                if (auto* dreg = res.updated_state.get_region(t.region_id)) {
                    dreg->default_initialized = true;
                    dreg->field_values.clear();
                    if (is_zero) {
                        dreg->default_element_value = AbstractValue::make_int(DisjointIntervals(0), true);
                        dreg->null_term_offset = DisjointIntervals(0);
                    } else {
                        dreg->default_element_value = AbstractValue::make_int(val.int_domain, true);
                        dreg->null_term_offset = DisjointIntervals(0, DisjointIntervals::MAX_VAL);
                    }
                }
            }
            res.return_value = dst;
        }
        return res;
    }

    // 12. strcpy / strncpy
    if (fn_name == "strcpy" || fn_name == "strncpy") {
        if (nargs >= 2) {
            AbstractValue dst = eval_arg(call_inst.getArgOperand(0), current_state, mod);
            AbstractValue src = eval_arg(call_inst.getArgOperand(1), current_state, mod);
            DisjointIntervals copy_len(1);
            if (fn_name == "strncpy" && nargs >= 3) {
                copy_len = eval_arg(call_inst.getArgOperand(2), current_state, mod).int_domain;
            }
            check_ptr_valid_and_bounds(dst, value_display_name(call_inst.getArgOperand(0), "dst"),
                                       copy_len, fn_name == "strncpy", current_state,
                                       call_loc, caller_name, analysis_name, fn_name, res.hazards);
            check_ptr_valid_and_bounds(src, value_display_name(call_inst.getArgOperand(1), "src"),
                                       DisjointIntervals(1), false, current_state,
                                       call_loc, caller_name, analysis_name, fn_name, res.hazards);
            if (fn_name == "strcpy") {
                check_cstring_termination(src, value_display_name(call_inst.getArgOperand(1), "src"),
                                          current_state, call_loc, caller_name, analysis_name,
                                          fn_name, res.hazards);
            }
            for (const auto& t : dst.ptr_targets) {
                if (auto* dreg = res.updated_state.get_region(t.region_id)) {
                    dreg->default_initialized = true;
                    if (fn_name == "strcpy") {
                        int64_t max_off = std::max<int64_t>(0, dreg->byte_size.max_value() - 1);
                        dreg->null_term_offset = DisjointIntervals(0, max_off);
                    } else {
                        dreg->null_term_offset = DisjointIntervals(0, DisjointIntervals::MAX_VAL);
                    }
                }
            }
            res.return_value = dst;
        }
        return res;
    }

    // 13. strcmp / strncmp / memcmp
    if (fn_name == "strcmp" || fn_name == "strncmp" || fn_name == "memcmp") {
        if (nargs >= 2) {
            AbstractValue s1 = eval_arg(call_inst.getArgOperand(0), current_state, mod);
            AbstractValue s2 = eval_arg(call_inst.getArgOperand(1), current_state, mod);
            check_ptr_valid_and_bounds(s1, value_display_name(call_inst.getArgOperand(0), "s1"),
                                       DisjointIntervals(1), false, current_state,
                                       call_loc, caller_name, analysis_name, fn_name, res.hazards);
            check_ptr_valid_and_bounds(s2, value_display_name(call_inst.getArgOperand(1), "s2"),
                                       DisjointIntervals(1), false, current_state,
                                       call_loc, caller_name, analysis_name, fn_name, res.hazards);
            if (fn_name == "strcmp") {
                check_cstring_termination(s1, value_display_name(call_inst.getArgOperand(0), "s1"),
                                          current_state, call_loc, caller_name, analysis_name,
                                          fn_name, res.hazards);
                check_cstring_termination(s2, value_display_name(call_inst.getArgOperand(1), "s2"),
                                          current_state, call_loc, caller_name, analysis_name,
                                          fn_name, res.hazards);
            }
        }
        res.return_value = AbstractValue::make_int(DisjointIntervals(-1, 1), true);
        return res;
    }

    // 14. snprintf / sprintf
    if (fn_name == "snprintf" || fn_name == "sprintf") {
        if (nargs >= 2) {
            AbstractValue dst = eval_arg(call_inst.getArgOperand(0), current_state, mod);
            DisjointIntervals sz(1);
            unsigned fmt_idx = 1;
            if (fn_name == "snprintf" && nargs >= 3) {
                sz = eval_arg(call_inst.getArgOperand(1), current_state, mod).int_domain;
                fmt_idx = 2;
            }
            check_ptr_valid_and_bounds(dst, value_display_name(call_inst.getArgOperand(0), "dst"),
                                       sz, fn_name == "snprintf", current_state,
                                       call_loc, caller_name, analysis_name, fn_name, res.hazards);
            if (fmt_idx < nargs) {
                AbstractValue fmt = eval_arg(call_inst.getArgOperand(fmt_idx), current_state, mod);
                check_ptr_valid_and_bounds(fmt, value_display_name(call_inst.getArgOperand(fmt_idx), "fmt"),
                                           DisjointIntervals(1), false, current_state,
                                           call_loc, caller_name, analysis_name, fn_name, res.hazards);
            }
            for (const auto& t : dst.ptr_targets) {
                if (auto* dreg = res.updated_state.get_region(t.region_id)) {
                    dreg->default_initialized = true;
                    int64_t cap = fn_name == "snprintf"
                        ? std::max<int64_t>(0, sz.max_value() - 1)
                        : std::max<int64_t>(0, dreg->byte_size.max_value() - 1);
                    dreg->null_term_offset = DisjointIntervals(0, cap);
                }
            }
        }
        res.return_value = AbstractValue::make_int(DisjointIntervals(0, DisjointIntervals::MAX_VAL), true);
        return res;
    }

    // 15. strtol / strtoul
    if (fn_name == "strtol" || fn_name == "strtoul") {
        if (nargs >= 1) {
            AbstractValue nptr = eval_arg(call_inst.getArgOperand(0), current_state, mod);
            check_ptr_valid_and_bounds(nptr, value_display_name(call_inst.getArgOperand(0), "nptr"),
                                       DisjointIntervals(1), false, current_state,
                                       call_loc, caller_name, analysis_name, fn_name, res.hazards);
            if (nargs >= 2) {
                AbstractValue endptr = eval_arg(call_inst.getArgOperand(1), current_state, mod);
                if (!endptr.must_be_null()) {
                    for (const auto& t : endptr.ptr_targets) {
                        res.updated_state.store_to_region(t.region_id, t.offset, nptr);
                    }
                }
            }
        }
        res.return_value = AbstractValue::make_int(DisjointIntervals::full_i32(), true);
        return res;
    }

    // 16. malloc / calloc
    if (fn_name == "malloc" || fn_name == "calloc") {
        DisjointIntervals alloc_sz(4);
        if (fn_name == "malloc" && nargs >= 1) {
            alloc_sz = eval_arg(call_inst.getArgOperand(0), current_state, mod).int_domain;
        } else if (fn_name == "calloc" && nargs >= 2) {
            AbstractValue nmemb = eval_arg(call_inst.getArgOperand(0), current_state, mod);
            AbstractValue elem = eval_arg(call_inst.getArgOperand(1), current_state, mod);
            alloc_sz = nmemb.int_domain.mul(elem.int_domain);
        }
        alloc_sz = alloc_sz.intersect(DisjointIntervals(0, DisjointIntervals::MAX_VAL));
        if (alloc_sz.is_empty()) alloc_sz = DisjointIntervals(0);

        bool is_calloc = (fn_name == "calloc");
        std::string rname = (is_calloc ? "calloc@" : "malloc@") + call_loc.to_string();
        RegionId rid = res.updated_state.allocate_region(
            rname, alloc_sz, /*is_heap=*/true, /*is_global=*/false, /*is_stack=*/false,
            /*initialized=*/is_calloc, call_loc);
        if (auto* reg = res.updated_state.get_region(rid)) {
            if (is_calloc) {
                reg->default_element_value = AbstractValue::make_int(DisjointIntervals(0), true);
                reg->null_term_offset = DisjointIntervals(0);
            } else {
                reg->default_element_value = AbstractValue::make_int(DisjointIntervals::full_i32(), false);
                reg->null_term_offset = DisjointIntervals(0, DisjointIntervals::MAX_VAL);
            }
        }
        res.return_value = AbstractValue::make_valid_ptr(rid, DisjointIntervals(0), Nullability::MayNull);
        return res;
    }

    // 17. free
    if (fn_name == "free") {
        if (nargs >= 1) {
            AbstractValue ptr = eval_arg(call_inst.getArgOperand(0), current_state, mod);
            std::string pname = value_display_name(call_inst.getArgOperand(0), "ptr");
            for (const auto& t : ptr.ptr_targets) {
                auto* reg = res.updated_state.get_region(t.region_id);
                if (!reg) continue;
                if (reg->liveness == RegionLiveness::Freed) {
                    res.hazards.push_back(make_hazard(
                        HazardKind::DoubleFree, call_loc, caller_name, analysis_name,
                        "region(" + reg->name + ") == Freed",
                        pname + " -> already freed " + reg->name,
                        "Double free of heap allocation " + reg->name));
                } else if (!reg->is_heap || !t.offset.is_singleton() || t.offset.singleton_value() != 0) {
                    res.hazards.push_back(make_hazard(
                        HazardKind::InvalidFree, call_loc, caller_name, analysis_name,
                        "!is_heap || offset != 0",
                        pname + " -> " + reg->name + " (offset " + t.offset.to_string() + ")",
                        "Invalid free of non-heap or interior pointer " + reg->name));
                } else {
                    reg->liveness = RegionLiveness::Freed;
                }
            }
        }
        return res;
    }

    return res;
}

} // namespace sqc
