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
#include "ad_uart.h"
#include "dgtl.h"
#include "dgtl_msg.h"
#include "dgtl_pkt.h"
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
                        .baud_rate = HW_UART_BAUDRATE_1000000,
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
        /*
         * H4 flow control over spare lines of stock's SPI link: RTS on P0_0
         * (host PB0) and CTS on P0_1 (host PB1). The host only drives CTS once
         * this image runs, since the bootrom probes these pins for SPI.
         */
        hw_gpio_set_pin_function(HW_GPIO_PORT_0, HW_GPIO_PIN_0, HW_GPIO_MODE_OUTPUT,
                                 HW_GPIO_FUNC_UART2_RTSN);
        hw_gpio_set_pin_function(HW_GPIO_PORT_0, HW_GPIO_PIN_1, HW_GPIO_MODE_INPUT,
                                 HW_GPIO_FUNC_UART2_CTSN);
        /* Wake line to the host (PA10), high while data for it is pending. */
        hw_gpio_configure_pin(HW_GPIO_PORT_0, HW_GPIO_PIN_4, HW_GPIO_MODE_OUTPUT,
                              HW_GPIO_FUNC_GPIO, false);
}

/*
 * The host may deassert CTS and enter STOP mode when idle. Raise the wake line
 * for as long as a write to it is in progress, so it wakes and reasserts CTS.
 * The DGTL writes one packet at a time. Linked with --wrap.
 */
void __real_ad_uart_write_async(uart_device dev, const char *wbuf, size_t wlen,
                                ad_uart_user_cb cb, void *user_data);

static ad_uart_user_cb s_write_cb;

static void prv_write_done(void *user_data, uint16_t transferred)
{
        hw_gpio_set_inactive(HW_GPIO_PORT_0, HW_GPIO_PIN_4);
        s_write_cb(user_data, transferred);
}

void __wrap_ad_uart_write_async(uart_device dev, const char *wbuf, size_t wlen,
                                ad_uart_user_cb cb, void *user_data)
{
        s_write_cb = cb;
        hw_gpio_set_active(HW_GPIO_PORT_0, HW_GPIO_PIN_4);
        __real_ad_uart_write_async(dev, wbuf, wlen, prv_write_done, user_data);
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

        /*
         * Send a vendor event (04 ff 01 'H') through the DGTL once it owns the
         * UART, so the host can tell a silent DGTL from a stack that does not
         * answer HCI.
         */
        OS_DELAY(OS_MS_2_TICKS(20));
        dgtl_msg_t *hello = dgtl_msg_alloc(DGTL_PKT_TYPE_HCI_EVT, 4);
        hello->data[1] = 0xff;
        hello->data[2] = 1;
        hello->data[3] = 'H';
        dgtl_send(hello);

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
