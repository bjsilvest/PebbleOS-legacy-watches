/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

//! STM32F205 / STM32F412 / STM32F439 clock setup.
//!
//! The stock bootloader calls RCC_DeInit() before jumping to the firmware,
//! so we start on the 16 MHz HSI. Run the PLL from it:
//!   VCO    = 16 MHz / M(16) * N(192) = 192 MHz
//!   SYSCLK = VCO / P(2)               = 96 MHz
//!   48 MHz = VCO / Q(4)               (RNG)
//!
//! VTOR is left alone; the kernel points it at its own vector table.

#include <stdint.h>

#include <stm32f4xx.h>

#define PLL_M 16
#define PLL_N 192
#define PLL_P 2
#define PLL_Q 4

#if defined(CONFIG_HAL_STM32_STM32F439)
// I2S clock for the mic (the F439 has no DFSDM), as on stock snowy:
//   PLLI2S = 16 MHz / M(16) * N(192) / R(5) = 38.4 MHz
// The mic driver starts the PLL only while it records.
#define PLLI2S_N 192
#define PLLI2S_R 5
// RM0090: 4 wait states up to 100 MHz at 1.8-2.1 V, as stock snowy used.
#define FLASH_LATENCY FLASH_ACR_LATENCY_4WS
#elif defined(CONFIG_HAL_STM32_STM32F205)
// RM0033: 16 MHz per wait state at 1.8-2.1 V, so 6 covers 96 MHz at any supply.
#define FLASH_LATENCY FLASH_ACR_LATENCY_6WS
#else
// VDD on silk is 1.8-2.0V. 6 wait states covers 100 MHz at the lowest
// supply voltage the part supports; tune once checked against RM0402.
#define FLASH_LATENCY FLASH_ACR_LATENCY_6WS
#endif

uint32_t SystemCoreClock = HSI_VALUE;

__I uint8_t AHBPrescTable[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 6, 7, 8, 9};

void SystemInit(void) {
  // Back to the reset clock state: HSI on, everything else off.
  RCC->CR |= RCC_CR_HSION;
  RCC->CFGR = 0;
  RCC->CR &= ~(RCC_CR_HSEON | RCC_CR_CSSON | RCC_CR_PLLON | RCC_CR_HSEBYP);
  RCC->CIR = 0;

  RCC->APB1ENR |= RCC_APB1ENR_PWREN;
#if !defined(CONFIG_HAL_STM32_STM32F205)
  // The F2 has no regulator voltage scaling
  PWR->CR |= PWR_CR_VOS;
#endif

  // HCLK = SYSCLK, PCLK2 = HCLK / 2, PCLK1 = HCLK / 4
  RCC->CFGR |= RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE2_DIV2 | RCC_CFGR_PPRE1_DIV4;

  RCC->PLLCFGR =
      PLL_M | (PLL_N << 6) | (((PLL_P >> 1) - 1) << 16) | RCC_PLLCFGR_PLLSRC_HSI | (PLL_Q << 24);
  RCC->CR |= RCC_CR_PLLON;
  while ((RCC->CR & RCC_CR_PLLRDY) == 0) {
  }

  FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_LATENCY;

  RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
  while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {
  }

#if defined(CONFIG_HAL_STM32_STM32F439)
  RCC->CFGR &= ~RCC_CFGR_I2SSRC;
  RCC->PLLI2SCFGR = (PLLI2S_N << 6) | (PLLI2S_R << 28);
#endif

  SystemCoreClockUpdate();
}

void SystemCoreClockUpdate(void) {
  uint32_t sysclk;

  switch (RCC->CFGR & RCC_CFGR_SWS) {
    case RCC_CFGR_SWS_HSE:
      sysclk = HSE_VALUE;
      break;
    case RCC_CFGR_SWS_PLL: {
      const uint32_t pllm = RCC->PLLCFGR & RCC_PLLCFGR_PLLM;
      const uint32_t plln = (RCC->PLLCFGR & RCC_PLLCFGR_PLLN) >> 6;
      const uint32_t pllp = (((RCC->PLLCFGR & RCC_PLLCFGR_PLLP) >> 16) + 1) * 2;
      const uint32_t src =
          (RCC->PLLCFGR & RCC_PLLCFGR_PLLSRC) == RCC_PLLCFGR_PLLSRC_HSE ? HSE_VALUE : HSI_VALUE;
      sysclk = src / pllm * plln / pllp;
      break;
    }
    default:
      sysclk = HSI_VALUE;
      break;
  }

  SystemCoreClock = sysclk >> AHBPrescTable[(RCC->CFGR & RCC_CFGR_HPRE) >> 4];
}
