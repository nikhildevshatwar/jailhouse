/*
 * boot.S - simple register setup code for stand-alone Linux booting
 *
 * Copyright (C) 2016 Texas Instruments Incorporated - http://www.ti.com/
 * Copyright (C) 2012 ARM Limited. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE.txt file.
 */

#define MPIDR_AFFINITY_BITS     8
#define MPIDR_AFFLVL_MASK       0xff
#define MPIDR_CPU_MASK          MPIDR_AFFLVL_MASK
#define MPIDR_CLUSTER_MASK      (MPIDR_AFFLVL_MASK << MPIDR_AFFINITY_BITS)

#define CURRENTEL_EL3   (3 << 2)
#define STACK_CPUX_BASE			0x84000000
#define HVC_SET_VECTORS			0x0
#define PSR_SPSR_EL1			0x3c5
	.text

	.globl	_start
_start:

	mov		x2, #0x09000000
	mov		x1, #0x23
	str		w1, [x2]
	str		w1, [x2]

	/*
	 * EL3 initialisation
	 */
	mrs	x0, CurrentEL
	cmp	x0, #CURRENTEL_EL3

	/* Only support non EL3 */
	b.eq	unsupported

	mrs     x0, mpidr_el1
	and     x0, x0, #(MPIDR_CPU_MASK | MPIDR_CLUSTER_MASK)
	mov     x1, #0x0
	cmp     x1, x0
	/* Only support primary CPU */
	b.ne	unsupported

	bl		setup_el2

	mov		x0, #0x0
	mov		w0, #STACK_CPUX_BASE
	mov		sp, x0

	bl		type1_hypervisor

unsupported:
	/* Un supported config */
	nop
	wfi
	b unsupported

	.globl	entry_wrapper
entry_wrapper:
	mov		x0, #0x0
	mov		w0, #0xa0000000
	mov		sp, x0

	bl		setup_el2

	/*
	 * arm_write_sysreg(ICC_CTLR_EL1, 0);
	 * arm_write_sysreg(ICC_PMR_EL1, 0xf0);
	 * arm_write_sysreg(ICC_IGRPEN1_EL1, ICC_IGRPEN1_EN);
	 */
	mov		x0, #0x0
	msr		s3_0_c12_c12_4, x0
	mov		x0, #0xf0
	msr		s3_0_c4_c6_0, x0
	mov		x0, #0x1
	msr		s3_0_c12_c12_7, x0
	mov		x0, #0x1
	msr		s3_0_c12_c12_7, x0

	mrs     x0, mpidr_el1
	mov		x1, x0
	and		x1, x1, #0xff
	mov		x2, x0
	asr		x2, x2, #0x7
	and		x2, x2, #0xff
	add		x1, x2, x1

	/* setup stack */
	mov		x2, #0x0
	mov		w2, #STACK_CPUX_BASE
	mov		x3, #0x1000
	madd	x2, x1, x3, x2
	mov		sp, x2

	/* x0 has mpidr, x1 has cpu_id */
	bl		bootwrapper_entry
	ret


/* Following sets up dummy exception vectors
 * Only useful vector here is the el2_sync
 * This does only one job - Replace the vectors as provided
 */
.macro  ventry  label
	.align	7
	bl		\label
.endm

	.align	11
simple_el2_vectors:
	ventry		invalid_exception
	ventry		invalid_exception
	ventry		invalid_exception
	ventry		invalid_exception

	ventry		invalid_exception
	ventry		invalid_exception
	ventry		invalid_exception
	ventry		invalid_exception

	ventry		hvc_set_vectors
	ventry		invalid_exception
	ventry		invalid_exception
	ventry		invalid_exception

	ventry		invalid_exception
	ventry		invalid_exception
	ventry		invalid_exception
	ventry		invalid_exception


invalid_exception:
	mov		x2, #0x09000000
	mov		x1, #0x21
	str		w1, [x2]
	str		w1, [x2]
	b .

