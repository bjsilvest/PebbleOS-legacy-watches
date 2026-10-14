# SPDX-FileCopyrightText: 2026 Brian Silvestri
# SPDX-License-Identifier: Apache-2.0
#
# Builds the silk DA14681 images: the HCI controller and the stage 2 loader.
# They are built from the Dialog DA1468x SDK 1.0.14.1081, which may not be
# redistributed, and neither may the output. See docs/boards/silk.
#
#   make -C tools/silk_bt SDK=/path/to/DA1468x_DA15xxx_SDK_1.0.14.1081

ifndef SDK
$(error Set SDK to the Dialog DA1468x SDK 1.0.14.1081 directory)
endif
CROSS ?= arm-none-eabi-
CC := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SIZE := $(CROSS)size
OUT := build

include includes.mk

CONFIG_H := config/custom_config_ram.h
DEFS := -Ddg_configBLACK_ORCA_IC_REV=BLACK_ORCA_IC_REV_A \
        -Ddg_configBLACK_ORCA_IC_STEP=BLACK_ORCA_IC_STEP_E -DRELEASE_BUILD
CPUFLAGS := -mcpu=cortex-m0 -mthumb
INCS := -Iconfig $(addprefix -I,$(SDK_INCLUDES)) -I$(SDK)/sdk/bsp/config
CFLAGS := $(CPUFLAGS) -Os -fmessage-length=0 -fsigned-char -ffunction-sections \
          -fdata-sections -std=gnu11 -Wall -Wno-unused -fcommon \
          $(DEFS) $(INCS) -include $(CONFIG_H)

SRC_DIRS := sdk/bsp/startup sdk/bsp/free_rtos sdk/bsp/adapters/src sdk/interfaces/ble/src \
            sdk/bsp/system/sys_man sdk/bsp/memory/src sdk/bsp/osal sdk/bsp/peripherals/src \
            sdk/middleware/dgtl/src
SDK_SRCS := $(foreach d,$(SRC_DIRS),$(shell find $(SDK)/$(d) \( -name '*.c' -o -name '*.S' \) \
             ))
# Never link the DA14681 charger or USB, and skip unused flash/sensor adapters.
EXCLUDE := sys_charger.c hw_usb_charger.c hw_usb.c usb_osal_wrapper.c ad_battery.c \
           ad_nvms.c ad_nvms_ves.c ad_nvms_direct.c ad_flash.c ad_nvparam.c \
           storage_flash.c
SDK_SRCS := $(filter-out $(addprefix %/,$(EXCLUDE)),$(SDK_SRCS))
SRCS := main.c $(SDK_SRCS)
OBJS := $(patsubst %,$(OUT)/obj/%.o,$(subst $(SDK)/,,$(SRCS)))

LDSCRIPTS := $(OUT)/mem.ld $(OUT)/sections.ld

all: $(OUT)/silk_bt.bin $(OUT)/silk_bt_stage2.bin

$(OUT)/%.ld: $(SDK)/sdk/bsp/ldscripts/ble_projects/%.ld.h $(CONFIG_H)
	@mkdir -p $(@D)
	$(CC) -I$(SDK)/sdk/bsp/config -Iconfig -include $(CONFIG_H) $(DEFS) -E -P -c $< -o $@

$(OUT)/obj/%.c.o: $(SDK)/%.c $(CONFIG_H)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/obj/%.S.o: $(SDK)/%.S $(CONFIG_H)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -x assembler-with-cpp -c $< -o $@

$(OUT)/obj/main.c.o: main.c $(CONFIG_H) config/platform_devices.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/silk_bt.elf: $(OBJS) $(LDSCRIPTS)
	$(CC) $(CPUFLAGS) -Wl,--gc-sections --specs=nano.specs --specs=nosys.specs \
	  -Wl,--wrap=ad_uart_write_async \
	  -L$(OUT) -L$(SDK)/sdk/bsp/misc -L$(SDK)/sdk/interfaces/ble_stack/DA14681-01-Release \
	  -T$(OUT)/mem.ld -T$(OUT)/sections.ld -Wl,-Map,$(OUT)/silk_bt.map \
	  -o $@ $(OBJS) -lble_stack_da14681_01
	$(SIZE) $@

$(OUT)/silk_bt.bin: $(OUT)/silk_bt.elf
	$(OBJCOPY) -O binary $< $@
	@ls -l $@

# Stage 2 loader: no OS, just the UART and GPIO drivers.
STAGE2_SRCS := stage2/start.S stage2/stage2.c \
               $(SDK)/sdk/bsp/peripherals/src/hw_gpio.c \
               $(SDK)/sdk/bsp/peripherals/src/hw_watchdog.c
STAGE2_OBJS := $(patsubst %,$(OUT)/stage2/%.o,$(notdir $(STAGE2_SRCS)))
vpath %.c stage2 $(SDK)/sdk/bsp/peripherals/src
vpath %.S stage2

$(OUT)/stage2/%.c.o: %.c $(CONFIG_H)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/stage2/%.S.o: %.S
	@mkdir -p $(@D)
	$(CC) $(CPUFLAGS) -c $< -o $@

$(OUT)/silk_bt_stage2.elf: $(STAGE2_OBJS) stage2/stage2.ld
	$(CC) $(CPUFLAGS) -nostdlib -nostartfiles -Wl,--gc-sections -Tstage2/stage2.ld \
	  -Wl,-Map,$(OUT)/silk_bt_stage2.map -o $@ $(STAGE2_OBJS) -lgcc
	$(SIZE) $@

$(OUT)/silk_bt_stage2.bin: $(OUT)/silk_bt_stage2.elf
	$(OBJCOPY) -O binary -j .image $< $@
	@ls -l $@

clean:
	rm -rf $(OUT)

.PHONY: all clean
