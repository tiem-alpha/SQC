#include "sqc/intervals.h"
#include "sqc/state.h"
#include "sqc/equivalence.h"
#include "sqc/smt.h"

#include <iostream>
#include <cstdlib>
#include <string>
#include <vector>

using namespace sqc;

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define SQC_CHECK(cond, msg)                                                   \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "[FAIL] " << __func__ << ":" << __LINE__ << " — "     \
                      << (msg) << "\n";                                        \
            ++g_tests_failed;                                                  \
        } else {                                                               \
            ++g_tests_passed;                                                  \
        }                                                                      \
    } while (0)

static void test_disjoint_intervals_set_ops() {
    std::cout << "Running test_disjoint_intervals_set_ops...\n";

    // Basic construction & normalization
    DisjointIntervals a(-100, 100);
    SQC_CHECK(!a.is_empty(), "[-100, 100] should not be empty");
    SQC_CHECK(a.contains(0), "[-100, 100] should contain 0");
    SQC_CHECK(a.contains(-100) && a.contains(100), "endpoints included");
    SQC_CHECK(!a.contains(101) && !a.contains(-101), "out of bounds excluded");

    // Subtract single point from middle: [-100, 100] \ [0, 0] == [-100, -1] U [1, 100]
    DisjointIntervals no_zero = a.subtract(DisjointIntervals(0, 0));
    DisjointIntervals expected_no_zero(std::vector<DisjointIntervals::Range>{
        {-100, -1}, {1, 100}
    });
    SQC_CHECK(no_zero == expected_no_zero,
              "[-100, 100].subtract([0, 0]) must equal [-100, -1] U [1, 100]");
    SQC_CHECK(!no_zero.contains(0), "0 must be removed");
    SQC_CHECK(no_zero.contains(-1) && no_zero.contains(1), "-1 and 1 remain");

    // Remainder slicing subtraction: [-20, 300] \ [1, INT_MAX] == [-20, 0]
    DisjointIntervals wide(-20, 300);
    DisjointIntervals pos_region(1, DisjointIntervals::MAX_VAL);
    DisjointIntervals rem = wide.subtract(pos_region);
    SQC_CHECK(rem == DisjointIntervals(-20, 0),
              "[-20, 300].subtract([1, 2147483647]) must equal [-20, 0]");

    // Union of disjoint & adjacent intervals
    DisjointIntervals u1 = DisjointIntervals(-10, -1).unite(DisjointIntervals(0, 10));
    SQC_CHECK(u1 == DisjointIntervals(-10, 10), "Adjacent intervals [-10,-1] U [0,10] merge to [-10,10]");

    DisjointIntervals u2 = DisjointIntervals(10, 20).unite(DisjointIntervals(30, 40));
    SQC_CHECK(u2.ranges().size() == 2, "Disjoint union preserves 2 ranges");
    SQC_CHECK(DisjointIntervals(12, 18).is_subset_of(u2), "[12, 18] subset of [10,20] U [30,40]");
    SQC_CHECK(!DisjointIntervals(15, 35).is_subset_of(u2), "[15, 35] not subset of [10,20] U [30,40]");

    // Intersection
    DisjointIntervals inter = u2.intersect(DisjointIntervals(15, 35));
    DisjointIntervals expected_inter(std::vector<DisjointIntervals::Range>{
        {15, 20}, {30, 35}
    });
    SQC_CHECK(inter == expected_inter, "Intersection across disjoint ranges");
    SQC_CHECK(u2.overlaps(DisjointIntervals(15, 35)), "overlaps() true");
    SQC_CHECK(!u2.overlaps(DisjointIntervals(21, 29)), "overlaps() false in gap");
}

