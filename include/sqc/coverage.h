#pragma once

#include "sqc/types.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>

namespace llvm {
class Module;
class Function;
} // namespace llvm

namespace sqc {

enum class LineStatus : uint8_t {
    NonExecutable  = 0, // Blank, comment, typedef/struct decl (excluded from LoC denominator)
    Uncovered      = 1, // Red: Executable line never reached
    CoveredSafe    = 2, // Green: Reached and proven safe (all sub-conditions covered if branch)
    CoveredPartial = 3, // Yellow: Branch/Loop header where at least one sub-condition (&&/||) never occurred
    CoveredAlarm   = 4, // Orange/Red: Reached and triggered a hazard/UB
    DeadAfterUB    = 5  // Purple: Unreachable because preceding instruction always triggers fatal UB
};

struct SubConditionCoverage {
    std::string file;
    uint32_t line = 0;
    uint16_t col_start = 0;
    uint16_t col_end = 0;
    std::string expr_text;
    bool seen_true = false;
    bool seen_false = false;

    bool is_fully_covered() const { return seen_true && seen_false; }
    std::string status_note() const {
        if (seen_true && seen_false) return "Both True & False covered";
        if (seen_true && !seen_false) return "Never False (only True occurred)";
        if (!seen_true && seen_false) return "Never True (only False occurred)";
        return "Never evaluated (short-circuited)";
    }
};

struct FunctionCoverageMetrics {
    std::string function_name;
    std::string file_path;
    uint32_t start_line = 0;
    uint32_t end_line = 0;
    uint32_t executable_lines_count = 0;
    uint32_t covered_lines_count = 0;
    uint32_t total_sub_conditions = 0;
    uint32_t fully_covered_sub_conditions = 0;
    uint32_t alarms_count = 0;
    bool was_called = false;

    double loc_coverage_percent() const {
        return executable_lines_count ? (100.0 * covered_lines_count / executable_lines_count) : 100.0;
    }
    double condition_coverage_percent() const {
        return total_sub_conditions ? (100.0 * fully_covered_sub_conditions / total_sub_conditions) : 100.0;
    }
};

struct FileCoverageReport {
    std::string file_path;
    uint32_t total_lines = 0;
    uint32_t total_executable_lines = 0;
    uint32_t total_covered_lines = 0;
    uint32_t total_alarms = 0;

    double file_loc_coverage_percent() const {
        return total_executable_lines ? (100.0 * total_covered_lines / total_executable_lines) : 100.0;
    }

    std::vector<FunctionCoverageMetrics> functions;
    // 1-indexed (index 0 unused; line L is at line_statuses[L])
    std::vector<LineStatus> line_statuses;
    std::vector<std::string> source_lines;
    std::map<uint32_t, std::vector<SubConditionCoverage>> line_sub_conditions;
    std::map<uint32_t, std::vector<HazardReport>> line_alarms;
};

/**
 * @brief Tracks executable lines per function/file from LLVM DWARF & source syntax,
 * records covered lines and 2-bit sub-condition truth masks (&&, ||, if, while, for),
 * and produces per-file and per-function LoC coverage reports.
 */
class CoverageTracker {
public:
    CoverageTracker() = default;

    // Register all functions and executable lines from compiled LLVM Module & source files
    void initialize_from_module(const llvm::Module& mod,
                                const std::vector<std::string>& skip_function_regexes = {});

    void mark_line_covered(const std::string& file, uint32_t line);
    void mark_line_dead_after_ub(const std::string& file, uint32_t line);
    void record_branch_outcome(const SourceLoc& loc, bool taken_true, bool taken_false);
    void record_alarm(const HazardReport& alarm);

    // Merge coverage bitmaps from a cached BehavioralEquivalenceRegion or another analysis unit
    void merge_cached_coverage(
        const std::set<std::pair<std::string, uint32_t>>& covered_lines,
        const std::unordered_map<std::string, uint8_t>& subcond_masks);

    void merge_from(const CoverageTracker& other);

    // Finalize line statuses (applying Yellow COVERED_PARTIAL to if/while lines with unreached sub-conditions)
    std::vector<FileCoverageReport> build_reports() const;

    const std::set<std::pair<std::string, uint32_t>>& covered_lines_set() const { return covered_lines_; }
    const std::unordered_map<std::string, uint8_t>& subcond_masks_map() const { return subcond_masks_; }

private:
    struct RegisteredFunc {
        std::string name;
        std::string file;
        uint32_t start_line = 0;
        uint32_t end_line = 0;
        std::set<uint32_t> exec_lines;
    };

    std::map<std::string, std::set<uint32_t>> file_exec_lines_;
    std::map<std::string, std::vector<RegisteredFunc>> file_functions_;
    std::set<std::pair<std::string, uint32_t>> covered_lines_;
    std::set<std::pair<std::string, uint32_t>> dead_after_ub_lines_;
    // Key: "file:line:col" -> bit 0 (seen_true), bit 1 (seen_false)
    std::unordered_map<std::string, uint8_t> subcond_masks_;
    std::vector<HazardReport> alarms_;
};

} // namespace sqc
