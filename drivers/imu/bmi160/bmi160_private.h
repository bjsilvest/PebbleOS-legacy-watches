/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stddef.h>
#include <stdint.h>

//! Set up the SPI bus and pins.
void bmi160_spi_init(void);

//! Ask the chip to accept input from the SPI bus. Required after issuing a soft reset.
void bmi160_enable_spi_mode(void);

void bmi160_begin_burst(uint8_t addr);
//! Read @p len more bytes of a burst started with bmi160_begin_burst().
void bmi160_burst_read(void *buf, size_t len);
void bmi160_end_burst(void);

uint8_t bmi160_read_reg(uint8_t reg);
uint16_t bmi160_read_16bit_reg(uint8_t reg);
void bmi160_write_reg(uint8_t reg, uint8_t value);
