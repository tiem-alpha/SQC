#include "sqc/frontend.h"

#include <llvm/IR/Attributes.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalValue.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Linker/Linker.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Transforms/Utils/Mem2Reg.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace sqc {

namespace {

bool is_ident_char(char c) {
    unsigned char uc = static_cast<unsigned char>(c);
    return std::isalnum(uc) || c == '_';
}

std::string replace_all_substr(const std::string& input,
                               const std::string& from,
                               const std::string& to) {
    if (from.empty()) return input;
    std::string out;
    out.reserve(input.size());
    size_t pos = 0;
    while (true) {
        size_t found = input.find(from, pos);
        if (found == std::string::npos) {
            out.append(input, pos, std::string::npos);
            break;
        }
        out.append(input, pos, found - pos);
        out.append(to);
        pos = found + from.size();
    }
    return out;
}

std::string replace_whole_word(const std::string& input,
                               const std::string& from,
                               const std::string& to) {
    if (from.empty()) return input;
    std::string out;
    out.reserve(input.size());
    size_t pos = 0;
    while (true) {
        size_t found = input.find(from, pos);
        if (found == std::string::npos) {
            out.append(input, pos, std::string::npos);
            break;
        }
        bool left_ok = (found == 0) || !is_ident_char(input[found - 1]);
        size_t end = found + from.size();
        bool right_ok = (end == input.size()) || !is_ident_char(input[end]);
        if (left_ok && right_ok) {
            out.append(input, pos, found - pos);
            out.append(to);
            pos = end;
        } else {
            out.append(input, pos, found - pos + 1);
            pos = found + 1;
        }
    }
    return out;
}

std::string trim_copy(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(start, end - start);
}

std::string escape_c_string(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '\\' || c == '"') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

std::string sanitize_name(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || c == '_' || c == '-') {
            out.push_back(c);
        } else {
            out.push_back('_');
        }
    }
    return out.empty() ? "unit" : out;
}

bool read_file_to_string(const fs::path& path, std::string& out) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return false;
    std::ostringstream oss;
    oss << ifs.rdbuf();
    out = oss.str();
    return true;
}

bool write_string_to_file(const fs::path& path, const std::string& content) {
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs) return false;
    ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
    return ofs.good();
}

bool is_acsl_expr_convertible(const std::string& expr) {
    if (expr.empty()) return false;
    if (expr.find('\\') != std::string::npos) return false;
    if (expr.find("UB:") != std::string::npos) return false;
    if (expr.find("==>") != std::string::npos) return false;
    if (expr.find("<==>") != std::string::npos) return false;
    return true;
}

bool try_transform_acsl_assert_line(const std::string& line, std::string& out_line) {
    // Check single-line block ACSL: /*@ assert (expr); */
    size_t bstart = line.find("/*@");
    if (bstart != std::string::npos) {
        size_t bend = line.find("*/", bstart + 3);
        if (bend != std::string::npos) {
            std::string inner = trim_copy(line.substr(bstart + 3, bend - (bstart + 3)));
            if (inner.rfind("assert", 0) == 0 &&
                (inner.size() == 6 || std::isspace(static_cast<unsigned char>(inner[6])) || inner[6] == '(')) {
                std::string expr = trim_copy(inner.substr(6));
                while (!expr.empty() && expr.back() == ';') {
                    expr.pop_back();
                    expr = trim_copy(expr);
                }
                if (is_acsl_expr_convertible(expr)) {
                    out_line = line.substr(0, bstart) + "sqc_assert(" + expr + ");" + line.substr(bend + 2);
                    return true;
                }
            }
        }
    }

    // Check single-line line-comment ACSL: //@ assert expr;
    size_t lstart = line.find("//@");
    if (lstart != std::string::npos) {
        std::string inner = trim_copy(line.substr(lstart + 3));
        if (inner.rfind("assert", 0) == 0 &&
            (inner.size() == 6 || std::isspace(static_cast<unsigned char>(inner[6])) || inner[6] == '(')) {
            std::string expr = trim_copy(inner.substr(6));
            while (!expr.empty() && expr.back() == ';') {
                expr.pop_back();
                expr = trim_copy(expr);
            }
            if (is_acsl_expr_convertible(expr)) {
                out_line = line.substr(0, lstart) + "sqc_assert(" + expr + ");";
                return true;
            }
        }
    }

    return false;
}

