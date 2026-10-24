/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/ambient/stm32_adc.h>
#ifdef CONFIG_BACKLIGHT_IS31FL3196
#include <pbl/drivers/backlight/is31fl3196.h>
#else
#include <pbl/drivers/backlight/pwm.h>
#endif
#include <pbl/drivers/i2c/stm32.h>
#include <pbl/drivers/imu/lis3dh/lis3dh.h>
#include <pbl/drivers/imu/mag3110/mag3110.h>
#include <pbl/drivers/regulator/gpio.h>
#include <pbl/drivers/temperature/stm32.h>
#include <pbl/drivers/uart/stm32.h>
#include <pbl/drivers/vibe/gpio.h>
#include <pbl/kernel/irq.h>

#include <board/board.h>

// Buttons
///////////////////////////////////////////////////////////////////////////////

const BoardConfigButton BOARD_CONFIG_BUTTON = {
  .buttons = {
    [BUTTON_ID_BACK] = {"Back", PBL_GPIO(STM32_GPIOC, 3, PBL_GPIO_ACTIVE_LOW), PBL_INPUT_KEY_BACK},
    [BUTTON_ID_UP] = {"Up", PBL_GPIO(STM32_GPIOA, 2, PBL_GPIO_ACTIVE_LOW), PBL_INPUT_KEY_UP},
    [BUTTON_ID_SELECT] =
        {"Select", PBL_GPIO(STM32_GPIOC, 6, PBL_GPIO_ACTIVE_LOW), PBL_INPUT_KEY_SELECT},
    [BUTTON_ID_DOWN] = {"Down", PBL_GPIO(STM32_GPIOA, 1, PBL_GPIO_ACTIVE_LOW), PBL_INPUT_KEY_DOWN},
  },
};

// Debug UART
///////////////////////////////////////////////////////////////////////////////

static UARTDeviceState s_dbg_uart_state;
static UARTDevice DBG_UART_DEVICE = {
  .state = &s_dbg_uart_state,
  .periph = USART3,
  .rcc_apb_periph = RCC_APB1Periph_USART3,
#if defined(CONFIG_BOARD_TINTIN_V1_5) || defined(CONFIG_BOARD_TINTIN_V2_0)
  .tx = PBL_GPIO(STM32_GPIOD, 8, 0),
  .rx = PBL_GPIO(STM32_GPIOD, 9, 0),
#else
  .tx = PBL_GPIO(STM32_GPIOC, 10, 0),
  .rx = PBL_GPIO(STM32_GPIOC, 11, 0),
#endif
  .af = GPIO_AF_USART3,
  .irqn = PBL_IRQN(USART3),
};
UARTDevice *const DBG_UART = &DBG_UART_DEVICE;
PBL_IRQ_CONNECT(USART3, 13, uart_irq_handler, DBG_UART, 0);

#ifdef CONFIG_BT_HCI_UART
// Bluetooth HCI
///////////////////////////////////////////////////////////////////////////////

// Hardware flow control holds the controller off while a byte is unread, so
// RX can stay interrupt driven.
static UARTDeviceState s_bt_hci_uart_state;
static UARTDevice BT_HCI_UART_DEVICE = {
  .state = &s_bt_hci_uart_state,
  .periph = USART1,
  .rcc_apb_periph = RCC_APB2Periph_USART1,
  .apb2 = true,
  .tx = PBL_GPIO(STM32_GPIOA, 9, 0),
  .rx = PBL_GPIO(STM32_GPIOA, 10, 0),
  .cts = PBL_GPIO(STM32_GPIOA, 11, 0),
  .rts = PBL_GPIO(STM32_GPIOA, 12, 0),
  .af = GPIO_AF_USART1,
  .irqn = PBL_IRQN(USART1),
};
UARTDevice *const BT_HCI_UART = &BT_HCI_UART_DEVICE;
PBL_IRQ_CONNECT(USART1, 12, uart_irq_handler, BT_HCI_UART, 0);
#endif

// I2C1: accelerometer (and on v2_0 the backlight's LED controller)
///////////////////////////////////////////////////////////////////////////////

PBL_I2C_STM32_DEFINE(s_i2c1, "i2c1", I2C1, RCC_APB1Periph_I2C1, 400000, PBL_GPIO(STM32_GPIOB, 8, 0),
                     GPIO_AF_I2C1, PBL_GPIO(STM32_GPIOB, 7, 0), GPIO_AF_I2C1, nullptr);
