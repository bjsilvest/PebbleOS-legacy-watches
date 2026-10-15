# SPDX-FileCopyrightText: 2026 Brian Silvestri
# SPDX-License-Identifier: Apache-2.0

pbl_clar_test(test_le_crypto
  SOURCES
    subsys/bluetooth/le_crypto.c
    third_party/mbedtls/mbedtls/library/aes.c
    third_party/mbedtls/mbedtls/library/platform_util.c
  INCLUDES
    third_party/mbedtls/mbedtls/include
    third_party/mbedtls/include
  DEFINES
    MBEDTLS_CONFIG_FILE="pebble_mbedtls_config.h"
)
