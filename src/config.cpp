#include "sqc/config.h"

#include <llvm/Support/JSON.h>
#include <llvm/Support/MemoryBuffer.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <unordered_set>

namespace fs = std::filesystem;

namespace sqc {

namespace {

std::vector<std::string> split_shell_args(const std::string& input) {
    std::vector<std::string> tokens;
    std::string cur;
    bool in_single = false;
    bool in_double = false;
    bool escaped = false;

    for (char c : input) {
        if (escaped) {
            cur.push_back(c);
            escaped = false;
            continue;
        }
        if (c == '\\' && !in_single) {
            escaped = true;
            continue;
        }
        if (c == '\'' && !in_double) {
            in_single = !in_single;
            continue;
        }
        if (c == '"' && !in_single) {
            in_double = !in_double;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c)) && !in_single && !in_double) {
            if (!cur.empty()) {
                tokens.push_back(cur);
                cur.clear();
            }
            continue;
        }
        cur.push_back(c);
    }
    if (!cur.empty()) {
        tokens.push_back(cur);
    }
    return tokens;
}

void extract_string_list(const llvm::json::Object& obj,
                         const char* key,
                         std::vector<std::string>& out,
                         bool split_single_string = false) {
    const llvm::json::Value* val = obj.get(key);
    if (!val) return;

    if (auto s = val->getAsString()) {
        if (split_single_string) {
            auto parts = split_shell_args(s->str());
            out.insert(out.end(), parts.begin(), parts.end());
        } else if (!s->empty()) {
            out.push_back(s->str());
        }
    } else if (const auto* arr = val->getAsArray()) {
        for (const auto& elem : *arr) {
            if (auto es = elem.getAsString()) {
                if (split_single_string) {
                    auto parts = split_shell_args(es->str());
                    out.insert(out.end(), parts.begin(), parts.end());
                } else if (!es->empty()) {
                    out.push_back(es->str());
                }
            }
        }
    }
}

bool wildcard_match(const std::string& pattern, const std::string& text) {
    size_t pi = 0;
    size_t ti = 0;
    size_t star_idx = std::string::npos;
    size_t match_idx = 0;

    while (ti < text.size()) {
        if (pi < pattern.size() && (pattern[pi] == '?' || pattern[pi] == text[ti])) {
            ++pi;
            ++ti;
        } else if (pi < pattern.size() && pattern[pi] == '*') {
            star_idx = pi++;
            match_idx = ti;
        } else if (star_idx != std::string::npos) {
            pi = star_idx + 1;
            ti = ++match_idx;
        } else {
            return false;
        }
    }
    while (pi < pattern.size() && pattern[pi] == '*') {
        ++pi;
    }
    return pi == pattern.size();
}

void parse_analysis_object(const llvm::json::Object& aobj,
                           uint32_t id,
                           const std::string& default_prefix,
                           const std::string& default_machdep,
                           const std::string& default_entry,
                           const std::vector<std::string>& common_files,
                           const std::vector<std::string>& common_args,
                           const std::vector<std::string>& common_split_ret,
                           AnalysisUnitConfig& unit) {
    unit.id = id;
    unit.prefix_path = default_prefix;
    unit.machdep = default_machdep;
    unit.entry_function = default_entry;
    unit.files = common_files;
    unit.cpp_extra_args = common_args;
    unit.split_return_functions = common_split_ret;

    if (auto name = aobj.getString("name")) {
        unit.name = name->str();
    } else {
        unit.name = "analysis_" + std::to_string(id);
    }

    if (auto pp = aobj.getString("prefix_path")) {
        unit.prefix_path = pp->str();
    } else if (auto pp2 = aobj.getString("prefix-path")) {
        unit.prefix_path = pp2->str();
    }

    if (auto md = aobj.getString("machdep")) {
        unit.machdep = md->str();
    }

    if (auto main_fn = aobj.getString("main")) {
        if (!main_fn->empty()) {
            unit.entry_function = main_fn->str();
        }
    }

    extract_string_list(aobj, "files", unit.files, false);
    extract_string_list(aobj, "cpp-extra-args", unit.cpp_extra_args, true);
    extract_string_list(aobj, "cpp_extra_args", unit.cpp_extra_args, true);
    extract_string_list(aobj, "split-return-functions", unit.split_return_functions, false);
    extract_string_list(aobj, "split_return_functions", unit.split_return_functions, false);

    std::vector<std::string> inc_dirs;
    extract_string_list(aobj, "include-dirs", inc_dirs, false);
    extract_string_list(aobj, "include_dirs", inc_dirs, false);
    for (const auto& d : inc_dirs) {
        unit.cpp_extra_args.push_back("-I" + d);
    }
}

} // namespace