/*
 * HVC_SET_VECTORS - Set the value of the vbar_el2 register.
 *
 * @x1: Physical address of the new vector table.
 */

hvc_set_vectors:
	cmp		x0, #HVC_SET_VECTORS
	b.ne	2f
	msr		vbar_el2, x1
	mov		x0, xzr
	b		3f
2:	ldr		x0, =0xbadca11
3:	eret



#define CPTR_EL2_RES1           (3 << 12 | 0x3ff)           /* Reserved, RES1 */
#define CPACR_EL1_FPEN_EN       (3 << 20) /* SIMD and FP instruction enabled  */

#define SCTLR_EL1_RES1          (3 << 28 | 3 << 22 | 1 << 20 | 1 << 11)
                                          /* Reserved, RES1                   */
#define SCTLR_EL1_UCI_DIS       (0 << 26) /* Cache instruction disabled       */
#define SCTLR_EL1_EE_LE         (0 << 25) /* Exception Little-endian          */
#define SCTLR_EL1_WXN_DIS       (0 << 19) /* Write permission is not XN       */
#define SCTLR_EL1_NTWE_DIS      (0 << 18) /* WFE instruction disabled         */
#define SCTLR_EL1_NTWI_DIS      (0 << 16) /* WFI instruction disabled         */
#define SCTLR_EL1_UCT_DIS       (0 << 15) /* CTR_EL0 access disabled          */
#define SCTLR_EL1_DZE_DIS       (0 << 14) /* DC ZVA instruction disabled      */
#define SCTLR_EL1_ICACHE_DIS    (0 << 12) /* Instruction cache disabled       */
#define SCTLR_EL1_UMA_DIS       (0 << 9)  /* User Mask Access disabled        */
#define SCTLR_EL1_SED_EN        (0 << 8)  /* SETEND instruction enabled       */
#define SCTLR_EL1_ITD_EN        (0 << 7)  /* IT instruction enabled           */
#define SCTLR_EL1_CP15BEN_DIS   (0 << 5)  /* CP15 barrier operation disabled  */
#define SCTLR_EL1_SA0_DIS       (0 << 4)  /* Stack Alignment EL0 disabled     */
#define SCTLR_EL1_SA_DIS        (0 << 3)  /* Stack Alignment EL1 disabled     */
#define SCTLR_EL1_DCACHE_DIS    (0 << 2)  /* Data cache disabled              */
#define SCTLR_EL1_ALIGN_DIS     (0 << 1)  /* Alignment check disabled         */
#define SCTLR_EL1_MMU_DIS       (0)       /* MMU disabled                     */

#define HCR_EL2_RW_AARCH64      (1 << 31) /* EL1 is AArch64                   */
#define HCR_EL2_RW_AARCH32      (0 << 31) /* Lower levels are AArch32         */
#define HCR_EL2_HCD_DIS         (1 << 29) /* Hypervisor Call disabled         */

#define SPSR_EL_END_LE          (0 << 9)  /* Exception Little-endian          */
#define SPSR_EL_DEBUG_MASK      (1 << 9)  /* Debug exception masked           */
#define SPSR_EL_ASYN_MASK       (1 << 8)  /* Asynchronous data abort masked   */
#define SPSR_EL_SERR_MASK       (1 << 8)  /* System Error exception masked    */
#define SPSR_EL_IRQ_MASK        (1 << 7)  /* IRQ exception masked             */
#define SPSR_EL_FIQ_MASK        (1 << 6)  /* FIQ exception masked             */
#define SPSR_EL_T_A32           (0 << 5)  /* AArch32 instruction set A32      */
#define SPSR_EL_M_AARCH64       (0 << 4)  /* Exception taken from AArch64     */
#define SPSR_EL_M_AARCH32       (1 << 4)  /* Exception taken from AArch32     */
#define SPSR_EL_M_SVC           (0x3)     /* Exception taken from SVC mode    */
#define SPSR_EL_M_HYP           (0xa)     /* Exception taken from HYP mode    */
#define SPSR_EL_M_EL1H          (5)       /* Exception taken from EL1h mode   */
#define SPSR_EL_M_EL2H          (9)       /* Exception taken from EL2h mode   */


