#pragma once

#include "sqc/types.h"
#include "sqc/config.h"
#include "sqc/state.h"
#include "sqc/equivalence.h"
#include "sqc/summaries.h"
#include "sqc/smt.h"
#include "sqc/coverage.h"
#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace llvm {
class Module;
class Function;
class BasicBlock;
class Instruction;
class Value;
} // namespace llvm

namespace sqc {

struct AnalysisRunResult {
    uint32_t unit_id = 0;
    std::string unit_name;
    bool success = false;
    std::string error_message;
    double elapsed_ms = 0.0;

    std::vector<HazardReport> alarms;
    std::vector<SymbolicInputSpec> input_specs;
    CoverageTracker coverage;

    uint64_t instructions_analyzed = 0;
    uint64_t equivalence_queries = 0;
    uint64_t equivalence_cache_hits = 0;
    uint64_t remainder_slices = 0;
    uint64_t smt_queries = 0;
    uint64_t smt_false_positives_eliminated = 0;
};

/**
 * @brief Core Hazard-Guided Symbolic Partitioning & Graph Behavioral Equivalence Analyzer.
 */
class HazardGuidedAnalyzer {
public:
    explicit HazardGuidedAnalyzer(const llvm::Module& mod, const AnalysisUnitConfig& cfg);

    void set_verbose(bool v) { verbose_ = v; }
    AnalysisRunResult run();

private:
    const llvm::Module& mod_;
    AnalysisUnitConfig cfg_;
    LivenessAndModRefPass liveness_modref_;
    GraphEquivalenceCache equiv_cache_;
    BuiltinSummaryRegistry summaries_;
    Z3Refiner smt_refiner_;
    CoverageTracker coverage_;

    std::vector<HazardReport> alarms_;
    std::set<std::string> deduplicated_alarm_keys_;
    std::vector<SymbolicInputSpec> input_specs_;
    std::set<std::string> deduplicated_input_keys_;
    uint64_t instructions_analyzed_ = 0;
    bool verbose_ = false;
    std::chrono::steady_clock::time_point run_start_time_;
    std::chrono::steady_clock::time_point last_progress_time_;

    struct FunctionExecOutcome {
        AbstractValue return_value;
        AbstractState post_state;
        std::vector<std::pair<AbstractValue, AbstractState>> split_returns;
        std::vector<HazardReport> local_hazards;
        std::set<std::pair<std::string, uint32_t>> covered_lines;
        std::unordered_map<std::string, uint8_t> subcond_masks;
    };

    FunctionExecOutcome analyze_function(
        const llvm::Function* fn,
        const AbstractState& entry_state,
        std::vector<std::string>& callstack,
        uint32_t depth);

    AbstractValue eval_operand(const llvm::Value* v, AbstractState& state) const;
    void emit_hazard(HazardReport rep, std::vector<HazardReport>& local_hazards);
};

} // namespace sqc
