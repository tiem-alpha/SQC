#pragma once

#include "sqc/analyzer.h"
#include "sqc/coverage.h"
#include <string>
#include <vector>

namespace sqc {

class ReportGenerator {
public:
    /**
     * @brief Generate a rich, self-contained Interactive HTML Report containing:
     * 1. Analyses Summary & Graph Equivalence Cache speedup metrics
     * 2. Consolidated Undefined Behaviors / Hazards table with Callstack, Predicate & Concrete Witness
     * 3. Per-File and Per-Function Line-of-Code (LoC) Coverage & Sub-Condition Coverage % table
     * 4. Interactive Source Code Viewer with 6 line-status colors:
     *    - Green (Covered & Safe)
     *    - Yellow (Partial Sub-Condition on if/while/for with && or || where a sub-condition never occurred)
     *    - Orange/Red (Line with UB/Hazard alarm)
     *    - Purple (Dead after fatal UB)
     *    - Light Red (Uncovered executable line)
     */
    static bool generate_html_report(
        const std::string& output_html_path,
        const std::string& project_name,
        const std::vector<AnalysisRunResult>& results,
        std::string& err_msg);

    static bool generate_sarif_report(
        const std::string& output_sarif_path,
        const std::vector<AnalysisRunResult>& results,
        std::string& err_msg);

    static void print_cli_summary(
        const std::vector<AnalysisRunResult>& results,
        const std::string& html_report_path);

    // Save/Load binary or JSON run results for multi-step `sqc run` followed by `sqc report`
    static bool save_run_result(const std::string& cache_dir, const AnalysisRunResult& res);
    static std::vector<AnalysisRunResult> load_all_run_results(const std::string& cache_dir);
};

} // namespace sqc
