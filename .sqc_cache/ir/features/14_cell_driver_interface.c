#include <sqc_builtin.h>
#line 1 "/home/nguyentiem/Project/Code_quality/cmcell/com/src/cell_driver_interface.c"
/**
 * @file cell_driver_interface.c
 * @copyright
 * Copyright (C) 2025 Sagemcom. All rights reserved.
 * @par Licence
 * The source code contained herein contains proprietary
 * information which is the property of Sagemcom.
 * The source code and program may be used and/or copied
 * only with the written permission of Sagemcom.
 * or in accordance with the terms and conditions
 * stipulated in the agreement/contract under which the programs
 * have been supplied.
 * @author Dev : TRAN DUC Manh
 * @brief Weakly defined driver interface functions.
 * @date 2025-04-03
 */
#include "cell_driver_interface.h"

__attribute__((weak)) cell_err_t
cell_driver_init (char * dma_buffer, size_t dma_buffer_size)
{
    return CELL_SUCCESS;
}

__attribute__((weak)) void
cell_driver_uart_irq_handler (void)
{
}

__attribute__((weak)) void
cell_driver_enable_uart_irq (void)
{
}

__attribute__((weak)) void
cell_driver_disable_uart_irq (void)
{
}

__attribute__((weak)) int
cell_driver_get_dma_index (void)
{
    return 0;
}

__attribute__((weak)) void
cell_driver_modem_reset (bool reset)
{
}

__attribute__((weak)) void
cell_uart_send_buffer (uint8_t * buffer, uint32_t size)
{
}

__attribute__((weak)) void
cell_uart_set_baudrate (uint32_t baudrate)
{
}

__attribute__((weak)) void
cell_driver_set_power_pin (bool state)
{
}