PBL_IRQ_CONNECT(I2C1_EV, 12, pbl_i2c_stm32_ev_irq_handler, &s_i2c1, 0);
PBL_IRQ_CONNECT(I2C1_ER, 12, pbl_i2c_stm32_er_irq_handler, &s_i2c1, 0);

static const struct pbl_i2c_dev I2C_SLAVE_LIS3DH = PBL_I2C_DEV(&s_i2c1.bus, 0x19);
const struct pbl_i2c_dev *const I2C_LIS3DH = &I2C_SLAVE_LIS3DH;

// Accelerometer: LIS3DH, see BOARD_CONFIG_ACCEL
PBL_LIS3DH_DEFINE(s_lis3dh, &s_i2c1.bus);
const struct pbl_accel *const ACCEL = &s_lis3dh.accel;

#ifdef CONFIG_BACKLIGHT_IS31FL3196
// Backlight and status LED: IS31FL3196, shutdown line on PB5 (active = running)
PBL_IS31FL3196_DEFINE(s_is31fl3196, &s_i2c1.bus, 0x64, .sdb = PBL_GPIO(STM32_GPIOB, 5, 0),
                      // Battery voltage drop while the status LED is lit
                      .status_led_compensation_mv = 30);
const struct pbl_backlight *const BACKLIGHT = &s_is31fl3196.backlight;
#else
// Backlight: TIM3 CH2
PBL_BACKLIGHT_PWM_DEFINE(s_backlight,
                         .pwm =
                             {
                               .output = PBL_GPIO(STM32_GPIOB, 5, 0),
                               .timer = TIM3,
                               .rcc = RCC_APB1Periph_TIM3,
                               .af = GPIO_AF_TIM3,
                               .channel = 2,
                             },
                         .max_duty_cycle_percent = 100);
const struct pbl_backlight *const BACKLIGHT = &s_backlight.backlight;
#endif

// I2C2: magnetometer (and the unused MFi chip), on a switched 2.5 V rail
///////////////////////////////////////////////////////////////////////////////

PBL_GPIO_REGULATOR_DEFINE(BOARD_RAIL_2V5, "rail_2v5", STM32_GPIOH, 0, 0, false);

PBL_I2C_STM32_DEFINE(s_i2c2, "i2c2", I2C2, RCC_APB1Periph_I2C2, 400000,
                     PBL_GPIO(STM32_GPIOB, 10, 0), GPIO_AF_I2C2, PBL_GPIO(STM32_GPIOB, 11, 0),
                     GPIO_AF_I2C2, nullptr);
PBL_IRQ_CONNECT(I2C2_EV, 12, pbl_i2c_stm32_ev_irq_handler, &s_i2c2, 0);
PBL_IRQ_CONNECT(I2C2_ER, 12, pbl_i2c_stm32_er_irq_handler, &s_i2c2, 0);

// Compass: MAG3110, data ready active high on PC4; the 2.5 V rail also feeds
// the unused Apple MFi chip and is on only while the compass is in use.
PBL_MAG3110_DEFINE(s_mag3110, &s_i2c2.bus, 0x0e, .int_gpio = PBL_GPIO(STM32_GPIOC, 4, 0),
                   .supply = &BOARD_RAIL_2V5.reg, .axis_map = {1, 0, 2}, .axis_dir = {1, -1, -1});
const struct pbl_mag *const MAG = &s_mag3110.mag;

// Vibe: switched, no strength control
#ifdef CONFIG_BOARD_TINTIN_BB2
PBL_VIBE_GPIO_DEFINE(s_vibe, PBL_GPIO(STM32_GPIOB, 13, 0));
#else
PBL_VIBE_GPIO_DEFINE(s_vibe, PBL_GPIO(STM32_GPIOB, 0, 0));
#endif
const struct pbl_vibe *const VIBE = &s_vibe.vibe;

// Ambient light: phototransistor on ADC1 channel 12, powered by PD2; raw
// levels, no lux calibration
PBL_ALS_STM32_ADC_DEFINE(s_als, nullptr, .enable = PBL_GPIO(STM32_GPIOD, 2, 0),
                         .channel = ADC_Channel_12, .pin = PBL_GPIO(STM32_GPIOC, 2, 0));
const struct pbl_als *const ALS = &s_als.als;

const struct pbl_temp *const DIE_TEMP = STM32_TEMP;

void board_early_init(void) {
  // The buttons' common line
  pbl_gpio_configure(&BOARD_BUTTON_COM, PBL_GPIO_OUTPUT_INACTIVE);
}

void board_init(void) {
}
