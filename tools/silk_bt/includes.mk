# SPDX-FileCopyrightText: 2026 Brian Silvestri
# SPDX-License-Identifier: Apache-2.0
#
# Include paths of the SDK ble_external_host project (DA14681-01).

SDK_INCLUDES := \
  $(SDK)/sdk/interfaces/ble_stack \
  $(SDK)/sdk/bsp/adapters/include \
  $(SDK)/sdk/middleware/dgtl/include \
  $(SDK)/sdk/interfaces/ble/config \
  $(SDK)/sdk/interfaces/ble/include \
  $(SDK)/sdk/interfaces/ble/include/adapter \
  $(SDK)/sdk/interfaces/ble/include/manager \
  $(SDK)/sdk/interfaces/ble/src/stack/config \
  $(SDK)/sdk/interfaces/ble/src/stack/ip/ble/hl/src/host/att \
  $(SDK)/sdk/interfaces/ble/src/stack/ip/ble/hl/src/host/att/attc \
  $(SDK)/sdk/interfaces/ble/src/stack/ip/ble/hl/src/host/att/attm \
  $(SDK)/sdk/interfaces/ble/src/stack/ip/ble/hl/src/host/att/atts \
  $(SDK)/sdk/interfaces/ble/src/stack/ip/ble/hl/src/host/gap \
  $(SDK)/sdk/interfaces/ble/src/stack/ip/ble/hl/src/host/gap/gapc \
  $(SDK)/sdk/interfaces/ble/src/stack/ip/ble/hl/src/host/gap/gapm \
  $(SDK)/sdk/interfaces/ble/src/stack/ip/ble/hl/src/host/gatt \
  $(SDK)/sdk/interfaces/ble/src/stack/ip/ble/hl/src/host/gatt/gattc \
  $(SDK)/sdk/interfaces/ble/src/stack/ip/ble/hl/src/host/gatt/gattm \
  $(SDK)/sdk/interfaces/ble/src/stack/ip/ble/hl/src/host/l2c/l2cc \
  $(SDK)/sdk/interfaces/ble/src/stack/ip/ble/hl/src/host/l2c/l2cm \
  $(SDK)/sdk/interfaces/ble/src/stack/ip/ble/hl/src/host/smp \
  $(SDK)/sdk/interfaces/ble/src/stack/ip/ble/hl/src/host/smp/smpc \
  $(SDK)/sdk/interfaces/ble/src/stack/ip/ble/hl/src/host/smp/smpm \
  $(SDK)/sdk/interfaces/ble/src/stack/ip/ble/hl/src/rwble_hl \
  $(SDK)/sdk/interfaces/ble/src/stack/ip/ble/ll/src/controller/em \
  $(SDK)/sdk/interfaces/ble/src/stack/ip/ble/ll/src/controller/llc \
  $(SDK)/sdk/interfaces/ble/src/stack/ip/ble/ll/src/controller/lld \
  $(SDK)/sdk/interfaces/ble/src/stack/ip/ble/ll/src/controller/llm \
  $(SDK)/sdk/interfaces/ble/src/stack/ip/ble/ll/src/rwble \
  $(SDK)/sdk/interfaces/ble/src/stack/ip/ble/profiles \
  $(SDK)/sdk/interfaces/ble/src/stack/ip/ea/api \
  $(SDK)/sdk/interfaces/ble/src/stack/ip/em/api \
  $(SDK)/sdk/interfaces/ble/src/stack/ip/hci/api \
  $(SDK)/sdk/interfaces/ble/src/stack/ip/hci/src \
  $(SDK)/sdk/interfaces/ble/src/stack/modules/common/api \
  $(SDK)/sdk/interfaces/ble/src/stack/modules/dbg/api \
  $(SDK)/sdk/interfaces/ble/src/stack/modules/gtl/api \
  $(SDK)/sdk/interfaces/ble/src/stack/modules/gtl/src \
  $(SDK)/sdk/interfaces/ble/src/stack/modules/h4tl/api \
  $(SDK)/sdk/interfaces/ble/src/stack/modules/ke/api \
  $(SDK)/sdk/interfaces/ble/src/stack/modules/ke/src \
  $(SDK)/sdk/interfaces/ble/src/stack/modules/nvds/api \
  $(SDK)/sdk/interfaces/ble/src/stack/modules/rwip/api \
  $(SDK)/sdk/interfaces/ble/src/stack/plf/black_orca/src/arch \
  $(SDK)/sdk/interfaces/ble/src/stack/plf/black_orca/src/arch/ll/armgcc_4_8 \
  $(SDK)/sdk/interfaces/ble/src/stack/plf/black_orca/src/arch/boot/armgcc_4_8 \
  $(SDK)/sdk/interfaces/ble/src/stack/plf/black_orca/src/arch/compiler/armgcc_4_8 \
  $(SDK)/sdk/interfaces/ble/src/stack/plf/black_orca/src/build/ble-full/reg/fw \
  $(SDK)/sdk/interfaces/ble/src/stack/plf/black_orca/src/driver/flash \
  $(SDK)/sdk/interfaces/ble/src/stack/plf/black_orca/src/driver/reg \
  $(SDK)/sdk/interfaces/ble/src/stack/plf/black_orca/src/driver/rf \
  $(SDK)/sdk/interfaces/ble/src/stack/plf/black_orca/src/driver/rf/api \
  $(SDK)/sdk/bsp/include \
  $(SDK)/sdk/bsp/config \
  $(SDK)/sdk/bsp/system/sys_man/include \
  $(SDK)/sdk/bsp/free_rtos/include \
  $(SDK)/sdk/bsp/osal \
  $(SDK)/sdk/bsp/peripherals/include \
  $(SDK)/sdk/bsp/memory/include
