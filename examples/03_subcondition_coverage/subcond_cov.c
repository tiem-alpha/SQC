/**
 * @file subcond_cov.c
 * @brief Example 03: Sub-Condition Coverage (||, && on if/while) & 6-Color Source Highlighting
 *
 * Demonstrates how `sqc` separates branch header sub-condition coverage from body coverage:
 *   - `if (status == 1 || status == 99)` with status in [0, 5]:
 *       `status == 99` NEVER occurs -> `if` header is Yellow (COVERED_PARTIAL),
 *       while statements inside the `if` body are Green (COVERED_SAFE).
 *   - `while (count < 4 && hw_ready)` with hw_ready == 1:
 *       `hw_ready` is NEVER False -> `while` header is Yellow (COVERED_PARTIAL),
 *       while statements inside the `while` body are Green (COVERED_SAFE).
 *   - `dead_emergency_handler()` and unreachable `if (status < -100)` branch:
 *       Never reached -> Red (UNCOVERED).
 */
#include <sqc_builtin.h>

/* Unreachable function -> highlighted in Red (UNCOVERED) */
int dead_emergency_handler(int error_code) {
    int recovery = error_code * -1;
    return recovery + 10;
}

int process_sensor(int status, int hw_ready) {
    int acc = 0;

    /* Yellow (COVERED_PARTIAL): `status == 99` is never True for status in [0, 5] */
    if (status == 1 || status == 99) {
        /* Green (COVERED_SAFE): Body IS reached and proven safe */
        acc += 100;
        acc += status;
    } else {
        /* Green (COVERED_SAFE): Else body reached for status in {0, 2, 3, 4, 5} */
        acc += 10;
    }

    int count = 0;
    /* Yellow (COVERED_PARTIAL): `hw_ready` is always 1 (never False) */
    while (count < 4 && hw_ready) {
        /* Green (COVERED_SAFE): Loop body IS reached and proven safe */
        acc += count;
        count++;
    }

    /* Unreachable branch inside a reachable function -> Red (UNCOVERED) */
    if (status < -100) {
        acc += dead_emergency_handler(status);
    }

    return acc;
}

int sqc_main(void) {
    int status = sqc_interval(0, 5);
    int hw_ready = 1;
    return process_sensor(status, hw_ready);
}
