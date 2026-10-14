/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

/* Silk (Pebble 2) DA14681 HCI controller, loaded into RAM by the STM32.
 *
 * Power settings are deliberately conservative: the DA14681 charger, USB and
 * 1V8 rails stay off (the PMIC owns the battery), and the LP clock is the
 * internal RCX because the board's 32 kHz wiring is unknown.
 */
#ifndef CUSTOM_CONFIG_RAM_H_
#define CUSTOM_CONFIG_RAM_H_

#include "bsp_definitions.h"

#define CONFIG_USE_BLE
#undef CONFIG_USE_FTDF

#define dg_configUSE_LP_CLK                     LP_CLK_RCX
#define dg_configEXEC_MODE                      MODE_IS_RAM
#define dg_configCODE_LOCATION                  NON_VOLATILE_IS_NONE
#define dg_configEXT_CRYSTAL_FREQ               EXT_CRYSTAL_IS_16M

#define dg_configIMAGE_SETUP                    DEVELOPMENT_MODE
#define dg_configEMULATE_OTP_COPY               (0)

#define dg_configUSER_CAN_USE_TIMER1            (0)
#define dg_configUSE_WDOG                       (0)

#define dg_configFLASH_CONNECTED_TO             (FLASH_IS_NOT_CONNECTED)
#define dg_configFLASH_POWER_DOWN               (0)
#define dg_configPOWER_1V8_ACTIVE               (0)
#define dg_configPOWER_1V8_SLEEP                (0)
#define dg_configPOWER_1V8P                     (0)

#define dg_configBATTERY_TYPE                   (BATTERY_TYPE_NO_BATTERY)
#define dg_configUSE_USB                        (0)
#define dg_configUSE_USB_CHARGER                (0)
#define dg_configALLOW_CHARGING_NOT_ENUM        (0)
/* Referenced by SDK inline helpers only; with no battery and no USB charger
 * the DA14681 charger is never enabled. */
#define dg_configBATTERY_CHARGE_CURRENT         (0)
#define dg_configBATTERY_PRECHARGE_CURRENT      (0)
#define dg_configBATTERY_CHARGE_NTC             (1)     /* disabled */
#define dg_configUSE_ProDK                      (0)
#define dg_configUSE_SW_CURSOR                  (0)

/* Code + data must fit what the STM32 can load; no flash, no NVMS. */
#define dg_configFLASH_ADAPTER                  (0)
#define dg_configNVMS_ADAPTER                   (0)
#define dg_configNVMS_VES                       (0)
#define dg_configGPADC_ADAPTER                  (1)
#define dg_configUART_ADAPTER                   (1)
#define dg_configUSE_DGTL                       (1)

/* The DA14681 bootrom loads at most 65535 bytes, so the whole image must fit
 * below that. The rest of the RAM below the ROM variables goes to RetRAM. */
#define dg_configRAM_CODE_SIZE_AE               (62 * 1024)

#define OS_FREERTOS
/* RetRAM0 has room below the BLE retention area for a larger heap. */
#define configTOTAL_HEAP_SIZE                   (30000)

#define BLE_EXTERNAL_HOST
#define BLE_MGR_COMMAND_QUEUE_LENGTH            (16)
#define BLE_MGR_EVENT_QUEUE_LENGTH              (16)
#define AD_BLE_COMMAND_QUEUE_LENGTH             (16)
#define AD_BLE_EVENT_QUEUE_LENGTH               (16)

#define DGTL_QUEUE_ENABLE_APP                   (0)
#define DGTL_QUEUE_ENABLE_LOG                   (0)

#include "bsp_defaults.h"
#include "bsp_memory_layout.h"

#endif /* CUSTOM_CONFIG_RAM_H_ */
