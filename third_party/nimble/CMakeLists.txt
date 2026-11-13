# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

set(nimble mynewt-nimble)

set(includes
  # Ahead of porting/nimble/include, whose syscfg.h is a placeholder.
  port/include
  ${nimble}/nimble/include
  ${nimble}/nimble/host/include
  ${nimble}/nimble/host/services/ans/include
  ${nimble}/nimble/host/services/bas/include
  ${nimble}/nimble/host/services/bleuart/include
  ${nimble}/nimble/host/services/gap/include
  ${nimble}/nimble/host/services/gatt/include
  ${nimble}/nimble/host/services/ias/include
  ${nimble}/nimble/host/services/dis/include
  ${nimble}/nimble/host/services/lls/include
  ${nimble}/nimble/host/services/tps/include
  ${nimble}/nimble/host/util/include
  ${nimble}/nimble/transport/include
  ${nimble}/nimble/transport/common/hci_h4/include
  ${nimble}/porting/nimble/include
)

file(GLOB sources CONFIGURE_DEPENDS
  ${nimble}/porting/nimble/src/*.c
  ${nimble}/nimble/src/*.c
  ${nimble}/nimble/host/src/*.c
  ${nimble}/nimble/host/util/src/*.c
  ${nimble}/nimble/host/services/gap/src/*.c
  ${nimble}/nimble/host/services/gatt/src/*.c
  ${nimble}/nimble/transport/src/*.c
  ${nimble}/nimble/transport/common/hci_h4/src/*.c
  port/src/npl_os_pebble.c
)
if(CONFIG_NIMBLE_BLE_SVC_BAS)
  list(APPEND sources ${nimble}/nimble/host/services/bas/src/ble_svc_bas.c)
endif()
if(CONFIG_NIMBLE_BLE_SVC_DIS)
  list(APPEND sources ${nimble}/nimble/host/services/dis/src/ble_svc_dis.c)
endif()
pbl_filter_sources(sources EXCLUDE
  # has a duplicate define, and is not needed anyway
  ${nimble}/nimble/host/src/ble_gatts_lcl.c
  ${nimble}/porting/nimble/src/hal_timer.c
)

# assert() without file, function and expression strings; the LR is kept
set(defines __ASSERT_QUIET)

if(CONFIG_NIMBLE_CONTROLLER)
  file(GLOB controller_sources CONFIGURE_DEPENDS ${nimble}/nimble/controller/src/*.c)
  list(APPEND sources ${controller_sources})
  list(PREPEND includes ${nimble}/nimble/controller/include)
  list(APPEND defines NIMBLE_CFG_CONTROLLER=1)
  pbl_compile_definitions(NIMBLE_CFG_CONTROLLER=1)
endif()

if(CONFIG_NIMBLE_DRIVER_NRF5X)
  file(GLOB driver_sources CONFIGURE_DEPENDS
    ${nimble}/nimble/drivers/nrf5x/src/*.c
    ${nimble}/nimble/drivers/nrf5x/src/nrf52/*.c
  )
  list(APPEND sources ${driver_sources})
  list(PREPEND includes ${nimble}/nimble/drivers/nrf5x/include)
endif()

if(CONFIG_BT_HCI_NRF52)
  list(APPEND sources
    ${nimble}/porting/nimble/src/hal_timer.c
    port/src/nrf52.c
  )
  list(APPEND defines FREERTOS=1 PEBBLEOS NRF52_SERIES)
elseif(CONFIG_BT_HCI_SF32LB52)
  list(APPEND sources
    transport/hci_sf32lb52.c
    ../nonfree/pebbleos-nonfree/sf32lb52/lcpu_52x.c
  )
  if(CONFIG_NIMBLE_HCI_SF32LB52_TRACE_BINARY)
    pbl_compile_definitions(NIMBLE_HCI_SF32LB52_TRACE NIMBLE_HCI_SF32LB52_TRACE_BINARY)
  elseif(CONFIG_NIMBLE_HCI_SF32LB52_TRACE_LOG)
    pbl_compile_definitions(NIMBLE_HCI_SF32LB52_TRACE NIMBLE_HCI_SF32LB52_TRACE_LOG)
  endif()
elseif(CONFIG_BT_HCI_UART)
  list(APPEND sources transport/hci_uart.c)
elseif(CONFIG_BT_HCI_FAKE)
  list(APPEND sources transport/hci_fake.c)
endif()

if(CONFIG_NIMBLE_TRNG)
  list(APPEND sources port/src/trng.c)
endif()

if(CONFIG_NIMBLE_BLE_SM_COMPACT_AES)
  list(APPEND sources port/src/aes_compact.c)
endif()

if(CONFIG_NIMBLE_BLE_SM_SC_MICRO_ECC)
  list(APPEND sources port/src/ble_sm_alg_uecc.c ../micro-ecc/micro-ecc/uECC.c)
  list(APPEND includes ../micro-ecc/micro-ecc)
  list(APPEND defines
    uECC_OPTIMIZATION_LEVEL=1
    uECC_SUPPORTS_secp160r1=0
    uECC_SUPPORTS_secp192r1=0
    uECC_SUPPORTS_secp224r1=0
    uECC_SUPPORTS_secp256k1=0
    uECC_SUPPORT_COMPRESSED_POINT=0
  )
  # ble_sm_alg.c keeps the rest of the toolbox; these come from the port file
  set_source_files_properties(${nimble}/nimble/host/src/ble_sm_alg.c PROPERTIES
    COMPILE_DEFINITIONS
    "ble_sm_alg_gen_dhkey=ble_sm_alg_gen_dhkey_mbedtls;ble_sm_alg_gen_key_pair=ble_sm_alg_gen_key_pair_mbedtls;mbedtls_cipher_cmac=ble_sm_port_cmac;mbedtls_cipher_info_from_type=ble_sm_port_cipher_info"
  )
endif()

pbl_include_directories(${includes})

pbl_library_named(nimble)
pbl_library_sources(${sources})
pbl_library_compile_definitions(${defines})
pbl_library_compile_options(
  -Wno-pointer-arith
  -Wno-unused-function
  -Wno-sign-compare
  -Wno-old-style-declaration
  -Wno-unused-but-set-global
  -Wimplicit-fallthrough=1
)
