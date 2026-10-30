/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

// Embeds the framebuffer FPGA bitstream, as a raw DEFLATE stream with a
// 512-byte window.

__asm__(
    ".section .rodata.ice40lp_bitstream, \"a\"\n"
    ".global ice40lp_bitstream_start\n"
    "ice40lp_bitstream_start:\n"
    ".incbin \"" ICE40LP_BITSTREAM_PATH
    "\"\n"
    ".global ice40lp_bitstream_end\n"
    "ice40lp_bitstream_end:\n"
    ".previous\n");
