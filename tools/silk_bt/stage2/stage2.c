/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

/* Silk DA14681 stage 2: receive the HCI controller image from the STM32 at
 * 1 Mbaud on UART2 (TX P1_3, RX P2_3) into the start of RAM and run it.
 *
 * Protocol (all little endian):
 *   host -> "PBL2" len:u32 crc32:u32   stage2 -> 'K' (or 'E' if len is bad)
 *   host -> len bytes                  stage2 -> 'O' and runs the image,
 *                                                or 'C' on CRC mismatch
 * After 'E' or 'C' stage 2 waits for "PBL2" again. */

#include <stdint.h>

#include "sdk_defs.h"
#include "hw_gpio.h"
#include "hw_watchdog.h"

#define IMAGE_BASE      (0x07FC0000UL)
#define IMAGE_MAX       (62UL * 1024UL)

static uint32_t prv_crc32(const uint8_t *data, uint32_t len)
{
        uint32_t crc = 0xFFFFFFFFUL;

        while (len--) {
                crc ^= *data++;
                for (int i = 0; i < 8; i++) {
                        crc = (crc >> 1) ^ (0xEDB88320UL & -(crc & 1));
                }
        }

        return ~crc;
}

/* UART2 at 1 Mbaud, 8N1, FIFOs on, no interrupts, no flow control. The
 * divisor assumes the 16 MHz peripheral clock the bootrom runs on. */
static void prv_uart_init(void)
{
        CRG_PER->CLK_PER_REG |= CRG_PER_CLK_PER_REG_UART_ENABLE_Msk;

        UART2->UART2_SFE_REG = UART2_UART2_SFE_REG_UART_SHADOW_FIFO_ENABLE_Msk;
        UART2->UART2_SRR_REG = (1 << UART2_UART2_SRR_REG_UART_RFR_Pos) |
                               (1 << UART2_UART2_SRR_REG_UART_XFR_Pos);

        UART2->UART2_LCR_REG = 0x80;            /* DLAB */
        UART2->UART2_DLF_REG = 0x00;
        UART2->UART2_RBR_THR_DLL_REG = 0x01;
        UART2->UART2_IER_DLH_REG = 0x00;
        UART2->UART2_LCR_REG = 0x03;            /* 8 data bits, DLAB off */
        UART2->UART2_IER_DLH_REG = 0x00;        /* no interrupts */
        UART2->UART2_MCR_REG = 0x00;
}

static uint8_t prv_getc(void)
{
        while (!(UART2->UART2_LSR_REG & UART2_UART2_LSR_REG_UART_DR_Msk)) {
        }
        return UART2->UART2_RBR_THR_DLL_REG;
}

static void prv_putc(uint8_t c)
{
        while (!(UART2->UART2_LSR_REG & UART2_UART2_LSR_REG_UART_THRE_Msk)) {
        }
        UART2->UART2_RBR_THR_DLL_REG = c;
}

static uint32_t prv_get_u32(void)
{
        uint32_t v = 0;

        for (int i = 0; i < 4; i++) {
                v |= (uint32_t)prv_getc() << (8 * i);
        }

        return v;
}

static void prv_wait_magic(void)
{
        static const uint8_t magic[4] = { 'P', 'B', 'L', '2' };
        int matched = 0;

        while (matched < 4) {
                uint8_t c = prv_getc();
                if (c == magic[matched]) {
                        matched++;
                } else {
                        matched = (c == magic[0]) ? 1 : 0;
                }
        }
}

/*
 * Like stock silk's loader, start the image with a system reset rather than a
 * jump: the bootrom left address 0 remapped to SysRAM, where the image's
 * vector table now is, and the SDK startup code expects reset state.
 */
static void __attribute__((noreturn)) prv_run_image(void)
{
        while (UART2->UART2_USR_REG & UART2_UART2_USR_REG_UART_BUSY_Msk) {
        }

        __disable_irq();
        NVIC->ICER[0] = ~0UL;
        NVIC->ICPR[0] = ~0UL;
        NVIC_SystemReset();
        __builtin_unreachable();
}

void __attribute__((noreturn)) stage2_main(void)
{
        hw_watchdog_freeze();

        hw_gpio_set_pin_function(HW_GPIO_PORT_1, HW_GPIO_PIN_3, HW_GPIO_MODE_OUTPUT,
                                 HW_GPIO_FUNC_UART2_TX);
        hw_gpio_set_pin_function(HW_GPIO_PORT_2, HW_GPIO_PIN_3, HW_GPIO_MODE_INPUT,
                                 HW_GPIO_FUNC_UART2_RX);

        prv_uart_init();

        for (;;) {
                prv_wait_magic();
                const uint32_t len = prv_get_u32();
                const uint32_t crc = prv_get_u32();

                if (len < 8 || len > IMAGE_MAX) {
                        prv_putc('E');
                        continue;
                }
                prv_putc('K');

                uint8_t *dst = (uint8_t *)IMAGE_BASE;
                for (uint32_t i = 0; i < len; i++) {
                        dst[i] = prv_getc();
                }

                if (prv_crc32(dst, len) != crc) {
                        prv_putc('C');
                        continue;
                }

                prv_putc('O');
                prv_run_image();
        }
}
