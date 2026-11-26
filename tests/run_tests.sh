#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SQC_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${SQC_ROOT}/build"

SQC_BIN="${BUILD_DIR}/sqc"
UNIT_BIN="${BUILD_DIR}/test_engine_unit"

if [[ ! -x "${UNIT_BIN}" ]]; then
    echo "[ERROR] Unit test binary not found: ${UNIT_BIN}"
    echo "Build first: cmake -S ${SQC_ROOT} -B ${BUILD_DIR} -G Ninja && ninja -C ${BUILD_DIR}"
    exit 1
fi

if [[ ! -x "${SQC_BIN}" ]]; then
    echo "[ERROR] sqc binary not found: ${SQC_BIN}"
    exit 1
fi

echo "============================================================"
echo " 1. Running C++ Core Engine Unit Tests (test_engine_unit)"
echo "============================================================"
"${UNIT_BIN}"

run_example() {
    local name="$1"
    local cfg="${SQC_ROOT}/examples/${name}/sqc.json"
    local html="${SQC_ROOT}/examples/${name}/report.html"
    local expected_pattern="$2"

    echo ""
    echo "============================================================"
    echo " Running Example: ${name}"
    echo "============================================================"
    rm -f "${html}"

    local output
    output="$("${SQC_BIN}" run -c "${cfg}" -o "${html}" 2>&1)"
    echo "${output}"

    if [[ ! -s "${html}" ]]; then
        echo "[FAIL] ${name}: HTML report was not generated or is empty: ${html}"
        exit 1
    fi

    if [[ -n "${expected_pattern}" ]]; then
        if ! echo "${output}" | grep -Eqi "${expected_pattern}" && ! grep -Eqi "${expected_pattern}" "${html}"; then
            echo "[FAIL] ${name}: Expected pattern '${expected_pattern}' not found in CLI output or HTML report."
            exit 1
        fi
    fi

    echo "[PASS] ${name}: report generated at ${html}"
}

run_example "01_basic_hazards" "Null Pointer|Division By Zero|Overflow|Out-of-Bounds"
run_example "02_graph_equivalence" "Cache|Equivalence|validate_and_process"
run_example "03_subcondition_coverage" "subcond_cov|Never|Partial|Coverage"
run_example "04_rtos_uart_dma_mock" "rtos_dma_app|Coverage|sqc_main"

echo ""
echo "============================================================"
echo " ALL SQC UNIT TESTS & 4 EXAMPLES PASSED!"
echo "============================================================"
