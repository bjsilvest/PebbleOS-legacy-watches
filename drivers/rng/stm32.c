/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/drivers/rng.h>
#include <pbl/kernel/mutex.h>

#include <stm32f4xx.h>
#include <pbl/kernel/assert.h>
#include <pbl/kernel/fatal.h>

// Give up on a peripheral that never becomes ready (e.g. not emulated).
#define MAX_POLLS 100000U

static PBL_MUTEX_DEFINE(s_rng_lock);

bool rng_rand(uint32_t *rand_out) {
  PBL_ASSERTN(rand_out);

  bool success = false;
  // maximum number of seed errors we allow before giving up:
  uint8_t attempts_left = 3;
  uint8_t non_equal_count = 0;
  uint32_t previous_value = 0;
  uint32_t polls = 0;

  pbl_mutex_lock(&s_rng_lock, PBL_FOREVER);
  RCC->AHB2ENR |= RCC_AHB2ENR_RNGEN;
  RNG->CR |= RNG_CR_RNGEN;

  while (true) {
    while (attempts_left && polls++ < MAX_POLLS) {
      const uint32_t status = RNG->SR;

      // Clock errors would indicate a programming error (RNG clock not 48 MHz).
      PBL_ASSERTN((status & (RNG_SR_CECS | RNG_SR_CEIS)) == 0);

      if (status & (RNG_SR_SECS | RNG_SR_SEIS)) {
        // On a seed error ST recommends clearing SEI and restarting the RNG.
        RNG->SR &= ~RNG_SR_SEIS;
        RNG->CR &= ~RNG_CR_RNGEN;
        RNG->CR |= RNG_CR_RNGEN;
        non_equal_count = 0;
        previous_value = 0;
        --attempts_left;
        continue;
      }

      if (status & RNG_SR_DRDY) {
        break;
      }
    }

    if (!attempts_left || polls >= MAX_POLLS) {
      break;
    }

    // FIPS 140-2 continuous test: never use the first value after enabling,
    // and fail if two consecutive values are equal.
    *rand_out = RNG->DR;
    if (*rand_out != previous_value) {
      ++non_equal_count;
      if (non_equal_count >= 2) {
        success = true;
        break;
      }
    }
    previous_value = *rand_out;
  }

  RNG->CR &= ~RNG_CR_RNGEN;
  RCC->AHB2ENR &= ~RCC_AHB2ENR_RNGEN;
  pbl_mutex_unlock(&s_rng_lock);

  return success;
}
