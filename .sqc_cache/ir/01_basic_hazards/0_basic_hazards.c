#include <sqc_builtin.h>
#line 1 "/home/nguyentiem/Project/Code_quality/sqc/examples/01_basic_hazards/basic_hazards.c"
/**
 * @file basic_hazards.c
 * @brief Example 01: Hazard-Guided Symbolic Partitioning
 *
 * Demonstrates how `sqc` detects 4 classic C undefined behaviors in a single pass
 * without brute-force enumeration:
 *   1. Null Pointer Dereference (`a == NULL`)
 *   2. Signed Integer Overflow (`x + 10` when `x > INT_MAX - 10`)
 *   3. Division By Zero (`100 / (y - 25)` when `*a == 15`)
 *   4. Out-of-Bounds Array Access (`table[idx]` when `idx < 0 || idx >= 8`)
 */
#include <sqc_builtin.h>
#include <limits.h>
#include <stddef.h>

static int helper_b(int mode) {
    /* Pure helper whose input depends only on `mode`, independent of `a`, `x`, `y` */
    return mode * 2 + 1;
}

int compute(int *a, int mode) {
    int x = *a;                  /* Hazard 1: Null Pointer Dereference (a == NULL) */
    int y = x + 10;              /* Hazard 2: Signed Integer Overflow (x > INT_MAX - 10) */

    if (mode > 0) {
        mode = 1;
    } else {
        mode = 1;
    }

    /* Join point: both branches yield mode == 1; helper_b(1) analyzed once */
    int z = helper_b(mode);

    /* Hazard 3: Division By Zero when y - 25 == 0 (i.e. *a == 15) */
    int q = 100 / (y - 25);
    return q + z;
}

int array_access(int idx) {
    int table[8] = {10, 20, 30, 40, 50, 60, 70, 80};
    /* Hazard 4: Out-of-Bounds Memory Access when idx < 0 or idx >= 8 */
    return table[idx];
}

int sqc_main(void) {
    int val = sqc_interval(INT_MIN, INT_MAX);
    int *ptr = (int *)sqc_nondet_ptr(&val, NULL);
    int mode = sqc_interval(-10, 10);

    int r1 = compute(ptr, mode);

    int idx = sqc_interval(-2, 12);
    int r2 = array_access(idx);

    return r1 + r2;
}
