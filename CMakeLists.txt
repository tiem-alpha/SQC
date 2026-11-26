cmake_minimum_required(VERSION 3.20)
project(sqc VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(LLVM REQUIRED CONFIG PATHS /usr/lib/llvm-18/lib/cmake/llvm NO_DEFAULT_PATH)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION} in ${LLVM_INSTALL_PREFIX}")

find_library(Z3_LIBRARIES NAMES z3 PATHS /usr/lib/x86_64-linux-gnu REQUIRED)
find_path(Z3_INCLUDE_DIRS NAMES z3++.h PATHS /usr/include REQUIRED)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${LLVM_INCLUDE_DIRS}
    ${Z3_INCLUDE_DIRS}
)
add_definitions(${LLVM_DEFINITIONS})

set(SQC_CORE_SOURCES
    src/config.cpp
    src/frontend.cpp
    src/intervals.cpp
    src/state.cpp
    src/equivalence.cpp
    src/summaries.cpp
    src/smt.cpp
    src/coverage.cpp
    src/analyzer.cpp
    src/report.cpp
)

add_library(sqc_core STATIC ${SQC_CORE_SOURCES})
target_compile_options(sqc_core PRIVATE -Wall -Wextra -Wno-unused-parameter -fno-rtti)
target_link_libraries(sqc_core PUBLIC LLVM ${Z3_LIBRARIES} pthread dl)

add_executable(sqc src/main.cpp)
target_compile_options(sqc PRIVATE -Wall -Wextra -Wno-unused-parameter -fno-rtti)
target_link_libraries(sqc PRIVATE sqc_core)

add_executable(test_engine_unit tests/test_engine_unit.cpp)
target_compile_options(test_engine_unit PRIVATE -Wall -Wextra -Wno-unused-parameter -fno-rtti)
target_link_libraries(test_engine_unit PRIVATE sqc_core)
