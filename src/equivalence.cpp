#include "sqc/equivalence.h"

namespace sqc {

namespace {

bool nullability_compatible(Nullability actual, Nullability required) {
    return (static_cast<uint8_t>(actual) & ~static_cast<uint8_t>(required)) == 0;
}

void merge_region_outcome(EquivalenceQueryResult& result,
                          const BehavioralEquivalenceRegion& reg) {
    if (result.matched_regions_count == 0) {
        result.cached_return_value = reg.return_value;
    } else {
        result.cached_return_value = result.cached_return_value.join(reg.return_value);
    }
    result.matched_regions_count++;
    result.cached_post_states.push_back(reg.post_state_delta);
    result.cached_hazards.insert(result.cached_hazards.end(),
                                 reg.triggered_hazards.begin(),
                                 reg.triggered_hazards.end());
    result.cached_covered_lines.insert(reg.covered_file_lines.begin(),
                                       reg.covered_file_lines.end());
    for (const auto& kv : reg.subcondition_truth_masks) {
        result.cached_subcondition_masks[kv.first] |= kv.second;
    }
}

} // namespace

EquivalenceQueryResult GraphEquivalenceCache::query_and_slice_remainder(
    GraphNodeId node_id,
    const AbstractState& input_state,
    const std::vector<ValueId>& relevant_inputs,
    uint64_t predicate_sig) {

    total_queries_++;

    EquivalenceQueryResult result;
    result.remainder_state = input_state;

    auto it = table_.find(node_id);
    if (it == table_.end() || it->second.empty()) {
        return result;
    }

    for (const BehavioralEquivalenceRegion& reg : it->second) {
        if (predicate_sig != 0 && reg.predicate_signature_mask != 0 &&
            predicate_sig != reg.predicate_signature_mask) {
            continue;
        }

        // Check nullability requirements
        bool null_ok = true;
        for (const auto& nkv : reg.required_null_states) {
            AbstractValue val = result.remainder_state.get_value(nkv.first);
            if (!nullability_compatible(val.null_state, nkv.second)) {
                null_ok = false;
                break;
            }
        }
        if (!null_ok) {
            continue;
        }

        if (relevant_inputs.empty()) {
            merge_region_outcome(result, reg);
            result.full_cache_hit = true;
            cache_hits_++;
            result.remainder_state = AbstractState();
            return result;
        }

        bool any_disjoint = false;
        size_t partial_count = 0;
        ValueId slice_vid = 0;

        for (ValueId vid : relevant_inputs) {
            auto env_it = reg.input_envelopes.find(vid);
            if (env_it == reg.input_envelopes.end()) {
                continue;
            }
            const DisjointIntervals& env = env_it->second;
            DisjointIntervals cur_dom = result.remainder_state.get_value(vid).int_domain;
            if (!cur_dom.overlaps(env)) {
                any_disjoint = true;
                break;
            }
            if (!cur_dom.is_subset_of(env)) {
                partial_count++;
                slice_vid = vid;
            }
        }

        if (any_disjoint) {
            continue;
        }

        if (partial_count == 0) {
            // Full behavioral equivalence hit
            merge_region_outcome(result, reg);
            result.full_cache_hit = true;
            cache_hits_++;
            result.remainder_state = AbstractState();
            return result;
        }

        if (partial_count == 1) {
            // Exact remainder slicing along the single partially-overlapping dimension
            auto env_it = reg.input_envelopes.find(slice_vid);
            if (env_it != reg.input_envelopes.end()) {
                AbstractValue cur_val = result.remainder_state.get_value(slice_vid);
                DisjointIntervals rem_dom = cur_val.int_domain.subtract(env_it->second);
                if (rem_dom != cur_val.int_domain) {
                    merge_region_outcome(result, reg);
                    result.partial_cache_hit = true;
                    remainder_slices_++;

                    if (rem_dom.is_empty()) {
                        result.full_cache_hit = true;
                        cache_hits_++;
                        result.remainder_state = AbstractState();
                        return result;
                    }

                    cur_val.int_domain = std::move(rem_dom);
                    result.remainder_state.set_value(slice_vid, cur_val);
                }
            }
        }
    }

    return result;
}

void GraphEquivalenceCache::record_region(
    GraphNodeId node_id,
    const std::unordered_map<ValueId, DisjointIntervals>& maximal_envelopes,
    const std::unordered_map<ValueId, Nullability>& null_reqs,
    uint64_t predicate_sig,
    const AbstractValue& ret_val,
    const AbstractState& post_state,
    const std::vector<HazardReport>& hazards,
    const std::set<std::pair<std::string, uint32_t>>& covered_lines,
    const std::unordered_map<std::string, uint8_t>& subcond_masks) {

    BehavioralEquivalenceRegion reg;
    reg.region_id = next_region_id_++;
    reg.predicate_signature_mask = predicate_sig;
    reg.input_envelopes = maximal_envelopes;
    reg.required_null_states = null_reqs;
    reg.return_value = ret_val;
    reg.post_state_delta = post_state;
    reg.triggered_hazards = hazards;
    reg.covered_file_lines = covered_lines;
    reg.subcondition_truth_masks = subcond_masks;

    auto& bucket = table_[node_id];
    constexpr size_t kMaxRegionsPerNode = 64;
    // Deduplicate identical envelopes in-place
    for (auto& existing : bucket) {
        if (existing.predicate_signature_mask == reg.predicate_signature_mask &&
            existing.input_envelopes == reg.input_envelopes &&
            existing.required_null_states == reg.required_null_states) {
            existing.return_value = existing.return_value.join(reg.return_value);
            existing.post_state_delta = existing.post_state_delta.join(reg.post_state_delta);
            existing.covered_file_lines.insert(reg.covered_file_lines.begin(), reg.covered_file_lines.end());
            for (const auto& kv : reg.subcondition_truth_masks) {
                existing.subcondition_truth_masks[kv.first] |= kv.second;
            }
            return;
        }
    }
    if (bucket.size() >= kMaxRegionsPerNode) {
        bucket.erase(bucket.begin());
    }
    bucket.push_back(std::move(reg));
}

size_t GraphEquivalenceCache::total_stored_regions() const {
    size_t total = 0;
    for (const auto& kv : table_) {
        total += kv.second.size();
    }
    return total;
}

} // namespace sqc
