/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

/* Silk DA14681 HCI controller: BLE stack in pass-through mode, H4 over UART2. */

#include <stdbool.h>

#include "osal.h"
#include "resmgmt.h"
#include "ad_ble.h"
#include "hw_cpm.h"
#include "hw_gpio.h"
#include "hw_uart.h"
#include "sys_clock_mgr.h"
#include "sys_power_mgr.h"
#include "sys_watchdog.h"
#include "ble_mgr.h"
#include "dgtl.h"
#include "platform_devices.h"

static OS_TASK s_init_task;

/*
 * Bring-up markers: one byte per init step on the HCI UART at its baud rate,
 * sent before the DGTL takes the UART over, so the host can tell how far the
 * controller got.
 */
static void prv_mark(char c)
{
        static bool s_uart_up;

        if (!s_uart_up) {
                const uart_config cfg = {
                        .baud_rate = HW_UART_BAUDRATE_115200,
                        .data = HW_UART_DATABITS_8,
                        .parity = HW_UART_PARITY_NONE,
                        .stop = HW_UART_STOPBITS_1,
                        .use_fifo = 1,
                };

                hw_cpm_power_up_per_pd();
                hw_gpio_set_pin_function(HW_GPIO_PORT_1, HW_GPIO_PIN_3, HW_GPIO_MODE_OUTPUT,
                                         HW_GPIO_FUNC_UART2_TX);
                hw_uart_init(HW_UART2, &cfg);
                s_uart_up = true;
        }

        hw_uart_write(HW_UART2, (uint8_t)c);
        while (!hw_uart_is_tx_fifo_empty(HW_UART2)) {
        }
        while (hw_uart_is_busy(HW_UART2)) {
        }
}

static void prv_periph_init(void)
{
        hw_gpio_set_pin_function(HW_GPIO_PORT_1, HW_GPIO_PIN_3, HW_GPIO_MODE_OUTPUT,
                                 HW_GPIO_FUNC_UART2_TX);
        hw_gpio_set_pin_function(HW_GPIO_PORT_2, HW_GPIO_PIN_3, HW_GPIO_MODE_INPUT,
                                 HW_GPIO_FUNC_UART2_RX);
}

static void prv_system_init(void *arg)
{
        cm_sys_clk_init(sysclk_XTAL16M);
        cm_apb_set_clock_divider(apb_div1);
        cm_ahb_set_clock_divider(ahb_div1);
        cm_lp_clk_init();

        sys_watchdog_init();
        cm_sys_clk_set(sysclk_XTAL16M);
        prv_mark('c');

        pm_system_init(prv_periph_init);
        prv_mark('p');
        resource_init();

        /* No flow control: stay awake so UART bytes from the host are never lost. */
        pm_set_sleep_mode(pm_mode_active);

        ble_mgr_init();
        prv_mark('b');
        dgtl_init();

        OS_TASK_DELETE(OS_GET_CURRENT_TASK());
}

int main(void)
{
        OS_BASE_TYPE status;

        prv_mark('m');
        cm_clk_init_low_level();
        status = OS_TASK_CREATE("SysInit", prv_system_init, NULL, 1024,
                                OS_TASK_PRIORITY_HIGHEST, s_init_task);
        OS_ASSERT(status == OS_TASK_CREATE_SUCCESS);
        vTaskStartScheduler();
        for (;;) {
        }
}

void vApplicationMallocFailedHook(void)
{
        taskDISABLE_INTERRUPTS();
        for (;;) {
        }
}

void vApplicationIdleHook(void)
{
}

void vApplicationStackOverflowHook(OS_TASK pxTask, char *pcTaskName)
{
        (void)pxTask;
        (void)pcTaskName;
        taskDISABLE_INTERRUPTS();
        for (;;) {
        }
}

void vApplicationTickHook(void)
{
}