bool ConfigLoader::load(const std::string& path, ProjectConfig& out_cfg, std::string& err_msg) {
    out_cfg = ProjectConfig{};
    err_msg.clear();

    std::error_code ec;
    fs::path cfg_path = fs::absolute(fs::path(path), ec).lexically_normal();
    if (ec || !fs::exists(cfg_path, ec)) {
        err_msg = "Configuration file not found: " + path;
        return false;
    }

    out_cfg.config_file_path = cfg_path.string();
    out_cfg.config_dir = cfg_path.parent_path().string();

    auto buf_or_err = llvm::MemoryBuffer::getFile(cfg_path.string());
    if (!buf_or_err) {
        err_msg = "Failed to read configuration file '" + cfg_path.string() +
                  "': " + buf_or_err.getError().message();
        return false;
    }

    auto parsed = llvm::json::parse((*buf_or_err)->getBuffer());
    if (!parsed) {
        err_msg = "JSON parse error in '" + cfg_path.string() +
                  "': " + llvm::toString(parsed.takeError());
        return false;
    }

    std::string common_entry = "sqc_main";
    std::vector<std::string> common_files;
    std::vector<std::string> common_args;
    std::vector<std::string> common_split_ret;

    if (const auto* root_arr = parsed->getAsArray()) {
        uint32_t next_id = 1;
        for (const auto& elem : *root_arr) {
            if (const auto* aobj = elem.getAsObject()) {
                AnalysisUnitConfig unit;
                parse_analysis_object(*aobj, next_id++, out_cfg.prefix_path,
                                      out_cfg.machdep, common_entry,
                                      common_files, common_args, common_split_ret, unit);
                out_cfg.analyses.push_back(std::move(unit));
            }
        }
        return true;
    }

    const auto* root = parsed->getAsObject();
    if (!root) {
        err_msg = "Root JSON value in '" + cfg_path.string() + "' must be an object or array.";
        return false;
    }

    if (auto pp = root->getString("prefix_path")) {
        out_cfg.prefix_path = pp->str();
    } else if (auto pp2 = root->getString("prefix-path")) {
        out_cfg.prefix_path = pp2->str();
    }
    if (auto md = root->getString("machdep")) {
        out_cfg.machdep = md->str();
    }
    extract_string_list(*root, "skip_functions", out_cfg.skip_functions, false);
    extract_string_list(*root, "skip-functions", out_cfg.skip_functions, false);
    extract_string_list(*root, "split-return-functions", common_split_ret, false);
    extract_string_list(*root, "split_return_functions", common_split_ret, false);

    if (const auto* common = root->getObject("common")) {
        if (auto pp = common->getString("prefix_path")) {
            out_cfg.prefix_path = pp->str();
        } else if (auto pp2 = common->getString("prefix-path")) {
            out_cfg.prefix_path = pp2->str();
        }
        if (auto md = common->getString("machdep")) {
            out_cfg.machdep = md->str();
        }
        if (auto main_fn = common->getString("main")) {
            if (!main_fn->empty()) {
                common_entry = main_fn->str();
            }
        }
        extract_string_list(*common, "files", common_files, false);
        extract_string_list(*common, "cpp-extra-args", common_args, true);
        extract_string_list(*common, "cpp_extra_args", common_args, true);
        extract_string_list(*common, "skip_functions", out_cfg.skip_functions, false);
        extract_string_list(*common, "skip-functions", out_cfg.skip_functions, false);
        extract_string_list(*common, "split-return-functions", common_split_ret, false);
        extract_string_list(*common, "split_return_functions", common_split_ret, false);
    }

    const auto* analyses = root->getArray("analyses");
    if (!analyses) {
        err_msg = "Configuration file '" + cfg_path.string() + "' is missing 'analyses' array.";
        return false;
    }

    uint32_t next_id = 1;
    for (const auto& elem : *analyses) {
        const auto* aobj = elem.getAsObject();
        if (!aobj) continue;
        AnalysisUnitConfig unit;
        parse_analysis_object(*aobj, next_id++, out_cfg.prefix_path,
                              out_cfg.machdep, common_entry,
                              common_files, common_args, common_split_ret, unit);
        out_cfg.analyses.push_back(std::move(unit));
    }

    return true;
}

std::vector<std::string> ConfigLoader::expand_source_files(
    const AnalysisUnitConfig& unit,
    const std::string& base_dir) {
    std::vector<std::string> result;
    std::unordered_set<std::string> seen;

    fs::path root_dir = (fs::path(base_dir) / fs::path(unit.prefix_path)).lexically_normal();

    auto add_unique = [&](const fs::path& p) {
        std::string norm = p.lexically_normal().string();
        if (seen.insert(norm).second) {
            result.push_back(std::move(norm));
        }
    };

    for (const auto& spec : unit.files) {
        if (spec.empty()) continue;

        fs::path spec_path(spec);
        fs::path full_path = spec_path.is_absolute() ? spec_path : (root_dir / spec_path);

        bool has_glob = (spec.find('*') != std::string::npos || spec.find('?') != std::string::npos);
        if (!has_glob) {
            add_unique(full_path);
            continue;
        }

        fs::path parent_dir = full_path.parent_path().lexically_normal();
        std::string pattern = full_path.filename().string();

        std::error_code ec;
        if (!fs::exists(parent_dir, ec) || !fs::is_directory(parent_dir, ec)) {
            continue;
        }

        std::vector<fs::path> matches;
        for (const auto& entry : fs::directory_iterator(parent_dir, ec)) {
            if (ec) break;
            std::error_code file_ec;
            if (!entry.is_regular_file(file_ec)) continue;
            std::string fname = entry.path().filename().string();
            if (wildcard_match(pattern, fname)) {
                matches.push_back(entry.path().lexically_normal());
            }
        }

        std::sort(matches.begin(), matches.end(),
                  [](const fs::path& a, const fs::path& b) {
                      return a.filename().string() < b.filename().string();
                  });

        for (const auto& m : matches) {
            add_unique(m);
        }
    }

    return result;
}

} // namespace sqc
