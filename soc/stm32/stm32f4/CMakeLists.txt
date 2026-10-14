# SPDX-FileCopyrightText: 2026 Brian Silvestri
# SPDX-License-Identifier: Apache-2.0

pbl_library()
pbl_include_directories(.)
pbl_library_sources(
  system_stm32f4xx.c
  init.c
  idle.c
)