bool is_driver_or_stub_path(const std::string& path) {
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.find("sqc") != std::string::npos ||
           lower.find("sqc") != std::string::npos ||
           lower.find("stub") != std::string::npos ||
           lower.find("driver") != std::string::npos ||
           lower.find("test") != std::string::npos;
}

bool ends_with(const std::string& str, const std::string& suffix) {
    if (suffix.size() > str.size()) return false;
    return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::unordered_set<std::string> collect_sqc_override_annotations(const llvm::Module& mod) {
    std::unordered_set<std::string> overrides;
    const llvm::GlobalVariable* ga = mod.getNamedGlobal("llvm.global.annotations");
    if (!ga || !ga->hasInitializer()) return overrides;

    const auto* arr = llvm::dyn_cast<llvm::ConstantArray>(ga->getInitializer());
    if (!arr) return overrides;

    for (unsigned i = 0; i < arr->getNumOperands(); ++i) {
        const auto* cs = llvm::dyn_cast<llvm::ConstantStruct>(arr->getOperand(i));
        if (!cs || cs->getNumOperands() < 2) continue;

        const llvm::Value* target = cs->getOperand(0)->stripPointerCasts();
        const auto* fn = llvm::dyn_cast<llvm::Function>(target);
        if (!fn) continue;

        const llvm::Value* annot_val = cs->getOperand(1)->stripPointerCasts();
        const auto* annot_gv = llvm::dyn_cast<llvm::GlobalVariable>(annot_val);
        if (!annot_gv || !annot_gv->hasInitializer()) continue;

        if (const auto* cda = llvm::dyn_cast<llvm::ConstantDataArray>(annot_gv->getInitializer())) {
            if (cda->isCString()) {
                llvm::StringRef annot_str = cda->getAsCString();
                if (annot_str == "sqc_override") {
                    overrides.insert(fn->getName().str());
                }
            }
        }
    }
    return overrides;
}

std::string build_sqc_preamble(const std::string& orig_file_path) {
    std::ostringstream oss;
    oss << "#include <sqc_builtin.h>\n"
        << "#line 1 \"" << escape_c_string(orig_file_path) << "\"\n";
    return oss.str();
}

} // namespace

