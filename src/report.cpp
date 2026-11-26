#include "sqc/report.h"

#include <llvm/Support/Error.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

namespace sqc {

namespace {

std::string current_timestamp_iso() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
    localtime_r(&t, &tm_buf);
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::string hazard_rule_id(HazardKind k) {
    switch (k) {
        case HazardKind::NullPointerDeref:        return "SQC-NullPointerDeref";
        case HazardKind::OutOfBoundsAccess:       return "SQC-OutOfBoundsAccess";
        case HazardKind::DivisionByZero:          return "SQC-DivisionByZero";
        case HazardKind::SignedIntegerOverflow:   return "SQC-SignedIntegerOverflow";
        case HazardKind::InvalidBitShift:         return "SQC-InvalidBitShift";
        case HazardKind::UninitializedRead:       return "SQC-UninitializedRead";
        case HazardKind::UseAfterFree:            return "SQC-UseAfterFree";
        case HazardKind::DoubleFree:              return "SQC-DoubleFree";
        case HazardKind::MemoryLeak:              return "SQC-MemoryLeak";
        case HazardKind::InvalidFree:             return "SQC-InvalidFree";
        case HazardKind::StringNotNullTerminated: return "SQC-StringNotNullTerminated";
        case HazardKind::AssertionViolation:      return "SQC-AssertionViolation";
        case HazardKind::CustomUndefinedBehavior: return "SQC-CustomUndefinedBehavior";
    }
    return "SQC-UnknownHazard";
}

std::string certainty_to_string(HazardCertainty c) {
    return (c == HazardCertainty::DefiniteBug) ? "Definite Bug" : "Potential UB";
}

std::string sanitize_filename(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') {
            out.push_back(c);
        } else {
            out.push_back('_');
        }
    }
    if (out.empty()) out = "unit";
    return out;
}

std::string html_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;"; break;
            case '<':  out += "&lt;"; break;
            case '>':  out += "&gt;"; break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default:   out.push_back(c); break;
        }
    }
    return out;
}

// Escape "</" inside embedded JSON <script> blocks so HTML parser never terminates the script early
std::string escape_json_for_script(const std::string& json_str) {
    std::string out;
    out.reserve(json_str.size() + 32);
    for (size_t i = 0; i < json_str.size(); ++i) {
        if (json_str[i] == '<' && i + 1 < json_str.size() && json_str[i + 1] == '/') {
            out += "<\\/";
            ++i;
        } else {
            out.push_back(json_str[i]);
        }
    }
    return out;
}

std::string json_to_string(const llvm::json::Value& val, bool pretty = false) {
    std::string out;
    llvm::raw_string_ostream os(out);
    if (pretty) {
        os << llvm::formatv("{0:2}", val);
    } else {
        os << val;
    }
    os.flush();
    return out;
}

llvm::json::Object serialize_hazard(const HazardReport& h) {
    llvm::json::Object obj;
    obj["kind"] = static_cast<int64_t>(h.kind);
    obj["kind_str"] = hazard_kind_to_string(h.kind);
    obj["rule_id"] = hazard_rule_id(h.kind);
    obj["certainty"] = static_cast<int64_t>(h.certainty);
    obj["certainty_str"] = certainty_to_string(h.certainty);
    obj["file"] = h.loc.file;
    obj["line"] = static_cast<int64_t>(h.loc.line);
    obj["col_start"] = static_cast<int64_t>(h.loc.col_start);
    obj["col_end"] = static_cast<int64_t>(h.loc.col_end);
    obj["function_name"] = h.function_name;
    obj["analysis_name"] = h.analysis_name;
    obj["predicate"] = h.predicate;
    obj["concrete_witness"] = h.concrete_witness;
    obj["message"] = h.message;
    obj["occurrences"] = static_cast<int64_t>(h.occurrences);
    llvm::json::Array cs;
    for (const auto& frame : h.callstack) {
        cs.push_back(frame);
    }
    obj["callstack"] = std::move(cs);
    return obj;
}

HazardReport deserialize_hazard(const llvm::json::Object& obj) {
    HazardReport h;
    if (auto v = obj.getInteger("kind")) h.kind = static_cast<HazardKind>(*v);
    if (auto v = obj.getInteger("certainty")) h.certainty = static_cast<HazardCertainty>(*v);
    if (auto v = obj.getString("file")) h.loc.file = v->str();
    if (auto v = obj.getInteger("line")) h.loc.line = static_cast<uint32_t>(*v);
    if (auto v = obj.getInteger("col_start")) h.loc.col_start = static_cast<uint16_t>(*v);
    if (auto v = obj.getInteger("col_end")) h.loc.col_end = static_cast<uint16_t>(*v);
    if (auto v = obj.getString("function_name")) h.function_name = v->str();
    if (auto v = obj.getString("analysis_name")) h.analysis_name = v->str();
    if (auto v = obj.getString("predicate")) h.predicate = v->str();
    if (auto v = obj.getString("concrete_witness")) h.concrete_witness = v->str();
    if (auto v = obj.getString("message")) h.message = v->str();
    if (auto v = obj.getInteger("occurrences")) h.occurrences = static_cast<uint32_t>(*v);
    if (const auto* cs = obj.getArray("callstack")) {
        for (const auto& item : *cs) {
            if (auto s = item.getAsString()) {
                h.callstack.push_back(s->str());
            }
        }
    }
    return h;
}

struct ConsolidatedData {
    CoverageTracker merged_coverage;
    std::vector<FileCoverageReport> file_reports;
    std::vector<HazardReport> alarms;

    double total_elapsed_ms = 0.0;
    uint64_t total_instructions = 0;
    uint64_t total_equiv_queries = 0;
    uint64_t total_equiv_hits = 0;
    uint64_t total_remainder_slices = 0;
    uint64_t total_smt_queries = 0;
    uint64_t total_smt_fp_eliminated = 0;
    uint32_t total_exec_lines = 0;
    uint32_t total_covered_lines = 0;
    uint32_t definite_bugs = 0;
    uint32_t potential_ubs = 0;
    uint32_t succeeded_units = 0;
};

ConsolidatedData consolidate_results(const std::vector<AnalysisRunResult>& results) {
    ConsolidatedData cd;
    std::map<std::string, size_t> alarm_index;

    for (const auto& r : results) {
        if (r.success) ++cd.succeeded_units;
        cd.total_elapsed_ms += r.elapsed_ms;
        cd.total_instructions += r.instructions_analyzed;
        cd.total_equiv_queries += r.equivalence_queries;
        cd.total_equiv_hits += r.equivalence_cache_hits;
        cd.total_remainder_slices += r.remainder_slices;
        cd.total_smt_queries += r.smt_queries;
        cd.total_smt_fp_eliminated += r.smt_false_positives_eliminated;

        cd.merged_coverage.merge_from(r.coverage);

        for (const auto& a : r.alarms) {
            HazardReport copy = a;
            if (copy.analysis_name.empty()) copy.analysis_name = r.unit_name;
            cd.merged_coverage.record_alarm(copy);

            std::ostringstream key;
            key << static_cast<int>(copy.kind) << "|"
                << copy.loc.file << ":" << copy.loc.line << ":" << copy.loc.col_start << "|"
                << copy.function_name << "|" << copy.predicate;
            auto it = alarm_index.find(key.str());
            if (it == alarm_index.end()) {
                alarm_index[key.str()] = cd.alarms.size();
                cd.alarms.push_back(std::move(copy));
            } else {
                cd.alarms[it->second].occurrences += copy.occurrences;
            }
        }
    }

    for (const auto& a : cd.alarms) {
        if (a.certainty == HazardCertainty::DefiniteBug) ++cd.definite_bugs;
        else ++cd.potential_ubs;
    }

    cd.file_reports = cd.merged_coverage.build_reports();
    for (const auto& fr : cd.file_reports) {
        cd.total_exec_lines += fr.total_executable_lines;
        cd.total_covered_lines += fr.total_covered_lines;
    }
    return cd;
}

} // namespace

