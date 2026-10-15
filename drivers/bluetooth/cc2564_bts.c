/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

// Embeds TI's CC256xB init script, unmodified (third_party/ti_bt_sp).

__asm__(
    ".section .rodata.cc2564_bts, \"a\"\n"
    ".balign 4\n"
    ".global cc2564_bts_start\n"
    "cc2564_bts_start:\n"
    ".incbin \"" CC2564_BTS_PATH
    "\"\n"
    ".global cc2564_bts_end\n"
    "cc2564_bts_end:\n"
    ".previous\n");