static void test_disjoint_intervals_arithmetic_and_cmp() {
    std::cout << "Running test_disjoint_intervals_arithmetic_and_cmp...\n";

    // Addition & subtraction
    DisjointIntervals x(10, 20);
    DisjointIntervals c10(10);
    DisjointIntervals y = x.add(c10);
    SQC_CHECK(y == DisjointIntervals(20, 30), "[10, 20] + 10 == [20, 30]");

    DisjointIntervals d = y.sub(DisjointIntervals(25));
    SQC_CHECK(d == DisjointIntervals(-5, 5), "[20, 30] - 25 == [-5, 5]");

    // Signed division (must safely exclude 0 from divisor)
    DisjointIntervals num(100);
    DisjointIntervals div_with_zero(-2, 2);
    DisjointIntervals q = num.sdiv(div_with_zero);
    SQC_CHECK(!q.is_empty(), "100 / [-2, 2] (excluding 0) should be non-empty");
    SQC_CHECK(q.contains(100) && q.contains(-100) && q.contains(50) && q.contains(-50),
              "100 / {-2, -1, 1, 2} covers {-100, -50, 50, 100}");

    // Relational filtering: filter_by_cmp & eval_cmp
    DisjointIntervals status(0, 5);

    auto eq1_eval = status.eval_cmp(DisjointIntervals::CmpOp::EQ, DisjointIntervals(1));
    SQC_CHECK(eq1_eval.can_be_true && eq1_eval.can_be_false,
              "status in [0, 5]: (status == 1) can be both True and False");

    auto eq99_eval = status.eval_cmp(DisjointIntervals::CmpOp::EQ, DisjointIntervals(99));
    SQC_CHECK(!eq99_eval.can_be_true && eq99_eval.can_be_false,
              "status in [0, 5]: (status == 99) is NEVER True, always False");

    DisjointIntervals filtered_gt = DisjointIntervals(-10, 10).filter_by_cmp(
        DisjointIntervals::CmpOp::SGT, DisjointIntervals(0));
    SQC_CHECK(filtered_gt == DisjointIntervals(1, 10),
              "[-10, 10] filtered by > 0 must be [1, 10]");

    DisjointIntervals filtered_ne = DisjointIntervals(-5, 5).filter_by_cmp(
        DisjointIntervals::CmpOp::NE, DisjointIntervals(0));
    DisjointIntervals expected_ne(std::vector<DisjointIntervals::Range>{
        {-5, -1}, {1, 5}
    });
    SQC_CHECK(filtered_ne == expected_ne,
              "[-5, 5] filtered by != 0 must be [-5, -1] U [1, 5]");

    auto always_true = DisjointIntervals(1).eval_cmp(
        DisjointIntervals::CmpOp::NE, DisjointIntervals(0));
    SQC_CHECK(always_true.can_be_true && !always_true.can_be_false,
              "1 != 0 is always True, never False");
}

static void test_linear_sym_expr() {
    std::cout << "Running test_linear_sym_expr...\n";

    // Model: y = *a + 10, hazard at y - 25 == 0  <=>  y == 25  <=>  *a == 15
    LinearSymExpr y_expr;
    y_expr.is_valid = true;
    y_expr.root_param_id = 1;
    y_expr.root_param_name = "*a";
    y_expr.coeff = 1;
    y_expr.addend = 10;

    auto root_val = y_expr.solve_for_root(25);
    SQC_CHECK(root_val.has_value() && *root_val == 15,
              "Solving y = *a + 10 for y == 25 must yield *a == 15");

    // Model: denom = y - 25 = *a - 15, hazard at denom == 0 <=> *a == 15
    LinearSymExpr denom_expr;
    denom_expr.is_valid = true;
    denom_expr.root_param_id = 1;
    denom_expr.root_param_name = "*a";
    denom_expr.coeff = 1;
    denom_expr.addend = -15;

    auto root_val2 = denom_expr.solve_for_root(0);
    SQC_CHECK(root_val2.has_value() && *root_val2 == 15,
              "Solving (*a - 15) == 0 must yield *a == 15");

    // Range inversion: y = *a + 10 in [20, 30] <=> *a in [10, 20]
    DisjointIntervals inv = y_expr.invert_range(DisjointIntervals(20, 30));
    SQC_CHECK(inv == DisjointIntervals(10, 20),
              "Inverting y = *a + 10 in [20, 30] must yield *a in [10, 20]");

    // Non-unit coefficient: z = 2 * alpha + 4 == 14 => alpha == 5
    LinearSymExpr scaled;
    scaled.is_valid = true;
    scaled.root_param_id = 2;
    scaled.root_param_name = "alpha";
    scaled.coeff = 2;
    scaled.addend = 4;
    auto alpha_sol = scaled.solve_for_root(14);
    SQC_CHECK(alpha_sol.has_value() && *alpha_sol == 5,
              "Solving 2 * alpha + 4 == 14 must yield alpha == 5");
    auto no_int_sol = scaled.solve_for_root(15);
    SQC_CHECK(!no_int_sol.has_value(),
              "2 * alpha + 4 == 15 has no integer solution");
}

