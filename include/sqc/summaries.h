#pragma once

#include "sqc/types.h"
#include "sqc/state.h"
#include <string>
#include <vector>

namespace llvm {
class CallBase;
class Module;
} // namespace llvm

namespace sqc {

struct SummaryCallResult {
    bool handled = false;
    AbstractValue return_value;
    AbstractState updated_state;
    std::vector<HazardReport> hazards;
    std::optional<SymbolicInputSpec> input_spec;
};

/**
 * @brief Fast O(1) built-in semantic models for C standard library functions
 * (sscanf, strlen, strcpy, strncpy, strcmp, strncmp, memcpy, memmove, memset,
 *  snprintf, sprintf, strtol, strtoul, malloc, calloc, free) and SQC builtins
 * (__sqc_interval_*, __sqc_make_unknown, __sqc_nondet_*, __sqc_find_variable, __sqc_assert...).
 * Eliminates the need for 500+ line manual libc stubs!
 */
class BuiltinSummaryRegistry {
public:
    BuiltinSummaryRegistry() = default;

    bool is_summarized_function(const std::string& fn_name) const;

    SummaryCallResult evaluate_call(
        const std::string& fn_name,
        const llvm::CallBase& call_inst,
        const AbstractState& current_state,
        const llvm::Module& mod,
        const SourceLoc& call_loc,
        const std::string& caller_name,
        const std::string& analysis_name) const;
};

} // namespace sqc
