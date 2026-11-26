#pragma once

#include "sqc/config.h"
#include <string>
#include <vector>
#include <memory>

namespace llvm {
class LLVMContext;
class Module;
} // namespace llvm

namespace sqc {

class FrontendPipeline {
public:
    FrontendPipeline() = default;

    /**
     * @brief Migrate legacy C driver files (converting legacy prefixes & ACSL annotations
     * into clean sqc_* primitives and sqc_builtin.h).
     */
    static bool migrate_directory(const std::string& dir_path, uint32_t& out_files_modified, std::string& err_msg);

    /**
     * @brief Preprocess a C source file into a temporary file:
     * - Normalizes ACSL comments (e.g. //@ assert ... -> sqc_assert(...))
     * - Injects compatibility aliases so both sqc_* and legacy driver files compile cleanly
     */
    static std::string preprocess_source_text(const std::string& content, const std::string& orig_file_path);

    /**
     * @brief Compile all C files of an AnalysisUnitConfig to LLVM Bitcode (-g -gcolumn-info -O0),
     * run mem2reg (preserving short-circuit &&/|| blocks), resolve SQC_OVERRIDE symbol linkage,
     * and link into a single unified llvm::Module.
     */
    static std::unique_ptr<llvm::Module> compile_and_link_unit(
        const AnalysisUnitConfig& unit,
        const std::string& base_dir,
        const std::string& sqc_include_dir,
        llvm::LLVMContext& ctx,
        std::string& err_msg);
};

} // namespace sqc
