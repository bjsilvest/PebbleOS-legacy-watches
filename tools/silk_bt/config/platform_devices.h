/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#ifndef PLATFORM_DEVICES_H_
#define PLATFORM_DEVICES_H_

#include <ad_gpadc.h>
#include <ad_uart.h>

/* HCI H4 to the STM32 on the bootrom UART pins (TX P1_3, RX P2_3).
 * Silk has no RTS/CTS lines, so no flow control. */
/*
 * Hardware flow control: RTS and CTS are routed in main.c. 1 Mbaud, so the
 * DGTL's queue to the host does not back up behind the air link: when both of
 * its queues fill, its task and the BLE manager wait on each other for good.
 * The host's BT_DA1468X_HCI_BAUD must match.
 */
UART_BUS(UART2, DGTL_UART, HW_UART_BAUDRATE_1000000, HW_UART_DATABITS_8, HW_UART_PARITY_NONE,
         HW_UART_STOPBITS_1, 1, 1, HW_DMA_CHANNEL_1, HW_DMA_CHANNEL_0, 0, 0)

/* Die temperature, used by the RF driver for calibration. */
GPADC_SOURCE(TEMP_SENSOR, HW_GPADC_CLOCK_INTERNAL, HW_GPADC_INPUT_MODE_SINGLE_ENDED,
             HW_GPADC_INPUT_SE_TEMPSENS, 5, false, HW_GPADC_OVERSAMPLING_1_SAMPLE,
             HW_GPADC_INPUT_VOLTAGE_UP_TO_1V2)

#endif /* PLATFORM_DEVICES_H_ */
