/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

/* Silk DA14681 HCI controller: BLE stack in pass-through mode, H4 over UART2. */

#include <stdbool.h>

#include "osal.h"
#include "resmgmt.h"
#include "ad_ble.h"
#include "hw_gpio.h"
#include "sys_clock_mgr.h"
#include "sys_power_mgr.h"
#include "sys_watchdog.h"
#include "ble_mgr.h"
#include "dgtl.h"
#include "platform_devices.h"

static OS_TASK s_init_task;

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

        pm_system_init(prv_periph_init);
        resource_init();

        /* No flow control: stay awake so UART bytes from the host are never lost. */
        pm_set_sleep_mode(pm_mode_active);

        ble_mgr_init();
        dgtl_init();

        OS_TASK_DELETE(OS_GET_CURRENT_TASK());
}

int main(void)
{
        OS_BASE_TYPE status;

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