static void test_graph_equivalence_cache() {
    std::cout << "Running test_graph_equivalence_cache...\n";

    GraphEquivalenceCache cache;
    const GraphNodeId node_G = 42;
    const ValueId var_x = 1001;
    const std::vector<ValueId> relevant_inputs = {var_x};

    // Step 1: Record maximal equivalence region R_eq = [1, INT_MAX] for graph node G
    std::unordered_map<ValueId, DisjointIntervals> envelopes;
    envelopes[var_x] = DisjointIntervals(1, DisjointIntervals::MAX_VAL);

    std::unordered_map<ValueId, Nullability> null_reqs;
    AbstractValue ret_val = AbstractValue::make_int(DisjointIntervals(1, 1000));
    AbstractState post_state;
    std::vector<HazardReport> hazards;
    std::set<std::pair<std::string, uint32_t>> covered_lines = {
        {"graph_equiv.c", 12}, {"graph_equiv.c", 13}, {"graph_equiv.c", 14}
    };
    std::unordered_map<std::string, uint8_t> subcond_masks = {
        {"graph_equiv.c:12:9", 0x1}
    };

    cache.record_region(
        node_G,
        envelopes,
        null_reqs,
        /*predicate_sig=*/0,
        ret_val,
        post_state,
        hazards,
        covered_lines,
        subcond_masks);

    SQC_CHECK(cache.total_stored_regions() == 1, "Cache should store 1 equivalence region");

    // Step 2: Query G with x in [10, 50] -> MUST be full_cache_hit == true
    AbstractState state_branch1;
    state_branch1.set_value(var_x, AbstractValue::make_int(DisjointIntervals(10, 50)));
    auto res1 = cache.query_and_slice_remainder(node_G, state_branch1, relevant_inputs);
    SQC_CHECK(res1.full_cache_hit, "Query x in [10, 50] must be a full cache hit on R_eq=[1, INT_MAX]");
    SQC_CHECK(res1.matched_regions_count == 1, "Matched 1 equivalence region");
    SQC_CHECK(res1.cached_covered_lines.size() == 3, "Cached covered lines returned on hit");

    // Step 3: Query G with x in [100, 500] (disjoint from [10, 50]!) -> STILL full_cache_hit == true!
    AbstractState state_branch2;
    state_branch2.set_value(var_x, AbstractValue::make_int(DisjointIntervals(100, 500)));
    auto res2 = cache.query_and_slice_remainder(node_G, state_branch2, relevant_inputs);
    SQC_CHECK(res2.full_cache_hit,
              "Query x in [100, 500] (disjoint from [10, 50]) must STILL be a full cache hit on R_eq=[1, INT_MAX]");
    SQC_CHECK(res2.cached_covered_lines.count({"graph_equiv.c", 13}) == 1,
              "Covered lines merged on generalized equivalence hit");

    // Step 4: Query G with x in [-20, 300] -> partial_cache_hit == true, remainder x in [-20, 0]!
    AbstractState state_branch3;
    state_branch3.set_value(var_x, AbstractValue::make_int(DisjointIntervals(-20, 300)));
    auto res3 = cache.query_and_slice_remainder(node_G, state_branch3, relevant_inputs);
    SQC_CHECK(res3.partial_cache_hit, "Query x in [-20, 300] must be a partial cache hit");
    SQC_CHECK(!res3.full_cache_hit, "Query x in [-20, 300] is not a full hit because [-20, 0] is uncovered");
    DisjointIntervals rem_dom = res3.remainder_state.get_value(var_x).int_domain;
    SQC_CHECK(rem_dom == DisjointIntervals(-20, 0),
              "Remainder state after slicing R_eq=[1, INT_MAX] from [-20, 300] must be [-20, 0]");

    SQC_CHECK(cache.total_queries() == 3, "3 queries recorded");
    SQC_CHECK(cache.cache_hits() == 2, "2 full cache hits recorded");
    SQC_CHECK(cache.remainder_slices() >= 1, "At least 1 remainder slice recorded");
}

