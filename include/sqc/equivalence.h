#pragma once

#include "sqc/types.h"
#include "sqc/intervals.h"
#include "sqc/state.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace sqc {

/**
 * @brief Represents a Maximal Behavioral Equivalence Region (R_eq) for a Graph Node
 * (either a Function entry or a CFG Join/Subgraph entry).
 * Any two inputs falling inside R_eq follow the exact same branch outcomes and hazard checks.
 */
struct BehavioralEquivalenceRegion {
    uint64_t region_id = 0;
    uint64_t predicate_signature_mask = 0;

    // Maximal allowed interval per relevant input variable (parameter or live-in register)
    std::unordered_map<ValueId, DisjointIntervals> input_envelopes;
    std::unordered_map<ValueId, Nullability> required_null_states;

    // Cached outcome of executing the graph on this equivalence region
    AbstractValue return_value;
    AbstractState post_state_delta;
    std::vector<HazardReport> triggered_hazards;

    // Source coverage attached to this equivalence region (OR-ed on cache hit!)
    std::set<std::pair<std::string, uint32_t>> covered_file_lines;
    // Key: "file:line:col" -> 2-bit truth mask (0x1 = seen_true, 0x2 = seen_false)
    std::unordered_map<std::string, uint8_t> subcondition_truth_masks;
};

struct EquivalenceQueryResult {
    bool full_cache_hit = false;
    bool partial_cache_hit = false;
    uint32_t matched_regions_count = 0;

    // Remaining state after subtracting all matched R_eq envelopes (empty if full_cache_hit)
    AbstractState remainder_state;

    // Aggregated return value and post-states from matched equivalence regions
    AbstractValue cached_return_value;
    std::vector<AbstractState> cached_post_states;
    std::vector<HazardReport> cached_hazards;
    std::set<std::pair<std::string, uint32_t>> cached_covered_lines;
    std::unordered_map<std::string, uint8_t> cached_subcondition_masks;
};

/**
 * @brief Manages Graph Behavioral Equivalence classes & Remainder Slicing across
 * Function calls and CFG join points so equivalent branches are traversed only ONCE.
 */
class GraphEquivalenceCache {
public:
    GraphEquivalenceCache() = default;

    // Query cache at graph entry (Function or CFG BasicBlock) with relevant variables
    EquivalenceQueryResult query_and_slice_remainder(
        GraphNodeId node_id,
        const AbstractState& input_state,
        const std::vector<ValueId>& relevant_inputs,
        uint64_t predicate_sig = 0);

    // Record a newly explored equivalence region with its maximal guard envelope
    void record_region(
        GraphNodeId node_id,
        const std::unordered_map<ValueId, DisjointIntervals>& maximal_envelopes,
        const std::unordered_map<ValueId, Nullability>& null_reqs,
        uint64_t predicate_sig,
        const AbstractValue& ret_val,
        const AbstractState& post_state,
        const std::vector<HazardReport>& hazards,
        const std::set<std::pair<std::string, uint32_t>>& covered_lines,
        const std::unordered_map<std::string, uint8_t>& subcond_masks);

    uint64_t total_queries() const { return total_queries_; }
    uint64_t cache_hits() const { return cache_hits_; }
    uint64_t remainder_slices() const { return remainder_slices_; }
    size_t total_stored_regions() const;

private:
    std::unordered_map<GraphNodeId, std::vector<BehavioralEquivalenceRegion>> table_;
    uint64_t next_region_id_ = 1;
    uint64_t total_queries_ = 0;
    uint64_t cache_hits_ = 0;
    uint64_t remainder_slices_ = 0;
};

} // namespace sqc
