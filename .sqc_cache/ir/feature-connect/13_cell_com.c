#include <sqc_builtin.h>
#line 1 "/home/nguyentiem/Project/Code_quality/cmcell/com/src/cell_com.c"
/**
 * @file cell_com.c
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
 * @brief Common UART communication interface using DMA and circular buffer.
 * @date 2025-04-03
 */
#include "cell_com.h"
#include "cell_driver_interface.h"
#include "cell_log.h"

#include <string.h>

// Define the circular buffer structure
typedef struct
{
    uint8_t * buffer; // Data buffer
    size_t    size;   // Size of the buffer
    size_t    tail;   // Point to the next read position
} cell_circular_buffer_t;

// This initializes all members to 0
static cell_circular_buffer_t rx_buffer = {0};

/**
 * @brief Gets the number of unread bytes in the circular buffer.
 *
 * @param None
 * @return size_t Number of unread bytes available in the buffer.
 */
static size_t
circular_buf_get_unread (void)
{
    size_t head = cell_driver_get_dma_index();
    return (head >= rx_buffer.tail) ? (head - rx_buffer.tail)
                                    : (rx_buffer.size - rx_buffer.tail + head);
}

/**
 * @brief Retrieves a byte from the circular buffer tail -> head.
 *
 * @param  data: A pointer to where the retrieved byte will be stored.
 * @return 0 if successful, or -1 if there is nothing to read
 */
static int
circular_buf_get (uint8_t * data)
{
    int status = -1;
    if (circular_buf_get_unread() > 0)
    {
        // Read from tail (FIFO behavior)
        *data = rx_buffer.buffer[rx_buffer.tail];

        // Move tail forward
        rx_buffer.tail = (rx_buffer.tail + 1) % rx_buffer.size;
        status         = 0;
    }

    return status;
}

cell_err_t
cell_com_init (char * buffer, int32_t size)
{
    rx_buffer.buffer = (uint8_t *) buffer;
    rx_buffer.size   = (size_t) size;
    rx_buffer.tail   = 0;

    return cell_driver_init((char *) rx_buffer.buffer, rx_buffer.size);
}

int32_t
cell_com_read_last_bytes (char * buffer, int32_t length, bool stop_on_char,
                          char stop_char)
{
    size_t count = 0;

    bool   stop  = false;

    while ((count < (size_t) length) && (!stop))
    {
        // Read data from the circular buffer
        char c = 0;
        if (circular_buf_get((uint8_t *) &c) == 0)
        {
            buffer[count] = c; // Store in to buffer
            count++;

            // Stop if stop_char is found
            if (stop_on_char && (c == stop_char))
            {
                stop = true;
            }
        }
        else
        {
            stop = true;
        }
    }

    return (int32_t) count;
}

void
cell_com_enable_rxne_irq (bool enable)
{
    if (enable)
    {
        cell_driver_enable_uart_irq();
    }
    else
    {
        cell_driver_disable_uart_irq();
    }
}

bool
cell_com_is_char_available (void)
{
    size_t head = cell_driver_get_dma_index();
    return (head != rx_buffer.tail);
}

void
cell_com_send_buffer (const char * buffer, uint32_t length)
{
    cell_log_debug("......TX(%d) ==> : %s", length, buffer);
    cell_uart_send_buffer((uint8_t *) buffer, length);
}

void
cell_driver_uart_irq_handler (void)
{
    cell_com_enable_rxne_irq(false);
    cell_com_notify_task_on_rxne();
}

void
cell_com_set_baudrate (uint32_t baudrate)
{
    cell_log_debug("com baudrate = %d", baudrate);
    cell_uart_set_baudrate(baudrate);
}

void
cell_com_set_power_pin (bool state)
{
    cell_log_debug("power pin = %d", state);
    cell_driver_set_power_pin(state);
}

void
cell_com_set_reset_pin (bool state)
{
    cell_log_debug("reset pin = %d", state);
    cell_driver_modem_reset(state);
}
