#include <sqc_builtin.h>
#line 1 "/home/nguyentiem/Project/Code_quality/sqc/examples/02_graph_equivalence/graph_equiv.c"
/**
 * @file graph_equiv.c
 * @brief Example 02: Graph Behavioral Equivalence & Remainder Slicing
 *
 * Demonstrates how `sqc`'s GraphEquivalenceCache generalizes the maximal
 * equivalence region R_eq = [1, INT_MAX] on the first traversal of
 * `validate_and_process(packet_len, mode)`:
 *   - Call 1: packet_len in [10, 50]   -> traverses graph once, records R_eq = [1, INT_MAX]
 *   - Call 2: packet_len in [100, 500] -> FULL CACHE HIT (disjoint from [10, 50], yet inside R_eq!)
 *   - Call 3: packet_len in [-20, 300] -> PARTIAL CACHE HIT on [1, 300], slices remainder [-20, 0]
 *                                         and detects Division By Zero at packet_len == -5.
 * Also demonstrates `HelperB(mode)` being cached when caller's unrelated variables vary.
 */
#include <sqc_builtin.h>

int HelperB(int mode) {
    if (mode == 1) {
        return 42;
    }
    return 7;
}

int validate_and_process(int packet_len, int mode) {
    int base = HelperB(mode);

    if (packet_len > 0) {
        /* For any packet_len in [1, INT_MAX], packet_len + 5 >= 6 (always safe) */
        int scaled = 1000 / (packet_len + 5);
        return scaled + base;
    } else {
        /* Remainder slice [-20, 0] enters here -> Division By Zero when packet_len == -5 */
        int fallback = 1000 / (packet_len + 5);
        return fallback - base;
    }
}

int caller_pipeline_a(int unrelated_sensor, int mode) {
    int adjusted = unrelated_sensor * 3 + 1;
    /* HelperB only reads `mode`; `unrelated_sensor` and `adjusted` do not affect HelperB */
    int h = HelperB(mode);
    return h + (adjusted & 0xFF);
}

int sqc_main(void) {
    int total = 0;

    /* Branch 1: First traversal of validate_and_process with packet_len in [10, 50] */
    int len1 = sqc_interval(10, 50);
    total += validate_and_process(len1, 1);

    /* Branch 2: Disjoint range [100, 500] still falls in R_eq = [1, INT_MAX] -> FULL CACHE HIT */
    int len2 = sqc_interval(100, 500);
    total += validate_and_process(len2, 1);

    /* Branch 3: Wide range [-20, 300] -> [1, 300] hits cache; only remainder [-20, 0] is analyzed */
    int len3 = sqc_interval(-20, 300);
    total += validate_and_process(len3, 1);

    /* Compositional ModRef: HelperB(1) reused regardless of caller's unrelated variable */
    int sensor_a = sqc_interval(0, 100);
    int sensor_b = sqc_interval(200, 400);
    total += caller_pipeline_a(sensor_a, 1);
    total += caller_pipeline_a(sensor_b, 1);

    return total;
}
