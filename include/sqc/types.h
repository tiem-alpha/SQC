#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <memory>

namespace sqc {

using ValueId = uint64_t;
using RegionId = uint32_t;
using GraphNodeId = uint64_t;

struct SourceLoc {
    std::string file;
    uint32_t line = 0;
    uint16_t col_start = 0;
    uint16_t col_end = 0;

    bool is_valid() const { return !file.empty() && line > 0; }
    std::string to_string() const {
        if (!is_valid()) return "<unknown>";
        return file + ":" + std::to_string(line) +
               (col_start ? (":" + std::to_string(col_start)) : "");
    }
    bool operator==(const SourceLoc& o) const {
        return file == o.file && line == o.line && col_start == o.col_start;
    }
    bool operator<(const SourceLoc& o) const {
        if (file != o.file) return file < o.file;
        if (line != o.line) return line < o.line;
        return col_start < o.col_start;
    }
};

enum class HazardKind {
    NullPointerDeref,
    OutOfBoundsAccess,
    DivisionByZero,
    SignedIntegerOverflow,
    InvalidBitShift,
    UninitializedRead,
    UseAfterFree,
    DoubleFree,
    MemoryLeak,
    InvalidFree,
    StringNotNullTerminated,
    AssertionViolation,
    CustomUndefinedBehavior
};

inline std::string hazard_kind_to_string(HazardKind k) {
    switch (k) {
        case HazardKind::NullPointerDeref:        return "Null Pointer Dereference";
        case HazardKind::OutOfBoundsAccess:       return "Out-of-Bounds Memory Access";
        case HazardKind::DivisionByZero:          return "Division By Zero";
        case HazardKind::SignedIntegerOverflow:   return "Signed Integer Overflow";
        case HazardKind::InvalidBitShift:         return "Invalid Bit Shift";
        case HazardKind::UninitializedRead:       return "Uninitialized Memory Read";
        case HazardKind::UseAfterFree:            return "Use After Free";
        case HazardKind::DoubleFree:              return "Double Free";
        case HazardKind::MemoryLeak:              return "Memory Leak";
        case HazardKind::InvalidFree:             return "Invalid Pointer Free";
        case HazardKind::StringNotNullTerminated: return "Unbounded / Non-Null-Terminated String";
        case HazardKind::AssertionViolation:      return "Assertion / Contract Violation";
        case HazardKind::CustomUndefinedBehavior: return "Undefined Behavior";
    }
    return "Unknown Hazard";
}

enum class HazardCertainty {
    DefiniteBug,   // Confirmed feasible via interval/SMT with concrete witness
    PotentialUB    // Over-approximated or complex path
};

struct HazardReport {
    HazardKind kind = HazardKind::CustomUndefinedBehavior;
    HazardCertainty certainty = HazardCertainty::DefiniteBug;
    SourceLoc loc;
    std::string function_name;
    std::string analysis_name;
    std::string predicate;            // e.g., "divisor == 0" or "a == NULL"
    std::string concrete_witness;     // e.g., "a = NULL" or "*a = 15 (divisor = 0)"
    std::string message;
    std::vector<std::string> callstack;
    uint32_t occurrences = 1;
};

struct SymbolicInputSpec {
    std::string kind;          // e.g., "sqc_interval", "sqc_make_unknown", "sqc_nondet_ptr"
    std::string target;        // variable / buffer / call expression name
    std::string range_desc;    // e.g., "[0, 10]", "unknown 772 bytes (may lack '\\0')", "{&settings, NULL}"
    std::string function_name; // enclosing C function where input is defined
    SourceLoc loc;
};

enum class Nullability : uint8_t {
    MustNull = 1,
    MustNonNull = 2,
    MayNull = 3
};

enum class RegionLiveness : uint8_t {
    Alive = 0,
    Freed = 1,
    EscapedOrOutOfScope = 2
};

} // namespace sqc
