#include "sqc/analyzer.h"

#include <llvm/IR/CFG.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Operator.h>

#include <algorithm>
#include <chrono>
#include <deque>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <unordered_map>

namespace sqc {

namespace {

SourceLoc extract_source_loc(const llvm::DebugLoc& dl) {
    SourceLoc loc;
    if (!dl) return loc;
    if (const llvm::DILocation* diloc = dl.get()) {
        std::string dir = diloc->getDirectory().str();
        std::string fname = diloc->getFilename().str();
        if (!fname.empty()) {
            if (!dir.empty() && fname[0] != '/') {
                loc.file = (std::filesystem::path(dir) / fname).lexically_normal().string();
            } else {
                loc.file = std::filesystem::path(fname).lexically_normal().string();
            }
        }
        loc.line = diloc->getLine();
        loc.col_start = static_cast<uint16_t>(diloc->getColumn());
    }
    return loc;
}

std::string value_name(const llvm::Value* v, const std::string& fallback = "v") {
    if (!v) return fallback;
    if (v->hasName()) return v->getName().str();
    if (const auto* li = llvm::dyn_cast<llvm::LoadInst>(v)) {
        if (li->getPointerOperand()->hasName()) {
            return li->getPointerOperand()->getName().str();
        }
    }
    return fallback;
}

DisjointIntervals::CmpOp map_icmp_predicate(llvm::CmpInst::Predicate pred) {
    switch (pred) {
        case llvm::CmpInst::ICMP_EQ:  return DisjointIntervals::CmpOp::EQ;
        case llvm::CmpInst::ICMP_NE:  return DisjointIntervals::CmpOp::NE;
        case llvm::CmpInst::ICMP_SLT: return DisjointIntervals::CmpOp::SLT;
        case llvm::CmpInst::ICMP_SLE: return DisjointIntervals::CmpOp::SLE;
        case llvm::CmpInst::ICMP_SGT: return DisjointIntervals::CmpOp::SGT;
        case llvm::CmpInst::ICMP_SGE: return DisjointIntervals::CmpOp::SGE;
        case llvm::CmpInst::ICMP_ULT: return DisjointIntervals::CmpOp::ULT;
        case llvm::CmpInst::ICMP_ULE: return DisjointIntervals::CmpOp::ULE;
        case llvm::CmpInst::ICMP_UGT: return DisjointIntervals::CmpOp::UGT;
        case llvm::CmpInst::ICMP_UGE: return DisjointIntervals::CmpOp::UGE;
        default:                      return DisjointIntervals::CmpOp::EQ;
    }
}

DisjointIntervals::CmpOp negate_cmp(DisjointIntervals::CmpOp op) {
    switch (op) {
        case DisjointIntervals::CmpOp::EQ:  return DisjointIntervals::CmpOp::NE;
        case DisjointIntervals::CmpOp::NE:  return DisjointIntervals::CmpOp::EQ;
        case DisjointIntervals::CmpOp::SLT: return DisjointIntervals::CmpOp::SGE;
        case DisjointIntervals::CmpOp::SLE: return DisjointIntervals::CmpOp::SGT;
        case DisjointIntervals::CmpOp::SGT: return DisjointIntervals::CmpOp::SLE;
        case DisjointIntervals::CmpOp::SGE: return DisjointIntervals::CmpOp::SLT;
        case DisjointIntervals::CmpOp::ULT: return DisjointIntervals::CmpOp::UGE;
        case DisjointIntervals::CmpOp::ULE: return DisjointIntervals::CmpOp::UGT;
        case DisjointIntervals::CmpOp::UGT: return DisjointIntervals::CmpOp::ULE;
        case DisjointIntervals::CmpOp::UGE: return DisjointIntervals::CmpOp::ULT;
    }
    return DisjointIntervals::CmpOp::NE;
}

DisjointIntervals::CmpOp swap_cmp(DisjointIntervals::CmpOp op) {
    switch (op) {
        case DisjointIntervals::CmpOp::EQ:  return DisjointIntervals::CmpOp::EQ;
        case DisjointIntervals::CmpOp::NE:  return DisjointIntervals::CmpOp::NE;
        case DisjointIntervals::CmpOp::SLT: return DisjointIntervals::CmpOp::SGT;
        case DisjointIntervals::CmpOp::SLE: return DisjointIntervals::CmpOp::SGE;
        case DisjointIntervals::CmpOp::SGT: return DisjointIntervals::CmpOp::SLT;
        case DisjointIntervals::CmpOp::SGE: return DisjointIntervals::CmpOp::SLE;
        case DisjointIntervals::CmpOp::ULT: return DisjointIntervals::CmpOp::UGT;
        case DisjointIntervals::CmpOp::ULE: return DisjointIntervals::CmpOp::UGE;
        case DisjointIntervals::CmpOp::UGT: return DisjointIntervals::CmpOp::ULT;
        case DisjointIntervals::CmpOp::UGE: return DisjointIntervals::CmpOp::ULE;
    }
    return DisjointIntervals::CmpOp::EQ;
}

std::string subcond_key(const SourceLoc& loc) {
    return loc.file + ":" + std::to_string(loc.line) + ":" + std::to_string(loc.col_start);
}

void populate_constant_into_region(
    const llvm::Constant* c,
    const llvm::DataLayout& dl,
    const AbstractState& root_state,
    MemoryRegion* reg,
    int64_t base_offset) {
    if (!c || !reg) return;

    if (const auto* ci = llvm::dyn_cast<llvm::ConstantInt>(c)) {
        int64_t v = (ci->getBitWidth() == 1)
                        ? static_cast<int64_t>(ci->getZExtValue())
                        : (ci->getBitWidth() <= 64 ? ci->getSExtValue() : 0);
        reg->field_values[base_offset] = AbstractValue::make_int(DisjointIntervals(v), true);
        return;
    }
    if (llvm::isa<llvm::ConstantPointerNull>(c)) {
        reg->field_values[base_offset] = AbstractValue::make_null_ptr();
        return;
    }
    if (const auto* fn = llvm::dyn_cast<llvm::Function>(c)) {
        AbstractValue fv = AbstractValue::make_valid_ptr(0, DisjointIntervals(0), Nullability::MustNonNull);
        fv.func_targets.push_back(fn);
        reg->field_values[base_offset] = fv;
        return;
    }
    if (const auto* gv = llvm::dyn_cast<llvm::GlobalVariable>(c)) {
        ValueId gvid = reinterpret_cast<ValueId>(gv);
        if (root_state.has_value(gvid)) {
            reg->field_values[base_offset] = root_state.get_value(gvid);
        }
        return;
    }
    if (const auto* ce = llvm::dyn_cast<llvm::ConstantExpr>(c)) {
        if (ce->isCast()) {
            populate_constant_into_region(ce->getOperand(0), dl, root_state, reg, base_offset);
            return;
        }
        if (const auto* gep = llvm::dyn_cast<llvm::GEPOperator>(ce)) {
            if (const auto* base_c = llvm::dyn_cast<llvm::Constant>(gep->getPointerOperand())) {
                populate_constant_into_region(base_c, dl, root_state, reg, base_offset);
                llvm::APInt off(dl.getPointerSizeInBits(0), 0);
                if (gep->accumulateConstantOffset(dl, off)) {
                    auto it = reg->field_values.find(base_offset);
                    if (it != reg->field_values.end()) {
                        int64_t delta = off.getSExtValue();
                        for (auto& t : it->second.ptr_targets) {
                            t.offset = t.offset.add(DisjointIntervals(delta));
                        }
                    }
                }
            }
        }
        return;
    }
    if (llvm::isa<llvm::ConstantAggregateZero>(c)) {
        if (base_offset == 0) {
            reg->default_element_value = AbstractValue::make_int(DisjointIntervals(0), true);
            reg->null_term_offset = DisjointIntervals(0);
        } else {
            reg->field_values[base_offset] = AbstractValue::make_int(DisjointIntervals(0), true);
        }
        return;
    }
    if (const auto* cds = llvm::dyn_cast<llvm::ConstantDataSequential>(c)) {
        if (cds->isCString()) {
            std::string s = cds->getAsCString().str();
            reg->null_term_offset = DisjointIntervals(static_cast<int64_t>(s.size()));
            size_t limit = std::min<size_t>(s.size() + 1, 64);
            for (size_t i = 0; i < limit; ++i) {
                int64_t ch = (i < s.size()) ? static_cast<unsigned char>(s[i]) : 0;
                reg->field_values[base_offset + static_cast<int64_t>(i)] =
                    AbstractValue::make_int(DisjointIntervals(ch), true);
            }
        } else if (cds->isString()) {
            llvm::StringRef sr = cds->getAsString();
            size_t zpos = sr.find('\0');
            if (zpos != llvm::StringRef::npos) {
                reg->null_term_offset = DisjointIntervals(static_cast<int64_t>(zpos));
            }
        } else {
            unsigned n = std::min<unsigned>(cds->getNumElements(), 64);
            uint64_t esz = cds->getElementByteSize();
            for (unsigned i = 0; i < n; ++i) {
                if (cds->getElementType()->isIntegerTy()) {
                    int64_t v = static_cast<int64_t>(cds->getElementAsInteger(i));
                    reg->field_values[base_offset + static_cast<int64_t>(i * esz)] =
                        AbstractValue::make_int(DisjointIntervals(v), true);
                }
            }
        }
        return;
    }
    if (const auto* cs = llvm::dyn_cast<llvm::ConstantStruct>(c)) {
        llvm::StructType* st = cs->getType();
        const llvm::StructLayout* layout = dl.getStructLayout(st);
        for (unsigned i = 0; i < cs->getNumOperands(); ++i) {
            int64_t foff = base_offset + static_cast<int64_t>(layout->getElementOffset(i));
            populate_constant_into_region(cs->getOperand(i), dl, root_state, reg, foff);
        }
        return;
    }
    if (const auto* ca = llvm::dyn_cast<llvm::ConstantArray>(c)) {
        llvm::Type* ety = ca->getType()->getElementType();
        uint64_t esz = ety->isSized() ? dl.getTypeAllocSize(ety) : 1;
        unsigned n = std::min<unsigned>(ca->getNumOperands(), 64);
        for (unsigned i = 0; i < n; ++i) {
            populate_constant_into_region(
                ca->getOperand(i), dl, root_state, reg,
                base_offset + static_cast<int64_t>(i * esz));
        }
        return;
    }
}

void propagate_narrowed_to_slot(
    const llvm::Value* val,
    const AbstractValue& narrowed,
    AbstractState& state) {
    const llvm::Value* cur = val;
    while (const auto* ci = llvm::dyn_cast<llvm::CastInst>(cur)) {
        cur = ci->getOperand(0);
        if (!llvm::isa<llvm::Constant>(cur)) {
            state.set_value(reinterpret_cast<ValueId>(cur), narrowed);
        }
    }
    if (const auto* li = llvm::dyn_cast<llvm::LoadInst>(cur)) {
        const llvm::Value* ptr_op = li->getPointerOperand();
        ValueId ptr_vid = reinterpret_cast<ValueId>(ptr_op);
        if (state.has_value(ptr_vid)) {
            AbstractValue slot_ptr = state.get_value(ptr_vid);
            if (slot_ptr.ptr_targets.size() == 1 &&
                slot_ptr.ptr_targets[0].region_id != 0 &&
                slot_ptr.ptr_targets[0].offset.is_singleton()) {
                const MemoryRegion* sreg = state.get_region(slot_ptr.ptr_targets[0].region_id);
                if (sreg) {
                    state.store_to_region(
                        slot_ptr.ptr_targets[0].region_id,
                        slot_ptr.ptr_targets[0].offset,
                        narrowed);
                }
            } else if (slot_ptr.ptr_targets.size() == 1 &&
                       slot_ptr.ptr_targets[0].region_id != 0 &&
                       slot_ptr.ptr_targets[0].sym_index_id != 0 &&
                       narrowed.is_pointer &&
                       narrowed.null_state == Nullability::MustNonNull) {
                state.mark_indexed_nonnull(
                    slot_ptr.ptr_targets[0].region_id,
                    slot_ptr.ptr_targets[0].sym_index_id);
            } else if (slot_ptr.ptr_targets.size() > 1 &&
                       narrowed.is_pointer &&
                       (narrowed.null_state == Nullability::MustNonNull ||
                        narrowed.null_state == Nullability::MustNull)) {
                if (const auto* gep = llvm::dyn_cast<llvm::GEPOperator>(ptr_op)) {
                    if (const llvm::Module* m = li->getModule()) {
                        const llvm::DataLayout& dl = m->getDataLayout();
                        llvm::APInt ap_off(dl.getPointerSizeInBits(0), 0);
                        const llvm::Value* base_val = gep->getPointerOperand();
                        ValueId base_vid = reinterpret_cast<ValueId>(base_val);
                        if (gep->accumulateConstantOffset(dl, ap_off) && state.has_value(base_vid)) {
                            int64_t foff = ap_off.getSExtValue();
                            AbstractValue base_av = state.get_value(base_vid);
                            std::vector<PointerTarget> kept;
                            for (const auto& bt : base_av.ptr_targets) {
                                if (bt.region_id == 0) {
                                    kept.push_back(bt);
                                    continue;
                                }
                                DisjointIntervals fld_off = bt.offset.add(DisjointIntervals(foff));
                                AbstractValue fv = state.load_from_region(bt.region_id, fld_off);
                                if (fv.ptr_targets.empty() && fv.func_targets.empty() && !fv.is_pointer) {
                                    kept.push_back(bt);
                                } else if (narrowed.null_state == Nullability::MustNonNull) {
                                    if (!fv.must_be_null()) kept.push_back(bt);
                                } else {
                                    if (fv.may_be_null()) kept.push_back(bt);
                                }
                            }
                            if (!kept.empty()) {
                                if (narrowed.null_state == Nullability::MustNonNull) {
                                    for (const auto& stgt : kept) {
                                        if (stgt.region_id == 0) continue;
                                        DisjointIntervals fld_off = stgt.offset.add(DisjointIntervals(foff));
                                        if (!fld_off.is_singleton()) continue;
                                        AbstractValue fv = state.load_from_region(stgt.region_id, fld_off);
                                        if (!fv.must_be_null() && fv.may_be_null()) {
                                            fv.is_pointer = true;
                                            fv.null_state = Nullability::MustNonNull;
                                            state.store_to_region(stgt.region_id, fld_off, fv);
                                        }
                                    }
                                }
                                if (kept.size() < base_av.ptr_targets.size()) {
                                    base_av.ptr_targets = std::move(kept);
                                    state.set_value(base_vid, base_av);
                                    propagate_narrowed_to_slot(base_val, base_av, state);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

} // namespace

HazardGuidedAnalyzer::HazardGuidedAnalyzer(const llvm::Module& mod, const AnalysisUnitConfig& cfg)
    : mod_(mod), cfg_(cfg) {}

void HazardGuidedAnalyzer::emit_hazard(HazardReport rep, std::vector<HazardReport>& local_hazards) {
    if (rep.analysis_name.empty()) {
        rep.analysis_name = cfg_.name;
    }
    local_hazards.push_back(rep);

    std::string key = std::to_string(static_cast<int>(rep.kind)) + "|" +
                      rep.loc.to_string() + "|" + rep.function_name + "|" + rep.predicate;
    if (deduplicated_alarm_keys_.insert(key).second) {
        alarms_.push_back(rep);
        coverage_.record_alarm(rep);
    } else {
        for (auto& existing : alarms_) {
            std::string ekey = std::to_string(static_cast<int>(existing.kind)) + "|" +
                               existing.loc.to_string() + "|" + existing.function_name + "|" + existing.predicate;
            if (ekey == key) {
                existing.occurrences++;
                break;
            }
        }
    }
}

AbstractValue HazardGuidedAnalyzer::eval_operand(const llvm::Value* v, AbstractState& state) const {
    if (!v) {
        return AbstractValue::make_int(DisjointIntervals::full_i32(), false);
    }
    ValueId vid = reinterpret_cast<ValueId>(v);
    if (state.has_value(vid)) {
        return state.get_value(vid);
    }
    if (const auto* ci = llvm::dyn_cast<llvm::ConstantInt>(v)) {
        int64_t val = (ci->getBitWidth() == 1)
                          ? static_cast<int64_t>(ci->getZExtValue())
                          : (ci->getBitWidth() <= 64 ? ci->getSExtValue() : 0);
        return AbstractValue::make_int(DisjointIntervals(val), true);
    }
    if (llvm::isa<llvm::ConstantPointerNull>(v)) {
        return AbstractValue::make_null_ptr();
    }
    if (llvm::isa<llvm::UndefValue>(v) || llvm::isa<llvm::PoisonValue>(v)) {
        if (v->getType()->isPointerTy()) {
            AbstractValue av = AbstractValue::make_null_ptr();
            av.is_initialized = false;
            return av;
        }
        return AbstractValue::make_int(DisjointIntervals::full_i32(), false);
    }
    if (const auto* fn = llvm::dyn_cast<llvm::Function>(v)) {
        AbstractValue av = AbstractValue::make_valid_ptr(0, DisjointIntervals(0), Nullability::MustNonNull);
        av.func_targets.push_back(fn);
        return av;
    }
    if (const auto* gep = llvm::dyn_cast<llvm::GEPOperator>(v)) {
        AbstractValue base = eval_operand(gep->getPointerOperand(), state);
        const llvm::DataLayout& dl = mod_.getDataLayout();
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
            return eval_operand(op->getOperand(0), state);
        }
    }
    if (v->getType()->isPointerTy()) {
        return AbstractValue::make_valid_ptr(0, DisjointIntervals(0), Nullability::MayNull);
    }
    return AbstractValue::make_int(DisjointIntervals::full_i32(), true);
}

AnalysisRunResult HazardGuidedAnalyzer::run() {
    auto t0 = std::chrono::steady_clock::now();

    AnalysisRunResult result;
    result.unit_id = cfg_.id;
    result.unit_name = cfg_.name;

    liveness_modref_.run(mod_);
    coverage_.initialize_from_module(mod_);

    const llvm::DataLayout& dl = mod_.getDataLayout();
    AbstractState root_state;

    // Pass 1: Allocate regions for all global variables
    std::unordered_map<const llvm::GlobalVariable*, RegionId> gv_regions;
    for (const llvm::GlobalVariable& gv : mod_.globals()) {
        if (gv.getName().starts_with("llvm.")) continue;
        int64_t gsz = gv.getValueType()->isSized()
            ? static_cast<int64_t>(dl.getTypeAllocSize(gv.getValueType()))
            : 4;
        RegionId rid = root_state.allocate_region(
            gv.getName().str(),
            DisjointIntervals(std::max<int64_t>(1, gsz)),
            /*is_heap=*/false, /*is_global=*/true, /*is_stack=*/false,
            /*initialized=*/true);
        gv_regions[&gv] = rid;
        root_state.set_value(
            reinterpret_cast<ValueId>(&gv),
            AbstractValue::make_valid_ptr(rid, DisjointIntervals(0), Nullability::MustNonNull));
    }

    // Pass 2: Populate global initializers (constants, strings, function-pointer structs)
    for (const llvm::GlobalVariable& gv : mod_.globals()) {
        auto it = gv_regions.find(&gv);
        if (it == gv_regions.end()) continue;
        MemoryRegion* reg = root_state.get_region(it->second);
        if (!reg) continue;
        if (gv.hasInitializer()) {
            populate_constant_into_region(gv.getInitializer(), dl, root_state, reg, 0);
        } else if (gv.getValueType()->isPointerTy()) {
            RegionId ext_target_rid = root_state.allocate_region(
                "ext_target_" + gv.getName().str(),
                DisjointIntervals(4096),
                /*is_heap=*/false, /*is_global=*/true, /*is_stack=*/false,
                /*initialized=*/true);
            if (MemoryRegion* treg = root_state.get_region(ext_target_rid)) {
                treg->is_symbolic_input = true;
            }
            reg->field_values[0] = AbstractValue::make_valid_ptr(
                ext_target_rid, DisjointIntervals(0), Nullability::MayNull);
        }
    }

    // Locate entry function
    const llvm::Function* entry_fn = nullptr;
    if (!cfg_.entry_function.empty()) {
        entry_fn = mod_.getFunction(cfg_.entry_function);
    }
    if (!entry_fn) entry_fn = mod_.getFunction("sqc_main");
    if (!entry_fn) entry_fn = mod_.getFunction("main");
    if (!entry_fn) {
        for (const llvm::Function& f : mod_) {
            if (!f.isDeclaration() && !f.getName().starts_with("llvm.")) {
                entry_fn = &f;
                break;
            }
        }
    }

    if (!entry_fn || entry_fn->isDeclaration()) {
        result.success = false;
        result.error_message = "Entry function '" + cfg_.entry_function + "' not found in module";
        return result;
    }

    // Auto-initialize symbolic parameters for standalone entry functions
    for (const llvm::Argument& arg : entry_fn->args()) {
        ValueId aid = reinterpret_cast<ValueId>(&arg);
        std::string aname = arg.hasName() ? arg.getName().str() : ("arg" + std::to_string(arg.getArgNo()));
        if (arg.getType()->isPointerTy()) {
            RegionId rid = root_state.allocate_region(
                "sym_" + aname, DisjointIntervals(64),
                /*is_heap=*/false, /*is_global=*/false, /*is_stack=*/false,
                /*initialized=*/true);
            if (MemoryRegion* reg = root_state.get_region(rid)) {
                reg->is_symbolic_input = true;
                reg->null_term_offset = DisjointIntervals(0, 63);
                AbstractValue elem = AbstractValue::make_int(DisjointIntervals::full_i32(), true);
                elem.sym_expr = LinearSymExpr{true, aid, "*" + aname, 1, 0};
                reg->default_element_value = elem;
            }
            root_state.set_value(
                aid, AbstractValue::make_valid_ptr(rid, DisjointIntervals(0), Nullability::MayNull));
            input_specs_.push_back(SymbolicInputSpec{
                "sym_arg_ptr", aname, "{&sym_" + aname + " (64 B), NULL}",
                entry_fn->getName().str(), {}});
        } else if (arg.getType()->isIntegerTy()) {
            uint32_t bw = arg.getType()->getIntegerBitWidth();
            AbstractValue iv = AbstractValue::make_int(DisjointIntervals::full_i64(bw, true), true);
            iv.sym_expr = LinearSymExpr{true, aid, aname, 1, 0};
            root_state.set_value(aid, iv);
            input_specs_.push_back(SymbolicInputSpec{
                "sym_arg_int", aname, iv.int_domain.to_string(),
                entry_fn->getName().str(), {}});
        } else {
            root_state.set_value(aid, AbstractValue::make_int(DisjointIntervals::full_i32(), true));
        }
    }

    run_start_time_ = std::chrono::steady_clock::now();
    last_progress_time_ = run_start_time_;
    std::vector<std::string> callstack;
    FunctionExecOutcome outcome = analyze_function(entry_fn, root_state, callstack, 0);

    // Check for leaked heap allocations still alive at exit
    std::vector<HazardReport> leak_hazards;
    for (const auto& kv : outcome.post_state.regions()) {
        const MemoryRegion& reg = *kv.second;
        if (reg.is_heap && reg.liveness == RegionLiveness::Alive) {
            HazardReport rep;
            rep.kind = HazardKind::MemoryLeak;
            rep.certainty = HazardCertainty::DefiniteBug;
            rep.loc = reg.alloc_loc;
            rep.function_name = entry_fn->getName().str();
            rep.analysis_name = cfg_.name;
            rep.predicate = "liveness(" + reg.name + ") == Alive at exit";
            rep.concrete_witness = reg.name + " never freed";
            rep.message = "Memory leak: heap allocation '" + reg.name + "' is not freed before exit";
            emit_hazard(rep, leak_hazards);
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    result.success = true;
    result.elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    result.alarms = alarms_;
    result.input_specs = input_specs_;
    result.coverage = coverage_;
    result.instructions_analyzed = instructions_analyzed_;
    result.equivalence_queries = equiv_cache_.total_queries();
    result.equivalence_cache_hits = equiv_cache_.cache_hits();
    result.remainder_slices = equiv_cache_.remainder_slices();
    result.smt_queries = smt_refiner_.total_queries();
    result.smt_false_positives_eliminated = smt_refiner_.unsat_eliminated();
    return result;
}

HazardGuidedAnalyzer::FunctionExecOutcome HazardGuidedAnalyzer::analyze_function(
    const llvm::Function* fn,
    const AbstractState& entry_state,
    std::vector<std::string>& callstack,
    uint32_t depth) {
    FunctionExecOutcome outcome;
    outcome.post_state = entry_state;

    if (!fn || fn->isDeclaration() || fn->empty()) {
        return outcome;
    }

    const std::string fname = fn->getName().str();
    if (depth > 14 || instructions_analyzed_ > 800000 ||
        std::find(callstack.begin(), callstack.end(), fname) != callstack.end()) {
        outcome.return_value = fn->getReturnType()->isPointerTy()
            ? AbstractValue::make_valid_ptr(0, DisjointIntervals(0), Nullability::MayNull)
            : AbstractValue::make_int(DisjointIntervals::full_i32(), true);
        return outcome;
    }

    const bool is_split_return_fn =
        std::find(cfg_.split_return_functions.begin(),
                  cfg_.split_return_functions.end(),
                  fname) != cfg_.split_return_functions.end();

    bool fn_has_return_inst = false;
    for (const llvm::BasicBlock& b : *fn) {
        if (llvm::isa<llvm::ReturnInst>(b.getTerminator())) {
            fn_has_return_inst = true;
            break;
        }
    }

    // Build relevant input keys (formal args + read globals)
    // Skip equivalence cache if function takes pointer arguments pointing to caller stack regions (out-params)
    bool has_pointer_arg = false;
    GraphNodeId fn_node_id = reinterpret_cast<GraphNodeId>(fn);
    std::vector<ValueId> relevant_inputs;
    for (const llvm::Argument& arg : fn->args()) {
        if (arg.getType()->isPointerTy()) {
            has_pointer_arg = true;
        }
        relevant_inputs.push_back(reinterpret_cast<ValueId>(&arg));
    }
    for (ValueId gvid : liveness_modref_.func_read_globals(fn)) {
        relevant_inputs.push_back(gvid);
    }

    // Query Graph Behavioral Equivalence Cache ONLY for value-parameterized functions
    // (avoids stale caller-stack RegionId collisions on out-parameter pointers!)
    EquivalenceQueryResult qr;
    if (!has_pointer_arg && !is_split_return_fn) {
        qr = equiv_cache_.query_and_slice_remainder(
            fn_node_id, entry_state, relevant_inputs);
    }

    if (qr.full_cache_hit || qr.partial_cache_hit) {
        coverage_.merge_cached_coverage(qr.cached_covered_lines, qr.cached_subcondition_masks);
        outcome.covered_lines.insert(qr.cached_covered_lines.begin(), qr.cached_covered_lines.end());
        for (const auto& kv : qr.cached_subcondition_masks) {
            outcome.subcond_masks[kv.first] |= kv.second;
        }
        for (const auto& h : qr.cached_hazards) {
            emit_hazard(h, outcome.local_hazards);
        }
        if (qr.full_cache_hit) {
            outcome.return_value = qr.cached_return_value;
            if (!qr.cached_post_states.empty()) {
                AbstractState cached_globals = qr.cached_post_states[0];
                for (size_t i = 1; i < qr.cached_post_states.size(); ++i) {
                    cached_globals = cached_globals.join(qr.cached_post_states[i]);
                }
                outcome.post_state.merge_regions_from(cached_globals, /*skip_stack=*/true);
            }
            return outcome;
        }
    }

    callstack.push_back(fname);

    const llvm::DataLayout& dl = mod_.getDataLayout();
    AbstractState start_state = qr.partial_cache_hit ? qr.remainder_state : entry_state;

    struct WorkItem {
        const llvm::BasicBlock* bb = nullptr;
        const llvm::BasicBlock* prev_bb = nullptr;
        AbstractState state;
        std::vector<PathConstraint> constraints;
        uint32_t start_inst_idx = 0;
        bool keep_split = false;
    };

    std::deque<WorkItem> worklist;
    auto push_worklist = [&](const llvm::BasicBlock* target_bb,
                             const llvm::BasicBlock* from_bb,
                             AbstractState st,
                             std::vector<PathConstraint> pcs,
                             bool keep_split = false) {
        if (!keep_split && !(is_split_return_fn && worklist.size() < 24)) {
            if (target_bb->phis().empty()) {
                for (auto& existing : worklist) {
                    if (existing.bb == target_bb && existing.start_inst_idx == 0 && !existing.keep_split) {
                        existing.state = existing.state.join(st);
                        return;
                    }
                }
            } else {
                for (auto& existing : worklist) {
                    if (existing.bb == target_bb && existing.prev_bb == from_bb &&
                        existing.start_inst_idx == 0 && !existing.keep_split) {
                        existing.state = existing.state.join(st);
                        return;
                    }
                }
            }
        }
        worklist.push_back(WorkItem{target_bb, from_bb, std::move(st), std::move(pcs), 0, keep_split});
    };
    push_worklist(&fn->getEntryBlock(), nullptr, start_state, {});

    std::unordered_map<const llvm::BasicBlock*, uint32_t> bb_order;
    uint32_t ord_idx = 0;
    for (const llvm::BasicBlock& b : *fn) {
        bb_order[&b] = ord_idx++;
    }

    std::unordered_map<const llvm::BasicBlock*, AbstractState> bb_visited_states;
    std::unordered_map<const llvm::BasicBlock*, uint32_t> bb_visit_counts;
    bool has_return = false;

    auto refine_branch_edge = [&](const llvm::Value* cond,
                                  bool take_true,
                                  AbstractState& st,
                                  std::vector<PathConstraint>& pcs) -> bool {
        if (!cond) return true;
        if (const auto* icmp = llvm::dyn_cast<llvm::ICmpInst>(cond)) {
            const llvm::Value* op0 = icmp->getOperand(0);
            const llvm::Value* op1 = icmp->getOperand(1);
            AbstractValue v0 = eval_operand(op0, st);
            AbstractValue v1 = eval_operand(op1, st);
            DisjointIntervals::CmpOp cmp = map_icmp_predicate(icmp->getPredicate());
            if (!take_true) cmp = negate_cmp(cmp);

            // Pointer vs NULL refinement (handle both `p != NULL` and Yoda `NULL != p`)
            const bool is_ptr_null_cmp =
                (v0.is_pointer && v1.must_be_null()) ||
                (v1.is_pointer && v0.must_be_null()) ||
                (op0->getType()->isPointerTy() && llvm::isa<llvm::ConstantPointerNull>(op1)) ||
                (op1->getType()->isPointerTy() && llvm::isa<llvm::ConstantPointerNull>(op0));
            if (is_ptr_null_cmp) {
                const bool lhs_is_null_const = llvm::isa<llvm::ConstantPointerNull>(op0) ||
                                               (v0.must_be_null() && !v1.must_be_null());
                const llvm::Value* ptr_val = lhs_is_null_const ? op1 : op0;
                AbstractValue ptr = lhs_is_null_const ? v1 : v0;
                ptr.is_pointer = true;
                if (cmp == DisjointIntervals::CmpOp::EQ) {
                    if (!ptr.may_be_null()) return false;
                    ptr.null_state = Nullability::MustNull;
                    ptr.ptr_targets.clear();
                } else if (cmp == DisjointIntervals::CmpOp::NE) {
                    if (ptr.must_be_null()) return false;
                    ptr.null_state = Nullability::MustNonNull;
                }
                st.set_value(reinterpret_cast<ValueId>(ptr_val), ptr);
                propagate_narrowed_to_slot(ptr_val, ptr, st);
                return true;
            }

            if (v0.is_pointer && v1.is_pointer) {
                return true;
            }

            // Integer interval refinement
            DisjointIntervals n0 = v0.int_domain.filter_by_cmp(cmp, v1.int_domain);
            DisjointIntervals n1 = v1.int_domain.filter_by_cmp(swap_cmp(cmp), v0.int_domain);
            if (n0.is_empty() || n1.is_empty()) return false;

            v0.int_domain = n0;
            v1.int_domain = n1;
            if (!llvm::isa<llvm::Constant>(op0)) {
                st.set_value(reinterpret_cast<ValueId>(op0), v0);
                propagate_narrowed_to_slot(op0, v0, st);
                // If op0 is a 1-byte character load (*c) and narrowed v0 excludes '\0' (0),
                // then pointer c cannot be at or past the region's guaranteed '\0' sentinel offset!
                if (!v0.int_domain.contains(0)) {
                    const llvm::Value* base_v = op0;
                    while (const auto* ci = llvm::dyn_cast<llvm::CastInst>(base_v)) {
                        base_v = ci->getOperand(0);
                    }
                    if (const auto* li = llvm::dyn_cast<llvm::LoadInst>(base_v)) {
                        const llvm::Value* ptr_op = li->getPointerOperand();
                        AbstractValue ptr_av = eval_operand(ptr_op, st);
                        bool updated_ptr = false;
                        for (auto& pt : ptr_av.ptr_targets) {
                            const MemoryRegion* reg = st.get_region(pt.region_id);
                            if (reg && reg->null_term_offset.max_value() < reg->byte_size.max_value()) {
                                int64_t max_non_null = reg->null_term_offset.max_value() - 1;
                                if (max_non_null >= 0 && pt.offset.max_value() > max_non_null) {
                                    pt.offset = pt.offset.intersect(DisjointIntervals(0, max_non_null));
                                    updated_ptr = true;
                                }
                            }
                        }
                        if (updated_ptr) {
                            st.set_value(reinterpret_cast<ValueId>(ptr_op), ptr_av);
                            propagate_narrowed_to_slot(ptr_op, ptr_av, st);
                        }
                    }
                }
                PathConstraint pc;
                pc.var_name = v0.sym_expr.is_valid ? v0.sym_expr.root_param_name : value_name(op0, "lhs");
                pc.allowed_domain = v0.sym_expr.is_valid ? v0.sym_expr.invert_range(n0) : n0;
                if (!pc.allowed_domain.is_empty()) pcs.push_back(pc);
            }
            if (!llvm::isa<llvm::Constant>(op1)) {
                st.set_value(reinterpret_cast<ValueId>(op1), v1);
                propagate_narrowed_to_slot(op1, v1, st);
                PathConstraint pc;
                pc.var_name = v1.sym_expr.is_valid ? v1.sym_expr.root_param_name : value_name(op1, "rhs");
                pc.allowed_domain = v1.sym_expr.is_valid ? v1.sym_expr.invert_range(n1) : n1;
                if (!pc.allowed_domain.is_empty()) pcs.push_back(pc);
            }
            return true;
        }

        // Direct i1 value
        AbstractValue cv = eval_operand(cond, st);
        DisjointIntervals narrowed = take_true
            ? cv.int_domain.remove_value(0)
            : cv.int_domain.intersect(DisjointIntervals(0));
        if (narrowed.is_empty()) return false;
        cv.int_domain = narrowed;
        if (!llvm::isa<llvm::Constant>(cond)) {
            st.set_value(reinterpret_cast<ValueId>(cond), cv);
        }
        return true;
    };

    while (!worklist.empty()) {
        WorkItem item = (worklist.back().keep_split || worklist.back().start_inst_idx > 0)
            ? std::move(worklist.back())
            : std::move(worklist.front());
        if (worklist.back().keep_split || worklist.back().start_inst_idx > 0) {
            worklist.pop_back();
        } else {
            worklist.pop_front();
        }

        const llvm::BasicBlock* bb = item.bb;
        AbstractState& state = item.state;

        bool is_backedge = (item.start_inst_idx == 0) && item.prev_bb && (bb_order[item.prev_bb] >= bb_order[bb]);
        if (is_backedge || (item.start_inst_idx == 0 && llvm::pred_size(bb) > 1)) {
            item.keep_split = false;
        }
        uint32_t& visits = bb_visit_counts[bb];
        if (item.start_inst_idx == 0) {
            uint32_t max_backedge_visits = fn_has_return_inst ? 8u : 3u;
            if (is_backedge && visits >= max_backedge_visits) continue;
            if (!item.keep_split && !is_split_return_fn && visits >= 24) continue;

            // 1. Evaluate PHI nodes FIRST so incoming edge values are reflected in state
            if (item.prev_bb) {
                std::vector<std::pair<ValueId, AbstractValue>> phi_updates;
                for (const llvm::PHINode& phi : bb->phis()) {
                    int idx = phi.getBasicBlockIndex(item.prev_bb);
                    if (idx >= 0) {
                        const llvm::Value* inc = phi.getIncomingValue(static_cast<unsigned>(idx));
                        phi_updates.emplace_back(reinterpret_cast<ValueId>(&phi), eval_operand(inc, state));
                    }
                }
                for (const auto& pu : phi_updates) {
                    state.set_value(pu.first, pu.second);
                }
            }

            // 2. Join-point liveness projection, Loop Widening, & subset early cut-off
            bool just_widened = false;
            if (!is_split_return_fn && !item.keep_split &&
                llvm::pred_size(bb) > 1 && !llvm::isa<llvm::ReturnInst>(bb->getTerminator())) {
                std::unordered_set<ValueId> live_set = liveness_modref_.live_in(bb);
                for (const llvm::Argument& a : fn->args()) {
                    if (a.getType()->isPointerTy()) {
                        live_set.insert(reinterpret_cast<ValueId>(&a));
                    }
                }
                AbstractState projected = state.project_live(live_set);
                auto vit = bb_visited_states.find(bb);
                if (vit != bb_visited_states.end()) {
                    if (projected.is_subset_of(vit->second)) {
                        continue;
                    }
                    constexpr int64_t kLoopWidenMax = 1048576;
                    constexpr int64_t kLoopWidenMin = -1048576;
                    if (is_backedge && visits >= 5 && !bb->phis().empty()) {
                        for (const llvm::PHINode& phi : bb->phis()) {
                            ValueId vid = reinterpret_cast<ValueId>(&phi);
                            if (!state.has_value(vid) || !vit->second.has_value(vid)) continue;
                            AbstractValue cur_v = state.get_value(vid);
                            const AbstractValue& old_v = vit->second.get_value(vid);
                            if (!cur_v.is_pointer && !cur_v.int_domain.is_empty() && !old_v.int_domain.is_empty()) {
                                int64_t lo = std::min(cur_v.int_domain.min_value(), old_v.int_domain.min_value());
                                int64_t hi = std::max(cur_v.int_domain.max_value(), old_v.int_domain.max_value());
                                if (cur_v.int_domain.max_value() > old_v.int_domain.max_value()) {
                                    hi = std::max(hi, kLoopWidenMax);
                                }
                                if (cur_v.int_domain.min_value() < old_v.int_domain.min_value()) {
                                    lo = std::min(lo, kLoopWidenMin);
                                }
                                bool expanded_beyond_old = (hi > old_v.int_domain.max_value()) ||
                                                           (lo < old_v.int_domain.min_value());
                                cur_v.int_domain = DisjointIntervals(lo, hi);
                                state.set_value(vid, cur_v);
                                if (expanded_beyond_old) {
                                    just_widened = true;
                                }
                            } else if (cur_v.is_pointer) {
                                for (auto& pt : cur_v.ptr_targets) {
                                    const MemoryRegion* reg = state.get_region(pt.region_id);
                                    int64_t max_off = reg ? std::max<int64_t>(0, reg->byte_size.max_value() - 1) : 1024;
                                    if (pt.offset.max_value() < max_off) {
                                        pt.offset = DisjointIntervals(0, max_off);
                                        just_widened = true;
                                    }
                                }
                                if (just_widened) {
                                    state.set_value(vid, cur_v);
                                }
                            }
                        }
                        projected = state.project_live(live_set);
                        if (!just_widened && projected.is_subset_of(vit->second)) {
                            continue;
                        }
                    }
                    vit->second = vit->second.join(projected);
                } else {
                    bb_visited_states.emplace(bb, projected);
                }
            }
            ++visits;
        }

        bool path_alive = true;
        uint32_t cur_inst_idx = 0;

        for (const llvm::Instruction& inst : *bb) {
            uint32_t inst_idx = cur_inst_idx++;
            if (inst_idx < item.start_inst_idx) continue;
            if (llvm::isa<llvm::PHINode>(inst)) continue;
            ++instructions_analyzed_;
            if ((instructions_analyzed_ & 0x3FFFULL) == 0) {
                auto now = std::chrono::steady_clock::now();
                double since_last = std::chrono::duration<double>(now - last_progress_time_).count();
                double threshold = verbose_ ? 1.0 : 3.0;
                if (since_last >= threshold) {
                    double elapsed_s = std::chrono::duration<double>(now - run_start_time_).count();
                    double rate = elapsed_s > 0.001 ? (static_cast<double>(instructions_analyzed_) / elapsed_s) : 0.0;
                    std::cerr << "  [" << cfg_.name << "] "
                              << std::fixed << std::setprecision(1) << elapsed_s << "s | "
                              << instructions_analyzed_ << " IR instrs ("
                              << static_cast<uint64_t>(rate) << " instr/s) | fn: " << fname
                              << " | cache hits: " << equiv_cache_.cache_hits()
                              << " | alarms: " << alarms_.size() << std::endl;
                    last_progress_time_ = now;
                }
            }

            SourceLoc loc = extract_source_loc(inst.getDebugLoc());
            if (loc.is_valid()) {
                coverage_.mark_line_covered(loc.file, loc.line);
                outcome.covered_lines.insert({loc.file, loc.line});
            }

            ValueId inst_vid = reinterpret_cast<ValueId>(&inst);

            // 1. AllocaInst
            if (const auto* ai = llvm::dyn_cast<llvm::AllocaInst>(&inst)) {
                int64_t elem_sz = ai->getAllocatedType()->isSized()
                    ? static_cast<int64_t>(dl.getTypeAllocSize(ai->getAllocatedType()))
                    : 4;
                AbstractValue arr_cnt = eval_operand(ai->getArraySize(), state);
                DisjointIntervals total_sz = arr_cnt.int_domain.mul(DisjointIntervals(std::max<int64_t>(1, elem_sz)));
                total_sz = total_sz.intersect(DisjointIntervals(1, DisjointIntervals::MAX_VAL));
                if (total_sz.is_empty()) total_sz = DisjointIntervals(std::max<int64_t>(1, elem_sz));

                std::string rname = ai->hasName() ? ai->getName().str() : ("stack_" + std::to_string(inst_vid));
                bool is_aggregate_buf = ai->getAllocatedType()->isArrayTy() || ai->getAllocatedType()->isStructTy();
                RegionId rid = state.allocate_region(
                    rname, total_sz, /*is_heap=*/false, /*is_global=*/false, /*is_stack=*/true,
                    /*initialized=*/is_aggregate_buf, loc);
                if (auto* reg = state.get_region(rid)) {
                    reg->null_term_offset = DisjointIntervals(0, DisjointIntervals::MAX_VAL);
                }
                state.set_value(inst_vid, AbstractValue::make_valid_ptr(rid, DisjointIntervals(0), Nullability::MustNonNull));
                continue;
            }

            // 2. LoadInst
            if (const auto* li = llvm::dyn_cast<llvm::LoadInst>(&inst)) {
                const llvm::Value* ptr_op = li->getPointerOperand();
                AbstractValue ptr = eval_operand(ptr_op, state);
                std::string pname = value_name(ptr_op, "ptr");
                int64_t access_sz = li->getType()->isSized()
                    ? static_cast<int64_t>(dl.getTypeStoreSize(li->getType()))
                    : 4;

                if (ptr.may_be_null()) {
                    HazardReport rep;
                    rep.kind = HazardKind::NullPointerDeref;
                    rep.certainty = HazardCertainty::DefiniteBug;
                    rep.loc = loc;
                    rep.function_name = fname;
                    rep.predicate = pname + " == NULL";
                    rep.concrete_witness = pname + " = NULL";
                    rep.message = "Null pointer dereference on read of '" + pname + "'";
                    rep.callstack = callstack;
                    emit_hazard(rep, outcome.local_hazards);

                    if (ptr.must_be_null()) {
                        path_alive = false;
                        break;
                    }
                    ptr.null_state = Nullability::MustNonNull;
                    state.set_value(reinterpret_cast<ValueId>(ptr_op), ptr);
                    propagate_narrowed_to_slot(ptr_op, ptr, state);
                }

                AbstractValue loaded_val;
                bool has_loaded = false;
                std::vector<PointerTarget> surviving_targets;

                for (auto target : ptr.ptr_targets) {
                    const MemoryRegion* reg = state.get_region(target.region_id);
                    if (!reg) continue;
                    if (reg->liveness == RegionLiveness::Freed) {
                        HazardReport rep;
                        rep.kind = HazardKind::UseAfterFree;
                        rep.loc = loc;
                        rep.function_name = fname;
                        rep.predicate = "region(" + reg->name + ") == Freed";
                        rep.concrete_witness = pname + " -> freed " + reg->name;
                        rep.message = "Use-after-free on read from '" + reg->name + "'";
                        rep.callstack = callstack;
                        emit_hazard(rep, outcome.local_hazards);
                        continue;
                    }

                    int64_t reg_max = reg->byte_size.max_value();
                    int64_t max_valid_off = std::max<int64_t>(-1, reg_max - access_sz);
                    DisjointIntervals oob_zone = DisjointIntervals(DisjointIntervals::MIN_VAL, -1)
                        .unite(DisjointIntervals(max_valid_off + 1, DisjointIntervals::MAX_VAL));
                    if (reg_max > 0 && target.offset.overlaps(oob_zone)) {
                        auto smt_res = smt_refiner_.verify_hazard(
                            pname + "_offset", target.offset, oob_zone, LinearSymExpr{}, item.constraints);
                        if (smt_res.is_satisfiable) {
                            HazardReport rep;
                            rep.kind = HazardKind::OutOfBoundsAccess;
                            rep.loc = loc;
                            rep.function_name = fname;
                            rep.predicate = "offset < 0 || offset + " + std::to_string(access_sz) +
                                            " > " + std::to_string(reg_max);
                            rep.concrete_witness = smt_res.concrete_witness;
                            rep.message = "Out-of-bounds read on '" + reg->name + "' (" + smt_res.concrete_witness + ")";
                            rep.callstack = callstack;
                            emit_hazard(rep, outcome.local_hazards);
                        }
                        if (max_valid_off >= 0) {
                            target.offset = target.offset.intersect(DisjointIntervals(0, max_valid_off));
                        } else {
                            target.offset = DisjointIntervals::empty();
                        }
                        if (target.offset.is_empty()) continue;
                    }

                    surviving_targets.push_back(target);
                    AbstractValue v = state.load_from_region(
                        target.region_id, target.offset, static_cast<uint32_t>(access_sz));
                    if (li->getType()->isPointerTy()) {
                        if (!v.is_pointer) {
                            v.is_pointer = true;
                        }
                        if (target.sym_index_id != 0 &&
                            state.is_indexed_nonnull(target.region_id, target.sym_index_id)) {
                            v.null_state = Nullability::MustNonNull;
                        }
                        if (v.ptr_targets.empty() && v.func_targets.empty() && !v.must_be_null() &&
                            target.region_id != 0) {
                            if (target.offset.is_singleton()) {
                                int64_t off_v = target.offset.singleton_value();
                                RegionId lazy_rid = state.allocate_region(
                                    "sym_ptr_" + std::to_string(target.region_id) + "_" + std::to_string(off_v),
                                    DisjointIntervals(4096),
                                    /*is_heap=*/false, /*is_global=*/reg->is_global, /*is_stack=*/false,
                                    /*initialized=*/true, loc);
                                if (MemoryRegion* lreg = state.get_region(lazy_rid)) {
                                    lreg->is_symbolic_input = true;
                                }
                                v = AbstractValue::make_valid_ptr(lazy_rid, DisjointIntervals(0), v.null_state);
                                state.store_to_region(target.region_id, target.offset, v);
                            } else if (MemoryRegion* mreg = state.get_region(target.region_id)) {
                                if (mreg->cached_elem_pointee_rid == 0) {
                                    RegionId elem_rid = state.allocate_region(
                                        "sym_elem_" + std::to_string(target.region_id),
                                        DisjointIntervals(4096),
                                        /*is_heap=*/false, /*is_global=*/mreg->is_global, /*is_stack=*/false,
                                        /*initialized=*/true, loc);
                                    if (MemoryRegion* ereg = state.get_region(elem_rid)) {
                                        ereg->is_symbolic_input = true;
                                    }
                                    mreg = state.get_region(target.region_id);
                                    if (mreg) mreg->cached_elem_pointee_rid = elem_rid;
                                }
                                if (mreg && mreg->cached_elem_pointee_rid != 0) {
                                    v = AbstractValue::make_valid_ptr(
                                        mreg->cached_elem_pointee_rid, DisjointIntervals(0), v.null_state);
                                }
                            }
                        }
                    }
                    loaded_val = has_loaded ? loaded_val.join(v) : v;
                    has_loaded = true;
                }

                if (!ptr.ptr_targets.empty() && surviving_targets.empty()) {
                    path_alive = false;
                    break;
                }

                if (!has_loaded) {
                    loaded_val = li->getType()->isPointerTy()
                        ? AbstractValue::make_valid_ptr(0, DisjointIntervals(0), Nullability::MayNull)
                        : AbstractValue::make_int(DisjointIntervals::full_i32(), true);
                } else if (!loaded_val.is_initialized) {
                    HazardReport rep;
                    rep.kind = HazardKind::UninitializedRead;
                    rep.loc = loc;
                    rep.function_name = fname;
                    rep.predicate = "uninitialized(" + pname + ")";
                    rep.concrete_witness = pname + " is uninitialized";
                    rep.message = "Read of uninitialized memory via '" + pname + "'";
                    rep.callstack = callstack;
                    emit_hazard(rep, outcome.local_hazards);
                    loaded_val.is_initialized = true;
                }

                if (li->getType()->isPointerTy()) {
                    loaded_val.is_pointer = true;
                } else if (li->getType()->isIntegerTy()) {
                    uint32_t lbw = li->getType()->getIntegerBitWidth();
                    if (lbw < 32) {
                        loaded_val.int_domain = loaded_val.int_domain.intersect(DisjointIntervals::full_i64(lbw, true));
                        if (loaded_val.int_domain.is_empty()) {
                            loaded_val.int_domain = DisjointIntervals::full_i64(lbw, true);
                        }
                    }
                }
                state.set_value(inst_vid, loaded_val);
                continue;
            }

            // 3. StoreInst
            if (const auto* si = llvm::dyn_cast<llvm::StoreInst>(&inst)) {
                AbstractValue val = eval_operand(si->getValueOperand(), state);
                const llvm::Value* ptr_op = si->getPointerOperand();
                AbstractValue ptr = eval_operand(ptr_op, state);
                std::string pname = value_name(ptr_op, "ptr");
                int64_t access_sz = si->getValueOperand()->getType()->isSized()
                    ? static_cast<int64_t>(dl.getTypeStoreSize(si->getValueOperand()->getType()))
                    : 4;

                if (ptr.may_be_null()) {
                    HazardReport rep;
                    rep.kind = HazardKind::NullPointerDeref;
                    rep.loc = loc;
                    rep.function_name = fname;
                    rep.predicate = pname + " == NULL";
                    rep.concrete_witness = pname + " = NULL";
                    rep.message = "Null pointer dereference on write to '" + pname + "'";
                    rep.callstack = callstack;
                    emit_hazard(rep, outcome.local_hazards);

                    if (ptr.must_be_null()) {
                        path_alive = false;
                        break;
                    }
                    ptr.null_state = Nullability::MustNonNull;
                    state.set_value(reinterpret_cast<ValueId>(ptr_op), ptr);
                    propagate_narrowed_to_slot(ptr_op, ptr, state);
                }

                std::vector<PointerTarget> surviving_targets;
                for (auto target : ptr.ptr_targets) {
                    MemoryRegion* reg = state.get_region(target.region_id);
                    if (!reg) continue;
                    if (reg->liveness == RegionLiveness::Freed) {
                        HazardReport rep;
                        rep.kind = HazardKind::UseAfterFree;
                        rep.loc = loc;
                        rep.function_name = fname;
                        rep.predicate = "region(" + reg->name + ") == Freed";
                        rep.concrete_witness = pname + " -> freed " + reg->name;
                        rep.message = "Use-after-free on write to '" + reg->name + "'";
                        rep.callstack = callstack;
                        emit_hazard(rep, outcome.local_hazards);
                        continue;
                    }

                    int64_t reg_max = reg->byte_size.max_value();
                    int64_t max_valid_off = std::max<int64_t>(-1, reg_max - access_sz);
                    DisjointIntervals oob_zone = DisjointIntervals(DisjointIntervals::MIN_VAL, -1)
                        .unite(DisjointIntervals(max_valid_off + 1, DisjointIntervals::MAX_VAL));
                    if (reg_max > 0 && target.offset.overlaps(oob_zone)) {
                        auto smt_res = smt_refiner_.verify_hazard(
                            pname + "_offset", target.offset, oob_zone, LinearSymExpr{}, item.constraints);
                        if (smt_res.is_satisfiable) {
                            HazardReport rep;
                            rep.kind = HazardKind::OutOfBoundsAccess;
                            rep.loc = loc;
                            rep.function_name = fname;
                            rep.predicate = "offset < 0 || offset + " + std::to_string(access_sz) +
                                            " > " + std::to_string(reg_max);
                            rep.concrete_witness = smt_res.concrete_witness;
                            rep.message = "Out-of-bounds write on '" + reg->name + "' (" + smt_res.concrete_witness + ")";
                            rep.callstack = callstack;
                            emit_hazard(rep, outcome.local_hazards);
                        }
                        if (max_valid_off >= 0) {
                            target.offset = target.offset.intersect(DisjointIntervals(0, max_valid_off));
                        } else {
                            target.offset = DisjointIntervals::empty();
                        }
                        if (target.offset.is_empty()) continue;
                    }

                    surviving_targets.push_back(target);
                    state.store_to_region(target.region_id, target.offset, val);
                    state.invalidate_index_var(reinterpret_cast<ValueId>(ptr_op));

                    // Track explicit '\0' termination writes
                    if (access_sz == 1 && val.int_domain.is_singleton() && val.int_domain.singleton_value() == 0) {
                        if (MemoryRegion* mreg = state.get_region(target.region_id)) {
                            int64_t new_max = std::min(mreg->null_term_offset.max_value(), target.offset.max_value());
                            mreg->null_term_offset = DisjointIntervals(0, std::max<int64_t>(0, new_max));
                        }
                    }
                }

                if (!ptr.ptr_targets.empty() && surviving_targets.empty()) {
                    path_alive = false;
                    break;
                }
                continue;
            }

            // 4. GetElementPtrInst
            if (const auto* gep = llvm::dyn_cast<llvm::GetElementPtrInst>(&inst)) {
                AbstractValue base = eval_operand(gep->getPointerOperand(), state);
                DisjointIntervals total_off(0);
                llvm::Type* cur_ty = gep->getSourceElementType();
                ValueId dyn_sym_idx = 0;

                auto extract_dyn_idx_id = [](const llvm::Value* iv) -> ValueId {
                    const llvm::Value* u = iv;
                    while (const auto* ci = llvm::dyn_cast<llvm::CastInst>(u)) {
                        u = ci->getOperand(0);
                    }
                    if (llvm::isa<llvm::Constant>(u)) return 0;
                    if (const auto* li = llvm::dyn_cast<llvm::LoadInst>(u)) {
                        return reinterpret_cast<ValueId>(li->getPointerOperand());
                    }
                    return reinterpret_cast<ValueId>(u);
                };

                for (unsigned idx_i = 0; idx_i < gep->getNumIndices(); ++idx_i) {
                    const llvm::Value* idx_val = gep->getOperand(1 + idx_i);
                    AbstractValue idx_av = eval_operand(idx_val, state);
                    if (idx_i == 0) {
                        int64_t sz = cur_ty->isSized() ? static_cast<int64_t>(dl.getTypeAllocSize(cur_ty)) : 1;
                        total_off = total_off.add(idx_av.int_domain.mul(DisjointIntervals(sz)));
                        if (!idx_av.int_domain.is_singleton()) {
                            dyn_sym_idx = extract_dyn_idx_id(idx_val);
                        }
                    } else if (auto* st = llvm::dyn_cast<llvm::StructType>(cur_ty)) {
                        unsigned field_idx = idx_av.int_domain.is_singleton()
                            ? static_cast<unsigned>(std::max<int64_t>(0, idx_av.int_domain.singleton_value()))
                            : 0;
                        if (field_idx < st->getNumElements()) {
                            int64_t foff = static_cast<int64_t>(dl.getStructLayout(st)->getElementOffset(field_idx));
                            total_off = total_off.add(DisjointIntervals(foff));
                            cur_ty = st->getElementType(field_idx);
                        }
                    } else if (auto* at = llvm::dyn_cast<llvm::ArrayType>(cur_ty)) {
                        cur_ty = at->getElementType();
                        int64_t esz = cur_ty->isSized() ? static_cast<int64_t>(dl.getTypeAllocSize(cur_ty)) : 1;
                        total_off = total_off.add(idx_av.int_domain.mul(DisjointIntervals(esz)));
                        if (!idx_av.int_domain.is_singleton()) {
                            dyn_sym_idx = extract_dyn_idx_id(idx_val);
                        }
                    } else if (auto* vt = llvm::dyn_cast<llvm::VectorType>(cur_ty)) {
                        cur_ty = vt->getElementType();
                        int64_t esz = cur_ty->isSized() ? static_cast<int64_t>(dl.getTypeAllocSize(cur_ty)) : 1;
                        total_off = total_off.add(idx_av.int_domain.mul(DisjointIntervals(esz)));
                    }
                }

                for (auto& t : base.ptr_targets) {
                    t.offset = t.offset.add(total_off);
                    if (dyn_sym_idx != 0) {
                        t.sym_index_id = dyn_sym_idx;
                    }
                }
                base.is_pointer = true;
                state.set_value(inst_vid, base);
                continue;
            }

            // 5. BinaryOperator
            if (const auto* bo = llvm::dyn_cast<llvm::BinaryOperator>(&inst)) {
                AbstractValue lhs = eval_operand(bo->getOperand(0), state);
                AbstractValue rhs = eval_operand(bo->getOperand(1), state);
                uint32_t bw = bo->getType()->isIntegerTy() ? bo->getType()->getIntegerBitWidth() : 32;
                AbstractValue res = AbstractValue::make_int(DisjointIntervals::full_i64(bw, true),
                                                            lhs.is_initialized && rhs.is_initialized);

                auto opc = bo->getOpcode();
                if (opc == llvm::Instruction::SDiv || opc == llvm::Instruction::UDiv ||
                    opc == llvm::Instruction::SRem || opc == llvm::Instruction::URem) {
                    if (rhs.int_domain.contains(0)) {
                        std::string rname = value_name(bo->getOperand(1), "divisor");
                        auto smt_res = smt_refiner_.verify_hazard(
                            rname, rhs.int_domain, DisjointIntervals(0), rhs.sym_expr, item.constraints);
                        if (smt_res.is_satisfiable) {
                            HazardReport rep;
                            rep.kind = HazardKind::DivisionByZero;
                            rep.loc = loc;
                            rep.function_name = fname;
                            rep.predicate = rname + " == 0";
                            rep.concrete_witness = smt_res.concrete_witness;
                            rep.message = "Division/modulo by zero (" + smt_res.concrete_witness + ")";
                            rep.callstack = callstack;
                            emit_hazard(rep, outcome.local_hazards);
                        }
                        rhs.int_domain = rhs.int_domain.remove_value(0);
                        if (rhs.int_domain.is_empty()) {
                            path_alive = false;
                            break;
                        }
                        state.set_value(reinterpret_cast<ValueId>(bo->getOperand(1)), rhs);
                    }
                    res.int_domain = (opc == llvm::Instruction::SDiv || opc == llvm::Instruction::UDiv)
                        ? lhs.int_domain.sdiv(rhs.int_domain)
                        : lhs.int_domain.srem(rhs.int_domain);
                } else if (opc == llvm::Instruction::Add || opc == llvm::Instruction::Sub ||
                           opc == llvm::Instruction::Mul) {
                    // Check signed integer overflow (nsw) for 32-bit signed arithmetic
                    if (bo->hasNoSignedWrap() && bw == 32) {
                        __int128 lo64 = 0, hi64 = 0;
                        if (opc == llvm::Instruction::Add) {
                            lo64 = static_cast<__int128>(lhs.int_domain.min_value()) + rhs.int_domain.min_value();
                            hi64 = static_cast<__int128>(lhs.int_domain.max_value()) + rhs.int_domain.max_value();
                        } else if (opc == llvm::Instruction::Sub) {
                            lo64 = static_cast<__int128>(lhs.int_domain.min_value()) - rhs.int_domain.max_value();
                            hi64 = static_cast<__int128>(lhs.int_domain.max_value()) - rhs.int_domain.min_value();
                        } else {
                            __int128 p1 = static_cast<__int128>(lhs.int_domain.min_value()) * rhs.int_domain.min_value();
                            __int128 p2 = static_cast<__int128>(lhs.int_domain.min_value()) * rhs.int_domain.max_value();
                            __int128 p3 = static_cast<__int128>(lhs.int_domain.max_value()) * rhs.int_domain.min_value();
                            __int128 p4 = static_cast<__int128>(lhs.int_domain.max_value()) * rhs.int_domain.max_value();
                            lo64 = std::min({p1, p2, p3, p4});
                            hi64 = std::max({p1, p2, p3, p4});
                        }
                        constexpr __int128 kI32Min = -2147483648LL;
                        constexpr __int128 kI32Max = 2147483647LL;
                        if (lo64 < kI32Min || hi64 > kI32Max) {
                            std::string oname = value_name(bo, "arith_result");
                            std::string witness = (hi64 > kI32Max)
                                ? (oname + " > INT32_MAX (" + std::to_string(static_cast<long long>(std::min<__int128>(hi64, std::numeric_limits<long long>::max()))) + ")")
                                : (oname + " < INT32_MIN (" + std::to_string(static_cast<long long>(std::max<__int128>(lo64, std::numeric_limits<long long>::min()))) + ")");
                            HazardReport rep;
                            rep.kind = HazardKind::SignedIntegerOverflow;
                            rep.loc = loc;
                            rep.function_name = fname;
                            rep.predicate = "result < INT32_MIN || result > INT32_MAX";
                            rep.concrete_witness = witness;
                            rep.message = "Signed integer overflow in arithmetic operation (" + witness + ")";
                            rep.callstack = callstack;
                            emit_hazard(rep, outcome.local_hazards);
                        }
                    }

                    if (opc == llvm::Instruction::Add) {
                        res.int_domain = lhs.int_domain.add(rhs.int_domain);
                        if (lhs.sym_expr.is_valid && rhs.int_domain.is_singleton()) {
                            res.sym_expr = lhs.sym_expr;
                            res.sym_expr.addend += rhs.int_domain.singleton_value();
                        } else if (rhs.sym_expr.is_valid && lhs.int_domain.is_singleton()) {
                            res.sym_expr = rhs.sym_expr;
                            res.sym_expr.addend += lhs.int_domain.singleton_value();
                        }
                    } else if (opc == llvm::Instruction::Sub) {
                        res.int_domain = lhs.int_domain.sub(rhs.int_domain);
                        if (lhs.sym_expr.is_valid && rhs.int_domain.is_singleton()) {
                            res.sym_expr = lhs.sym_expr;
                            res.sym_expr.addend -= rhs.int_domain.singleton_value();
                        } else if (rhs.sym_expr.is_valid && lhs.int_domain.is_singleton()) {
                            res.sym_expr = rhs.sym_expr;
                            res.sym_expr.coeff = -res.sym_expr.coeff;
                            res.sym_expr.addend = lhs.int_domain.singleton_value() - res.sym_expr.addend;
                        }
                    } else {
                        res.int_domain = lhs.int_domain.mul(rhs.int_domain);
                        if (lhs.sym_expr.is_valid && rhs.int_domain.is_singleton()) {
                            int64_t c = rhs.int_domain.singleton_value();
                            res.sym_expr = lhs.sym_expr;
                            res.sym_expr.coeff *= c;
                            res.sym_expr.addend *= c;
                        } else if (rhs.sym_expr.is_valid && lhs.int_domain.is_singleton()) {
                            int64_t c = lhs.int_domain.singleton_value();
                            res.sym_expr = rhs.sym_expr;
                            res.sym_expr.coeff *= c;
                            res.sym_expr.addend *= c;
                        }
                    }
                    if (bw == 32 && bo->hasNoSignedWrap()) {
                        res.int_domain = res.int_domain.intersect(DisjointIntervals::full_i32());
                        if (res.int_domain.is_empty()) res.int_domain = DisjointIntervals::full_i32();
                    }
                } else if (opc == llvm::Instruction::Shl || opc == llvm::Instruction::AShr ||
                           opc == llvm::Instruction::LShr) {
                    DisjointIntervals bad_shift = DisjointIntervals(DisjointIntervals::MIN_VAL, -1)
                        .unite(DisjointIntervals(static_cast<int64_t>(bw), DisjointIntervals::MAX_VAL));
                    if (rhs.int_domain.overlaps(bad_shift)) {
                        std::string sname = value_name(bo->getOperand(1), "shift_amt");
                        auto smt_res = smt_refiner_.verify_hazard(
                            sname, rhs.int_domain, bad_shift, rhs.sym_expr, item.constraints);
                        if (smt_res.is_satisfiable) {
                            HazardReport rep;
                            rep.kind = HazardKind::InvalidBitShift;
                            rep.loc = loc;
                            rep.function_name = fname;
                            rep.predicate = sname + " < 0 || " + sname + " >= " + std::to_string(bw);
                            rep.concrete_witness = smt_res.concrete_witness;
                            rep.message = "Invalid bit shift amount (" + smt_res.concrete_witness + ")";
                            rep.callstack = callstack;
                            emit_hazard(rep, outcome.local_hazards);
                        }
                        rhs.int_domain = rhs.int_domain.intersect(DisjointIntervals(0, static_cast<int64_t>(bw) - 1));
                        if (rhs.int_domain.is_empty()) {
                            path_alive = false;
                            break;
                        }
                    }
                    res.int_domain = (opc == llvm::Instruction::Shl)
                        ? lhs.int_domain.shl(rhs.int_domain, bw)
                        : lhs.int_domain.ashr(rhs.int_domain, bw);
                } else if (opc == llvm::Instruction::And) {
                    res.int_domain = lhs.int_domain.bitwise_and(rhs.int_domain);
                } else if (opc == llvm::Instruction::Or) {
                    res.int_domain = lhs.int_domain.bitwise_or(rhs.int_domain);
                } else if (opc == llvm::Instruction::Xor) {
                    res.int_domain = lhs.int_domain.bitwise_xor(rhs.int_domain);
                }

                state.set_value(inst_vid, res);
                continue;
            }

            // 6. ICmpInst
            if (const auto* icmp = llvm::dyn_cast<llvm::ICmpInst>(&inst)) {
                AbstractValue lhs = eval_operand(icmp->getOperand(0), state);
                AbstractValue rhs = eval_operand(icmp->getOperand(1), state);
                DisjointIntervals::CmpOp cmp = map_icmp_predicate(icmp->getPredicate());

                bool can_true = false;
                bool can_false = false;

                const bool is_ptr_null_icmp =
                    (lhs.is_pointer && rhs.must_be_null()) ||
                    (rhs.is_pointer && lhs.must_be_null()) ||
                    (icmp->getOperand(0)->getType()->isPointerTy() &&
                     llvm::isa<llvm::ConstantPointerNull>(icmp->getOperand(1))) ||
                    (icmp->getOperand(1)->getType()->isPointerTy() &&
                     llvm::isa<llvm::ConstantPointerNull>(icmp->getOperand(0)));
                if (is_ptr_null_icmp) {
                    const bool lhs_is_null_const = llvm::isa<llvm::ConstantPointerNull>(icmp->getOperand(0)) ||
                                                   (lhs.must_be_null() && !rhs.must_be_null());
                    const AbstractValue& ptr = lhs_is_null_const ? rhs : lhs;
                    if (cmp == DisjointIntervals::CmpOp::EQ) {
                        can_true = ptr.may_be_null();
                        can_false = !ptr.must_be_null();
                    } else if (cmp == DisjointIntervals::CmpOp::NE) {
                        can_true = !ptr.must_be_null();
                        can_false = ptr.may_be_null();
                    } else {
                        can_true = true;
                        can_false = true;
                    }
                } else if (lhs.is_pointer || rhs.is_pointer) {
                    can_true = true;
                    can_false = true;
                } else {
                    auto out = lhs.int_domain.eval_cmp(cmp, rhs.int_domain);
                    can_true = out.can_be_true;
                    can_false = out.can_be_false;
                }

                DisjointIntervals bool_dom = DisjointIntervals::empty();
                if (can_true && can_false) bool_dom = DisjointIntervals(0, 1);
                else if (can_true) bool_dom = DisjointIntervals(1);
                else if (can_false) bool_dom = DisjointIntervals(0);
                state.set_value(inst_vid, AbstractValue::make_int(bool_dom, true));
                continue;
            }

            // 7. Cast instructions
            if (const auto* ci = llvm::dyn_cast<llvm::CastInst>(&inst)) {
                AbstractValue src = eval_operand(ci->getOperand(0), state);
                if (llvm::isa<llvm::TruncInst>(ci) && ci->getType()->isIntegerTy(1)) {
                    bool can_t = !src.int_domain.remove_value(0).is_empty();
                    bool can_f = src.int_domain.contains(0);
                    if (can_t && can_f) src.int_domain = DisjointIntervals(0, 1);
                    else if (can_t) src.int_domain = DisjointIntervals(1);
                    else src.int_domain = DisjointIntervals(0);
                } else if (llvm::isa<llvm::ZExtInst>(ci) && ci->getOperand(0)->getType()->isIntegerTy()) {
                    unsigned src_bw = ci->getOperand(0)->getType()->getIntegerBitWidth();
                    if (src_bw == 1) {
                        bool can_t = !src.int_domain.remove_value(0).is_empty();
                        bool can_f = src.int_domain.contains(0);
                        if (can_t && can_f) src.int_domain = DisjointIntervals(0, 1);
                        else if (can_t) src.int_domain = DisjointIntervals(1);
                        else src.int_domain = DisjointIntervals(0);
                    } else if (src_bw <= 32) {
                        int64_t u_max = (1LL << src_bw) - 1;
                        int64_t s_min = -(1LL << (src_bw - 1));
                        DisjointIntervals pos_part = src.int_domain.intersect(DisjointIntervals(0, u_max));
                        DisjointIntervals neg_part = src.int_domain
                            .intersect(DisjointIntervals(s_min, -1))
                            .add(DisjointIntervals(1LL << src_bw));
                        src.int_domain = pos_part.unite(neg_part);
                    }
                }
                if (ci->getType()->isPointerTy()) {
                    src.is_pointer = true;
                }
                state.set_value(inst_vid, src);
                continue;
            }

            // 8. SelectInst
            if (const auto* sel = llvm::dyn_cast<llvm::SelectInst>(&inst)) {
                AbstractValue cond = eval_operand(sel->getCondition(), state);
                AbstractValue tv = eval_operand(sel->getTrueValue(), state);
                AbstractValue fv = eval_operand(sel->getFalseValue(), state);
                bool can_t = !cond.int_domain.remove_value(0).is_empty();
                bool can_f = cond.int_domain.contains(0);
                if (can_t && !can_f) state.set_value(inst_vid, tv);
                else if (!can_t && can_f) state.set_value(inst_vid, fv);
                else state.set_value(inst_vid, tv.join(fv));
                continue;
            }

            // 9. CallBase
            if (const auto* call = llvm::dyn_cast<llvm::CallBase>(&inst)) {
                const llvm::Function* callee = call->getCalledFunction();
                if (callee) {
                    std::string cname = callee->getName().str();
                    if (cname.rfind("llvm.dbg.", 0) == 0 ||
                        cname.rfind("llvm.lifetime.", 0) == 0 ||
                        cname.rfind("llvm.var.annotation", 0) == 0 ||
                        cname.rfind("llvm.annotation.", 0) == 0) {
                        continue;
                    }
                    if (cname == "llvm.assume") {
                        if (call->arg_size() >= 1) {
                            if (!refine_branch_edge(call->getArgOperand(0), true, state, item.constraints)) {
                                path_alive = false;
                                break;
                            }
                        }
                        continue;
                    }

                    if (summaries_.is_summarized_function(cname)) {
                        SummaryCallResult sres = summaries_.evaluate_call(
                            cname, *call, state, mod_, loc, fname, cfg_.name);
                        if (sres.input_spec.has_value()) {
                            const auto& ispec = *sres.input_spec;
                            std::string ikey = ispec.loc.to_string() + "|" + ispec.kind + "|" + ispec.range_desc;
                            if (deduplicated_input_keys_.insert(ikey).second) {
                                input_specs_.push_back(ispec);
                            }
                        }
                        for (auto& h : sres.hazards) {
                            h.callstack = callstack;
                            emit_hazard(h, outcome.local_hazards);
                        }
                        state = std::move(sres.updated_state);
                        if (cname == "__sqc_assume" && call->arg_size() >= 1) {
                            if (!refine_branch_edge(call->getArgOperand(0), true, state, item.constraints)) {
                                path_alive = false;
                                break;
                            }
                        }
                        if (!call->getType()->isVoidTy()) {
                            state.set_value(inst_vid, sres.return_value);
                        }
                        continue;
                    }

                    if (!callee->isDeclaration()) {
                        bool callee_has_ptr_arg = false;
                        AbstractState callee_state = state;
                        unsigned arg_i = 0;
                        for (const llvm::Argument& formal : callee->args()) {
                            if (formal.getType()->isPointerTy()) {
                                callee_has_ptr_arg = true;
                            }
                            if (arg_i < call->arg_size()) {
                                callee_state.set_value(
                                    reinterpret_cast<ValueId>(&formal),
                                    eval_operand(call->getArgOperand(arg_i), state));
                            }
                            ++arg_i;
                        }
                        FunctionExecOutcome sub = analyze_function(callee, callee_state, callstack, depth + 1);
                        outcome.covered_lines.insert(sub.covered_lines.begin(), sub.covered_lines.end());
                        for (const auto& kv : sub.subcond_masks) {
                            outcome.subcond_masks[kv.first] |= kv.second;
                        }
                        outcome.local_hazards.insert(
                            outcome.local_hazards.end(),
                            sub.local_hazards.begin(),
                            sub.local_hazards.end());

                        if (sub.split_returns.size() > 1) {
                            // Push forked continuations in reverse order so branch 0 runs first
                            for (size_t k = sub.split_returns.size() - 1; k >= 1; --k) {
                                AbstractState fork_st = state;
                                fork_st.merge_regions_from(sub.split_returns[k].second, !callee_has_ptr_arg);
                                if (!call->getType()->isVoidTy()) {
                                    fork_st.set_value(inst_vid, sub.split_returns[k].first);
                                }
                                worklist.push_back(WorkItem{
                                    bb, item.prev_bb, std::move(fork_st), item.constraints,
                                    inst_idx + 1, /*keep_split=*/true});
                            }
                            state.merge_regions_from(sub.split_returns[0].second, !callee_has_ptr_arg);
                            if (!call->getType()->isVoidTy()) {
                                state.set_value(inst_vid, sub.split_returns[0].first);
                            }
                            item.keep_split = true;
                            continue;
                        }

                        // Propagate modified regions back to caller state
                        state.merge_regions_from(sub.post_state, !callee_has_ptr_arg);
                        if (!call->getType()->isVoidTy()) {
                            state.set_value(inst_vid, sub.return_value);
                        }
                        continue;
                    }

                    // Auto-stub undefined external function
                    if (!call->getType()->isVoidTy()) {
                        if (call->getType()->isPointerTy()) {
                            RegionId rid = state.allocate_region(
                                "ext_" + cname, DisjointIntervals(64),
                                false, false, false, true, loc);
                            state.set_value(
                                inst_vid,
                                AbstractValue::make_valid_ptr(rid, DisjointIntervals(0), Nullability::MustNonNull));
                        } else if (call->getType()->isIntegerTy()) {
                            uint32_t bw = call->getType()->getIntegerBitWidth();
                            state.set_value(
                                inst_vid,
                                AbstractValue::make_int(DisjointIntervals::full_i64(bw, true), true));
                        } else {
                            state.set_value(
                                inst_vid,
                                AbstractValue::make_int(DisjointIntervals::full_i32(), true));
                        }
                    }
                    continue;
                } else {
                    // Indirect call via function pointer
                    AbstractValue fp = eval_operand(call->getCalledOperand(), state);
                    std::string fp_name = value_name(call->getCalledOperand(), "fn_ptr");
                    if (fp.may_be_null()) {
                        HazardReport rep;
                        rep.kind = HazardKind::NullPointerDeref;
                        rep.loc = loc;
                        rep.function_name = fname;
                        rep.predicate = fp_name + " == NULL";
                        rep.concrete_witness = fp_name + " = NULL";
                        rep.message = "Null function pointer call ('" + fp_name + "')";
                        rep.callstack = callstack;
                        emit_hazard(rep, outcome.local_hazards);
                        if (fp.must_be_null()) {
                            path_alive = false;
                            break;
                        }
                    }

                    if (!fp.func_targets.empty()) {
                        AbstractValue joined_ret;
                        AbstractState joined_post;
                        bool first_tgt = true;
                        for (const llvm::Function* tgt : fp.func_targets) {
                            if (!tgt || tgt->isDeclaration()) continue;
                            AbstractState callee_state = state;
                            unsigned arg_i = 0;
                            for (const llvm::Argument& formal : tgt->args()) {
                                if (arg_i < call->arg_size()) {
                                    callee_state.set_value(
                                        reinterpret_cast<ValueId>(&formal),
                                        eval_operand(call->getArgOperand(arg_i), state));
                                }
                                ++arg_i;
                            }
                            FunctionExecOutcome sub = analyze_function(tgt, callee_state, callstack, depth + 1);
                            outcome.covered_lines.insert(sub.covered_lines.begin(), sub.covered_lines.end());
                            for (const auto& kv : sub.subcond_masks) {
                                outcome.subcond_masks[kv.first] |= kv.second;
                            }
                            outcome.local_hazards.insert(
                                outcome.local_hazards.end(),
                                sub.local_hazards.begin(),
                                sub.local_hazards.end());
                            if (first_tgt) {
                                joined_ret = sub.return_value;
                                joined_post = sub.post_state;
                                first_tgt = false;
                            } else {
                                joined_ret = joined_ret.join(sub.return_value);
                                joined_post = joined_post.join(sub.post_state);
                            }
                        }
                        if (!first_tgt) {
                            state.merge_regions_from(joined_post, /*skip_stack=*/false);
                            if (!call->getType()->isVoidTy()) {
                                state.set_value(inst_vid, joined_ret);
                            }
                        }
                    } else if (!call->getType()->isVoidTy()) {
                        state.set_value(inst_vid, AbstractValue::make_int(DisjointIntervals::full_i32(), true));
                    }
                    continue;
                }
            }

            // 10. BranchInst
            if (const auto* bi = llvm::dyn_cast<llvm::BranchInst>(&inst)) {
                if (bi->isUnconditional()) {
                    push_worklist(bi->getSuccessor(0), bb, std::move(state), std::move(item.constraints), item.keep_split);
                } else {
                    AbstractValue cond_v = eval_operand(bi->getCondition(), state);
                    bool can_true = !cond_v.int_domain.remove_value(0).is_empty();
                    bool can_false = cond_v.int_domain.contains(0);

                    if (loc.is_valid()) {
                        coverage_.record_branch_outcome(loc, can_true, can_false);
                        uint8_t mask = (can_true ? 0x1 : 0) | (can_false ? 0x2 : 0);
                        outcome.subcond_masks[subcond_key(loc)] |= mask;
                    }

                    if (can_true) {
                        AbstractState t_state = state;
                        auto t_pcs = item.constraints;
                        if (refine_branch_edge(bi->getCondition(), true, t_state, t_pcs)) {
                            push_worklist(bi->getSuccessor(0), bb, std::move(t_state), std::move(t_pcs), item.keep_split);
                        }
                    }
                    if (can_false) {
                        AbstractState f_state = state;
                        auto f_pcs = item.constraints;
                        if (refine_branch_edge(bi->getCondition(), false, f_state, f_pcs)) {
                            push_worklist(bi->getSuccessor(1), bb, std::move(f_state), std::move(f_pcs), item.keep_split);
                        }
                    }
                }
                break;
            }

            // 11. SwitchInst
            if (const auto* sw = llvm::dyn_cast<llvm::SwitchInst>(&inst)) {
                const llvm::Value* cond_op = sw->getCondition();
                AbstractValue cond_v = eval_operand(cond_op, state);
                DisjointIntervals default_dom = cond_v.int_domain;

                for (const auto& c : sw->cases()) {
                    int64_t cv = c.getCaseValue()->getSExtValue();
                    if (cond_v.int_domain.contains(cv)) {
                        AbstractState c_state = state;
                        AbstractValue narrowed = cond_v;
                        narrowed.int_domain = DisjointIntervals(cv);
                        c_state.set_value(reinterpret_cast<ValueId>(cond_op), narrowed);
                        propagate_narrowed_to_slot(cond_op, narrowed, c_state);
                        push_worklist(c.getCaseSuccessor(), bb, std::move(c_state), item.constraints, item.keep_split);
                        default_dom = default_dom.remove_value(cv);
                    }
                }
                if (!default_dom.is_empty()) {
                    AbstractState d_state = state;
                    AbstractValue narrowed = cond_v;
                    narrowed.int_domain = default_dom;
                    d_state.set_value(reinterpret_cast<ValueId>(cond_op), narrowed);
                    propagate_narrowed_to_slot(cond_op, narrowed, d_state);
                    push_worklist(sw->getDefaultDest(), bb, std::move(d_state), item.constraints, item.keep_split);
                }
                break;
            }

            // 12. ReturnInst
            if (const auto* ri = llvm::dyn_cast<llvm::ReturnInst>(&inst)) {
                AbstractValue rv = ri->getReturnValue()
                    ? eval_operand(ri->getReturnValue(), state)
                    : AbstractValue::make_int(DisjointIntervals(0), true);
                if (is_split_return_fn && outcome.split_returns.size() < 24) {
                    outcome.split_returns.emplace_back(rv, state);
                }
                if (!has_return) {
                    outcome.return_value = rv;
                    outcome.post_state = state;
                    has_return = true;
                } else {
                    outcome.return_value = outcome.return_value.join(rv);
                    outcome.post_state = outcome.post_state.join(state);
                }
                break;
            }
        }

        (void)path_alive;
    }

    callstack.pop_back();

    // Record Behavioral Equivalence Region ONLY for value-parameterized non-split functions,
    // projecting post_state to globals/heap only so caller stack regions are never clobbered!
    if (!has_pointer_arg && !is_split_return_fn) {
        std::unordered_map<ValueId, DisjointIntervals> envelopes;
        std::unordered_map<ValueId, Nullability> null_reqs;
        for (ValueId vid : relevant_inputs) {
            if (!start_state.has_value(vid)) continue;
            AbstractValue av = start_state.get_value(vid);
            if (av.is_pointer) {
                null_reqs[vid] = av.null_state;
            } else {
                envelopes[vid] = av.int_domain;
            }
        }
        AbstractState global_post = outcome.post_state.project_live({});
        equiv_cache_.record_region(
            fn_node_id,
            envelopes,
            null_reqs,
            /*predicate_sig=*/0,
            outcome.return_value,
            global_post,
            outcome.local_hazards,
            outcome.covered_lines,
            outcome.subcond_masks);
    }

    return outcome;
}

} // namespace sqc
