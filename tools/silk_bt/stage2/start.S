/* SPDX-FileCopyrightText: 2026 Brian Silvestri */
/* SPDX-License-Identifier: Apache-2.0 */

/* Silk DA14681 stage 2 entry. The bootrom loads this image at the start of
 * RAM (0x07FC0000) and starts it through the vector table below. The image is
 * linked to run higher in RAM, so the reset handler is position independent:
 * it copies the image up, clears .bss and jumps to stage2_main(). The start of
 * RAM is then free for the HCI controller image. */

        .syntax unified
        .cpu cortex-m0
        .thumb

        .section .vectors, "a"
        .word   __stack_top
        .word   __reset_load
        .word   stage2_fault            /* NMI */
        .word   stage2_fault            /* HardFault */

        .text
        .thumb_func
        .global reset_pic
reset_pic:
        ldr     r0, =0x07FC0000         /* where the bootrom put us */
        ldr     r1, =__run_start
        ldr     r2, =__load_end
1:      ldr     r3, [r0]
        str     r3, [r1]
        adds    r0, #4
        adds    r1, #4
        cmp     r1, r2
        blo     1b

        ldr     r1, =__bss_start
        ldr     r2, =__bss_end
        movs    r3, #0
2:      cmp     r1, r2
        bhs     3f
        str     r3, [r1]
        adds    r1, #4
        b       2b

3:      ldr     r0, =stage2_main
        bx      r0

        .thumb_func
        .global stage2_fault
stage2_fault:
        b       stage2_fault

        .ltorg
