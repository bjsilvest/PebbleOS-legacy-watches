/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

/* Runs above the HCI image's load area (62K from 0x07FC0000) and below the
 * BLE ROM variables at 0x07FDC000. */
MEMORY
{
  RUN (rwx) : ORIGIN = 0x07FD9000, LENGTH = 0x2000
}

ENTRY(reset_pic)

SECTIONS
{
  .image :
  {
    __run_start = .;
    KEEP(*(.vectors))
    *(.text*)
    *(text_retained*)
    *(.rodata*)
    *(.data*)
    . = ALIGN(4);
    __load_end = .;
  } > RUN

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    __bss_start = .;
    *(.bss*)
    *(COMMON)
    *(retention_mem*)
    . = ALIGN(4);
    __bss_end = .;
  } > RUN

  __stack_top = ORIGIN(RUN) + LENGTH(RUN);
  __reset_load = reset_pic - ORIGIN(RUN) + 0x07FC0000 + 1;   /* Thumb */

  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) *(.comment) }
}