static void test_z3_refiner() {
    std::cout << "Running test_z3_refiner...\n";

    Z3Refiner refiner;

    // Case 1: Feasible hazard: y = *a + 10, y in [-100, 100], hazard at y == 25 (*a == 15)
    LinearSymExpr sym;
    sym.is_valid = true;
    sym.root_param_id = 1;
    sym.root_param_name = "*a";
    sym.coeff = 1;
    sym.addend = 10;

    std::vector<PathConstraint> feasible_pcs;
    PathConstraint pc1;
    pc1.var_name = "*a";
    pc1.allowed_domain = DisjointIntervals(0, 100);
    feasible_pcs.push_back(pc1);

    auto sat_res = refiner.verify_hazard(
        "y",
        DisjointIntervals(-100, 100),
        DisjointIntervals(25, 25),
        sym,
        feasible_pcs);
    SQC_CHECK(sat_res.is_satisfiable, "Hazard y == 25 with *a in [0, 100] must be SAT");
    SQC_CHECK(!sat_res.concrete_witness.empty(), "SAT hazard must produce a concrete witness string");

    // Case 2: Infeasible hazard by domain: y in [0, 10], hazard at y == 25
    auto unsat_domain_res = refiner.verify_hazard(
        "y",
        DisjointIntervals(0, 10),
        DisjointIntervals(25, 25),
        sym,
        feasible_pcs);
    SQC_CHECK(!unsat_domain_res.is_satisfiable, "Hazard y == 25 with y in [0, 10] must be UNSAT");

    // Case 3: Infeasible hazard by path constraint: *a in [0, 10] => y = *a + 10 in [10, 20], cannot be 25
    std::vector<PathConstraint> tight_pcs;
    PathConstraint pc_tight;
    pc_tight.var_name = "*a";
    pc_tight.allowed_domain = DisjointIntervals(0, 10);
    tight_pcs.push_back(pc_tight);

    auto unsat_pc_res = refiner.verify_hazard(
        "y",
        DisjointIntervals(-100, 100),
        DisjointIntervals(25, 25),
        sym,
        tight_pcs);
    SQC_CHECK(!unsat_pc_res.is_satisfiable,
              "Hazard y == 25 with *a in [0, 10] and y = *a + 10 must be UNSAT");

    SQC_CHECK(refiner.total_queries() == 3, "Z3Refiner recorded 3 queries");
    SQC_CHECK(refiner.unsat_eliminated() >= 2, "Z3Refiner eliminated at least 2 UNSAT hazards");
}

int main() {
    std::cout << "========================================\n";
    std::cout << " SQC Core Engine Unit Test Suite\n";
    std::cout << "========================================\n";

    test_disjoint_intervals_set_ops();
    test_disjoint_intervals_arithmetic_and_cmp();
    test_linear_sym_expr();
    test_graph_equivalence_cache();
    test_z3_refiner();

    std::cout << "----------------------------------------\n";
    std::cout << "Passed: " << g_tests_passed << ", Failed: " << g_tests_failed << "\n";
    std::cout << "========================================\n";
    return (g_tests_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
