#include "sqc/smt.h"

#include <z3++.h>
#include <string>
#include <sstream>

namespace sqc {

namespace {

z3::expr int64_val(z3::context& ctx, int64_t v) {
    return ctx.int_val(std::to_string(v).c_str());
}

z3::expr domain_to_z3(z3::context& ctx, const z3::expr& var, const DisjointIntervals& dom) {
    if (dom.is_empty()) {
        return ctx.bool_val(false);
    }
    z3::expr_vector clauses(ctx);
    for (const auto& r : dom.ranges()) {
        if (r.first == r.second) {
            clauses.push_back(var == int64_val(ctx, r.first));
        } else {
            clauses.push_back(var >= int64_val(ctx, r.first) && var <= int64_val(ctx, r.second));
        }
    }
    return z3::mk_or(clauses);
}

z3::expr cmp_to_z3(const z3::expr& lhs, DisjointIntervals::CmpOp op, const z3::expr& rhs) {
    switch (op) {
        case DisjointIntervals::CmpOp::EQ:  return lhs == rhs;
        case DisjointIntervals::CmpOp::NE:  return lhs != rhs;
        case DisjointIntervals::CmpOp::SLT:
        case DisjointIntervals::CmpOp::ULT: return lhs < rhs;
        case DisjointIntervals::CmpOp::SLE:
        case DisjointIntervals::CmpOp::ULE: return lhs <= rhs;
        case DisjointIntervals::CmpOp::SGT:
        case DisjointIntervals::CmpOp::UGT: return lhs > rhs;
        case DisjointIntervals::CmpOp::SGE:
        case DisjointIntervals::CmpOp::UGE: return lhs >= rhs;
    }
    return lhs == rhs;
}

std::string eval_model_int(const z3::model& m, const z3::expr& e) {
    z3::expr val = m.eval(e, true);
    int64_t num = 0;
    if (val.is_numeral_i64(num)) {
        return std::to_string(num);
    }
    return val.to_string();
}

} // namespace

SmtVerificationResult Z3Refiner::verify_hazard(
    const std::string& target_var,
    const DisjointIntervals& current_domain,
    const DisjointIntervals& hazard_zone,
    const LinearSymExpr& sym_expr,
    const std::vector<PathConstraint>& path_constraints) {
    ++queries_;

    if (current_domain.is_empty() || hazard_zone.is_empty()) {
        ++unsat_count_;
        return {false, ""};
    }

    try {
        z3::context ctx;
        z3::solver solver(ctx);

        const std::string vname = target_var.empty() ? "v" : target_var;
        z3::expr v = ctx.int_const(vname.c_str());

        // Constrain target variable to both its current abstract domain and the hazard zone
        solver.add(domain_to_z3(ctx, v, current_domain));
        solver.add(domain_to_z3(ctx, v, hazard_zone));

        // Link linear symbolic expression: v == alpha * coeff + addend
        std::string root_name;
        bool has_root = false;
        if (sym_expr.is_valid) {
            root_name = sym_expr.root_param_name.empty() ? "param" : sym_expr.root_param_name;
            has_root = true;
            z3::expr alpha = ctx.int_const(root_name.c_str());
            solver.add(v == alpha * int64_val(ctx, sym_expr.coeff) + int64_val(ctx, sym_expr.addend));
        }

        // Assert all accumulated path constraints
        for (const auto& pc : path_constraints) {
            if (pc.var_name.empty()) continue;
            z3::expr pv = ctx.int_const(pc.var_name.c_str());
            if (!pc.allowed_domain.is_empty()) {
                solver.add(domain_to_z3(ctx, pv, pc.allowed_domain));
            }
            if (!pc.rhs_var.empty()) {
                z3::expr rv = ctx.int_const(pc.rhs_var.c_str());
                solver.add(cmp_to_z3(pv, pc.op, rv));
            } else if (pc.allowed_domain.is_empty()) {
                solver.add(cmp_to_z3(pv, pc.op, int64_val(ctx, pc.rhs_const)));
            }
        }

        z3::check_result res = solver.check();
        if (res == z3::sat) {
            z3::model m = solver.get_model();
            std::string v_val = eval_model_int(m, v);
            std::string witness;
            if (has_root) {
                z3::expr alpha = ctx.int_const(root_name.c_str());
                std::string a_val = eval_model_int(m, alpha);
                if (root_name == vname) {
                    witness = root_name + " = " + a_val;
                } else {
                    witness = root_name + " = " + a_val + " (" + vname + " = " + v_val + ")";
                }
            } else {
                witness = vname + " = " + v_val;
            }
            return {true, witness};
        }

        if (res == z3::unsat) {
            ++unsat_count_;
            return {false, ""};
        }
    } catch (const z3::exception&) {
        // Fall through to interval-based fallback if Z3 raises an error
    }

    // Conservative fallback if Z3 returns unknown or throws
    DisjointIntervals inter = current_domain.intersect(hazard_zone);
    if (inter.is_empty()) {
        ++unsat_count_;
        return {false, ""};
    }
    int64_t sample = inter.sample_witness();
    const std::string vname = target_var.empty() ? "v" : target_var;
    if (sym_expr.is_valid) {
        if (auto root_val = sym_expr.solve_for_root(sample)) {
            std::string rname = sym_expr.root_param_name.empty() ? "param" : sym_expr.root_param_name;
            return {true, rname + " = " + std::to_string(*root_val) +
                          " (" + vname + " = " + std::to_string(sample) + ")"};
        }
    }
    return {true, vname + " = " + std::to_string(sample)};
}

} // namespace sqc
