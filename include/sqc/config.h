#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>

namespace sqc {

struct AnalysisUnitConfig {
    uint32_t id = 0;
    std::string name;
    std::string prefix_path = ".";
    std::string machdep = "gcc_arm_eabi";
    std::string entry_function = "sqc_main";
    std::vector<std::string> files;
    std::vector<std::string> cpp_extra_args;
    std::vector<std::string> split_return_functions;
};

struct ProjectConfig {
    std::string config_file_path;
    std::string config_dir;
    std::string prefix_path = ".";
    std::string machdep = "gcc_arm_eabi";
    std::vector<AnalysisUnitConfig> analyses;
    std::vector<std::string> skip_functions;
};

class ConfigLoader {
public:
    // Load sqc.json project configuration
    static bool load(const std::string& path, ProjectConfig& out_cfg, std::string& err_msg);

    // Resolve glob patterns (e.g. "com/src/*.c") relative to prefix_path
    static std::vector<std::string> expand_source_files(
        const AnalysisUnitConfig& unit,
        const std::string& base_dir);
};

} // namespace sqc
