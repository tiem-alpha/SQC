#include "sqc/analyzer.h"
#include "sqc/config.h"
#include "sqc/frontend.h"
#include "sqc/report.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace {

void print_usage() {
    std::cout
        << "SQC - Static Quality & Hazard Analyzer\n\n"
        << "Usage:\n"
        << "  sqc list [-c <config.json>]\n"
        << "  sqc run [all|<id>|<name>|<start>..<end>] [-c <config.json>] [-o <report.html>] [--sarif <report.sarif>] [-v|--verbose] [--no-cache] [-j <N>]\n"
        << "  sqc report [-c <config.json>] [-o <report.html>] [--sarif <report.sarif>] [--skip-function=<regex>]\n"
        << "  sqc migrate <dir>\n";
}

std::string find_default_config() {
    static const char* candidates[] = {
        "sqc.json",
        "config.json",
        "sqc/config.json"
    };
    std::error_code ec;
    for (const char* c : candidates) {
        if (fs::exists(c, ec)) return c;
    }
    return "sqc.json";
}

std::string find_sqc_include_dir(const char* argv0) {
    std::error_code ec;
    std::vector<fs::path> candidates;

    if (fs::exists("/proc/self/exe", ec)) {
        fs::path exe = fs::read_symlink("/proc/self/exe", ec);
        if (!ec && !exe.empty()) {
            fs::path exe_dir = exe.parent_path();
            candidates.push_back(exe_dir / ".." / "include");
            candidates.push_back(exe_dir / ".." / ".." / "include");
            candidates.push_back(exe_dir / "include");
        }
    }
    if (argv0 && argv0[0] != '\0') {
        fs::path exe_dir = fs::absolute(fs::path(argv0), ec).parent_path();
        candidates.push_back(exe_dir / ".." / "include");
        candidates.push_back(exe_dir / "include");
    }
    candidates.push_back(fs::current_path() / "include");
    candidates.push_back(fs::current_path() / "sqc" / "include");
    candidates.push_back("/home/nguyentiem/Project/Code_quality/sqc/include");

    for (const auto& dir : candidates) {
        fs::path norm = dir.lexically_normal();
        if (fs::exists(norm / "sqc_builtin.h", ec)) {
            return norm.string();
        }
    }
    return "/home/nguyentiem/Project/Code_quality/sqc/include";
}

bool is_all_digits(const std::string& s) {
    if (s.empty()) return false;
    return std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

std::vector<sqc::AnalysisUnitConfig> select_units(
    const sqc::ProjectConfig& cfg,
    const std::vector<std::string>& targets) {
    if (targets.empty()) {
        return cfg.analyses;
    }

    std::vector<sqc::AnalysisUnitConfig> selected;
    std::vector<bool> picked(cfg.analyses.size(), false);

    auto add_index = [&](size_t idx) {
        if (idx < cfg.analyses.size() && !picked[idx]) {
            picked[idx] = true;
            selected.push_back(cfg.analyses[idx]);
        }
    };

    for (const auto& t : targets) {
        if (t == "all") {
            for (size_t i = 0; i < cfg.analyses.size(); ++i) add_index(i);
            continue;
        }
        size_t dots = t.find("..");
        if (dots != std::string::npos) {
            std::string lhs = t.substr(0, dots);
            std::string rhs = t.substr(dots + 2);
            if (is_all_digits(lhs) && is_all_digits(rhs)) {
                uint32_t start_id = static_cast<uint32_t>(std::stoul(lhs));
                uint32_t end_id = static_cast<uint32_t>(std::stoul(rhs));
                for (size_t i = 0; i < cfg.analyses.size(); ++i) {
                    if (cfg.analyses[i].id >= start_id && cfg.analyses[i].id <= end_id) {
                        add_index(i);
                    }
                }
                continue;
            }
        }
        if (is_all_digits(t)) {
            uint32_t id = static_cast<uint32_t>(std::stoul(t));
            for (size_t i = 0; i < cfg.analyses.size(); ++i) {
                if (cfg.analyses[i].id == id) {
                    add_index(i);
                }
            }
            continue;
        }
        for (size_t i = 0; i < cfg.analyses.size(); ++i) {
            if (cfg.analyses[i].name == t) {
                add_index(i);
            }
        }
    }

    return selected;
}

std::string derive_project_name(const sqc::ProjectConfig& cfg) {
    if (!cfg.config_dir.empty()) {
        fs::path p = fs::path(cfg.config_dir);
        std::string name = p.filename().string();
        if (name == "sqc" || name == ".") {
            if (p.has_parent_path()) {
                std::string parent_name = p.parent_path().filename().string();
                if (!parent_name.empty()) return parent_name;
            }
        }
        if (!name.empty()) return name;
    }
    return "SQC Project";
}

uint64_t fnv1a_bytes(uint64_t h, const void* data, size_t len) {
    constexpr uint64_t FNV_PRIME = 1099511628211ULL;
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; ++i) {
        h ^= p[i];
        h *= FNV_PRIME;
    }
    return h;
}