bool FrontendPipeline::migrate_directory(const std::string& dir_path,
                                         uint32_t& out_files_modified,
                                         std::string& err_msg) {
    out_files_modified = 0;
    err_msg.clear();

    std::error_code ec;
    fs::path root(dir_path);
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
        err_msg = "Target directory does not exist: " + dir_path;
        return false;
    }

    // Ensure all C/H driver files in directory conform to sqc_builtin.h & sqc_* conventions
    for (auto it = fs::recursive_directory_iterator(root, ec);
         !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        const fs::path p = it->path();
        std::string fname = p.filename().string();
        if (it->is_directory(ec)) {
            if (fname == ".git" || fname == ".sqc_cache" || fname == "build") {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (!it->is_regular_file(ec)) continue;
        std::string ext = p.extension().string();
        if (ext != ".c" && ext != ".h" && ext != ".json") continue;
    }
    return true;
}

std::string FrontendPipeline::preprocess_source_text(const std::string& content,
                                                     const std::string& orig_file_path) {
    std::string result = build_sqc_preamble(orig_file_path);
    result.reserve(result.size() + content.size() + 256);

    bool in_block_comment = false;
    int brace_depth = 0;

    size_t pos = 0;
    while (pos <= content.size()) {
        size_t nl = content.find('\n', pos);
        bool has_nl = (nl != std::string::npos);
        std::string line = has_nl ? content.substr(pos, nl - pos) : content.substr(pos);

        // Strip trailing '\r' if CRLF, re-appended cleanly if needed
        bool has_cr = (!line.empty() && line.back() == '\r');
        if (has_cr) line.pop_back();

        // Transform single-line contract assertions inside function bodies
        if (!in_block_comment && brace_depth > 0) {
            std::string transformed;
            if (try_transform_acsl_assert_line(line, transformed)) {
                line = std::move(transformed);
            }
        }

        // 3. Update brace_depth and in_block_comment state for subsequent lines
        bool in_string = false;
        bool in_char = false;
        for (size_t i = 0; i < line.size(); ++i) {
            char c = line[i];
            char next = (i + 1 < line.size()) ? line[i + 1] : '\0';

            if (in_block_comment) {
                if (c == '*' && next == '/') {
                    in_block_comment = false;
                    ++i;
                }
                continue;
            }
            if (in_string) {
                if (c == '\\') {
                    ++i;
                } else if (c == '"') {
                    in_string = false;
                }
                continue;
            }
            if (in_char) {
                if (c == '\\') {
                    ++i;
                } else if (c == '\'') {
                    in_char = false;
                }
                continue;
            }

            if (c == '/' && next == '/') {
                break; // rest of line is line comment
            }
            if (c == '/' && next == '*') {
                in_block_comment = true;
                ++i;
                continue;
            }
            if (c == '"') {
                in_string = true;
                continue;
            }
            if (c == '\'') {
                in_char = true;
                continue;
            }
            if (c == '{') {
                ++brace_depth;
            } else if (c == '}') {
                if (brace_depth > 0) --brace_depth;
            }
        }

        result.append(line);
        if (has_cr) result.push_back('\r');
        if (has_nl) {
            result.push_back('\n');
            pos = nl + 1;
        } else {
            break;
        }
    }

    return result;
}

std::unique_ptr<llvm::Module> FrontendPipeline::compile_and_link_unit(
    const AnalysisUnitConfig& unit,
    const std::string& base_dir,
    const std::string& sqc_include_dir,
    llvm::LLVMContext& ctx,
    std::string& err_msg) {
    err_msg.clear();

    // 1. Create workspace-local cache directory for preprocessed C and LLVM bitcode
    fs::path sqc_root = fs::path(sqc_include_dir).parent_path();
    if (sqc_root.empty()) {
        sqc_root = fs::current_path();
    }
    fs::path ir_dir = (sqc_root / ".sqc_cache" / "ir" / sanitize_name(unit.name)).lexically_normal();
    std::error_code ec;
    fs::create_directories(ir_dir, ec);
    if (ec) {
        err_msg = "Failed to create IR cache directory '" + ir_dir.string() + "': " + ec.message();
        return nullptr;
    }

    // 2. Expand source files
    std::vector<std::string> expanded = ConfigLoader::expand_source_files(unit, base_dir);
    std::vector<std::string> source_files;
    source_files.reserve(expanded.size());
    for (const auto& f : expanded) {
        if (!fs::exists(fs::path(f), ec)) {
            err_msg = "Source file not found: " + f;
            return nullptr;
        }
        source_files.push_back(f);
    }

    if (source_files.empty()) {
        err_msg = "No C source files resolved for analysis unit '" + unit.name + "'.";
        return nullptr;
    }

    // 3. Resolve cpp_extra_args relative to base_dir / unit.prefix_path
    fs::path unit_root = (fs::path(base_dir) / fs::path(unit.prefix_path)).lexically_normal();
    std::vector<std::string> resolved_extra_args;
    resolved_extra_args.reserve(unit.cpp_extra_args.size());
    for (size_t i = 0; i < unit.cpp_extra_args.size(); ++i) {
        const std::string& arg = unit.cpp_extra_args[i];
        if (arg == "-I" || arg == "-isystem" || arg == "-include") {
            resolved_extra_args.push_back(arg);
            if (i + 1 < unit.cpp_extra_args.size()) {
                fs::path p(unit.cpp_extra_args[++i]);
                if (!p.is_absolute()) {
                    p = (unit_root / p).lexically_normal();
                }
                resolved_extra_args.push_back(p.string());
            }
        } else if (arg.rfind("-I", 0) == 0 && arg.size() > 2) {
            fs::path p(arg.substr(2));
            if (!p.is_absolute()) {
                p = (unit_root / p).lexically_normal();
            }
            resolved_extra_args.push_back("-I" + p.string());
        } else if (arg.rfind("-isystem", 0) == 0 && arg.size() > 8) {
            fs::path p(arg.substr(8));
            if (!p.is_absolute()) {
                p = (unit_root / p).lexically_normal();
            }
            resolved_extra_args.push_back("-isystem" + p.string());
        } else {
            resolved_extra_args.push_back(arg);
        }
    }

    // 4. Preprocess and compile each C file to LLVM bitcode using clang-18
    struct ParsedModuleInfo {
        std::unique_ptr<llvm::Module> mod;
        std::string orig_path;
        bool is_driver = false;
        std::unordered_set<std::string> annotated_overrides;
    };
    std::vector<ParsedModuleInfo> parsed_modules;
    parsed_modules.reserve(source_files.size());

    const std::string clang_bin = "/usr/bin/clang-18";

    struct CompileJobResult {
        fs::path bc_path;
        std::string orig_path;
        std::string error;
        bool ok = false;
    };
    std::vector<CompileJobResult> compile_results(source_files.size());

    std::atomic<size_t> next_idx{0};
    unsigned int hw = std::thread::hardware_concurrency();
    size_t num_workers = std::min<size_t>(source_files.size(), std::max<unsigned int>(1, hw));

    auto compile_worker = [&]() {
        while (true) {
            size_t idx = next_idx.fetch_add(1, std::memory_order_relaxed);
            if (idx >= source_files.size()) break;

            const std::string& orig_path = source_files[idx];
            CompileJobResult& job = compile_results[idx];
            job.orig_path = orig_path;

            std::string raw_src;
            if (!read_file_to_string(orig_path, raw_src)) {
                job.error = "Failed to read C source file: " + orig_path;
                continue;
            }

            std::string pp_src = preprocess_source_text(raw_src, orig_path);
            std::string base_name = std::to_string(idx) + "_" + fs::path(orig_path).stem().string();
            fs::path pp_path = ir_dir / (base_name + ".c");
            fs::path bc_path = ir_dir / (base_name + ".bc");
            fs::path stderr_path = ir_dir / (base_name + ".stderr.log");
            job.bc_path = bc_path;

            if (!write_string_to_file(pp_path, pp_src)) {
                job.error = "Failed to write preprocessed file: " + pp_path.string();
                continue;
            }

            std::vector<std::string> cmd_storage;
            cmd_storage.push_back(clang_bin);
            cmd_storage.push_back("-emit-llvm");
            cmd_storage.push_back("-c");
            cmd_storage.push_back("-g");
            cmd_storage.push_back("-gcolumn-info");
            cmd_storage.push_back("-O0");
            cmd_storage.push_back("-Xclang");
            cmd_storage.push_back("-disable-O0-optnone");
            cmd_storage.push_back("-Wno-everything");
            cmd_storage.push_back("-D__SQC_ANALYZER__=1");
            cmd_storage.push_back("-I" + sqc_include_dir);
            cmd_storage.push_back("-I" + fs::path(orig_path).parent_path().string());
            for (const auto& ea : resolved_extra_args) {
                cmd_storage.push_back(ea);
            }
            cmd_storage.push_back("-o");
            cmd_storage.push_back(bc_path.string());
            cmd_storage.push_back(pp_path.string());

            std::vector<llvm::StringRef> cmd_refs;
            cmd_refs.reserve(cmd_storage.size());
            for (const auto& s : cmd_storage) {
                cmd_refs.emplace_back(s);
            }

            std::string stderr_str = stderr_path.string();
            std::optional<llvm::StringRef> redirects[3] = {
                std::nullopt,
                std::nullopt,
                llvm::StringRef(stderr_str)
            };

            std::string exec_err;
            int rc = llvm::sys::ExecuteAndWait(clang_bin, cmd_refs, std::nullopt, redirects, 0, 0, &exec_err);
            if (rc != 0) {
                std::string clang_log;
                read_file_to_string(stderr_path, clang_log);
                job.error = "clang-18 failed (exit " + std::to_string(rc) + ") compiling '" + orig_path + "'";
                if (!exec_err.empty()) {
                    job.error += ": " + exec_err;
                }
                if (!clang_log.empty()) {
                    if (clang_log.size() > 1000) clang_log.resize(1000);
                    job.error += "\n" + clang_log;
                }
                continue;
            }
            job.ok = true;
        }
    };

    if (num_workers <= 1) {
        compile_worker();
    } else {
        std::vector<std::thread> threads;
        threads.reserve(num_workers);
        for (size_t w = 0; w < num_workers; ++w) {
            threads.emplace_back(compile_worker);
        }
        for (auto& t : threads) {
            t.join();
        }
    }

    for (size_t idx = 0; idx < source_files.size(); ++idx) {
        const auto& job = compile_results[idx];
        if (!job.ok) {
            err_msg = job.error;
            return nullptr;
        }
        llvm::SMDiagnostic sm_diag;
        std::unique_ptr<llvm::Module> mod = llvm::parseIRFile(job.bc_path.string(), sm_diag, ctx);
        if (!mod) {
            err_msg = "Failed to parse LLVM bitcode '" + job.bc_path.string() +
                      "': " + sm_diag.getMessage().str();
            return nullptr;
        }

        ParsedModuleInfo info;
        info.annotated_overrides = collect_sqc_override_annotations(*mod);
        info.orig_path = job.orig_path;
        info.is_driver = is_driver_or_stub_path(job.orig_path);
        info.mod = std::move(mod);
        parsed_modules.push_back(std::move(info));
    }

    // 5. Resolve SQC_OVERRIDE & duplicate function/global definitions before linking
    std::unordered_map<std::string, std::vector<size_t>> fn_definers;
    std::unordered_map<std::string, size_t> explicit_override_owner;

    for (size_t i = 0; i < parsed_modules.size(); ++i) {
        const auto& info = parsed_modules[i];
        for (const auto& fn : *info.mod) {
            if (fn.isDeclaration() || fn.hasLocalLinkage()) continue;
            std::string fname = fn.getName().str();
            fn_definers[fname].push_back(i);
            if (info.annotated_overrides.count(fname)) {
                explicit_override_owner[fname] = i;
            }
        }
    }

    for (const auto& [fname, owners] : fn_definers) {
        if (owners.size() <= 1) continue;

        size_t winner = owners.front();
        auto ov_it = explicit_override_owner.find(fname);
        if (ov_it != explicit_override_owner.end()) {
            winner = ov_it->second;
        } else {
            for (size_t idx : owners) {
                if (parsed_modules[idx].is_driver) {
                    winner = idx;
                    break;
                }
            }
        }

        if (llvm::Function* win_fn = parsed_modules[winner].mod->getFunction(fname)) {
            if (win_fn->hasWeakLinkage() || win_fn->hasLinkOnceLinkage()) {
                win_fn->setLinkage(llvm::GlobalValue::ExternalLinkage);
            }
        }

        for (size_t idx : owners) {
            if (idx == winner) continue;
            if (llvm::Function* lose_fn = parsed_modules[idx].mod->getFunction(fname)) {
                lose_fn->deleteBody();
                lose_fn->setLinkage(llvm::GlobalValue::ExternalLinkage);
            }
        }
    }

    // Handle duplicate strong global variable definitions across modules
    std::unordered_map<std::string, std::vector<size_t>> gv_definers;
    for (size_t i = 0; i < parsed_modules.size(); ++i) {
        for (const auto& gv : parsed_modules[i].mod->globals()) {
            if (!gv.hasInitializer() || gv.hasLocalLinkage()) continue;
            if (gv.getName().starts_with("llvm.")) continue;
            gv_definers[gv.getName().str()].push_back(i);
        }
    }
    for (const auto& [gname, owners] : gv_definers) {
        if (owners.size() <= 1) continue;
        size_t winner = owners.front();
        for (size_t idx : owners) {
            if (parsed_modules[idx].is_driver) {
                winner = idx;
                break;
            }
        }
        for (size_t idx : owners) {
            if (idx == winner) continue;
            if (llvm::GlobalVariable* gv = parsed_modules[idx].mod->getGlobalVariable(gname)) {
                if (!gv->hasWeakLinkage() && !gv->hasCommonLinkage() && !gv->hasLinkOnceLinkage()) {
                    gv->setLinkage(llvm::GlobalValue::WeakAnyLinkage);
                }
            }
        }
    }

    // Link all modules into the first module
    std::unique_ptr<llvm::Module> linked = std::move(parsed_modules[0].mod);
    for (size_t i = 1; i < parsed_modules.size(); ++i) {
        if (llvm::Linker::linkModules(*linked, std::move(parsed_modules[i].mod))) {
            err_msg = "LLVM module linking failed for analysis unit '" + unit.name +
                      "' while linking '" + parsed_modules[i].orig_path + "'.";
            return nullptr;
        }
    }

    // 6. Run LLVM PromotePass (mem2reg) on all functions while preserving short-circuit CFG & DWARF
    for (llvm::Function& fn : *linked) {
        if (!fn.isDeclaration() && fn.hasFnAttribute(llvm::Attribute::OptimizeNone)) {
            fn.removeFnAttr(llvm::Attribute::OptimizeNone);
        }
    }

    llvm::LoopAnalysisManager LAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::CGSCCAnalysisManager CGAM;
    llvm::ModuleAnalysisManager MAM;

    llvm::PassBuilder PB;
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    llvm::FunctionPassManager FPM;
    FPM.addPass(llvm::PromotePass());

    llvm::ModulePassManager MPM;
    MPM.addPass(llvm::createModuleToFunctionPassAdaptor(std::move(FPM)));
    MPM.run(*linked, MAM);

    return linked;
}

} // namespace sqc
