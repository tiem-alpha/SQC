#include <sqc_builtin.h>
#line 1 "/home/nguyentiem/Project/Code_quality/sqc/examples/04_rtos_uart_dma_mock/rtos_dma_app.c"
/**
 * @file rtos_dma_app.c
 * @brief Example 04: Miniature Embedded Cellular / UART DMA + RTOS Queue & Timer Task
 *
 * Inspired by real-world embedded cellular drivers (such as `cmcell`):
 *   - Hardware UART DMA and RTOS queue/timer functions are overridden in `sqc_driver.c`
 *     using `SQC_OVERRIDE`.
 *   - Uses standard `sscanf` to parse AT URC responses (`+CSQ: <rssi>,<ber>`); `sqc`
 *     analyzes `sscanf` via its built-in O(1) semantic summary with zero manual stubs!
 *   - Exposes internal static state (`g_rx_packets`, `g_modem_signal_dbm`) inspected
 *     white-box via `sqc_find_variable()` in `sqc_driver.c`.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Hardware / RTOS abstraction functions (mocked in sqc_driver.c via SQC_OVERRIDE) */
int rtos_queue_recv(char *buf, size_t max_len, uint32_t timeout_ms);
int uart_dma_send(const char *cmd, size_t len);
uint32_t rtos_get_tick_ms(void);

/* Internal static module state */
static int g_modem_signal_dbm = -113;
static uint32_t g_rx_packets = 0;
static uint32_t g_last_rx_tick = 0;

int parse_csq_urc(const char *line, int *out_rssi, int *out_ber) {
    if (line == NULL || out_rssi == NULL || out_ber == NULL) {
        return -1;
    }

    int rssi = 0;
    int ber = 0;
    /* Handled by sqc's built-in O(1) libc summary — no 500-line manual sscanf stub needed! */
    int matched = sscanf(line, "+CSQ: %d,%d", &rssi, &ber);
    if (matched == 2) {
        if (rssi >= 0 && rssi <= 31) {
            *out_rssi = rssi;
            *out_ber = ber;
            return 0;
        }
    }
    return -2;
}

int modem_task_step(int *optional_diag_out) {
    char dma_rx_buf[64];
    int rx_len = rtos_queue_recv(dma_rx_buf, sizeof(dma_rx_buf), 1000);
    if (rx_len <= 0 || rx_len >= (int)sizeof(dma_rx_buf)) {
        return -1;
    }

    /* Ensure null termination before string processing */
    dma_rx_buf[rx_len] = '\0';

    int rssi = 0;
    int ber = 0;
    int rc = parse_csq_urc(dma_rx_buf, &rssi, &ber);
    if (rc == 0) {
        g_modem_signal_dbm = -113 + (rssi * 2);
        g_rx_packets++;
        g_last_rx_tick = rtos_get_tick_ms();

        const char *ack = "OK\r\n";
        uart_dma_send(ack, strlen(ack));

        if (optional_diag_out != NULL) {
            *optional_diag_out = g_modem_signal_dbm;
        }
        return 0;
    }

    return rc;
}