uint64_t fnv1a_str(uint64_t h, const std::string& s) {
    h = fnv1a_bytes(h, s.data(), s.size());
    char sep = '\0';
    return fnv1a_bytes(h, &sep, 1);
}

std::string sanitize_cache_name(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || c == '_' || c == '-') out.push_back(c);
        else out.push_back('_');
    }
    return out.empty() ? "unit" : out;
}

std::string compute_unit_fingerprint(const sqc::AnalysisUnitConfig& unit,
                                     const std::string& config_dir,
                                     const std::string& sqc_include_dir) {
    uint64_t h = 14695981039346656037ULL;
    h = fnv1a_str(h, unit.name);
    h = fnv1a_str(h, unit.entry_function);
    h = fnv1a_str(h, unit.machdep);
    for (const auto& a : unit.cpp_extra_args) h = fnv1a_str(h, a);
    for (const auto& s : unit.split_return_functions) h = fnv1a_str(h, s);

    std::error_code ec;
    // Invalidate cache whenever sqc binary itself is rebuilt
    if (fs::exists("/proc/self/exe", ec)) {
        auto exe_path = fs::read_symlink("/proc/self/exe", ec);
        if (!ec) {
            auto ft = fs::last_write_time(exe_path, ec);
            if (!ec) {
                auto cnt = ft.time_since_epoch().count();
                h = fnv1a_bytes(h, &cnt, sizeof(cnt));
            }
        }
    }

    // Hash all expanded C source file contents
    auto source_files = sqc::ConfigLoader::expand_source_files(unit, config_dir);
    for (const auto& f : source_files) {
        h = fnv1a_str(h, f);
        std::ifstream ifs(f, std::ios::binary);
        if (ifs) {
            char buf[8192];
            while (ifs.read(buf, sizeof(buf)) || ifs.gcount() > 0) {
                h = fnv1a_bytes(h, buf, static_cast<size_t>(ifs.gcount()));
            }
        }
    }

    // Hash header metadata (path + size + mtime) in include dirs
    fs::path unit_root = (fs::path(config_dir) / fs::path(unit.prefix_path)).lexically_normal();
    std::vector<fs::path> inc_dirs;
    inc_dirs.push_back(fs::path(sqc_include_dir));
    for (size_t i = 0; i < unit.cpp_extra_args.size(); ++i) {
        const std::string& arg = unit.cpp_extra_args[i];
        fs::path p;
        if ((arg == "-I" || arg == "-isystem") && i + 1 < unit.cpp_extra_args.size()) {
            p = fs::path(unit.cpp_extra_args[++i]);
        } else if (arg.rfind("-I", 0) == 0 && arg.size() > 2) {
            p = fs::path(arg.substr(2));
        } else if (arg.rfind("-isystem", 0) == 0 && arg.size() > 8) {
            p = fs::path(arg.substr(8));
        } else {
            continue;
        }
        if (!p.is_absolute()) p = (unit_root / p).lexically_normal();
        inc_dirs.push_back(p);
    }

    std::vector<fs::path> headers;
    for (const auto& dir : inc_dirs) {
        if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) continue;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (entry.is_regular_file(ec) && entry.path().extension() == ".h") {
                headers.push_back(entry.path());
            }
        }
    }
    std::sort(headers.begin(), headers.end());
    headers.erase(std::unique(headers.begin(), headers.end()), headers.end());
    for (const auto& hp : headers) {
        h = fnv1a_str(h, hp.string());
        auto sz = fs::file_size(hp, ec);
        if (!ec) h = fnv1a_bytes(h, &sz, sizeof(sz));
        auto ft = fs::last_write_time(hp, ec);
        if (!ec) {
            auto cnt = ft.time_since_epoch().count();
            h = fnv1a_bytes(h, &cnt, sizeof(cnt));
        }
    }

    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << h;
    return oss.str();
}

