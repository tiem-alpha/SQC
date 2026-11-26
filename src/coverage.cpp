#include "sqc/coverage.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/DebugLoc.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Module.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <regex>
#include <sstream>

namespace sqc {

namespace {

std::string normalize_path(const std::string& raw_path) {
    if (raw_path.empty()) return {};
    std::filesystem::path p(raw_path);
    std::error_code ec;
    auto canon = std::filesystem::weakly_canonical(p, ec);
    if (!ec && !canon.empty()) {
        return canon.lexically_normal().string();
    }
    return p.lexically_normal().string();
}

std::string resolve_di_file(const llvm::DIFile* df) {
    if (!df) return {};
    std::string dir = df->getDirectory().str();
    std::string fname = df->getFilename().str();
    if (fname.empty()) return {};
    std::filesystem::path p(fname);
    if (p.is_relative() && !dir.empty()) {
        p = std::filesystem::path(dir) / p;
    }
    return normalize_path(p.string());
}

bool should_skip_file(const std::string& norm_path) {
    if (norm_path.empty()) return true;
    if (norm_path[0] == '<') return true;
    if (norm_path.rfind("/usr/include/", 0) == 0) return true;
    if (norm_path.rfind("/usr/lib/", 0) == 0) return true;
    if (norm_path.rfind("/usr/local/include/", 0) == 0) return true;
    std::string base = std::filesystem::path(norm_path).filename().string();
    if (base == "sqc_builtin.h") return true;
    return false;
}

bool parse_subcond_key(const std::string& key,
                       std::string& out_file,
                       uint32_t& out_line,
                       uint16_t& out_col) {
    auto last_colon = key.rfind(':');
    if (last_colon == std::string::npos || last_colon == 0) return false;
    auto prev_colon = key.rfind(':', last_colon - 1);
    if (prev_colon == std::string::npos || prev_colon == 0) return false;

    out_file = key.substr(0, prev_colon);
    try {
        unsigned long line_val = std::stoul(key.substr(prev_colon + 1, last_colon - prev_colon - 1));
        unsigned long col_val = std::stoul(key.substr(last_colon + 1));
        if (line_val == 0) return false;
        out_line = static_cast<uint32_t>(line_val);
        out_col = static_cast<uint16_t>(std::min<unsigned long>(col_val, 65535UL));
        return true;
    } catch (...) {
        return false;
    }
}

std::string trim_copy(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(start, end - start);
}

// Replace string/char literals and comments with spaces (preserving 1-to-1 column indices)
std::string mask_comments_and_literals(const std::string& line) {
    std::string out = line;
    bool in_str = false;
    bool in_chr = false;
    bool in_block = false;
    for (size_t i = 0; i < out.size(); ++i) {
        if (in_block) {
            if (out[i] == '*' && i + 1 < out.size() && out[i + 1] == '/') {
                out[i] = ' ';
                out[i + 1] = ' ';
                ++i;
                in_block = false;
            } else {
                out[i] = ' ';
            }
            continue;
        }
        if (in_str) {
            if (out[i] == '\\' && i + 1 < out.size()) {
                out[i] = ' ';
                out[i + 1] = ' ';
                ++i;
            } else if (out[i] == '"') {
                out[i] = ' ';
                in_str = false;
            } else {
                out[i] = ' ';
            }
            continue;
        }
        if (in_chr) {
            if (out[i] == '\\' && i + 1 < out.size()) {
                out[i] = ' ';
                out[i + 1] = ' ';
                ++i;
            } else if (out[i] == '\'') {
                out[i] = ' ';
                in_chr = false;
            } else {
                out[i] = ' ';
            }
            continue;
        }
        if (out[i] == '/' && i + 1 < out.size() && out[i + 1] == '/') {
            for (size_t j = i; j < out.size(); ++j) out[j] = ' ';
            break;
        }
        if (out[i] == '/' && i + 1 < out.size() && out[i + 1] == '*') {
            out[i] = ' ';
            out[i + 1] = ' ';
            ++i;
            in_block = true;
            continue;
        }
        if (out[i] == '"') {
            out[i] = ' ';
            in_str = true;
            continue;
        }
        if (out[i] == '\'') {
            out[i] = ' ';
            in_chr = true;
            continue;
        }
    }
    return out;
}

bool is_ident_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

struct ParsedSubCond {
    uint16_t col_start = 0; // 1-based column
    uint16_t col_end = 0;   // 1-based inclusive end column
    std::string expr_text;
};

// Recursively split a condition range [start, end) (0-based indices into raw/masked) on top-level && and ||
void split_logical_subconditions(const std::string& raw,
                                 const std::string& masked,
                                 size_t start,
                                 size_t end,
                                 std::vector<ParsedSubCond>& out) {
    while (start < end && std::isspace(static_cast<unsigned char>(raw[start]))) ++start;
    while (end > start && std::isspace(static_cast<unsigned char>(raw[end - 1]))) --end;
    if (start >= end) return;

    // Unwrap redundant matching outer parentheses
    while (start + 1 < end && masked[start] == '(' && masked[end - 1] == ')') {
        int depth = 0;
        bool wraps_all = true;
        for (size_t i = start; i < end; ++i) {
            if (masked[i] == '(') ++depth;
            else if (masked[i] == ')') --depth;
            if (depth == 0 && i + 1 < end) {
                wraps_all = false;
                break;
            }
        }
        if (wraps_all) {
            ++start;
            --end;
            while (start < end && std::isspace(static_cast<unsigned char>(raw[start]))) ++start;
            while (end > start && std::isspace(static_cast<unsigned char>(raw[end - 1]))) --end;
        } else {
            break;
        }
    }
    if (start >= end) return;

    int depth = 0;
    std::vector<std::pair<size_t, size_t>> splits;
    size_t seg_start = start;
    for (size_t i = start; i + 1 < end; ++i) {
        if (masked[i] == '(' || masked[i] == '[' || masked[i] == '{') {
            ++depth;
        } else if (masked[i] == ')' || masked[i] == ']' || masked[i] == '}') {
            if (depth > 0) --depth;
        } else if (depth == 0 &&
                   ((masked[i] == '&' && masked[i + 1] == '&') ||
                    (masked[i] == '|' && masked[i + 1] == '|'))) {
            splits.emplace_back(seg_start, i);
            seg_start = i + 2;
            ++i;
        }
    }

    if (!splits.empty()) {
        splits.emplace_back(seg_start, end);
        for (const auto& [s, e] : splits) {
            split_logical_subconditions(raw, masked, s, e, out);
        }
        return;
    }

    ParsedSubCond psc;
    psc.col_start = static_cast<uint16_t>(std::min<size_t>(start + 1, 65535));
    psc.col_end = static_cast<uint16_t>(std::min<size_t>(end, 65535));
    psc.expr_text = trim_copy(raw.substr(start, end - start));
    if (!psc.expr_text.empty()) {
        out.push_back(std::move(psc));
    }
}

// Find condition expression inside if (...), while (...), for (...; cond; ...), or ternary ?
std::vector<ParsedSubCond> extract_control_subconditions(const std::string& raw_line) {
    std::vector<ParsedSubCond> result;
    std::string masked = mask_comments_and_literals(raw_line);

    auto find_keyword_cond = [&](const std::string& kw, bool is_for) -> bool {
        size_t pos = 0;
        while ((pos = masked.find(kw, pos)) != std::string::npos) {
            bool left_ok = (pos == 0 || !is_ident_char(masked[pos - 1]));
            size_t after = pos + kw.size();
            bool right_ok = (after >= masked.size() || !is_ident_char(masked[after]));
            if (!left_ok || !right_ok) {
                pos = after;
                continue;
            }
            size_t p = after;
            while (p < masked.size() && std::isspace(static_cast<unsigned char>(masked[p]))) ++p;
            if (p < masked.size() && masked[p] == '(') {
                int depth = 0;
                size_t close_pos = std::string::npos;
                for (size_t i = p; i < masked.size(); ++i) {
                    if (masked[i] == '(') ++depth;
                    else if (masked[i] == ')') {
                        --depth;
                        if (depth == 0) {
                            close_pos = i;
                            break;
                        }
                    }
                }
                size_t cond_start = p + 1;
                size_t cond_end = (close_pos != std::string::npos) ? close_pos : masked.size();
                if (is_for) {
                    // Extract middle clause between first ';' and second ';' at depth 0
                    int inner_depth = 0;
                    size_t semi1 = std::string::npos;
                    size_t semi2 = std::string::npos;
                    for (size_t i = cond_start; i < cond_end; ++i) {
                        if (masked[i] == '(') ++inner_depth;
                        else if (masked[i] == ')') { if (inner_depth > 0) --inner_depth; }
                        else if (inner_depth == 0 && masked[i] == ';') {
                            if (semi1 == std::string::npos) semi1 = i;
                            else if (semi2 == std::string::npos) { semi2 = i; break; }
                        }
                    }
                    if (semi1 != std::string::npos) {
                        cond_start = semi1 + 1;
                        if (semi2 != std::string::npos) cond_end = semi2;
                    }
                }
                split_logical_subconditions(raw_line, masked, cond_start, cond_end, result);
                return !result.empty();
            }
            pos = after;
        }
        return false;
    };

    if (find_keyword_cond("if", false)) return result;
    if (find_keyword_cond("while", false)) return result;
    if (find_keyword_cond("for", true)) return result;

    // Check ternary operator '?'
    size_t qpos = masked.find('?');
    if (qpos != std::string::npos) {
        // Find start of ternary condition (after '=', 'return', '(', ',', or start of line)
        size_t cond_start = 0;
        for (size_t i = qpos; i > 0; --i) {
            char c = masked[i - 1];
            if (c == '=' || c == ',' || c == ';') {
                // Avoid splitting on ==, !=, <=, >=
                if (c == '=' && i >= 2 &&
                    (masked[i - 2] == '=' || masked[i - 2] == '!' ||
                     masked[i - 2] == '<' || masked[i - 2] == '>')) {
                    continue;
                }
                cond_start = i;
                break;
            }
        }
        split_logical_subconditions(raw_line, masked, cond_start, qpos, result);
    }

    return result;
}

std::string alarm_dedup_key(const HazardReport& a) {
    std::ostringstream oss;
    oss << static_cast<int>(a.kind) << "|"
        << a.loc.file << ":" << a.loc.line << ":" << a.loc.col_start << "|"
        << a.function_name << "|" << a.predicate;
    return oss.str();
}

} // namespace

void CoverageTracker::initialize_from_module(
    const llvm::Module& mod,
    const std::vector<std::string>& skip_function_regexes) {
    std::vector<std::regex> skip_res;
    skip_res.reserve(skip_function_regexes.size());
    for (const auto& pat : skip_function_regexes) {
        if (pat.empty()) continue;
        try {
            skip_res.emplace_back(pat);
        } catch (const std::regex_error&) {
            // Ignore invalid regex patterns
        }
    }

    for (const llvm::Function& fn : mod) {
        if (fn.isDeclaration()) continue;

        std::string fname = fn.getName().str();
        if (fname.rfind("__sqc_", 0) == 0) continue;

        bool skipped = false;
        for (const auto& re : skip_res) {
            if (std::regex_search(fname, re)) {
                skipped = true;
                break;
            }
        }
        if (skipped) continue;

        const llvm::DISubprogram* sp = fn.getSubprogram();
        if (!sp) continue;

        std::string file_path = resolve_di_file(sp->getFile());
        if (should_skip_file(file_path)) continue;

        RegisteredFunc rf;
        rf.name = fname;
        rf.file = file_path;
        rf.start_line = sp->getLine();
        rf.end_line = rf.start_line;

        for (const llvm::BasicBlock& bb : fn) {
            for (const llvm::Instruction& inst : bb) {
                const llvm::DebugLoc& dl = inst.getDebugLoc();
                if (!dl) continue;
                uint32_t line = dl.getLine();
                if (line == 0) continue;

                std::string inst_file = file_path;
                if (const llvm::DILocation* diloc = dl.get()) {
                    std::string resolved = resolve_di_file(diloc->getFile());
                    if (!resolved.empty()) inst_file = resolved;
                }

                if (inst_file != file_path) {
                    if (!should_skip_file(inst_file)) {
                        file_exec_lines_[inst_file].insert(line);
                    }
                    continue;
                }

                rf.exec_lines.insert(line);
                if (line > rf.end_line) rf.end_line = line;
                file_exec_lines_[file_path].insert(line);
            }
        }

        auto& funcs = file_functions_[file_path];
        bool merged = false;
        for (auto& existing : funcs) {
            if (existing.name == rf.name && existing.start_line == rf.start_line) {
                existing.exec_lines.insert(rf.exec_lines.begin(), rf.exec_lines.end());
                if (rf.end_line > existing.end_line) existing.end_line = rf.end_line;
                merged = true;
                break;
            }
        }
        if (!merged) {
            funcs.push_back(std::move(rf));
        }
    }
}

void CoverageTracker::mark_line_covered(const std::string& file, uint32_t line) {
    if (file.empty() || line == 0) return;
    std::string norm = normalize_path(file);
    if (should_skip_file(norm)) return;
    covered_lines_.emplace(norm, line);
    file_exec_lines_[norm].insert(line);
}

void CoverageTracker::mark_line_dead_after_ub(const std::string& file, uint32_t line) {
    if (file.empty() || line == 0) return;
    std::string norm = normalize_path(file);
    if (should_skip_file(norm)) return;
    dead_after_ub_lines_.emplace(norm, line);
    file_exec_lines_[norm].insert(line);
}

void CoverageTracker::record_branch_outcome(const SourceLoc& loc, bool taken_true, bool taken_false) {
    if (!loc.is_valid()) return;
    std::string norm = normalize_path(loc.file);
    if (should_skip_file(norm)) return;

    uint8_t mask = 0;
    if (taken_true) mask |= 0x1;
    if (taken_false) mask |= 0x2;

    std::string key = norm + ":" + std::to_string(loc.line) + ":" + std::to_string(loc.col_start);
    subcond_masks_[key] |= mask;
    covered_lines_.emplace(norm, loc.line);
    file_exec_lines_[norm].insert(loc.line);
}

void CoverageTracker::record_alarm(const HazardReport& alarm) {
    HazardReport copy = alarm;
    if (copy.loc.is_valid()) {
        copy.loc.file = normalize_path(copy.loc.file);
        if (!should_skip_file(copy.loc.file)) {
            covered_lines_.emplace(copy.loc.file, copy.loc.line);
            file_exec_lines_[copy.loc.file].insert(copy.loc.line);
        }
    }
    std::string key = alarm_dedup_key(copy);
    for (auto& existing : alarms_) {
        if (alarm_dedup_key(existing) == key) {
            existing.occurrences += copy.occurrences;
            return;
        }
    }
    alarms_.push_back(std::move(copy));
}

void CoverageTracker::merge_cached_coverage(
    const std::set<std::pair<std::string, uint32_t>>& covered_lines,
    const std::unordered_map<std::string, uint8_t>& subcond_masks) {
    for (const auto& [file, line] : covered_lines) {
        if (file.empty() || line == 0) continue;
        std::string norm = normalize_path(file);
        if (should_skip_file(norm)) continue;
        covered_lines_.emplace(norm, line);
        file_exec_lines_[norm].insert(line);
    }
    for (const auto& [raw_key, mask] : subcond_masks) {
        std::string file;
        uint32_t line = 0;
        uint16_t col = 0;
        if (parse_subcond_key(raw_key, file, line, col)) {
            std::string norm = normalize_path(file);
            if (should_skip_file(norm)) continue;
            std::string norm_key = norm + ":" + std::to_string(line) + ":" + std::to_string(col);
            subcond_masks_[norm_key] |= mask;
            file_exec_lines_[norm].insert(line);
        } else {
            subcond_masks_[raw_key] |= mask;
        }
    }
}

void CoverageTracker::merge_from(const CoverageTracker& other) {
    for (const auto& [file, lines] : other.file_exec_lines_) {
        file_exec_lines_[file].insert(lines.begin(), lines.end());
    }
    for (const auto& [file, funcs] : other.file_functions_) {
        auto& dst = file_functions_[file];
        for (const auto& rf : funcs) {
            bool merged = false;
            for (auto& existing : dst) {
                if (existing.name == rf.name && existing.start_line == rf.start_line) {
                    existing.exec_lines.insert(rf.exec_lines.begin(), rf.exec_lines.end());
                    if (rf.end_line > existing.end_line) existing.end_line = rf.end_line;
                    merged = true;
                    break;
                }
            }
            if (!merged) {
                dst.push_back(rf);
            }
        }
    }
    covered_lines_.insert(other.covered_lines_.begin(), other.covered_lines_.end());
    dead_after_ub_lines_.insert(other.dead_after_ub_lines_.begin(), other.dead_after_ub_lines_.end());
    for (const auto& [k, mask] : other.subcond_masks_) {
        subcond_masks_[k] |= mask;
    }
    for (const auto& alarm : other.alarms_) {
        record_alarm(alarm);
    }
}

std::vector<FileCoverageReport> CoverageTracker::build_reports() const {
    // Collect all tracked files
    std::set<std::string> all_files;
    for (const auto& [file, _] : file_exec_lines_) all_files.insert(file);
    for (const auto& [file, _] : file_functions_) all_files.insert(file);
    for (const auto& [file, _] : covered_lines_) all_files.insert(file);
    for (const auto& [file, _] : dead_after_ub_lines_) all_files.insert(file);
    for (const auto& alarm : alarms_) {
        if (alarm.loc.is_valid() && !should_skip_file(alarm.loc.file)) {
            all_files.insert(alarm.loc.file);
        }
    }

    // Group subcond_masks_ by (file, line) -> vector<(col, mask)>
    std::map<std::string, std::map<uint32_t, std::vector<std::pair<uint16_t, uint8_t>>>> file_line_branches;
    for (const auto& [key, mask] : subcond_masks_) {
        std::string file;
        uint32_t line = 0;
        uint16_t col = 0;
        if (parse_subcond_key(key, file, line, col)) {
            std::string norm = normalize_path(file);
            if (!should_skip_file(norm)) {
                file_line_branches[norm][line].emplace_back(col, mask);
                all_files.insert(norm);
            }
        }
    }

    // Group alarms by (file, line)
    std::map<std::string, std::map<uint32_t, std::vector<HazardReport>>> file_line_alarms;
    for (const auto& alarm : alarms_) {
        if (alarm.loc.is_valid() && !should_skip_file(alarm.loc.file)) {
            file_line_alarms[alarm.loc.file][alarm.loc.line].push_back(alarm);
        }
    }

    std::vector<FileCoverageReport> reports;
    reports.reserve(all_files.size());

    for (const auto& file : all_files) {
        if (should_skip_file(file)) continue;

        FileCoverageReport rep;
        rep.file_path = file;

        // 1. Read actual source lines from disk
        {
            std::ifstream ifs(file);
            if (ifs.is_open()) {
                std::string line_str;
                while (std::getline(ifs, line_str)) {
                    if (!line_str.empty() && line_str.back() == '\r') {
                        line_str.pop_back();
                    }
                    rep.source_lines.push_back(std::move(line_str));
                }
            }
        }

        // Determine max line number across source and tracked metadata
        uint32_t max_line = static_cast<uint32_t>(rep.source_lines.size());
        auto exec_it = file_exec_lines_.find(file);
        if (exec_it != file_exec_lines_.end() && !exec_it->second.empty()) {
            max_line = std::max(max_line, *exec_it->second.rbegin());
        }
        auto alarm_file_it = file_line_alarms.find(file);
        if (alarm_file_it != file_line_alarms.end() && !alarm_file_it->second.empty()) {
            max_line = std::max(max_line, alarm_file_it->second.rbegin()->first);
        }
        if (rep.source_lines.size() < max_line) {
            rep.source_lines.resize(max_line);
        }

        rep.total_lines = max_line;
        // 2. Initialize 1-indexed line_statuses
        rep.line_statuses.assign(static_cast<size_t>(rep.total_lines) + 1, LineStatus::NonExecutable);

        // 3. Assign base executable line statuses
        std::set<uint32_t> exec_lines;
        if (exec_it != file_exec_lines_.end()) {
            exec_lines = exec_it->second;
        }
        for (uint32_t L : exec_lines) {
            if (L == 0 || L > rep.total_lines) continue;
            if (covered_lines_.count({file, L}) > 0) {
                rep.line_statuses[L] = LineStatus::CoveredSafe;
            } else if (dead_after_ub_lines_.count({file, L}) > 0) {
                rep.line_statuses[L] = LineStatus::DeadAfterUB;
            } else {
                rep.line_statuses[L] = LineStatus::Uncovered;
            }
        }

        // 4. Process sub-conditions on control lines
        auto br_file_it = file_line_branches.find(file);
        for (uint32_t L : exec_lines) {
            if (L == 0 || L > rep.total_lines) continue;
            if (rep.line_statuses[L] != LineStatus::CoveredSafe) continue;

            const std::string& src_line = rep.source_lines[L - 1];
            std::vector<ParsedSubCond> parsed = extract_control_subconditions(src_line);

            std::vector<std::pair<uint16_t, uint8_t>> recorded;
            if (br_file_it != file_line_branches.end()) {
                auto br_line_it = br_file_it->second.find(L);
                if (br_line_it != br_file_it->second.end()) {
                    recorded = br_line_it->second;
                    std::sort(recorded.begin(), recorded.end(),
                              [](const auto& a, const auto& b) { return a.first < b.first; });
                }
            }

            if (recorded.empty() && parsed.size() < 2) {
                continue;
            }

            std::vector<SubConditionCoverage> subconds;
            if (parsed.size() >= 2) {
                // Compound condition (&& / ||)
                subconds.resize(parsed.size());
                std::vector<bool> matched(parsed.size(), false);
                for (size_t i = 0; i < parsed.size(); ++i) {
                    subconds[i].file = file;
                    subconds[i].line = L;
                    subconds[i].col_start = parsed[i].col_start;
                    subconds[i].col_end = parsed[i].col_end;
                    subconds[i].expr_text = parsed[i].expr_text;
                }

                for (const auto& [col, mask] : recorded) {
                    size_t best_idx = 0;
                    if (col == 0) {
                        // Assign to first unmatched sub-condition
                        bool found = false;
                        for (size_t i = 0; i < parsed.size(); ++i) {
                            if (!matched[i]) {
                                best_idx = i;
                                found = true;
                                break;
                            }
                        }
                        if (!found) best_idx = 0;
                    } else {
                        // Prefer direct containment [col_start - 1, col_end + 1]
                        bool contained = false;
                        for (size_t i = 0; i < parsed.size(); ++i) {
                            uint16_t lo = (parsed[i].col_start > 1) ? static_cast<uint16_t>(parsed[i].col_start - 1) : 1;
                            uint16_t hi = static_cast<uint16_t>(parsed[i].col_end + 1);
                            if (col >= lo && col <= hi) {
                                best_idx = i;
                                contained = true;
                                break;
                            }
                        }
                        if (!contained) {
                            // Nearest sub-condition center, with a slight preference for unmatched
                            int best_dist = std::numeric_limits<int>::max();
                            for (size_t i = 0; i < parsed.size(); ++i) {
                                int dist = 0;
                                if (col < parsed[i].col_start) {
                                    dist = static_cast<int>(parsed[i].col_start) - static_cast<int>(col);
                                } else if (col > parsed[i].col_end) {
                                    dist = static_cast<int>(col) - static_cast<int>(parsed[i].col_end);
                                }
                                if (matched[i]) dist += 3;
                                if (dist < best_dist) {
                                    best_dist = dist;
                                    best_idx = i;
                                }
                            }
                        }
                    }
                    matched[best_idx] = true;
                    if (mask & 0x1) subconds[best_idx].seen_true = true;
                    if (mask & 0x2) subconds[best_idx].seen_false = true;
                }
                auto is_unary_negated_expr = [](std::string s) -> bool {
                    s = trim_copy(s);
                    while (s.size() >= 2 && s.front() == '(' && s.back() == ')') {
                        int depth = 0;
                        bool wraps = true;
                        for (size_t i = 0; i + 1 < s.size(); ++i) {
                            if (s[i] == '(') ++depth;
                            else if (s[i] == ')') --depth;
                            if (depth == 0) { wraps = false; break; }
                        }
                        if (!wraps) break;
                        s = trim_copy(s.substr(1, s.size() - 2));
                    }
                    if (s.empty() || s[0] != '!' || (s.size() > 1 && s[1] == '=')) {
                        return false;
                    }
                    int depth = 0;
                    for (size_t i = 1; i < s.size(); ++i) {
                        char c = s[i];
                        if (c == '(' || c == '[' || c == '{') { ++depth; continue; }
                        if (c == ')' || c == ']' || c == '}') { if (depth > 0) --depth; continue; }
                        if (depth == 0) {
                            char next = (i + 1 < s.size()) ? s[i + 1] : '\0';
                            if ((c == '=' && next == '=') || (c == '!' && next == '=') ||
                                c == '<' || c == '>') {
                                return false;
                            }
                        }
                    }
                    return true;
                };
                for (auto& sc : subconds) {
                    if (is_unary_negated_expr(sc.expr_text)) {
                        std::swap(sc.seen_true, sc.seen_false);
                    }
                }
            } else if (parsed.size() == 1 && !recorded.empty()) {
                SubConditionCoverage sc;
                sc.file = file;
                sc.line = L;
                sc.col_start = parsed[0].col_start;
                sc.col_end = parsed[0].col_end;
                sc.expr_text = parsed[0].expr_text;
                uint8_t combined_mask = 0;
                for (const auto& [col, mask] : recorded) {
                    combined_mask |= mask;
                }
                sc.seen_true = (combined_mask & 0x1) != 0;
                sc.seen_false = (combined_mask & 0x2) != 0;
                {
                    std::string s = trim_copy(sc.expr_text);
                    if (!s.empty() && s[0] == '!' && (s.size() == 1 || s[1] != '=')) {
                        int depth = 0;
                        bool has_top_cmp = false;
                        for (size_t i = 1; i < s.size(); ++i) {
                            char c = s[i];
                            if (c == '(' || c == '[' || c == '{') { ++depth; continue; }
                            if (c == ')' || c == ']' || c == '}') { if (depth > 0) --depth; continue; }
                            if (depth == 0) {
                                char next = (i + 1 < s.size()) ? s[i + 1] : '\0';
                                if ((c == '=' && next == '=') || (c == '!' && next == '=') ||
                                    c == '<' || c == '>') {
                                    has_top_cmp = true;
                                    break;
                                }
                            }
                        }
                        if (!has_top_cmp) {
                            std::swap(sc.seen_true, sc.seen_false);
                        }
                    }
                }
                subconds.push_back(std::move(sc));
            } else if (!recorded.empty()) {
                // Multi-line or unrecognized branch syntax: use recorded column entries directly
                std::string fallback_expr = trim_copy(src_line);
                for (const auto& [col, mask] : recorded) {
                    SubConditionCoverage sc;
                    sc.file = file;
                    sc.line = L;
                    sc.col_start = col;
                    sc.col_end = col;
                    sc.expr_text = fallback_expr.empty()
                                       ? ("branch@" + std::to_string(L) + ":" + std::to_string(col))
                                       : fallback_expr;
                    sc.seen_true = (mask & 0x1) != 0;
                    sc.seen_false = (mask & 0x2) != 0;
                    subconds.push_back(std::move(sc));
                }
            }

            if (!subconds.empty()) {
                bool any_partial = false;
                for (const auto& sc : subconds) {
                    if (!sc.is_fully_covered()) {
                        any_partial = true;
                        break;
                    }
                }
                rep.line_sub_conditions[L] = std::move(subconds);
                if (any_partial && rep.line_statuses[L] == LineStatus::CoveredSafe) {
                    rep.line_statuses[L] = LineStatus::CoveredPartial;
                }
            }
        }

        // 5. Overlay alarms
        if (alarm_file_it != file_line_alarms.end()) {
            for (const auto& [L, line_alarms] : alarm_file_it->second) {
                if (L == 0 || L > rep.total_lines) continue;
                rep.line_alarms[L] = line_alarms;
                rep.line_statuses[L] = LineStatus::CoveredAlarm;
                rep.total_alarms += static_cast<uint32_t>(line_alarms.size());
            }
        }

        // 6. Compute per-function metrics
        auto fn_file_it = file_functions_.find(file);
        if (fn_file_it != file_functions_.end()) {
            for (const auto& rf : fn_file_it->second) {
                FunctionCoverageMetrics fm;
                fm.function_name = rf.name;
                fm.file_path = file;
                fm.start_line = rf.start_line;
                fm.end_line = rf.end_line;
                fm.executable_lines_count = static_cast<uint32_t>(rf.exec_lines.size());

                for (uint32_t L : rf.exec_lines) {
                    if (L == 0 || L > rep.total_lines) continue;
                    LineStatus st = rep.line_statuses[L];
                    if (st == LineStatus::CoveredSafe ||
                        st == LineStatus::CoveredPartial ||
                        st == LineStatus::CoveredAlarm) {
                        ++fm.covered_lines_count;
                        fm.was_called = true;
                    }
                    auto sc_it = rep.line_sub_conditions.find(L);
                    if (sc_it != rep.line_sub_conditions.end()) {
                        for (const auto& sc : sc_it->second) {
                            ++fm.total_sub_conditions;
                            if (sc.is_fully_covered()) {
                                ++fm.fully_covered_sub_conditions;
                            }
                        }
                    }
                    auto al_it = rep.line_alarms.find(L);
                    if (al_it != rep.line_alarms.end()) {
                        fm.alarms_count += static_cast<uint32_t>(al_it->second.size());
                    }
                }
                rep.functions.push_back(std::move(fm));
            }
            std::sort(rep.functions.begin(), rep.functions.end(),
                      [](const FunctionCoverageMetrics& a, const FunctionCoverageMetrics& b) {
                          if (a.start_line != b.start_line) return a.start_line < b.start_line;
                          return a.function_name < b.function_name;
                      });
        }

        // Per-file totals
        for (uint32_t L : exec_lines) {
            if (L == 0 || L > rep.total_lines) continue;
            ++rep.total_executable_lines;
            LineStatus st = rep.line_statuses[L];
            if (st == LineStatus::CoveredSafe ||
                st == LineStatus::CoveredPartial ||
                st == LineStatus::CoveredAlarm) {
                ++rep.total_covered_lines;
            }
        }

        reports.push_back(std::move(rep));
    }

    return reports;
}

} // namespace sqc