bool ReportGenerator::generate_html_report(
    const std::string& output_html_path,
    const std::string& project_name,
    const std::vector<AnalysisRunResult>& results,
    std::string& err_msg) {
    ConsolidatedData cd = consolidate_results(results);

    double overall_loc_pct = cd.total_exec_lines
                                 ? (100.0 * cd.total_covered_lines / cd.total_exec_lines)
                                 : 100.0;
    double cache_hit_pct = cd.total_equiv_queries
                               ? (100.0 * cd.total_equiv_hits / cd.total_equiv_queries)
                               : 0.0;

    // Build JSON payload for interactive UI
    llvm::json::Object root;
    root["project_name"] = project_name;
    root["timestamp"] = current_timestamp_iso();
    root["total_analyses"] = static_cast<int64_t>(results.size());
    root["succeeded_analyses"] = static_cast<int64_t>(cd.succeeded_units);
    root["total_elapsed_ms"] = cd.total_elapsed_ms;
    root["total_instructions"] = static_cast<int64_t>(cd.total_instructions);
    root["total_equiv_queries"] = static_cast<int64_t>(cd.total_equiv_queries);
    root["total_equiv_hits"] = static_cast<int64_t>(cd.total_equiv_hits);
    root["cache_hit_pct"] = cache_hit_pct;
    root["total_remainder_slices"] = static_cast<int64_t>(cd.total_remainder_slices);
    root["total_smt_queries"] = static_cast<int64_t>(cd.total_smt_queries);
    root["total_smt_fp_eliminated"] = static_cast<int64_t>(cd.total_smt_fp_eliminated);
    root["total_alarms"] = static_cast<int64_t>(cd.alarms.size());
    root["definite_bugs"] = static_cast<int64_t>(cd.definite_bugs);
    root["potential_ubs"] = static_cast<int64_t>(cd.potential_ubs);
    root["total_exec_lines"] = static_cast<int64_t>(cd.total_exec_lines);
    root["total_covered_lines"] = static_cast<int64_t>(cd.total_covered_lines);
    root["overall_loc_pct"] = overall_loc_pct;

    // Alarms array
    llvm::json::Array alarms_arr;
    for (const auto& a : cd.alarms) {
        alarms_arr.push_back(serialize_hazard(a));
    }
    root["alarms"] = std::move(alarms_arr);

    // Analyses array (including per-flow symbolic inputs & alarms)
    llvm::json::Array units_arr;
    for (const auto& r : results) {
        llvm::json::Object uo;
        uo["unit_id"] = static_cast<int64_t>(r.unit_id);
        uo["unit_name"] = r.unit_name;
        uo["success"] = r.success;
        uo["error_message"] = r.error_message;
        uo["elapsed_ms"] = r.elapsed_ms;
        uo["instructions_analyzed"] = static_cast<int64_t>(r.instructions_analyzed);
        uo["equivalence_queries"] = static_cast<int64_t>(r.equivalence_queries);
        uo["equivalence_cache_hits"] = static_cast<int64_t>(r.equivalence_cache_hits);
        uo["remainder_slices"] = static_cast<int64_t>(r.remainder_slices);
        uo["smt_queries"] = static_cast<int64_t>(r.smt_queries);
        uo["smt_false_positives_eliminated"] = static_cast<int64_t>(r.smt_false_positives_eliminated);
        uo["alarms_count"] = static_cast<int64_t>(r.alarms.size());

        llvm::json::Array u_inputs;
        for (const auto& inp : r.input_specs) {
            llvm::json::Object io;
            io["kind"] = inp.kind;
            io["target"] = inp.target;
            io["range_desc"] = inp.range_desc;
            io["function_name"] = inp.function_name;
            io["file"] = inp.loc.file;
            io["line"] = static_cast<int64_t>(inp.loc.line);
            u_inputs.push_back(std::move(io));
        }
        uo["inputs"] = std::move(u_inputs);

        llvm::json::Array u_alarms;
        for (const auto& ua : r.alarms) {
            HazardReport copy = ua;
            if (copy.analysis_name.empty()) copy.analysis_name = r.unit_name;
            u_alarms.push_back(serialize_hazard(copy));
        }
        uo["unit_alarms"] = std::move(u_alarms);

        units_arr.push_back(std::move(uo));
    }
    root["analyses"] = std::move(units_arr);

    // Files & Functions & Source Viewer data
    llvm::json::Array files_arr;
    for (const auto& fr : cd.file_reports) {
        llvm::json::Object fo;
        fo["file_path"] = fr.file_path;
        fo["file_name"] = std::filesystem::path(fr.file_path).filename().string();
        fo["total_lines"] = static_cast<int64_t>(fr.total_lines);
        fo["total_executable_lines"] = static_cast<int64_t>(fr.total_executable_lines);
        fo["total_covered_lines"] = static_cast<int64_t>(fr.total_covered_lines);
        fo["total_alarms"] = static_cast<int64_t>(fr.total_alarms);
        fo["loc_pct"] = fr.file_loc_coverage_percent();

        llvm::json::Array fns_arr;
        for (const auto& fn : fr.functions) {
            llvm::json::Object fno;
            fno["function_name"] = fn.function_name;
            fno["start_line"] = static_cast<int64_t>(fn.start_line);
            fno["end_line"] = static_cast<int64_t>(fn.end_line);
            fno["executable_lines"] = static_cast<int64_t>(fn.executable_lines_count);
            fno["covered_lines"] = static_cast<int64_t>(fn.covered_lines_count);
            fno["loc_pct"] = fn.loc_coverage_percent();
            fno["total_sub_conditions"] = static_cast<int64_t>(fn.total_sub_conditions);
            fno["fully_covered_sub_conditions"] = static_cast<int64_t>(fn.fully_covered_sub_conditions);
            fno["cond_pct"] = fn.condition_coverage_percent();
            fno["alarms_count"] = static_cast<int64_t>(fn.alarms_count);
            fno["was_called"] = fn.was_called;
            fns_arr.push_back(std::move(fno));
        }
        fo["functions"] = std::move(fns_arr);

        llvm::json::Array statuses_arr;
        for (uint32_t L = 1; L <= fr.total_lines; ++L) {
            uint8_t st = (L < fr.line_statuses.size()) ? static_cast<uint8_t>(fr.line_statuses[L]) : 0;
            statuses_arr.push_back(static_cast<int64_t>(st));
        }
        fo["line_statuses"] = std::move(statuses_arr);

        llvm::json::Array src_arr;
        for (uint32_t L = 0; L < fr.total_lines; ++L) {
            src_arr.push_back(L < fr.source_lines.size() ? fr.source_lines[L] : std::string{});
        }
        fo["source_lines"] = std::move(src_arr);

        llvm::json::Object subconds_obj;
        for (const auto& [L, scs] : fr.line_sub_conditions) {
            llvm::json::Array sc_arr;
            for (const auto& sc : scs) {
                llvm::json::Object sco;
                sco["expr"] = sc.expr_text;
                sco["col_start"] = static_cast<int64_t>(sc.col_start);
                sco["col_end"] = static_cast<int64_t>(sc.col_end);
                sco["seen_true"] = sc.seen_true;
                sco["seen_false"] = sc.seen_false;
                sco["fully_covered"] = sc.is_fully_covered();
                sco["note"] = sc.status_note();
                sc_arr.push_back(std::move(sco));
            }
            subconds_obj[std::to_string(L)] = std::move(sc_arr);
        }
        fo["line_sub_conditions"] = std::move(subconds_obj);

        llvm::json::Object line_alarms_obj;
        for (const auto& [L, als] : fr.line_alarms) {
            llvm::json::Array al_arr;
            for (const auto& al : als) {
                al_arr.push_back(serialize_hazard(al));
            }
            line_alarms_obj[std::to_string(L)] = std::move(al_arr);
        }
        fo["line_alarms"] = std::move(line_alarms_obj);

        files_arr.push_back(std::move(fo));
    }
    root["files"] = std::move(files_arr);

    std::string payload_json = escape_json_for_script(json_to_string(llvm::json::Value(std::move(root)), false));

    // Ensure parent directory exists
    std::filesystem::path out_path(output_html_path);
    if (out_path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(out_path.parent_path(), ec);
    }

    std::ofstream ofs(output_html_path, std::ios::out | std::ios::trunc);
    if (!ofs.is_open()) {
        err_msg = "Failed to open HTML report path for writing: " + output_html_path;
        return false;
    }

    ofs << R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>SQC Static Analysis Report — )HTML" << html_escape(project_name) << R"HTML(</title>
<style>
:root {
  --bg: #f8fafc;
  --surface: #ffffff;
  --surface-2: #f1f5f9;
  --border: #e2e8f0;
  --border-strong: #cbd5e1;
  --text: #0f172a;
  --muted: #475569;
  --accent: #0284c7;
  --green: #16a34a;
  --green-bg: #dcfce7;
  --yellow: #ca8a04;
  --yellow-bg: #fef9c3;
  --orange: #ea580c;
  --orange-bg: #ffedd5;
  --red: #dc2626;
  --red-bg: #ffe4e6;
  --purple: #9333ea;
  --purple-bg: #f3e8ff;
}
* { box-sizing: border-box; }
body {
  margin: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Inter, sans-serif;
  background: var(--bg); color: var(--text); line-height: 1.5;
}
header {
  background: #ffffff;
  border-bottom: 1px solid var(--border);
  padding: 20px 28px;
}
.header-top {
  display: flex; justify-content: space-between; align-items: baseline; flex-wrap: wrap; gap: 12px;
}
.header-top h1 { margin: 0; font-size: 22px; font-weight: 700; color: #0f172a; }
.header-top h1 span { color: var(--accent); }
.meta { color: var(--muted); font-size: 13px; }
.kpi-grid {
  display: grid; grid-template-columns: repeat(auto-fit, minmax(190px, 1fr));
  gap: 14px; margin-top: 18px;
}
.kpi {
  background: #ffffff; border: 1px solid var(--border); border-radius: 10px;
  padding: 14px 16px; box-shadow: 0 1px 2px rgba(15, 23, 42, 0.04);
}
.kpi-label { font-size: 12px; text-transform: uppercase; letter-spacing: 0.05em; color: var(--muted); font-weight: 600; }
.kpi-value { font-size: 24px; font-weight: 700; margin-top: 4px; color: #0f172a; }
.kpi-sub { font-size: 12px; color: var(--muted); margin-top: 4px; }
.progress-bar {
  width: 100%; height: 8px; background: #e2e8f0; border-radius: 999px; overflow: hidden; margin-top: 8px;
}
.progress-fill { height: 100%; background: linear-gradient(90deg, #16a34a, #0284c7); border-radius: 999px; }
.progress-fill.warn { background: linear-gradient(90deg, #ca8a04, #ea580c); }
.progress-fill.low { background: linear-gradient(90deg, #dc2626, #ea580c); }

.tabs {
  display: flex; gap: 6px; padding: 12px 28px 0; background: #ffffff;
  border-bottom: 1px solid var(--border-strong);
}
.tab-btn {
  background: transparent; border: none; color: var(--muted); padding: 10px 18px;
  font-size: 14px; font-weight: 600; cursor: pointer; border-bottom: 3px solid transparent;
  transition: color 0.15s, border-color 0.15s;
}
.tab-btn:hover { color: var(--text); }
.tab-btn.active { color: var(--accent); border-bottom-color: var(--accent); background: #f8fafc; border-radius: 6px 6px 0 0; }
.badge-count {
  display: inline-block; padding: 1px 7px; border-radius: 999px; font-size: 11px;
  background: var(--surface-2); color: var(--text); margin-left: 6px; border: 1px solid var(--border);
}
.badge-count.danger { background: #fee2e2; color: #991b1b; border-color: #fca5a5; }

main { padding: 22px 28px 44px; max-width: 1520px; margin: 0 auto; }
.tab-panel { display: none; }
.tab-panel.active { display: block; }
.page-title {
  margin: 0 0 14px 0; font-size: 18px; font-weight: 700; color: #0f172a;
  display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 10px;
}

.toolbar {
  display: flex; flex-wrap: wrap; gap: 10px; margin-bottom: 14px; align-items: center;
}
input[type="text"], select {
  background: #ffffff; border: 1px solid var(--border-strong); color: var(--text);
  padding: 7px 12px; border-radius: 6px; font-size: 13px;
}
input[type="text"] { min-width: 260px; }

table {
  width: 100%; border-collapse: collapse; background: #ffffff;
  border: 1px solid var(--border); border-radius: 8px; overflow: hidden; font-size: 13px;
  box-shadow: 0 1px 2px rgba(15, 23, 42, 0.04);
}
th, td { padding: 10px 12px; text-align: left; border-bottom: 1px solid var(--border); vertical-align: top; }
th { background: var(--surface-2); color: var(--muted); font-weight: 600; font-size: 12px; text-transform: uppercase; letter-spacing: 0.04em; }
tr.clickable:hover { background: #f0f9ff; cursor: pointer; }

.pill {
  display: inline-block; padding: 2px 8px; border-radius: 999px; font-size: 11px; font-weight: 600;
}
.pill-definite { background: #fee2e2; color: #991b1b; border: 1px solid #fca5a5; }
.pill-potential { background: #fef9c3; color: #854d0e; border: 1px solid #fde047; }
.pill-ok { background: #dcfce7; color: #166534; border: 1px solid #86efac; }
.pill-fail { background: #fee2e2; color: #991b1b; border: 1px solid #fca5a5; }

.alarm-detail-row { background: #f8fafc; }
.alarm-detail-box { padding: 16px; display: grid; grid-template-columns: 1fr 1fr; gap: 18px; }
@media (max-width: 900px) { .alarm-detail-box { grid-template-columns: 1fr; } }
.code-snippet {
  background: #ffffff; border: 1px solid var(--border-strong); border-radius: 6px;
  font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace;
  font-size: 12px; overflow-x: auto; padding: 8px 0; color: #0f172a;
}
.snip-line { display: flex; padding: 1px 10px; white-space: pre; }
.snip-line.hl { background: var(--orange-bg); border-left: 3px solid var(--orange); font-weight: 600; }
.snip-ln { width: 44px; color: var(--muted); user-select: none; text-align: right; padding-right: 12px; }

.callstack-box {
  margin-top: 8px; padding: 10px 12px; background: #ffffff; border: 1px solid var(--border);
  border-radius: 6px; font-size: 12.5px;
}
.callstack-step {
  font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace;
  padding: 2px 0; color: #0f172a;
}

.btn {
  background: var(--accent); color: #ffffff; border: none; padding: 7px 14px;
  border-radius: 6px; font-size: 12px; font-weight: 600; cursor: pointer;
}
.btn:hover { filter: brightness(0.95); }
.btn-ghost {
  background: #ffffff; color: var(--text); border: 1px solid var(--border-strong);
}

/* Page 2: Coverage Table */
.fn-row td { background: #fafbfd; padding-left: 30px; }
.mini-bar { display: inline-flex; align-items: center; gap: 8px; min-width: 150px; }
.mini-bar-track { flex: 1; height: 7px; background: #e2e8f0; border-radius: 999px; overflow: hidden; }
.mini-bar-fill { height: 100%; background: var(--green); }
.mini-bar-fill.warn { background: var(--yellow); }
.mini-bar-fill.low { background: var(--red); }

/* Page 3: Source Viewer */
.viewer-card {
  background: #ffffff; border: 1px solid var(--border); border-radius: 10px; overflow: hidden;
  box-shadow: 0 1px 3px rgba(15, 23, 42, 0.05);
}
.viewer-header {
  display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 10px;
  padding: 12px 16px; background: var(--surface-2); border-bottom: 1px solid var(--border);
}
.legend { display: flex; flex-wrap: wrap; gap: 12px; font-size: 12px; color: var(--muted); }
.legend-item { display: inline-flex; align-items: center; gap: 5px; }
.swatch { width: 12px; height: 12px; border-radius: 3px; display: inline-block; border: 1px solid rgba(15,23,42,0.15); }

.source-container {
  max-height: 760px; overflow: auto; background: #ffffff; color: #0f172a;
  font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace; font-size: 13px;
}
.src-line {
  display: flex; align-items: baseline; padding: 1px 12px 1px 0; border-left: 4px solid transparent;
  min-height: 22px;
}
.src-line.st-0 { background: #ffffff; border-left-color: transparent; }
.src-line.st-1 { background: var(--red-bg); border-left-color: var(--red); }
.src-line.st-2 { background: var(--green-bg); border-left-color: var(--green); }
.src-line.st-3 { background: var(--yellow-bg); border-left-color: var(--yellow); }
.src-line.st-4 { background: var(--orange-bg); border-left-color: var(--orange); }
.src-line.st-5 { background: var(--purple-bg); border-left-color: var(--purple); }
.src-line.flash { outline: 2px solid var(--accent); }

.ln-num {
  width: 56px; flex-shrink: 0; text-align: right; padding-right: 12px;
  color: #64748b; user-select: none; border-right: 1px solid #e2e8f0; margin-right: 12px;
}
.ln-code { white-space: pre; flex: 1; color: #0f172a; }
.sc-badges { display: inline-flex; flex-wrap: wrap; gap: 6px; margin-left: 14px; }
.sc-badge {
  font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
  font-size: 11px; padding: 1px 7px; border-radius: 4px; font-weight: 600;
}
.sc-badge.partial { background: #fef08a; color: #854d0e; border: 1px solid #eab308; }
.sc-badge.full { background: #bbf7d0; color: #166534; border: 1px solid #22c55e; }

.inline-alarm-card {
  margin: 4px 14px 8px 68px; padding: 10px 14px; border-radius: 6px;
  background: #fff7ed; border: 1px solid #fb923c; color: #0f172a;
  font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; font-size: 12.5px;
}
.inline-alarm-card strong { color: #c2410c; }
.mono { font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace; }
.muted { color: var(--muted); }
.diag-section {
  background: #ffffff; border: 1px solid var(--border); border-radius: 10px;
  padding: 18px 20px; margin-bottom: 22px; box-shadow: 0 1px 2px rgba(15, 23, 42, 0.04);
}
.diag-section h3 { margin: 0 0 6px 0; font-size: 16px; color: #0f172a; }
.diag-sub { font-size: 13px; color: var(--muted); margin-bottom: 14px; }
.flow-card {
  border: 1px solid var(--border-strong); border-radius: 8px; margin-bottom: 16px; overflow: hidden; background: #ffffff;
}
.flow-card-hdr {
  padding: 12px 16px; background: var(--surface-2); border-bottom: 1px solid var(--border);
  display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 10px;
}
.flow-card-body { padding: 14px 16px; }
.explain-box {
  background: #f0f9ff; border-left: 3px solid var(--accent); padding: 8px 12px;
  border-radius: 4px; font-size: 12.5px; color: #0f172a; line-height: 1.5;
}
.explain-box.warn { background: #fefce8; border-left-color: var(--yellow); }
.explain-box.danger { background: #fff7ed; border-left-color: var(--orange); }
</style>
</head>
<body>
<header>
  <div class="header-top">
    <h1><span>SQC</span> Static Analysis Report &mdash; <span id="hdr-project"></span></h1>
    <div class="meta" id="hdr-timestamp"></div>
  </div>
</header>

<nav class="tabs">
  <button class="tab-btn active" data-tab="overview">1. Overview</button>
  <button class="tab-btn" data-tab="alarms">2. Alarms &amp; Callstack <span class="badge-count danger" id="tab-alarms-cnt">0</span></button>
  <button class="tab-btn" data-tab="coverage">3. Coverage (Files &amp; Functions) <span class="badge-count" id="tab-files-cnt">0</span></button>
  <button class="tab-btn" data-tab="source">4. Source Viewer (Line-by-Line)</button>
  <button class="tab-btn" data-tab="analyses">5. Analyses &amp; Metrics <span class="badge-count" id="tab-units-cnt">0</span></button>
  <button class="tab-btn" data-tab="diagnostics">6. Flow &amp; Diagnostics (Input / Uncovered)</button>
</nav>

<main>
  <!-- PAGE 1: OVERVIEW -->
  <section id="panel-overview" class="tab-panel active">
    <h2 class="page-title">Project Overview &amp; Executive Summary</h2>
    <div class="kpi-grid" id="kpi-grid"></div>
    <div style="margin-top:22px;display:flex;flex-wrap:wrap;gap:12px;">
      <button class="btn" onclick="switchTab('alarms')">Inspect Alarms &amp; Callstack &rarr;</button>
      <button class="btn" style="background:#0f766e;" onclick="switchTab('diagnostics')">Open Flow &amp; Uncovered Diagnostics &rarr;</button>
      <button class="btn btn-ghost" onclick="switchTab('coverage')">View File &amp; Function Coverage &rarr;</button>
      <button class="btn btn-ghost" onclick="switchTab('source')">Open Line-by-Line Source Viewer &rarr;</button>
      <button class="btn btn-ghost" onclick="switchTab('analyses')">View Engine Metrics &rarr;</button>
    </div>
  </section>

  <!-- PAGE 2: ALARMS & CALLSTACK -->
  <section id="panel-alarms" class="tab-panel">
    <h2 class="page-title">Consolidated Alarms &amp; Callstack Details</h2>
    <div class="toolbar">
      <input type="text" id="alarm-search" placeholder="Search function, file, predicate, witness...">
      <select id="alarm-kind-filter"><option value="">All Kinds</option></select>
      <select id="alarm-cert-filter">
        <option value="">All Certainties</option>
        <option value="Definite Bug">Definite Bug</option>
        <option value="Potential UB">Potential UB</option>
      </select>
      <select id="alarm-unit-filter"><option value="">All Analyses</option></select>
    </div>
    <table>
      <thead>
        <tr>
          <th>Kind</th><th>Certainty</th><th>Function</th><th>File:Line</th>
          <th>Predicate</th><th>Concrete Witness</th><th>Callstack</th><th>Analysis</th>
        </tr>
      </thead>
      <tbody id="alarms-tbody"></tbody>
    </table>
  </section>

  <!-- PAGE 2: COVERAGE (FILES & FUNCTIONS) -->
  <section id="panel-coverage" class="tab-panel">
    <h2 class="page-title">
      <span>File &amp; Function Line-of-Code (LoC) Coverage</span>
      <span class="muted" style="font-size:13px;font-weight:400;">Click any File or Function to inspect line-by-line coverage in Source Viewer</span>
    </h2>
    <table>
      <thead>
        <tr>
          <th>File / Function</th><th>Lines</th><th>Executable</th><th>Covered</th>
          <th>LoC Coverage %</th><th>Sub-Condition %</th><th>Alarms</th>
        </tr>
      </thead>
      <tbody id="cov-tbody"></tbody>
    </table>
  </section>

  <!-- PAGE 3: SOURCE VIEWER (LINE-BY-LINE) -->
  <section id="panel-source" class="tab-panel">
    <div class="toolbar">
      <label class="muted" style="font-size:13px;font-weight:600;">File:</label>
      <select id="viewer-file-select"></select>
      <label class="muted" style="font-size:13px;font-weight:600;">Jump to Function:</label>
      <select id="viewer-func-select"></select>
      <button class="btn btn-ghost" onclick="switchTab('coverage')">&larr; Back to Coverage Table</button>
    </div>
    <div class="viewer-card" id="source-viewer">
      <div class="viewer-header">
        <div>
          <strong id="viewer-title" class="mono"></strong>
          <span id="viewer-stats" class="muted" style="margin-left:10px;font-size:12px;"></span>
        </div>
        <div class="legend">
          <span class="legend-item"><span class="swatch" style="background:var(--green-bg);border-color:var(--green)"></span>Covered &amp; Safe</span>
          <span class="legend-item"><span class="swatch" style="background:var(--yellow-bg);border-color:var(--yellow)"></span>Partial Sub-Condition</span>
          <span class="legend-item"><span class="swatch" style="background:var(--orange-bg);border-color:var(--orange)"></span>UB / Hazard Alarm</span>
          <span class="legend-item"><span class="swatch" style="background:var(--purple-bg);border-color:var(--purple)"></span>Dead After UB</span>
          <span class="legend-item"><span class="swatch" style="background:var(--red-bg);border-color:var(--red)"></span>Uncovered</span>
          <span class="legend-item"><span class="swatch" style="background:#ffffff"></span>Non-Executable</span>
        </div>
      </div>
      <div class="source-container" id="source-container"></div>
    </div>
  </section>

  <!-- PAGE 4: ANALYSES & METRICS -->
  <section id="panel-analyses" class="tab-panel">
    <h2 class="page-title">Individual Analyses &amp; Engine Performance Metrics</h2>
    <table>
      <thead>
        <tr>
          <th>ID</th><th>Analysis Unit</th><th>Status</th><th>Runtime (ms)</th>
          <th>Instructions</th><th>Equiv Queries</th><th>Cache Hits</th>
          <th>Remainder Slices</th><th>Z3 Queries</th><th>FP Eliminated</th><th>Alarms</th>
        </tr>
      </thead>
      <tbody id="analyses-tbody"></tbody>
    </table>
  </section>

  <!-- PAGE 6: FLOW & COVERAGE DIAGNOSTICS -->
  <section id="panel-diagnostics" class="tab-panel">
    <h2 class="page-title">
      <span>Flow Input Ranges, Triggered Alarms &amp; Coverage Gap Explanations</span>
      <span class="muted" style="font-size:13px;font-weight:400;">Chi tiết từng luồng input &rarr; lỗi xảy ra, lý do Sub-Condition chỉ đạt 1 chiều &amp; lý do các đoạn code chưa được cover</span>
    </h2>

    <!-- 6.1: Per-Flow Input Ranges -> Alarms -->
    <div class="diag-section">
      <h3>6.1. Từng Luồng Phân Tích: Dải Input Đã Thiết Lập &rarr; Lỗi Xảy Ra</h3>
      <div class="diag-sub">Chọn một luồng phân tích (Analysis Unit) để xem toàn bộ dải giá trị đầu vào tượng trưng đã bơm vào luồng đó và các lỗi (nếu có) xảy ra tương ứng với dải input nào.</div>
      <div class="toolbar">
        <label class="muted" style="font-size:13px;font-weight:600;">Luồng phân tích:</label>
        <select id="diag-flow-select"><option value="">Tất cả các luồng (All Flows)</option></select>
      </div>
      <div id="diag-flows-container"></div>
    </div>

    <!-- 6.2: Partial Sub-Conditions Explanation -->
    <div class="diag-section">
      <h3>6.2. Giải Thích Các Biểu Thức Điều Kiện Con Chỉ Đạt 1 Trường Hợp (Partial Sub-Conditions)</h3>
      <div class="diag-sub">Các điều kiện <code>if / while / &amp;&amp; / ||</code> được tô màu Vàng do mới chỉ xảy ra <code>True</code>, chỉ xảy ra <code>False</code>, hoặc bị ngắt mạch (short-circuit) chưa được tính giá trị.</div>
      <div class="toolbar">
        <input type="text" id="diag-sc-search" placeholder="Tìm tên file, hàm, biểu thức điều kiện...">
        <select id="diag-sc-file"><option value="">Tất cả File</option></select>
        <select id="diag-sc-type">
          <option value="">Tất cả trạng thái thiếu</option>
          <option value="Never False">Chỉ True (Never False)</option>
          <option value="Never True">Chỉ False (Never True)</option>
          <option value="Never evaluated">Ngắt mạch (Short-circuited)</option>
        </select>
      </div>
      <table>
        <thead>
          <tr>
            <th>File:Line</th><th>Hàm</th><th>Biểu Thức Con</th><th>Trạng Thái Đạt Được</th>
            <th>Giải Thích Cụ Thể Nguyên Nhân</th><th>Code</th>
          </tr>
        </thead>
        <tbody id="diag-sc-tbody"></tbody>
      </table>
    </div>

    <!-- 6.3: Uncovered Code Blocks Explanation -->
    <div class="diag-section">
      <h3>6.3. Giải Thích Chi Tiết Các Đoạn Code Chưa Được Cover (Uncovered Blocks &amp; Functions)</h3>
      <div class="diag-sub">Gom nhóm các dòng thực thi chưa chạy tới (màu Đỏ nhạt) theo từng hàm/khối lệnh và chẩn đoán nguyên nhân trực tiếp từ điều kiện rẽ nhánh phía trên hoặc cấu trúc mock.</div>
      <div class="toolbar">
        <input type="text" id="diag-uncov-search" placeholder="Tìm tên file, hàm, nội dung code...">
        <select id="diag-uncov-file"><option value="">Tất cả File</option></select>
        <label style="font-size:13px;display:inline-flex;align-items:center;gap:6px;cursor:pointer;">
          <input type="checkbox" id="diag-hide-zero-files" checked>
          <span>Ẩn các file tầng dưới đang được stub/mock toàn bộ (0% LoC)</span>
        </label>
      </div>
      <table>
        <thead>
          <tr>
            <th>File &amp; Dải Dòng</th><th>Hàm</th><th>Phân Loại</th>
            <th>Đoạn Code Mẫu</th><th>Giải Thích Cụ Thể Vì Sao Chưa Cover</th><th>Code</th>
          </tr>
        </thead>
        <tbody id="diag-uncov-tbody"></tbody>
      </table>
    </div>
  </section>
</main>

<script id="sqc-data" type="application/json">)HTML" << payload_json << R"HTML(</script>
<script>
const DATA = JSON.parse(document.getElementById('sqc-data').textContent);

function esc(s) {
  return String(s ?? '').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}
function fmtPct(v) { return Number(v || 0).toFixed(1) + '%'; }
function barClass(p) { return p >= 80 ? '' : (p >= 50 ? 'warn' : 'low'); }
function shortPath(p) {
  if (!p) return '<unknown>';
  const parts = p.split('/');
  return parts.length > 2 ? '…/' + parts.slice(-2).join('/') : p;
}

// Header & KPIs
document.getElementById('hdr-project').textContent = DATA.project_name;
document.getElementById('hdr-timestamp').textContent = 'Generated: ' + DATA.timestamp;
document.getElementById('tab-alarms-cnt').textContent = DATA.total_alarms;
document.getElementById('tab-files-cnt').textContent = DATA.files.length;
document.getElementById('tab-units-cnt').textContent = DATA.analyses.length;

document.getElementById('kpi-grid').innerHTML = `
  <div class="kpi">
    <div class="kpi-label">Analyses Run</div>
    <div class="kpi-value">${DATA.total_analyses}</div>
    <div class="kpi-sub">${DATA.succeeded_analyses} succeeded &middot; ${DATA.total_elapsed_ms.toFixed(1)} ms total</div>
  </div>
  <div class="kpi">
    <div class="kpi-label">Consolidated Alarms</div>
    <div class="kpi-value" style="color:${DATA.total_alarms ? '#f97316' : '#22c55e'}">${DATA.total_alarms}</div>
    <div class="kpi-sub">${DATA.definite_bugs} Definite Bugs &middot; ${DATA.potential_ubs} Potential UBs</div>
  </div>
  <div class="kpi">
    <div class="kpi-label">Overall Project LoC Coverage</div>
    <div class="kpi-value">${fmtPct(DATA.overall_loc_pct)}</div>
    <div class="progress-bar"><div class="progress-fill ${barClass(DATA.overall_loc_pct)}" style="width:${Math.min(100, DATA.overall_loc_pct)}%"></div></div>
    <div class="kpi-sub">${DATA.total_covered_lines} / ${DATA.total_exec_lines} executable lines</div>
  </div>
  <div class="kpi">
    <div class="kpi-label">Graph Equiv Cache Hit Rate</div>
    <div class="kpi-value">${fmtPct(DATA.cache_hit_pct)}</div>
    <div class="kpi-sub">${DATA.total_equiv_hits} hits / ${DATA.total_equiv_queries} queries &middot; ${DATA.total_remainder_slices} slices</div>
  </div>
  <div class="kpi">
    <div class="kpi-label">SMT (Z3) Refiner</div>
    <div class="kpi-value">${DATA.total_smt_queries}</div>
    <div class="kpi-sub">${DATA.total_smt_fp_eliminated} false positives eliminated</div>
  </div>
`;

// Tabs
function switchTab(name) {
  document.querySelectorAll('.tab-btn').forEach(b => b.classList.toggle('active', b.dataset.tab === name));
  document.querySelectorAll('.tab-panel').forEach(p => p.classList.toggle('active', p.id === 'panel-' + name));
}
document.querySelectorAll('.tab-btn').forEach(b => b.addEventListener('click', () => switchTab(b.dataset.tab)));

// Map file_path -> file object
const FILE_MAP = new Map();
DATA.files.forEach((f, idx) => FILE_MAP.set(f.file_path, { file: f, idx }));

// Populate alarm filters
const kindSet = new Set(DATA.alarms.map(a => a.kind_str));
const unitSet = new Set(DATA.alarms.map(a => a.analysis_name).filter(Boolean));
const kindSel = document.getElementById('alarm-kind-filter');
kindSet.forEach(k => { const o = document.createElement('option'); o.value = k; o.textContent = k; kindSel.appendChild(o); });
const unitSel = document.getElementById('alarm-unit-filter');
unitSet.forEach(u => { const o = document.createElement('option'); o.value = u; o.textContent = u; unitSel.appendChild(o); });

let expandedAlarmIdx = -1;
function renderAlarms() {
  const q = document.getElementById('alarm-search').value.toLowerCase();
  const kf = kindSel.value;
  const cf = document.getElementById('alarm-cert-filter').value;
  const uf = unitSel.value;
  const tbody = document.getElementById('alarms-tbody');
  tbody.innerHTML = '';

  const filtered = DATA.alarms.map((a, i) => ({ a, i })).filter(({ a }) => {
    if (kf && a.kind_str !== kf) return false;
    if (cf && a.certainty_str !== cf) return false;
    if (uf && a.analysis_name !== uf) return false;
    if (q) {
      const hay = [a.kind_str, a.function_name, a.file, a.predicate, a.concrete_witness, a.message, a.analysis_name].join(' ').toLowerCase();
      if (!hay.includes(q)) return false;
    }
    return true;
  });

  if (filtered.length === 0) {
    tbody.innerHTML = '<tr><td colspan="8" class="muted" style="text-align:center;padding:24px;">No alarms match the current filter.</td></tr>';
    return;
  }

  filtered.forEach(({ a, i }) => {
    const tr = document.createElement('tr');
    tr.className = 'clickable';
    const certCls = a.certainty_str === 'Definite Bug' ? 'pill-definite' : 'pill-potential';
    const csShort = (a.callstack && a.callstack.length)
      ? a.callstack.map(f => esc(f)).join(' &rarr; ')
      : '<span class="muted">N/A</span>';
    tr.innerHTML = `
      <td><strong>${esc(a.kind_str)}</strong></td>
      <td><span class="pill ${certCls}">${esc(a.certainty_str)}</span></td>
      <td class="mono">${esc(a.function_name || '<global>')}</td>
      <td class="mono" title="${esc(a.file)}">${esc(shortPath(a.file))}:${a.line}</td>
      <td class="mono">${esc(a.predicate || '-')}</td>
      <td class="mono">${esc(a.concrete_witness || '-')}</td>
      <td class="mono" style="font-size:11.5px;">${csShort}</td>
      <td>${esc(a.analysis_name || '-')}</td>
    `;
    tr.addEventListener('click', () => {
      expandedAlarmIdx = (expandedAlarmIdx === i) ? -1 : i;
      renderAlarms();
    });
    tbody.appendChild(tr);

    if (expandedAlarmIdx === i) {
      const dtr = document.createElement('tr');
      dtr.className = 'alarm-detail-row';
      const entry = FILE_MAP.get(a.file);
      let snippetHtml = '<div class="muted" style="padding:10px;">Source lines unavailable.</div>';
      if (entry && entry.file.source_lines.length > 0 && a.line > 0) {
        const start = Math.max(1, a.line - 3);
        const end = Math.min(entry.file.source_lines.length, a.line + 3);
        let linesHtml = '';
        for (let ln = start; ln <= end; ++ln) {
          linesHtml += `<div class="snip-line ${ln === a.line ? 'hl' : ''}"><span class="snip-ln">${ln}</span><span>${esc(entry.file.source_lines[ln - 1])}</span></div>`;
        }
        snippetHtml = `<div class="code-snippet">${linesHtml}</div>`;
      }
      const csSteps = (a.callstack && a.callstack.length)
        ? a.callstack.map((f, idx) => `<div class="callstack-step">${idx === 0 ? '&#9656;' : '&nbsp;&nbsp;&#8627;'} ${esc(f)}()</div>`).join('')
        : '<div class="muted">N/A</div>';
      dtr.innerHTML = `
        <td colspan="8">
          <div class="alarm-detail-box">
            <div>
              <div><strong>Message:</strong> ${esc(a.message || a.kind_str)}</div>
              <div style="margin-top:6px;"><strong>Predicate:</strong> <span class="mono">${esc(a.predicate || 'N/A')}</span></div>
              <div style="margin-top:6px;"><strong>Concrete Witness:</strong> <span class="mono">${esc(a.concrete_witness || 'N/A')}</span></div>
              <div class="callstack-box">
                <strong>Callstack:</strong>
                ${csSteps}
              </div>
              <div style="margin-top:10px;">
                ${entry ? `<button class="btn" onclick="jumpToSource(${entry.idx}, ${a.line})">Open in Source Viewer &rarr;</button>` : ''}
              </div>
            </div>
            <div>${snippetHtml}</div>
          </div>
        </td>
      `;
      tbody.appendChild(dtr);
    }
  });
}
['alarm-search', 'alarm-kind-filter', 'alarm-cert-filter', 'alarm-unit-filter'].forEach(id => {
  document.getElementById(id).addEventListener('input', renderAlarms);
  document.getElementById(id).addEventListener('change', renderAlarms);
});
renderAlarms();

// Tab 2: Coverage Table + Source Viewer
const expandedFiles = new Set(DATA.files.map((_, idx) => idx));
function renderCoverageTable() {
  const tbody = document.getElementById('cov-tbody');
  tbody.innerHTML = '';
  if (DATA.files.length === 0) {
    tbody.innerHTML = '<tr><td colspan="7" class="muted" style="text-align:center;padding:24px;">No coverage data recorded.</td></tr>';
    return;
  }
  DATA.files.forEach((f, idx) => {
    const isExp = expandedFiles.has(idx);
    const tr = document.createElement('tr');
    tr.className = 'clickable';
    tr.innerHTML = `
      <td>
        <span style="display:inline-block;width:16px;color:var(--accent);" onclick="event.stopPropagation();toggleFile(${idx})">${isExp ? '&#9662;' : '&#9656;'}</span>
        <strong class="mono" title="${esc(f.file_path)}">${esc(f.file_name)}</strong>
        <span class="muted" style="font-size:11px;margin-left:6px;">${esc(f.file_path)}</span>
      </td>
      <td>${f.total_lines}</td>
      <td>${f.total_executable_lines}</td>
      <td>${f.total_covered_lines}</td>
      <td>
        <div class="mini-bar">
          <div class="mini-bar-track"><div class="mini-bar-fill ${barClass(f.loc_pct)}" style="width:${Math.min(100, f.loc_pct)}%"></div></div>
          <span class="mono">${fmtPct(f.loc_pct)}</span>
        </div>
      </td>
      <td class="muted">&mdash;</td>
      <td>${f.total_alarms ? `<span class="pill pill-definite">${f.total_alarms}</span>` : '<span class="muted">0</span>'}</td>
    `;
    tr.addEventListener('click', () => jumpToSource(idx, 1));
    tbody.appendChild(tr);

    if (isExp) {
      f.functions.forEach(fn => {
        const ftr = document.createElement('tr');
        ftr.className = 'fn-row clickable';
        ftr.innerHTML = `
          <td class="mono">
            &#8627; ${esc(fn.function_name)}()
            ${fn.was_called ? '' : '<span class="pill" style="background:#fee2e2;color:#991b1b;border:1px solid #fca5a5;margin-left:6px;">Not Called</span>'}
          </td>
          <td class="muted">L${fn.start_line}&ndash;${fn.end_line}</td>
          <td>${fn.executable_lines}</td>
          <td>${fn.covered_lines}</td>
          <td>
            <div class="mini-bar">
              <div class="mini-bar-track"><div class="mini-bar-fill ${barClass(fn.loc_pct)}" style="width:${Math.min(100, fn.loc_pct)}%"></div></div>
              <span class="mono">${fmtPct(fn.loc_pct)}</span>
            </div>
          </td>
          <td class="mono">${fn.total_sub_conditions ? `${fmtPct(fn.cond_pct)} (${fn.fully_covered_sub_conditions}/${fn.total_sub_conditions})` : '<span class="muted">N/A</span>'}</td>
          <td>${fn.alarms_count ? `<span class="pill pill-definite">${fn.alarms_count}</span>` : '<span class="muted">0</span>'}</td>
        `;
        ftr.addEventListener('click', () => jumpToSource(idx, fn.start_line));
        tbody.appendChild(ftr);
      });
    }
  });
}

function toggleFile(idx) {
  if (expandedFiles.has(idx)) expandedFiles.delete(idx);
  else expandedFiles.add(idx);
  renderCoverageTable();
}

const fileSel = document.getElementById('viewer-file-select');
const funcSel = document.getElementById('viewer-func-select');
DATA.files.forEach((f, idx) => {
  const o = document.createElement('option');
  o.value = String(idx);
  o.textContent = `${f.file_name} (${fmtPct(f.loc_pct)})`;
  fileSel.appendChild(o);
});
fileSel.addEventListener('change', () => openSourceViewer(Number(fileSel.value), 1));
funcSel.addEventListener('change', () => {
  const ln = Number(funcSel.value);
  if (ln > 0) {
    const el = document.getElementById('src-ln-' + ln);
    if (el) {
      el.scrollIntoView({ behavior: 'smooth', block: 'center' });
      el.classList.add('flash');
      setTimeout(() => el.classList.remove('flash'), 1800);
    }
  }
});

function openSourceViewer(fileIdx, targetLine) {
  const f = DATA.files[fileIdx];
  if (!f) return;
  fileSel.value = String(fileIdx);
  funcSel.innerHTML = '<option value="0">-- Jump to Function --</option>';
  f.functions.forEach(fn => {
    const o = document.createElement('option');
    o.value = String(fn.start_line);
    o.textContent = `${fn.function_name}() — L${fn.start_line} (${fmtPct(fn.loc_pct)})`;
    funcSel.appendChild(o);
  });

  document.getElementById('viewer-title').textContent = f.file_path;
  document.getElementById('viewer-stats').textContent =
    `${f.total_covered_lines}/${f.total_executable_lines} executable lines (${fmtPct(f.loc_pct)}) · ${f.total_alarms} alarms`;

  const container = document.getElementById('source-container');
  let html = '';
  for (let i = 0; i < f.total_lines; ++i) {
    const ln = i + 1;
    const st = f.line_statuses[i] || 0;
    const code = f.source_lines[i] || '';
    const subconds = f.line_sub_conditions[String(ln)] || [];
    const alarms = f.line_alarms[String(ln)] || [];

    let badgesHtml = '';
    if (subconds.length > 0) {
      badgesHtml = '<span class="sc-badges">' + subconds.map(sc => {
        const cls = sc.fully_covered ? 'full' : 'partial';
        return `<span class="sc-badge ${cls}">[${esc(sc.expr)}: ${esc(sc.note)}]</span>`;
      }).join('') + '</span>';
    }

    html += `<div class="src-line st-${st}" id="src-ln-${ln}"><span class="ln-num">${ln}</span><span class="ln-code">${esc(code)}${badgesHtml}</span></div>`;

    if (alarms.length > 0) {
      alarms.forEach(al => {
        const csInline = (al.callstack && al.callstack.length) ? al.callstack.map(c => esc(c) + '()').join(' &rarr; ') : 'N/A';
        html += `<div class="inline-alarm-card">
          <strong>&#9888; ${esc(al.kind_str)} (${esc(al.certainty_str)})</strong> &mdash; ${esc(al.message || '')}<br>
          <span class="muted">Predicate:</span> <span class="mono">${esc(al.predicate || 'N/A')}</span> &nbsp;|&nbsp;
          <span class="muted">Concrete Witness:</span> <span class="mono">${esc(al.concrete_witness || 'N/A')}</span><br>
          <span class="muted">Callstack:</span> <span class="mono">${csInline}</span>
        </div>`;
      });
    }
  }
  container.innerHTML = html;

  if (targetLine > 0) {
    const el = document.getElementById('src-ln-' + targetLine);
    if (el) {
      el.scrollIntoView({ behavior: 'smooth', block: 'center' });
      el.classList.add('flash');
      setTimeout(() => el.classList.remove('flash'), 1800);
    }
  }
}

function jumpToSource(fileIdx, line) {
  switchTab('source');
  openSourceViewer(fileIdx, line);
}
window.jumpToSource = jumpToSource;
window.toggleFile = toggleFile;
window.switchTab = switchTab;

renderCoverageTable();
if (DATA.files.length > 0) openSourceViewer(0, 1);

// Tab 3: Individual Analyses
const atbody = document.getElementById('analyses-tbody');
DATA.analyses.forEach(u => {
  const hitPct = u.equivalence_queries ? (100 * u.equivalence_cache_hits / u.equivalence_queries).toFixed(1) + '%' : '0.0%';
  const tr = document.createElement('tr');
  tr.innerHTML = `
    <td class="mono">${u.unit_id}</td>
    <td><strong>${esc(u.unit_name)}</strong>${u.error_message ? `<div class="muted" style="font-size:11px;color:#fca5a5;">${esc(u.error_message)}</div>` : ''}</td>
    <td><span class="pill ${u.success ? 'pill-ok' : 'pill-fail'}">${u.success ? 'OK' : 'Failed'}</span></td>
    <td class="mono">${Number(u.elapsed_ms).toFixed(2)}</td>
    <td class="mono">${u.instructions_analyzed}</td>
    <td class="mono">${u.equivalence_queries}</td>
    <td class="mono">${u.equivalence_cache_hits} (${hitPct})</td>
    <td class="mono">${u.remainder_slices}</td>
    <td class="mono">${u.smt_queries}</td>
    <td class="mono">${u.smt_false_positives_eliminated}</td>
    <td>${u.alarms_count ? `<span class="pill pill-definite">${u.alarms_count}</span>` : '0'}</td>
  `;
  atbody.appendChild(tr);
});

// ============================================================================
// Tab 6: Flow Input Ranges, Triggered Alarms & Coverage Gap Diagnostics
// ============================================================================
function findEnclosingFunc(fileObj, line) {
  if (!fileObj || !fileObj.functions) return null;
  for (const fn of fileObj.functions) {
    if (line >= fn.start_line && line <= fn.end_line) return fn;
  }
  return null;
}

function getSourceLine(filePath, line) {
  const entry = FILE_MAP.get(filePath);
  if (!entry || line < 1 || line > entry.file.source_lines.length) return '';
  return (entry.file.source_lines[line - 1] || '').trim();
}

function explainAlarmFromInputs(al, unitInputs) {
  const kind = al.kind_str || '';
  const msg = al.message || '';
  const pred = al.predicate || '';
  const wit = al.concrete_witness || '';

  if (kind.includes('String') || pred.includes('null_term_offset')) {
    const unkInputs = (unitInputs || []).filter(i => i.kind === 'sqc_make_unknown');
    const unkHint = unkInputs.length
      ? `Trong luồng này có sử dụng <code>sqc_make_unknown</code> (ví dụ tại <code>${esc(shortPath(unkInputs[0].file))}:${unkInputs[0].line}</code> trong <code>${esc(unkInputs[0].function_name)}()</code>) tạo vùng byte ngẫu nhiên <code>[0..255]</code> mà không đảm bảo có ký tự kết thúc <code>'\\0'</code>.`
      : `Vùng nhớ chuỗi đầu vào được khởi tạo tượng trưng nhưng chưa đảm bảo có byte kết thúc <code>'\\0'</code> trong kích thước mảng.`;
    return `${unkHint} Khi truyền vào hàm chuỗi không giới hạn độ dài (như <code>strcmp</code> / <code>strlen</code> / <code>strcpy</code>) với điều kiện <code>${esc(pred)}</code> (${esc(wit)}), hàm đọc vượt ra ngoài biên mảng. Khắc phục: gán <code>buf[MAX] = '\\0'</code> ở nơi tạo dữ liệu hoặc đổi sang <code>strncmp</code>.`;
  }
  if (kind.includes('Null Pointer')) {
    return `Trên đường đi <code>${(al.callstack || []).map(esc).join(' &rarr; ')}</code>, con trỏ thỏa mãn điều kiện <code>${esc(pred)}</code> (mẫu: <code>${esc(wit)}</code>) do dải đầu vào tượng trưng (ví dụ <code>sqc_nondet_ptr(..., NULL)</code> hoặc nhánh chưa khởi tạo) cho phép giá trị <code>NULL</code> đi tới điểm giải tham chiếu mà chưa có lệnh kiểm tra <code>!= NULL</code> chặn trước.`;
  }
  if (kind.includes('Out-of-Bounds')) {
    return `Chỉ số hoặc kích thước truy cập bộ nhớ rơi vào miền <code>${esc(pred)}</code> (giá trị cụ thể gây lỗi: <code>${esc(wit)}</code>). Nguyên nhân trực tiếp là dải giá trị đầu vào tượng trưng cho phép chỉ số vượt quá kích thước vùng nhớ đích mà chưa có nhánh kiểm tra biên (bounds check) chặt trước khi đọc/ghi.`;
  }
  if (kind.includes('Division')) {
    return `Mẫu số nhận giá trị <code>0</code> từ dải input tượng trưng (<code>${esc(wit)}</code>) và đi tới phép chia/chia lấy dư mà chưa được kiểm tra <code>!= 0</code>.`;
  }
  if (kind.includes('Overflow')) {
    return `Phép toán số học có dấu vượt ngưỡng 32-bit (<code>${esc(pred)}</code>, mẫu: <code>${esc(wit)}</code>) khi các toán hạng nhận giá trị cực biên từ dải <code>sqc_interval</code>.`;
  }
  if (kind.includes('Uninitialized')) {
    return `Ô nhớ được đọc trên một nhánh thực thi trước khi được gán giá trị khởi tạo hợp lệ (<code>${esc(wit)}</code>).`;
  }
  return `Lỗi kích hoạt khi tổ hợp đầu vào thỏa mãn điều kiện <code>${esc(pred)}</code> (mẫu cụ thể: <code>${esc(wit)}</code>).`;
}

// 6.1 Render Flows
const diagFlowSel = document.getElementById('diag-flow-select');
DATA.analyses.forEach(u => {
  const o = document.createElement('option');
  o.value = u.unit_name;
  o.textContent = `[#${u.unit_id}] ${u.unit_name} (${u.alarms_count} alarms, ${(u.inputs || []).length} input specs)`;
  diagFlowSel.appendChild(o);
});

function renderDiagFlows() {
  const selected = diagFlowSel.value;
  const container = document.getElementById('diag-flows-container');
  const units = DATA.analyses.filter(u => !selected || u.unit_name === selected);
  if (!units.length) {
    container.innerHTML = '<div class="muted">Không có dữ liệu luồng phân tích.</div>';
    return;
  }
  let html = '';
  units.forEach(u => {
    const inputs = u.inputs || [];
    const uAlarms = (u.unit_alarms && u.unit_alarms.length)
      ? u.unit_alarms
      : DATA.alarms.filter(a => a.analysis_name === u.unit_name);

    let inputsHtml = '';
    if (inputs.length === 0) {
      inputsHtml = '<div class="muted" style="font-size:13px;margin-bottom:12px;">Luồng này chạy với dữ liệu hằng/mặc định (không gọi trực tiếp primitive bơm input tượng trưng hoặc tải từ cache).</div>';
    } else {
      inputsHtml = `<table style="margin-bottom:14px;">
        <thead>
          <tr>
            <th>Primitive</th><th>Biến / Đích</th><th>Dải Giá Trị Đã Set (Domain)</th>
            <th>Hàm Thiết Lập</th><th>Vị Trí &amp; Dòng Lệnh C</th>
          </tr>
        </thead>
        <tbody>` + inputs.map(inp => {
          const entry = FILE_MAP.get(inp.file);
          const srcLine = getSourceLine(inp.file, inp.line);
          const jumpBtn = (entry && inp.line > 0)
            ? `<a href="#" onclick="event.preventDefault();jumpToSource(${entry.idx},${inp.line})" class="mono">${esc(shortPath(inp.file))}:${inp.line}</a>`
            : `<span class="mono">${esc(shortPath(inp.file || '-'))}${inp.line ? ':' + inp.line : ''}</span>`;
          return `<tr>
            <td><span class="pill" style="background:#e0f2fe;color:#0369a1;border:1px solid #7dd3fc;">${esc(inp.kind)}</span></td>
            <td class="mono"><strong>${esc(inp.target || '-')}</strong></td>
            <td class="mono" style="color:#0f766e;font-weight:600;">${esc(inp.range_desc)}</td>
            <td class="mono">${esc(inp.function_name || '-')}()</td>
            <td>${jumpBtn}${srcLine ? `<div class="mono muted" style="font-size:11.5px;margin-top:2px;">${esc(srcLine)}</div>` : ''}</td>
          </tr>`;
        }).join('') + `</tbody></table>`;
    }

    let alarmsHtml = '';
    if (uAlarms.length === 0) {
      alarmsHtml = `<div class="explain-box" style="background:#f0fdf4;border-left-color:var(--green);">
        <strong style="color:#166534;">&#10003; 0 Alarms trên luồng <code>${esc(u.unit_name)}</code>:</strong>
        Toàn bộ các đường thực thi sinh ra từ tổ hợp dải input ở bảng trên đều đã được kiểm chứng không vi phạm bộ nhớ, con trỏ NULL hay tràn số.
      </div>`;
    } else {
      alarmsHtml = uAlarms.map(al => {
        const entry = FILE_MAP.get(al.file);
        const cs = (al.callstack && al.callstack.length) ? al.callstack.map(c => esc(c) + '()').join(' &rarr; ') : 'N/A';
        return `<div class="explain-box danger" style="margin-bottom:10px;">
          <div style="display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:8px;">
            <div>
              <strong style="color:#c2410c;">&#9888; ${esc(al.kind_str)} (${esc(al.certainty_str)})</strong>
              tại <span class="mono">${esc(al.function_name)}()</span> &mdash;
              <span class="mono">${esc(shortPath(al.file))}:${al.line}</span>
            </div>
            ${entry ? `<button class="btn" onclick="jumpToSource(${entry.idx},${al.line})">Mở dòng lỗi &rarr;</button>` : ''}
          </div>
          <div style="margin-top:6px;">
            <strong>Điều kiện xảy ra lỗi (Predicate):</strong> <code class="mono">${esc(al.predicate)}</code> &nbsp;|&nbsp;
            <strong>Mẫu giá trị gây lỗi (Witness):</strong> <code class="mono">${esc(al.concrete_witness)}</code>
          </div>
          <div style="margin-top:4px;"><strong>Luồng gọi (Callstack):</strong> <span class="mono" style="font-size:12px;">${cs}</span></div>
          <div style="margin-top:6px;"><strong>Giải thích từ dải Input:</strong> ${explainAlarmFromInputs(al, inputs)}</div>
        </div>`;
      }).join('');
    }

    html += `<div class="flow-card">
      <div class="flow-card-hdr">
        <div>
          <strong>Luồng #${u.unit_id}: <span class="mono">${esc(u.unit_name)}</span></strong>
          <span class="muted" style="margin-left:10px;font-size:12px;">${inputs.length} dải input tượng trưng &middot; ${u.instructions_analyzed} lệnh đã phân tích &middot; ${Number(u.elapsed_ms).toFixed(1)} ms</span>
        </div>
        <div>
          ${uAlarms.length
            ? `<span class="pill pill-definite">${uAlarms.length} Alarm(s)</span>`
            : `<span class="pill pill-ok">0 Alarms (Safe)</span>`}
        </div>
      </div>
      <div class="flow-card-body">
        <div style="font-weight:600;font-size:13px;margin-bottom:6px;">A. Các dải Input đã thiết lập trong luồng này:</div>
        ${inputsHtml}
        <div style="font-weight:600;font-size:13px;margin-bottom:6px;">B. Kết quả kiểm chứng &amp; Lỗi tương ứng:</div>
        ${alarmsHtml}
      </div>
    </div>`;
  });
  container.innerHTML = html;
}
diagFlowSel.addEventListener('change', renderDiagFlows);
renderDiagFlows();

// 6.2 Build & Explain Partial Sub-Conditions
function explainPartialSubCond(sc, allScOnLine, codeLine, fnName) {
  const expr = (sc.expr || '').trim();
  const note = sc.note || '';
  const idx = allScOnLine.indexOf(sc);

  if (note.includes('Never evaluated')) {
    const prev = idx > 0 ? allScOnLine[idx - 1] : null;
    if (prev) {
      return `Biểu thức đứng trước <code>${esc(prev.expr)}</code> luôn nhận một chiều (<em>${esc(prev.note)}</em>), gây ngắt mạch (short-circuit) toán tử <code>&amp;&amp;</code> / <code>||</code> nên vế <code>${esc(expr)}</code> chưa bao giờ được tính giá trị.`;
    }
    return `Bị ngắt mạch (short-circuit) bởi điều kiện đứng trước trong câu lệnh phức hợp nên không bao giờ được thực thi tới.`;
  }

  const isOnlyTrue = note.includes('Never False');
  const isNullCheck = /NULL|nullptr|\b0\b/.test(expr) || (!/[<>!=]=|[<>]/.test(expr) && /driver|ptr|cb|ctx|context|settings|msg|socket|handler/i.test(expr));

  if (expr.includes('gp_modem_driver->') || (codeLine.includes('gp_modem_driver->') && expr !== 'NULL != gp_modem_driver' && expr !== 'gp_modem_driver != NULL')) {
    if (isOnlyTrue) {
      return `Trong cả 3 bản mock modem đang hỗ trợ (<code>BG95</code>, <code>EG915U</code>, <code>HL7900</code>), trường callback này đều được gán hàm hợp lệ khác <code>NULL</code>. Do đó sau khi đi qua vế <code>gp_modem_driver != NULL</code>, điều kiện kiểm tra con trỏ hàm luôn <strong>True</strong> (nhánh <code>False</code> chỉ là phòng vệ an toàn nếu thêm modem mới không hỗ trợ hàm này).`;
    } else {
      return `Trên các nhánh thực thi đi tới đây, trường callback trong <code>gp_modem_driver</code> luôn là <code>NULL</code> (hoặc các modem hỗ trợ chưa đi vào trạng thái này), nên điều kiện chỉ nhận <strong>False</strong>.`;
    }
  }

  if (expr.includes('gp_modem_driver')) {
    if (isOnlyTrue) {
      return `Khi thực thi tới hàm <code>${esc(fnName)}()</code>, biến toàn cục <code>gp_modem_driver</code> đã được nạp driver hợp lệ ở bước khởi tạo trước đó (nhánh <code>gp_modem_driver == NULL</code> đã bị chặn từ tầng trên), nên kiểm tra khác <code>NULL</code> tại đây luôn <strong>True</strong>.`;
    } else {
      return `Tại điểm kiểm tra này <code>gp_modem_driver</code> luôn bằng <code>NULL</code> trên các đường đi thực tế.`;
    }
  }

  if (isNullCheck) {
    if (isOnlyTrue) {
      return `Con trỏ trong biểu thức <code>${esc(expr)}</code> luôn trỏ tới biến cục bộ/toàn cục hợp lệ từ caller (không có caller nào truyền <code>NULL</code> trên đường đi này), nên nhánh phòng vệ kiểm tra <code>NULL</code> không xảy ra.`;
    } else {
      return `Biểu thức kiểm tra <code>NULL</code> (<code>${esc(expr)}</code>) luôn nhận <strong>False</strong> (ví dụ kiểm tra <code>if (NULL == ptr)</code> trên con trỏ luôn hợp lệ, hoặc con trỏ callback chưa được đăng ký).`;
    }
  }

  if (/timer|TIMERS_MAXIMUM/i.test(expr)) {
    return `Biến đếm/ID timer nằm gọn trong giới hạn cấu hình của test driver (<code>TIMERS_MAXIMUM</code>), nên nhánh tràn giới hạn timer không kích hoạt.`;
  }

  if (isOnlyTrue) {
    return `Trên mọi trạng thái trừu tượng đi tới dòng này, miền giá trị của các biến trong <code>${esc(expr)}</code> luôn thỏa mãn điều kiện (<strong>luôn True</strong>); chưa có đường dữ liệu đầu vào hoặc trạng thái máy nào rơi vào phần bù để tạo kết quả <strong>False</strong>.`;
  }
  return `Trên mọi trạng thái trừu tượng đi tới dòng này, miền giá trị của <code>${esc(expr)}</code> luôn vi phạm điều kiện (<strong>luôn False</strong>); cần mở rộng trạng thái tiền điều kiện hoặc giá trị trả về của hàm gọi trước đó nếu muốn kích hoạt nhánh <strong>True</strong>.`;
}

const partialScRows = [];
const scFileSet = new Set();
DATA.files.forEach((f, fileIdx) => {
  Object.keys(f.line_sub_conditions || {}).forEach(lnStr => {
    const ln = Number(lnStr);
    const scList = f.line_sub_conditions[lnStr] || [];
    const fn = findEnclosingFunc(f, ln);
    const fnName = fn ? fn.function_name : '<global>';
    const codeLine = (f.source_lines[ln - 1] || '').trim();
    scList.forEach(sc => {
      if (!sc.fully_covered) {
        scFileSet.add(f.file_name);
        partialScRows.push({
          fileIdx,
          fileName: f.file_name,
          filePath: f.file_path,
          line: ln,
          fnName,
          expr: sc.expr,
          note: sc.note,
          codeLine,
          explanation: explainPartialSubCond(sc, scList, codeLine, fnName)
        });
      }
    });
  });
});

const diagScFileSel = document.getElementById('diag-sc-file');
Array.from(scFileSet).sort().forEach(fn => {
  const o = document.createElement('option');
  o.value = fn;
  o.textContent = fn;
  diagScFileSel.appendChild(o);
});

function renderDiagSubConds() {
  const q = document.getElementById('diag-sc-search').value.toLowerCase();
  const ff = diagScFileSel.value;
  const tf = document.getElementById('diag-sc-type').value;
  const tbody = document.getElementById('diag-sc-tbody');

  const rows = partialScRows.filter(r => {
    if (ff && r.fileName !== ff) return false;
    if (tf && !r.note.includes(tf)) return false;
    if (q) {
      const hay = [r.fileName, r.fnName, r.expr, r.note, r.codeLine, r.explanation].join(' ').toLowerCase();
      if (!hay.includes(q)) return false;
    }
    return true;
  });

  if (!rows.length) {
    tbody.innerHTML = '<tr><td colspan="6" class="muted" style="text-align:center;padding:20px;">Không có Sub-Condition thiếu nào khớp bộ lọc hiện tại.</td></tr>';
    return;
  }

  tbody.innerHTML = rows.map(r => `
    <tr>
      <td class="mono"><strong>${esc(r.fileName)}:${r.line}</strong></td>
      <td class="mono">${esc(r.fnName)}()</td>
      <td><code class="mono" style="background:#fef9c3;padding:2px 6px;border-radius:4px;border:1px solid #fde047;">${esc(r.expr)}</code></td>
      <td><span class="pill pill-potential">${esc(r.note)}</span></td>
      <td>
        <div class="explain-box warn">${r.explanation}</div>
        <div class="mono muted" style="font-size:11.5px;margin-top:4px;">Code: ${esc(r.codeLine)}</div>
      </td>
      <td><button class="btn btn-ghost" onclick="jumpToSource(${r.fileIdx},${r.line})">Mở code &rarr;</button></td>
    </tr>
  `).join('');
}
['diag-sc-search', 'diag-sc-file', 'diag-sc-type'].forEach(id => {
  document.getElementById(id).addEventListener('input', renderDiagSubConds);
  document.getElementById(id).addEventListener('change', renderDiagSubConds);
});
renderDiagSubConds();

// 6.3 Build & Explain Uncovered Code Blocks & Functions
function explainUncoveredBlock(f, fn, startLn, endLn) {
  if (f.total_covered_lines === 0) {
    return {
      category: 'File Tầng Dưới Đã Stub / Mock',
      reason: `Toàn bộ file <code>${esc(f.file_name)}</code> (0/${f.total_executable_lines} dòng) được biên dịch cùng module nhưng các hàm giao tiếp của nó đã được thay thế bằng hàm stub/mock (ví dụ qua con trỏ hàm <code>gp_modem_driver</code> hoặc <code>stub_at_parser.c</code>). Để kiểm chứng sâu file này, nên chạy unit phân tích riêng cho tầng driver/AT tương ứng.`
    };
  }

  if (fn && !fn.was_called) {
    return {
      category: 'Hàm Chưa Được Gọi (Not Called)',
      reason: `Hàm <code>${esc(fn.function_name)}()</code> không có đường gọi nào kích hoạt từ <code>sqc_main()</code> trong các luồng hiện tại (có thể là callback chưa gắn vào luồng sự kiện này, API phụ trợ chưa gọi, hoặc hàm static chỉ gọi từ một nhánh phía trên cũng chưa đạt tới).`
    };
  }

  // Inspect 1..6 lines immediately above startLn for guards, cases, or early returns
  for (let look = startLn - 1; look >= Math.max(1, startLn - 8); --look) {
    const prevTxt = (f.source_lines[look - 1] || '').trim();
    const prevScs = f.line_sub_conditions[String(look)] || [];
    const partialSc = prevScs.find(s => !s.fully_covered);

    if (/^default\s*:/.test(prevTxt)) {
      return {
        category: 'Nhánh default Phòng Vệ (Switch)',
        reason: `Nằm trong nhánh <code>default:</code> (dòng ${look}) của câu lệnh <code>switch</code>. Miền giá trị của biến điều khiển <code>switch</code> đã được phủ kín bởi các nhãn <code>case</code> hợp lệ, nên nhánh phòng vệ <code>default:</code> không bao giờ xảy ra (đúng thiết kế an toàn).`
      };
    }
    if (/^case\s+([^:]+):/.test(prevTxt)) {
      const m = prevTxt.match(/^case\s+([^:]+):/);
      return {
        category: 'Nhánh case Chưa Xuất Hiện',
        reason: `Thuộc nhãn <code>case ${esc(m ? m[1] : '')}:</code> (dòng ${look}), nhưng biểu thức điều khiển <code>switch</code> trên đường đi thực tế không nhận giá trị hằng số này.`
      };
    }
    if (partialSc) {
      return {
        category: 'Bị Chặn Bởi Điều Kiện 1 Chiều Phía Trên',
        reason: `Ngay phía trên tại dòng <strong>${look}</strong>, điều kiện <code>${esc(partialSc.expr)}</code> chỉ xảy ra 1 chiều (<em>${esc(partialSc.note)}</em>), làm nhánh chứa đoạn dòng <code>L${startLn}&ndash;L${endLn}</code> không thể đi vào.`
      };
    }
    if (/^else\b/.test(prevTxt)) {
      // Look further up for the matching if condition
      for (let k = look - 1; k >= Math.max(1, look - 12); --k) {
        const kScs = f.line_sub_conditions[String(k)] || [];
        const kp = kScs.find(s => !s.fully_covered);
        if (kp) {
          return {
            category: 'Nhánh else Của Điều Kiện Luôn True',
            reason: `Thuộc khối <code>else</code> (dòng ${look}) ứng với điều kiện tại dòng <strong>${k}</strong> (<code>${esc(kp.expr)}</code>: <em>${esc(kp.note)}</em>). Vì điều kiện <code>if</code> luôn đúng nên nhánh <code>else</code> không bao giờ chạy.`
          };
        }
      }
    }
  }

  return {
    category: 'Tiền Điều Kiện Trạng Thái / Luồng Phụ',
    reason: `Hàm <code>${esc(fn ? fn.function_name : '<global>')}()</code> đã được gọi (${fn ? fn.covered_lines + '/' + fn.executable_lines : ''} dòng), nhưng nhánh <code>L${startLn}&ndash;L${endLn}</code> đòi hỏi tổ hợp trạng thái nội bộ hoặc kết quả trả về từ bước trước chưa xuất hiện trong chu kỳ lặp hiện tại.`
  };
}

const uncovBlocks = [];
const uncovFileSet = new Set();
DATA.files.forEach((f, fileIdx) => {
  if (f.total_covered_lines === f.total_executable_lines) return;
  uncovFileSet.add(f.file_name);

  if (f.total_covered_lines === 0) {
    uncovBlocks.push({
      fileIdx,
      fileName: f.file_name,
      zeroCovFile: true,
      startLn: 1,
      endLn: f.total_lines,
      fnName: `Toàn bộ file (${f.functions.length} hàm)`,
      sampleCode: (f.source_lines.find(s => s.trim().length > 0) || '').trim(),
      ...explainUncoveredBlock(f, null, 1, f.total_lines)
    });
    return;
  }

  // Group uncovered functions first if completely uncalled, else contiguous uncovered executable lines
  const coveredFnRanges = [];
  f.functions.forEach(fn => {
    if (!fn.was_called && fn.executable_lines > 0) {
      coveredFnRanges.push([fn.start_line, fn.end_line]);
      const sample = (f.source_lines[fn.start_line - 1] || '').trim();
      uncovBlocks.push({
        fileIdx,
        fileName: f.file_name,
        zeroCovFile: false,
        startLn: fn.start_line,
        endLn: fn.end_line,
        fnName: fn.function_name,
        sampleCode: sample,
        ...explainUncoveredBlock(f, fn, fn.start_line, fn.end_line)
      });
    }
  });

  const inUncalledFn = (ln) => coveredFnRanges.some(([a, b]) => ln >= a && ln <= b);

  let runStart = 0, runEnd = 0;
  const flushRun = () => {
    if (!runStart) return;
    const fn = findEnclosingFunc(f, runStart);
    const sample = (f.source_lines[runStart - 1] || '').trim();
    uncovBlocks.push({
      fileIdx,
      fileName: f.file_name,
      zeroCovFile: false,
      startLn: runStart,
      endLn: runEnd,
      fnName: fn ? fn.function_name : '<global>',
      sampleCode: sample,
      ...explainUncoveredBlock(f, fn, runStart, runEnd)
    });
    runStart = 0; runEnd = 0;
  };

  for (let ln = 1; ln <= f.total_lines; ++ln) {
    const st = f.line_statuses[ln - 1] || 0;
    if (st === 1 && !inUncalledFn(ln)) {
      if (!runStart) { runStart = ln; runEnd = ln; }
      else { runEnd = ln; }
    } else if (st !== 0) {
      flushRun();
    }
  }
  flushRun();
});

const diagUncovFileSel = document.getElementById('diag-uncov-file');
Array.from(uncovFileSet).sort().forEach(fn => {
  const o = document.createElement('option');
  o.value = fn;
  o.textContent = fn;
  diagUncovFileSel.appendChild(o);
});

function renderDiagUncovered() {
  const q = document.getElementById('diag-uncov-search').value.toLowerCase();
  const ff = diagUncovFileSel.value;
  const hideZero = document.getElementById('diag-hide-zero-files').checked;
  const tbody = document.getElementById('diag-uncov-tbody');

  const rows = uncovBlocks.filter(b => {
    if (hideZero && b.zeroCovFile && !ff) return false;
    if (ff && b.fileName !== ff) return false;
    if (q) {
      const hay = [b.fileName, b.fnName, b.category, b.sampleCode, b.reason].join(' ').toLowerCase();
      if (!hay.includes(q)) return false;
    }
    return true;
  });

  if (!rows.length) {
    tbody.innerHTML = '<tr><td colspan="6" class="muted" style="text-align:center;padding:20px;">Không có đoạn code chưa cover nào khớp bộ lọc hiện tại.</td></tr>';
    return;
  }

  tbody.innerHTML = rows.map(b => `
    <tr>
      <td class="mono"><strong>${esc(b.fileName)}</strong><br><span class="muted">L${b.startLn}${b.endLn > b.startLn ? '&ndash;L' + b.endLn : ''}</span></td>
      <td class="mono">${esc(b.fnName)}${b.zeroCovFile ? '' : '()'}</td>
      <td><span class="pill" style="background:#fee2e2;color:#991b1b;border:1px solid #fca5a5;">${esc(b.category)}</span></td>
      <td class="mono" style="font-size:12px;max-width:260px;overflow:hidden;text-overflow:ellipsis;">${esc(b.sampleCode)}</td>
      <td><div class="explain-box">${b.reason}</div></td>
      <td><button class="btn btn-ghost" onclick="jumpToSource(${b.fileIdx},${b.startLn})">Mở code &rarr;</button></td>
    </tr>
  `).join('');
}
['diag-uncov-search', 'diag-uncov-file', 'diag-hide-zero-files'].forEach(id => {
  document.getElementById(id).addEventListener('input', renderDiagUncovered);
  document.getElementById(id).addEventListener('change', renderDiagUncovered);
});
renderDiagUncovered();
</script>
</body>
</html>
)HTML";

    ofs.flush();
    if (!ofs.good()) {
        err_msg = "Error writing HTML report to: " + output_html_path;
        return false;
    }
    return true;
}

bool ReportGenerator::generate_sarif_report(
    const std::string& output_sarif_path,
    const std::vector<AnalysisRunResult>& results,
    std::string& err_msg) {
    ConsolidatedData cd = consolidate_results(results);

    llvm::json::Array rules_arr;
    std::set<std::string> seen_rules;
    llvm::json::Array results_arr;

    for (const auto& a : cd.alarms) {
        std::string rid = hazard_rule_id(a.kind);
        if (seen_rules.insert(rid).second) {
            llvm::json::Object rule;
            rule["id"] = rid;
            rule["name"] = hazard_kind_to_string(a.kind);
            llvm::json::Object sdesc;
            sdesc["text"] = hazard_kind_to_string(a.kind);
            rule["shortDescription"] = std::move(sdesc);
            rules_arr.push_back(std::move(rule));
        }

        llvm::json::Object res_obj;
        res_obj["ruleId"] = rid;
        res_obj["level"] = (a.certainty == HazardCertainty::DefiniteBug) ? "error" : "warning";

        std::ostringstream msg;
        msg << hazard_kind_to_string(a.kind);
        if (!a.message.empty()) msg << ": " << a.message;
        if (!a.predicate.empty()) msg << " [Predicate: " << a.predicate << "]";
        if (!a.concrete_witness.empty()) msg << " [Witness: " << a.concrete_witness << "]";
        llvm::json::Object msg_obj;
        msg_obj["text"] = msg.str();
        res_obj["message"] = std::move(msg_obj);

        llvm::json::Array locs;
        if (a.loc.is_valid()) {
            llvm::json::Object loc_obj;
            llvm::json::Object phys;
            llvm::json::Object art;
            art["uri"] = a.loc.file;
            phys["artifactLocation"] = std::move(art);
            llvm::json::Object reg;
            reg["startLine"] = static_cast<int64_t>(a.loc.line);
            if (a.loc.col_start > 0) reg["startColumn"] = static_cast<int64_t>(a.loc.col_start);
            if (a.loc.col_end >= a.loc.col_start && a.loc.col_end > 0) {
                reg["endColumn"] = static_cast<int64_t>(a.loc.col_end);
            }
            phys["region"] = std::move(reg);
            loc_obj["physicalLocation"] = std::move(phys);

            if (!a.function_name.empty()) {
                llvm::json::Array log_locs;
                llvm::json::Object ll;
                ll["name"] = a.function_name;
                ll["kind"] = "function";
                log_locs.push_back(std::move(ll));
                loc_obj["logicalLocations"] = std::move(log_locs);
            }
            locs.push_back(std::move(loc_obj));
        }
        res_obj["locations"] = std::move(locs);

        llvm::json::Object props;
        props["certainty"] = certainty_to_string(a.certainty);
        props["analysis"] = a.analysis_name;
        props["predicate"] = a.predicate;
        props["concrete_witness"] = a.concrete_witness;
        props["occurrences"] = static_cast<int64_t>(a.occurrences);
        llvm::json::Array cs;
        for (const auto& f : a.callstack) cs.push_back(f);
        props["callstack"] = std::move(cs);
        res_obj["properties"] = std::move(props);

        results_arr.push_back(std::move(res_obj));
    }

    llvm::json::Object driver;
    driver["name"] = "sqc";
    driver["version"] = "1.0.0";
    driver["informationUri"] = "https://sqc.local";
    driver["rules"] = std::move(rules_arr);

    llvm::json::Object tool;
    tool["driver"] = std::move(driver);

    llvm::json::Object run;
    run["tool"] = std::move(tool);
    run["results"] = std::move(results_arr);

    llvm::json::Array runs;
    runs.push_back(std::move(run));

    llvm::json::Object sarif;
    sarif["$schema"] = "https://raw.githubusercontent.com/oasis-tcs/sarif-spec/master/Schemata/sarif-schema-2.1.0.json";
    sarif["version"] = "2.1.0";
    sarif["runs"] = std::move(runs);

    std::filesystem::path out_path(output_sarif_path);
    if (out_path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(out_path.parent_path(), ec);
    }

    std::ofstream ofs(output_sarif_path, std::ios::out | std::ios::trunc);
    if (!ofs.is_open()) {
        err_msg = "Failed to open SARIF report path for writing: " + output_sarif_path;
        return false;
    }
    ofs << json_to_string(llvm::json::Value(std::move(sarif)), true) << "\n";
    ofs.flush();
    if (!ofs.good()) {
        err_msg = "Error writing SARIF report to: " + output_sarif_path;
        return false;
    }
    return true;
}

void ReportGenerator::print_cli_summary(
    const std::vector<AnalysisRunResult>& results,
    const std::string& html_report_path) {
    ConsolidatedData cd = consolidate_results(results);
    double overall_loc_pct = cd.total_exec_lines
                                 ? (100.0 * cd.total_covered_lines / cd.total_exec_lines)
                                 : 100.0;
    double cache_hit_pct = cd.total_equiv_queries
                               ? (100.0 * cd.total_equiv_hits / cd.total_equiv_queries)
                               : 0.0;

    std::cout << "\n================================================================================\n";
    std::cout << "  SQC Static Analysis Summary\n";
    std::cout << "================================================================================\n";
    std::cout << std::fixed << std::setprecision(1);
    std::cout << "  Analyses Run     : " << results.size() << " (" << cd.succeeded_units << " succeeded)\n";
    std::cout << "  Total Runtime    : " << cd.total_elapsed_ms << " ms\n";
    std::cout << "  Equiv Cache Hits : " << cd.total_equiv_hits << " / " << cd.total_equiv_queries
              << " (" << cache_hit_pct << "%, " << cd.total_remainder_slices << " remainder slices)\n";
    std::cout << "  Z3 SMT Queries   : " << cd.total_smt_queries
              << " (" << cd.total_smt_fp_eliminated << " false positives eliminated)\n";
    std::cout << "  Total Alarms     : " << cd.alarms.size()
              << " (" << cd.definite_bugs << " Definite Bugs, " << cd.potential_ubs << " Potential UBs)\n";
    std::cout << "  Project LoC Cov  : " << overall_loc_pct << "% ("
              << cd.total_covered_lines << " / " << cd.total_exec_lines << " executable lines)\n";

    std::cout << "\n--- Consolidated Alarms (" << cd.alarms.size() << ") ------------------------------------------------\n";
    if (cd.alarms.empty()) {
        std::cout << "  No undefined behaviors or hazards detected.\n";
    } else {
        for (size_t i = 0; i < cd.alarms.size(); ++i) {
            const auto& a = cd.alarms[i];
            std::cout << "  [" << (i + 1) << "] " << hazard_kind_to_string(a.kind)
                      << " (" << certainty_to_string(a.certainty) << ")\n"
                      << "      Location : " << a.loc.to_string()
                      << " in " << (a.function_name.empty() ? "<global>" : a.function_name) << "()\n";
            if (!a.predicate.empty()) {
                std::cout << "      Predicate: " << a.predicate << "\n";
            }
            if (!a.concrete_witness.empty()) {
                std::cout << "      Witness  : " << a.concrete_witness << "\n";
            }
        }
    }

    std::cout << "\n--- Per-File & Per-Function LoC Coverage ---------------------------------------\n";
    std::cout << std::left << std::setw(42) << "  File / Function"
              << std::right << std::setw(12) << "Covered/Exec"
              << std::setw(10) << "LoC %"
              << std::setw(10) << "Cond %"
              << std::setw(8) << "Alarms" << "\n";
    std::cout << "  " << std::string(78, '-') << "\n";

    for (const auto& fr : cd.file_reports) {
        std::string fname = std::filesystem::path(fr.file_path).filename().string();
        if (fname.size() > 38) fname = "..." + fname.substr(fname.size() - 35);
        std::ostringstream cov_ratio;
        cov_ratio << fr.total_covered_lines << "/" << fr.total_executable_lines;

        std::cout << "  " << std::left << std::setw(40) << fname
                  << std::right << std::setw(12) << cov_ratio.str()
                  << std::setw(9) << fr.file_loc_coverage_percent() << "%"
                  << std::setw(10) << "-"
                  << std::setw(8) << fr.total_alarms << "\n";

        for (const auto& fn : fr.functions) {
            std::string fn_label = "  |- " + fn.function_name + "()";
            if (fn_label.size() > 38) fn_label = fn_label.substr(0, 35) + "...";
            std::ostringstream fn_ratio;
            fn_ratio << fn.covered_lines_count << "/" << fn.executable_lines_count;
            std::ostringstream cond_str;
            if (fn.total_sub_conditions > 0) {
                cond_str << std::fixed << std::setprecision(1) << fn.condition_coverage_percent() << "%";
            } else {
                cond_str << "N/A";
            }
            std::cout << "  " << std::left << std::setw(40) << fn_label
                      << std::right << std::setw(12) << fn_ratio.str()
                      << std::setw(9) << fn.loc_coverage_percent() << "%"
                      << std::setw(10) << cond_str.str()
                      << std::setw(8) << fn.alarms_count << "\n";
        }
    }

    if (!html_report_path.empty()) {
        std::cout << "\n  Interactive HTML Report: " << html_report_path << "\n";
    }
    std::cout << "================================================================================\n\n";
}

bool ReportGenerator::save_run_result(const std::string& cache_dir, const AnalysisRunResult& res) {
    std::error_code ec;
    std::filesystem::create_directories(cache_dir, ec);
    if (ec) return false;

    std::string filename = "run_" + std::to_string(res.unit_id) + "_" + sanitize_filename(res.unit_name) + ".json";
    std::filesystem::path out_file = std::filesystem::path(cache_dir) / filename;

    llvm::json::Object obj;
    obj["unit_id"] = static_cast<int64_t>(res.unit_id);
    obj["unit_name"] = res.unit_name;
    obj["success"] = res.success;
    obj["error_message"] = res.error_message;
    obj["elapsed_ms"] = res.elapsed_ms;
    obj["instructions_analyzed"] = static_cast<int64_t>(res.instructions_analyzed);
    obj["equivalence_queries"] = static_cast<int64_t>(res.equivalence_queries);
    obj["equivalence_cache_hits"] = static_cast<int64_t>(res.equivalence_cache_hits);
    obj["remainder_slices"] = static_cast<int64_t>(res.remainder_slices);
    obj["smt_queries"] = static_cast<int64_t>(res.smt_queries);
    obj["smt_false_positives_eliminated"] = static_cast<int64_t>(res.smt_false_positives_eliminated);

    llvm::json::Array alarms_arr;
    for (const auto& a : res.alarms) {
        alarms_arr.push_back(serialize_hazard(a));
    }
    obj["alarms"] = std::move(alarms_arr);

    llvm::json::Array inputs_arr;
    for (const auto& inp : res.input_specs) {
        llvm::json::Object io;
        io["kind"] = inp.kind;
        io["target"] = inp.target;
        io["range_desc"] = inp.range_desc;
        io["function_name"] = inp.function_name;
        io["file"] = inp.loc.file;
        io["line"] = static_cast<int64_t>(inp.loc.line);
        inputs_arr.push_back(std::move(io));
    }
    obj["input_specs"] = std::move(inputs_arr);

    llvm::json::Array cov_arr;
    for (const auto& [file, line] : res.coverage.covered_lines_set()) {
        llvm::json::Array pair;
        pair.push_back(file);
        pair.push_back(static_cast<int64_t>(line));
        cov_arr.push_back(std::move(pair));
    }
    obj["covered_lines"] = std::move(cov_arr);

    llvm::json::Object sc_obj;
    for (const auto& [key, mask] : res.coverage.subcond_masks_map()) {
        sc_obj[key] = static_cast<int64_t>(mask);
    }
    obj["subcond_masks"] = std::move(sc_obj);

    llvm::json::Array dead_arr;
    for (const auto& fr : res.coverage.build_reports()) {
        for (uint32_t L = 1; L <= fr.total_lines && L < fr.line_statuses.size(); ++L) {
            if (fr.line_statuses[L] == LineStatus::DeadAfterUB) {
                llvm::json::Array pair;
                pair.push_back(fr.file_path);
                pair.push_back(static_cast<int64_t>(L));
                dead_arr.push_back(std::move(pair));
            }
        }
    }
    obj["dead_after_ub_lines"] = std::move(dead_arr);

    std::ofstream ofs(out_file, std::ios::out | std::ios::trunc);
    if (!ofs.is_open()) return false;
    ofs << json_to_string(llvm::json::Value(std::move(obj)), true) << "\n";
    ofs.flush();
    return ofs.good();
}

std::vector<AnalysisRunResult> ReportGenerator::load_all_run_results(const std::string& cache_dir) {
    std::vector<AnalysisRunResult> results;
    std::error_code ec;
    if (!std::filesystem::exists(cache_dir, ec) || !std::filesystem::is_directory(cache_dir, ec)) {
        return results;
    }

    std::vector<std::filesystem::path> json_files;
    for (const auto& entry : std::filesystem::directory_iterator(cache_dir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            json_files.push_back(entry.path());
        }
    }
    std::sort(json_files.begin(), json_files.end());

    for (const auto& path : json_files) {
        std::ifstream ifs(path);
        if (!ifs.is_open()) continue;
        std::ostringstream ss;
        ss << ifs.rdbuf();

        auto parsed = llvm::json::parse(ss.str());
        if (!parsed) {
            llvm::consumeError(parsed.takeError());
            continue;
        }
        const auto* obj = parsed->getAsObject();
        if (!obj) continue;

        AnalysisRunResult res;
        if (auto v = obj->getInteger("unit_id")) res.unit_id = static_cast<uint32_t>(*v);
        if (auto v = obj->getString("unit_name")) res.unit_name = v->str();
        if (auto v = obj->getBoolean("success")) res.success = *v;
        if (auto v = obj->getString("error_message")) res.error_message = v->str();
        if (auto v = obj->getNumber("elapsed_ms")) res.elapsed_ms = *v;
        if (auto v = obj->getInteger("instructions_analyzed")) res.instructions_analyzed = static_cast<uint64_t>(*v);
        if (auto v = obj->getInteger("equivalence_queries")) res.equivalence_queries = static_cast<uint64_t>(*v);
        if (auto v = obj->getInteger("equivalence_cache_hits")) res.equivalence_cache_hits = static_cast<uint64_t>(*v);
        if (auto v = obj->getInteger("remainder_slices")) res.remainder_slices = static_cast<uint64_t>(*v);
        if (auto v = obj->getInteger("smt_queries")) res.smt_queries = static_cast<uint64_t>(*v);
        if (auto v = obj->getInteger("smt_false_positives_eliminated")) {
            res.smt_false_positives_eliminated = static_cast<uint64_t>(*v);
        }

        if (const auto* alarms_arr = obj->getArray("alarms")) {
            for (const auto& item : *alarms_arr) {
                if (const auto* aobj = item.getAsObject()) {
                    HazardReport h = deserialize_hazard(*aobj);
                    res.coverage.record_alarm(h);
                    res.alarms.push_back(std::move(h));
                }
            }
        }

        if (const auto* in_arr = obj->getArray("input_specs")) {
            for (const auto& item : *in_arr) {
                if (const auto* iobj = item.getAsObject()) {
                    SymbolicInputSpec inp;
                    if (auto v = iobj->getString("kind")) inp.kind = v->str();
                    if (auto v = iobj->getString("target")) inp.target = v->str();
                    if (auto v = iobj->getString("range_desc")) inp.range_desc = v->str();
                    if (auto v = iobj->getString("function_name")) inp.function_name = v->str();
                    if (auto v = iobj->getString("file")) inp.loc.file = v->str();
                    if (auto v = iobj->getInteger("line")) inp.loc.line = static_cast<uint32_t>(*v);
                    res.input_specs.push_back(std::move(inp));
                }
            }
        }

        std::set<std::pair<std::string, uint32_t>> cov_lines;
        if (const auto* cov_arr = obj->getArray("covered_lines")) {
            for (const auto& item : *cov_arr) {
                if (const auto* pair = item.getAsArray(); pair && pair->size() == 2) {
                    auto f = (*pair)[0].getAsString();
                    auto l = (*pair)[1].getAsInteger();
                    if (f && l && *l > 0) {
                        cov_lines.emplace(f->str(), static_cast<uint32_t>(*l));
                    }
                }
            }
        }

        std::unordered_map<std::string, uint8_t> sc_masks;
        if (const auto* sc_obj = obj->getObject("subcond_masks")) {
            for (const auto& kv : *sc_obj) {
                if (auto m = kv.second.getAsInteger()) {
                    sc_masks[kv.first.str()] = static_cast<uint8_t>(*m);
                }
            }
        }
        res.coverage.merge_cached_coverage(cov_lines, sc_masks);

        if (const auto* dead_arr = obj->getArray("dead_after_ub_lines")) {
            for (const auto& item : *dead_arr) {
                if (const auto* pair = item.getAsArray(); pair && pair->size() == 2) {
                    auto f = (*pair)[0].getAsString();
                    auto l = (*pair)[1].getAsInteger();
                    if (f && l && *l > 0) {
                        res.coverage.mark_line_dead_after_ub(f->str(), static_cast<uint32_t>(*l));
                    }
                }
            }
        }

        results.push_back(std::move(res));
    }

    std::sort(results.begin(), results.end(),
              [](const AnalysisRunResult& a, const AnalysisRunResult& b) {
                  return a.unit_id < b.unit_id;
              });
    return results;
}

} // namespace sqc