/*
 * Switch from EL2 to EL1 for ARMv8
 * @ep:     kernel entry point
 * @tmp:    temporary register
 *
 * For loading 32-bit OS, x1 is machine nr and x2 is ftaddr.
 * For loading 64-bit OS, x0 is physical address to the FDT blob.
 * They will be passed to the guest.
 */
.macro armv8_switch_to_el1_m, ep, tmp

#if 0
	/* Initialize Generic Timers */
	mrs	\tmp, cnthctl_el2
	/* Enable EL1 access to timers */
	orr	\tmp, \tmp, #(CNTHCTL_EL2_EL1PCEN_EN |\
		CNTHCTL_EL2_EL1PCTEN_EN)
	msr	cnthctl_el2, \tmp
	msr	cntvoff_el2, xzr
#endif

	/* Initilize MPID/MPIDR registers */
	mrs	\tmp, midr_el1
	msr	vpidr_el2, \tmp
	mrs	\tmp, mpidr_el1
	msr	vmpidr_el2, \tmp

	/* Disable coprocessor traps */
	mov	\tmp, #CPTR_EL2_RES1
	msr	cptr_el2, \tmp		/* Disable coprocessor traps to EL2 */
	msr	hstr_el2, xzr		/* Disable coprocessor traps to EL2 */
	mov	\tmp, #CPACR_EL1_FPEN_EN
	msr	cpacr_el1, \tmp		/* Enable FP/SIMD at EL1 */

	/* SCTLR_EL1 initialization
	 *
	 * setting RES1 bits (29,28,23,22,20,11) to 1
	 * and RES0 bits (31,30,27,21,17,13,10,6) +
	 * UCI,EE,EOE,WXN,nTWE,nTWI,UCT,DZE,I,UMA,SED,ITD,
	 * CP15BEN,SA0,SA,C,A,M to 0
	 */
	ldr	\tmp, =(SCTLR_EL1_RES1 | SCTLR_EL1_UCI_DIS |\
			SCTLR_EL1_EE_LE | SCTLR_EL1_WXN_DIS |\
			SCTLR_EL1_NTWE_DIS | SCTLR_EL1_NTWI_DIS |\
			SCTLR_EL1_UCT_DIS | SCTLR_EL1_DZE_DIS |\
			SCTLR_EL1_ICACHE_DIS | SCTLR_EL1_UMA_DIS |\
			SCTLR_EL1_SED_EN | SCTLR_EL1_ITD_EN |\
			SCTLR_EL1_CP15BEN_DIS | SCTLR_EL1_SA0_DIS |\
			SCTLR_EL1_SA_DIS | SCTLR_EL1_DCACHE_DIS |\
			SCTLR_EL1_ALIGN_DIS | SCTLR_EL1_MMU_DIS)
	msr	sctlr_el1, \tmp

	mov	\tmp, sp
	msr	sp_el1, \tmp		/* Migrate SP */
	mrs	\tmp, vbar_el2
	msr	vbar_el1, \tmp		/* Migrate VBAR */

	/* Initialize HCR_EL2 */
	ldr	\tmp, =(HCR_EL2_RW_AARCH64 | HCR_EL2_HCD_DIS)
	msr	hcr_el2, \tmp

	/* Return to the EL1_SP1 mode from EL2 */
	ldr	\tmp, =(SPSR_EL_DEBUG_MASK | SPSR_EL_SERR_MASK |\
			SPSR_EL_M_AARCH64 | SPSR_EL_M_EL1H)
	msr	spsr_el2, \tmp
	msr     elr_el2, \ep
	eret
.endm



setup_el2:
	/* Setup the exception vectors and Switch to EL1
	 * copied from  ./arch/arm64/kernel/head.S:el2_setup */
	adr		x0, simple_el2_vectors
	msr		vbar_el2, x0
	mov		x4, x30
	armv8_switch_to_el1_m x4, x6
