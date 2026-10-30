/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "kernel.h"

#include <stdint.h>
#include <string.h>

#include <pbl/kernel/init.h>

#ifdef CONFIG_COMPRESSED_DATA
#include <uzlib.h>

extern uint8_t __data_z_start[];
extern uint8_t __data_z_end[];
extern uint8_t _heap_start[];
#else
extern uint8_t __data_load_start[];
#endif
extern uint8_t __data_start[];
extern uint8_t __data_end[];
extern uint8_t __bss_start[];
extern uint8_t __bss_end[];
#if CONFIG_CCM_SIZE
extern uint8_t __ccm_bss_start[];
extern uint8_t __ccm_bss_end[];
#endif

#ifdef CONFIG_RAMFUNC
extern uint8_t __ramfunc_load_start[];
extern uint8_t __ramfunc_start[];
extern uint8_t __ramfunc_end[];
#endif

extern int main(void);

#ifdef CONFIG_COMPRESSED_DATA
//! The decoder's state goes where the heap will be; nothing uses it yet.
static void prv_inflate_data(void) {
  struct uzlib_uncomp *d = (struct uzlib_uncomp *)_heap_start;

  // No dictionary: back references read the output itself.
  uzlib_uncompress_init(d, nullptr, 0);
  d->source = __data_z_start;
  d->source_limit = __data_z_end;
  d->source_read_cb = nullptr;
  d->dest_start = __data_start;
  d->dest = __data_start;
  d->dest_limit = __data_end;
  uzlib_uncompress(d);
}
#endif

void kernel_prep_c(void) {
#ifdef CONFIG_COMPRESSED_DATA
  prv_inflate_data();
#else
  memcpy(__data_start, __data_load_start, __data_end - __data_start);
#endif
#ifdef CONFIG_RAMFUNC
  memcpy(__ramfunc_start, __ramfunc_load_start, __ramfunc_end - __ramfunc_start);
#endif
  memset(__bss_start, 0, __bss_end - __bss_start);
#if CONFIG_CCM_SIZE
  memset(__ccm_bss_start, 0, __ccm_bss_end - __ccm_bss_start);
#endif

  pbl_soc_early_init();

  main();

  PBL_ASSERTN(false);
}