std::vector<sqc::AnalysisRunResult> execute_units(
    const sqc::ProjectConfig& cfg,
    const std::vector<sqc::AnalysisUnitConfig>& units,
    const std::string& sqc_include_dir,
    const std::string& results_cache_dir,
    bool verbose = false,
    bool use_cache = true,
    size_t max_jobs = 0) {
    std::error_code ec;
    fs::create_directories(results_cache_dir, ec);

    std::vector<sqc::AnalysisRunResult> results(units.size());
    std::vector<bool> cached_hit(units.size(), false);
    std::vector<std::string> fingerprints(units.size());

    // Preload existing cached results if incremental cache is enabled
    std::unordered_map<std::string, sqc::AnalysisRunResult> existing_cache;
    if (use_cache) {
        auto loaded = sqc::ReportGenerator::load_all_run_results(results_cache_dir);
        for (auto& r : loaded) {
            std::string key = std::to_string(r.unit_id) + "_" + sanitize_cache_name(r.unit_name);
            existing_cache[key] = std::move(r);
        }
    }

    std::vector<size_t> to_run;
    for (size_t i = 0; i < units.size(); ++i) {
        const auto& unit = units[i];
        std::string key = std::to_string(unit.id) + "_" + sanitize_cache_name(unit.name);
        fs::path hash_path = fs::path(results_cache_dir) / ("run_" + key + ".hash");
        fingerprints[i] = compute_unit_fingerprint(unit, cfg.config_dir, sqc_include_dir);

        if (use_cache) {
            auto cit = existing_cache.find(key);
            if (cit != existing_cache.end() && cit->second.success && fs::exists(hash_path, ec)) {
                std::ifstream hfs(hash_path);
                std::string saved_fp;
                if (hfs && std::getline(hfs, saved_fp) && saved_fp == fingerprints[i]) {
                    std::cout << "[" << std::setw(3) << std::setfill('0') << unit.id << std::setfill(' ')
                              << "] '" << unit.name << "' [CACHED - unchanged, skipped]" << std::endl;
                    results[i] = std::move(cit->second);
                    cached_hit[i] = true;
                    continue;
                }
            }
        }
        to_run.push_back(i);
    }

    if (to_run.empty()) {
        return results;
    }

    std::mutex io_mtx;
    std::atomic<size_t> next_job{0};
    unsigned int hw = std::thread::hardware_concurrency();
    size_t jobs = (max_jobs > 0) ? max_jobs : std::max<size_t>(1, hw);
    size_t num_workers = std::min<size_t>(to_run.size(), jobs);

    auto worker = [&]() {
        while (true) {
            size_t job_idx = next_job.fetch_add(1, std::memory_order_relaxed);
            if (job_idx >= to_run.size()) break;
            size_t idx = to_run[job_idx];
            const auto& unit = units[idx];

            {
                std::lock_guard<std::mutex> lk(io_mtx);
                std::cout << "[" << std::setw(3) << std::setfill('0') << unit.id << std::setfill(' ')
                          << "] Analyzing '" << unit.name << "'..." << std::endl;
            }

            auto t_compile_start = std::chrono::steady_clock::now();
            llvm::LLVMContext ctx;
            std::string err_msg;
            auto mod = sqc::FrontendPipeline::compile_and_link_unit(
                unit, cfg.config_dir, sqc_include_dir, ctx, err_msg);
            auto t_compile_end = std::chrono::steady_clock::now();

            if (!mod) {
                sqc::AnalysisRunResult failed;
                failed.unit_id = unit.id;
                failed.unit_name = unit.name;
                failed.success = false;
                failed.error_message = err_msg;
                {
                    std::lock_guard<std::mutex> lk(io_mtx);
                    std::cerr << "  [ERROR] " << err_msg << std::endl;
                }
                sqc::ReportGenerator::save_run_result(results_cache_dir, failed);
                results[idx] = std::move(failed);
                continue;
            }

            if (verbose) {
                double comp_ms = std::chrono::duration<double, std::milli>(t_compile_end - t_compile_start).count();
                std::lock_guard<std::mutex> lk(io_mtx);
                std::cerr << "  [" << unit.name << "] Compiled & linked IR in "
                          << std::fixed << std::setprecision(1) << comp_ms << " ms" << std::endl;
            }

            sqc::HazardGuidedAnalyzer analyzer(*mod, unit);
            analyzer.set_verbose(verbose);
            sqc::AnalysisRunResult res = analyzer.run();
            res.unit_id = unit.id;
            res.unit_name = unit.name;

            sqc::ReportGenerator::save_run_result(results_cache_dir, res);
            if (res.success) {
                std::string key = std::to_string(unit.id) + "_" + sanitize_cache_name(unit.name);
                fs::path hash_path = fs::path(results_cache_dir) / ("run_" + key + ".hash");
                std::ofstream hfs(hash_path, std::ios::trunc);
                if (hfs) hfs << fingerprints[idx] << "\n";
            }
            if (verbose) {
                std::lock_guard<std::mutex> lk(io_mtx);
                std::cerr << "  [" << unit.name << "] Done in "
                          << std::fixed << std::setprecision(1) << (res.elapsed_ms / 1000.0)
                          << "s (" << res.instructions_analyzed << " IR instrs, "
                          << res.equivalence_cache_hits << " cache hits)" << std::endl;
            }
            results[idx] = std::move(res);
        }
    };

    if (num_workers <= 1) {
        worker();
    } else {
        std::vector<std::thread> threads;
        threads.reserve(num_workers);
        for (size_t w = 0; w < num_workers; ++w) {
            threads.emplace_back(worker);
        }
        for (auto& t : threads) {
            t.join();
        }
    }

    return results;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string cmd = argv[1];
    if (cmd == "-h" || cmd == "--help" || cmd == "help") {
        print_usage();
        return 0;
    }

    std::string config_path;
    std::string html_out = "sqc_report.html";
    std::string sarif_out;
    std::vector<std::string> skip_functions;
    std::vector<std::string> positional;
    bool verbose = false;
    bool use_cache = true;
    size_t max_jobs = 0;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            config_path = argv[++i];
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            html_out = argv[++i];
        } else if (arg == "--sarif" && i + 1 < argc) {
            sarif_out = argv[++i];
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (arg == "--no-cache") {
            use_cache = false;
        } else if ((arg == "-j" || arg == "--jobs") && i + 1 < argc) {
            max_jobs = static_cast<size_t>(std::max(1, std::atoi(argv[++i])));
        } else if (arg.rfind("--skip-function=", 0) == 0) {
            skip_functions.push_back(arg.substr(16));
        } else if (arg == "--skip-function" && i + 1 < argc) {
            skip_functions.push_back(argv[++i]);
        } else if (arg == "-h" || arg == "--help") {
            print_usage();
            return 0;
        } else {
            positional.push_back(arg);
        }
    }

    if (cmd == "migrate") {
        if (positional.empty()) {
            std::cerr << "Usage: sqc migrate <dir>\n";
            return 1;
        }
        uint32_t modified = 0;
        std::string err_msg;
        if (!sqc::FrontendPipeline::migrate_directory(positional[0], modified, err_msg)) {
            std::cerr << "Migration failed: " << err_msg << "\n";
            return 1;
        }
        std::cout << "Migration complete: " << modified << " file(s) updated in '"
                  << positional[0] << "'.\n";
        return 0;
    }

    if (config_path.empty()) {
        config_path = find_default_config();
    }

    std::string sqc_include_dir = find_sqc_include_dir(argv[0]);
    fs::path cache_root = (fs::current_path() / ".sqc_cache" / "results").lexically_normal();

    if (cmd == "list") {
        sqc::ProjectConfig cfg;
        std::string err_msg;
        if (!sqc::ConfigLoader::load(config_path, cfg, err_msg)) {
            std::cerr << "Error: " << err_msg << "\n";
            return 1;
        }
        for (const auto& u : cfg.analyses) {
            std::cout << "[" << std::setw(3) << std::setfill('0') << u.id << std::setfill(' ')
                      << "] '" << u.name << "'\n";
        }
        return 0;
    }

    if (cmd == "run") {
        sqc::ProjectConfig cfg;
        std::string err_msg;
        if (!sqc::ConfigLoader::load(config_path, cfg, err_msg)) {
            std::cerr << "Error: " << err_msg << "\n";
            return 1;
        }
        for (const auto& sf : skip_functions) {
            cfg.skip_functions.push_back(sf);
        }

        auto units = select_units(cfg, positional);
        if (units.empty()) {
            std::cerr << "No matching analysis units found.\n";
            return 1;
        }

        auto results = execute_units(cfg, units, sqc_include_dir, cache_root.string(), verbose, use_cache, max_jobs);
        std::string project_name = derive_project_name(cfg);

        if (!sqc::ReportGenerator::generate_html_report(html_out, project_name, results, err_msg)) {
            std::cerr << "Warning: failed to generate HTML report: " << err_msg << "\n";
        }
        if (!sarif_out.empty()) {
            if (!sqc::ReportGenerator::generate_sarif_report(sarif_out, results, err_msg)) {
                std::cerr << "Warning: failed to generate SARIF report: " << err_msg << "\n";
            }
        }
        sqc::ReportGenerator::print_cli_summary(results, html_out);

        bool all_ok = std::all_of(results.begin(), results.end(),
                                  [](const sqc::AnalysisRunResult& r) { return r.success; });
        return all_ok ? 0 : 1;
    }

    if (cmd == "report") {
        sqc::ProjectConfig cfg;
        std::string err_msg;
        bool has_cfg = sqc::ConfigLoader::load(config_path, cfg, err_msg);
        if (has_cfg) {
            for (const auto& sf : skip_functions) {
                cfg.skip_functions.push_back(sf);
            }
        }

        auto results = sqc::ReportGenerator::load_all_run_results(cache_root.string());
        if (results.empty()) {
            if (!has_cfg) {
                std::cerr << "Error: no cached results found in '" << cache_root.string()
                          << "' and failed to load config: " << err_msg << "\n";
                return 1;
            }
            results = execute_units(cfg, cfg.analyses, sqc_include_dir, cache_root.string(), verbose, use_cache, max_jobs);
        }

        std::string project_name = has_cfg ? derive_project_name(cfg) : "SQC Project";
        if (!sqc::ReportGenerator::generate_html_report(html_out, project_name, results, err_msg)) {
            std::cerr << "Error: failed to generate HTML report: " << err_msg << "\n";
            return 1;
        }
        if (!sarif_out.empty()) {
            if (!sqc::ReportGenerator::generate_sarif_report(sarif_out, results, err_msg)) {
                std::cerr << "Warning: failed to generate SARIF report: " << err_msg << "\n";
            }
        }
        sqc::ReportGenerator::print_cli_summary(results, html_out);
        return 0;
    }

    std::cerr << "Unknown command: " << cmd << "\n";
    print_usage();
    return 1;
}
