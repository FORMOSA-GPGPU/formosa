/*
 * SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
 *
 * SPDX-License-Identifier: Apache-2.0
 */

	.section .text
	.globl   start

#include "command_packet.h"
#include "dma.h"
#include <formosa_addr_map.h>

#define CP_FW_HOST_ADDR (FSA_CP_OFF_FW_HOST_ADDR - FSA_CP_OFF_RESET)
#define CP_FW_SIZE (FSA_CP_OFF_FW_SIZE - FSA_CP_OFF_RESET)
#define CP_FW_STATUS (FSA_CP_OFF_FW_STATUS - FSA_CP_OFF_RESET)
#define CP_FW_FAULT (FSA_CP_OFF_FW_FAULT_CODE - FSA_CP_OFF_RESET)

#define MSTATUS_MIE 0x8
#define MIE_MSIE 0x8

start:
	ebreak
	li       x1, 0
	li       x2, 0
	li       x3, 0
	li       x4, 0
	li       x5, 0
	li       x6, 0
	li       x7, 0
	li       x8, 0
	li       x9, 0
	li       x10, 0
	li       x11, 0
	li       x12, 0
	li       x13, 0
	li       x14, 0
	li       x15, 0
	li       x16, 0
	li       x17, 0
	li       x18, 0
	li       x19, 0
	li       x20, 0
	li       x21, 0
	li       x22, 0
	li       x23, 0
	li       x24, 0
	li       x25, 0
	li       x26, 0
	li       x27, 0
	li       x28, 0
	li       x29, 0
	li       x30, 0
	li       x31, 0

	/* Clear MSIP and disable interrupts while polling. */
	li       t0, FSA_CLINT_BASE
	sw       zero, FSA_CLINT_MSIP_OFFSET(t0)
	csrci    mstatus, MSTATUS_MIE

	/* Publish Reset while waiting for a Boot Descriptor. */
	li       t0, FSA_CP_CSR_BASE
	li       t1, FSA_FIRMWARE_STATUS_RESET
	sd       t1, CP_FW_STATUS(t0)

poll_descriptor:
	li       t0, FSA_CP_CSR_BASE
	ld       t1, CP_FW_SIZE(t0)       /* t1 = size */
	beqz     t1, hang
	ld       t2, CP_FW_HOST_ADDR(t0)  /* t2 = host addr */
	beqz     t2, boot_fault_desc
	/* 0 < size <= CP_TCM_SIZE */
	li       t3, FSA_CP_TCM_SIZE
	bgtu     t1, t3, boot_fault_desc

	/* Booting */
	li       t3, FSA_FIRMWARE_STATUS_BOOTING
	sd       t3, CP_FW_STATUS(t0)

	/* Program Host DMA: host → staging */
	li       t4, FSA_HOST_DMA_CSR_BASE
	sd       t2, FSA_DMA_OFF_ADDR0(t4)
	li       t3, FSA_FIRMWARE_STAGING_BASE
	sd       t3, FSA_DMA_OFF_ADDR1(t4)
	sd       t1, FSA_DMA_OFF_SIZE(t4)
	li       t3, 1
	sd       t3, FSA_DMA_OFF_START(t4)

wait_dma:
	ld       t3, FSA_DMA_OFF_STATUS(t4)
	li       t5, FSA_DMA_STATUS_DONE
	beq      t3, t5, dma_ok
	li       t5, FSA_DMA_STATUS_BUS_ERROR
	beq      t3, t5, boot_fault_dma
	li       t5, FSA_DMA_STATUS_INVALID_DESCRIPTOR
	beq      t3, t5, boot_fault_dma
	j        wait_dma

dma_ok:
	/* Host buffer lifetime ends: clear host addr then size.
	 * t1 still holds the original FW_SIZE (not clobbered by wait_dma). */
	li       t0, FSA_CP_CSR_BASE
	sd       zero, CP_FW_HOST_ADDR(t0)
	sd       zero, CP_FW_SIZE(t0)

	/* Staging → TCM, byte-accurate */
	li       t2, FSA_FIRMWARE_STAGING_BASE
	li       t4, FSA_CP_TCM_BASE
	mv       t5, t1                   /* remaining */

copy_words:
	li       t3, 8
	bltu     t5, t3, copy_tail
	ld       t3, (t2)
	sd       t3, (t4)
	addi     t2, t2, 8
	addi     t4, t4, 8
	addi     t5, t5, -8
	j        copy_words

copy_tail:
	beqz     t5, jump_fw
1:
	lbu      t3, (t2)
	sb       t3, (t4)
	addi     t2, t2, 1
	addi     t4, t4, 1
	addi     t5, t5, -1
	bnez     t5, 1b

jump_fw:
	li       t0, FSA_CP_TCM_BASE
	jalr     x0, t0

boot_fault_desc:
	li       t1, FSA_FIRMWARE_FAULT_BOOT_DESCRIPTOR
	j        boot_fault

boot_fault_dma:
	li       t1, FSA_FIRMWARE_FAULT_BOOT_DMA
	/* fall through */

boot_fault:
	li       t0, FSA_CP_CSR_BASE
	sd       t1, CP_FW_FAULT(t0)
	li       t1, FSA_FIRMWARE_STATUS_FAULT
	sd       t1, CP_FW_STATUS(t0)
	sd       zero, CP_FW_HOST_ADDR(t0)
	sd       zero, CP_FW_SIZE(t0)
	j        hang

hang:
	/* Busy-poll Boot Descriptor so first boot needs no MSIP.
	 * MSIP still re-enters start after a cooperative reboot handoff. */
	la       t0, reset_handler
	li       t1, -4
	and      t0, t0, t1
	csrw     mtvec, t0
	li       t0, MIE_MSIE
	csrs     mie, t0
	li       t0, MSTATUS_MIE
	csrs     mstatus, t0
1:
	li       t0, FSA_CP_CSR_BASE
	ld       t1, CP_FW_SIZE(t0)
	bnez     t1, start
	j        1b

	.balign  4
reset_handler:
	li       t0, FSA_CLINT_BASE
	sw       zero, FSA_CLINT_MSIP_OFFSET(t0)
	j        start
