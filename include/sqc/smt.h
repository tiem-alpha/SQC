#pragma once

#include "sqc/types.h"
#include "sqc/intervals.h"
#include "sqc/state.h"
#include <string>
#include <vector>

namespace sqc {

struct PathConstraint {
    std::string var_name;
    DisjointIntervals allowed_domain;
    DisjointIntervals::CmpOp op = DisjointIntervals::CmpOp::EQ;
    int64_t rhs_const = 0;
    std::string rhs_var;
};

struct SmtVerificationResult {
    bool is_satisfiable = false;
    std::string concrete_witness;
};

/**
 * @brief On-demand Z3 SMT solver integration for complex multi-variable path feasibility
 * and concrete counterexample witness generation.
 */
class Z3Refiner {
public:
    Z3Refiner() = default;

    // Verify if hazard condition (var_name in hazard_zone) is feasible under path_constraints
    SmtVerificationResult verify_hazard(
        const std::string& target_var,
        const DisjointIntervals& current_domain,
        const DisjointIntervals& hazard_zone,
        const LinearSymExpr& sym_expr,
        const std::vector<PathConstraint>& path_constraints);

    uint64_t total_queries() const { return queries_; }
    uint64_t unsat_eliminated() const { return unsat_count_; }

private:
    uint64_t queries_ = 0;
    uint64_t unsat_count_ = 0;
};

} // namespace sqc
