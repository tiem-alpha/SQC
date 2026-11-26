/**
 * @file sqc_driver.c
 * @brief SQC Verification Driver & RTOS/UART DMA Mocks for Example 04
 */
#include <sqc_builtin.h>
#include <stddef.h>
#include <stdint.h>

/* Application entry under test */
int modem_task_step(int *optional_diag_out);

/* Mock RTOS queue receive: fills buffer with symbolic DMA bytes and returns symbolic length */
SQC_OVERRIDE int rtos_queue_recv(char *buf, size_t max_len, uint32_t timeout_ms) {
    (void)timeout_ms;
    if (buf == NULL || max_len == 0) {
        return -1;
    }
    sqc_make_unknown(buf, max_len);
    int len = sqc_interval(-1, (int)max_len);
    return len;
}

/* Mock UART DMA transmit */
SQC_OVERRIDE int uart_dma_send(const char *cmd, size_t len) {
    if (cmd == NULL || len == 0) {
        return -1;
    }
    sqc_requires_valid_read(cmd, len);
    return (int)len;
}

/* Mock RTOS system tick timer */
SQC_OVERRIDE uint32_t rtos_get_tick_ms(void) {
    return (uint32_t)sqc_unsigned_int_interval(0, 60000);
}

int sqc_main(void) {
    int diag_val = 0;

    /* Exercise both NULL and non-NULL diagnostic pointer paths */
    int *diag_ptr = (int *)sqc_nondet_ptr(&diag_val, NULL);

    int rc1 = modem_task_step(diag_ptr);
    int rc2 = modem_task_step(&diag_val);

    /* White-box inspection of static variable inside rtos_dma_app.c */
    uint32_t *rx_cnt_ptr = (uint32_t *)sqc_find_variable("g_rx_packets", "rtos_dma_app.c");
    if (rx_cnt_ptr != NULL) {
        sqc_assert(*rx_cnt_ptr <= 2);
    }

    return rc1 + rc2;
}
