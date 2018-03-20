/*
 * Jailhouse Loader, a baremetal application to load Hypervisor and VMs
 *
 * Copyright (C) 2018 Texas Instruments Incorporated - http://www.ti.com/
 *
 * Authors:
 *  Nikhil Devshatwar <nikhil.nd@ti.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include <stdarg.h>
#include <jailhouse/types.h>
#include <jailhouse/string.h>
#include <jailhouse/header.h>
#include <jailhouse/hypercall.h>
#include <jailhouse/cell-config.h>
#include <asm/spinlock.h>
#include <jailhouse.h>
#include "loader.h"
#include "loader-data.h"

struct jailhouse_loader_data *jhl = &jh_loader;
static DEFINE_SPINLOCK(loader_printk_lock);

extern void secondary_wrapper();

/********************** printk stub ********************/
static inline void console_write(const char *buf) {
	volatile char *uart = (volatile char *)UART_WRITE_ADDR;

	while(*buf) {
		*uart = *buf++;
	}
}

#include "hypervisor/printk-core.c"

void printk(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);

	spin_lock(&loader_printk_lock);
	__vprintk(fmt, ap);
	spin_unlock(&loader_printk_lock);

	va_end(ap);
}

extern void arm_smccc_smc(unsigned long a0, unsigned long a1,
	unsigned long a2, unsigned long a3, unsigned long a4,
	unsigned long a5, unsigned long a6, unsigned long a7,
	void *res, void *quirk) {

	/* Not passing any res or quirk */
	asm volatile (
	"mov		x2, %0\n" : : "r" ((a2))
	);
	asm volatile(
	"mov		x1, %0\n" : : "r" ((a1))
	);
	asm volatile(
	"mov		x0, #0xc4000000\n" \
	"add		x0, x0, #0x3\n" \
	"mov		x3,	#0x0\n" \
	"mov		x4,	#0x0\n" \
	"mov		x5,	#0x0\n" \
	"mov		x6,	#0x0\n" \
	"mov		x7,	#0x0\n" \
	);
	asm volatile ("smc #0\n");
}

/*******************************************************/

void secondary_entry(unsigned long mpidr, unsigned long cpu_id) {

	unsigned int state = 1;
	unsigned int param;

	printk("Core %d calling entrypoint\n", cpu_id);
	jhl->hyp_entry(cpu_id);

	while(1)
		;
}

int bringup_one_core(int cpu_id) {

	void *entry = secondary_wrapper;
	unsigned long mpidr = jhl->mpidr_cpus[cpu_id];

	printk("Bringing up core: 0x%03x\n", mpidr);
	arm_smccc_smc(PSCI_FN64_CPU_ON, mpidr, (unsigned long)entry, 0,
		0, 0, 0, 0, NULL, NULL);
}

int load_hypervisor() {

	struct jailhouse_header *hdr;
	struct jailhouse_system *root_cfg, *cfg;
	unsigned long el2_vectors = 0x0;
	int i;

	hdr = jhl->jailhouse_bin;

	root_cfg = jhl->vms[0].config;

	printk("Jailhouse header signature = '%s'\n", hdr->signature);

	hdr->max_cpus = jhl->num_cpus;
	hdr->online_cpus = jhl->num_cpus;
	hdr->arm_linux_hyp_vectors = el2_vectors;
	hdr->arm_linux_hyp_abi = HYP_STUB_ABI_OPCODE;
	hdr->debug_console_base = (void *)UART_WRITE_ADDR;


	/* Copy the root cell config after the Hypervisor header */
	cfg = (struct jailhouse_system *)(
		(u8 *)jhl->jailhouse_bin
		+ hdr->core_size
		+ jhl->num_cpus * hdr->percpu_size
	      );

	printk("Copy root cell config(0x%llx) in HYP header(0x%llx)",
		jhl->vms[0].config, cfg);
	memcpy(cfg, jhl->vms[0].config, jhl->vms[0].config_size);

	cfg->hypervisor_memory.phys_start = (u64)jhl->jailhouse_bin;

	jhl->hyp_entry = (void *)((u8 *)jhl->jailhouse_bin + (u64)hdr->entry);
	printk("arch_entry = 0x%x\n", jhl->hyp_entry);

	for (i = 1; i < jhl->num_cpus; i++) {
		bringup_one_core(i);
	}

	jhl->hyp_entry(0);
	printk("Hypervisor enabled\n");

}

int load_images(struct jailhouse_loader_vminfo *vm, int i) {
	struct jailhouse_preload_image *img;
	int j;

	for (j = 0; j < vm->num_preload_images; j++) {
		img = &vm->imgs[j];

		printk("img[%d][%d] src %x dst %x size %x\n", i, j,
			img->source_address,
			img->target_address,
			img->size);
		if (img->target_address != img->source_address) {
			printk("Copying img[%d][%d] from 0x%016llx to 0x%016llx\n",
				i, j, img->source_address, img->target_address);
			memcpy((void *)img->target_address, (void *)img->source_address, img->size);
		}
	}
}

int start_nonroot_cells() {
	struct jailhouse_loader_vminfo *vm;
	struct jailhouse_cell_desc *cell;
	int i, j, ret = -1;

	for (i = 1; i < jhl->num_vms; i++) {

		vm = &jhl->vms[i];
		printk("Loading VM id=%d name=\"%s\"\n", i, vm->name);

		cell = vm->config;
		cell->id = i;
		ret = jailhouse_call_arg1(JAILHOUSE_HC_CELL_CREATE, (u64)vm->config);
		printk("  Cell created %d\n", ret);

		ret = jailhouse_call_arg1(JAILHOUSE_HC_CELL_SET_LOADABLE, i);
		printk("  Cell can be loaded %d\n", ret);

		ret = load_images(vm, i);
		printk("  Cell binaries loaded\n");

		ret = jailhouse_call_arg1(JAILHOUSE_HC_CELL_START, i);
		printk("  Cell started %d\n", ret);
	}
}

int start_root_cell() {
	struct jailhouse_loader_vminfo *vm;
	void (*root_entry)();
	int ret;

	vm = &jhl->vms[0];
	printk("Loading root VM name=\"%s\"\n", vm->name);
	ret = load_images(vm, 0);
	printk("  Cell binaries loaded\n");

	root_entry = (void (*)())vm->imgs[0].target_address;
	printk("Jumping to inmate entry = 0x%x\n", root_entry);
	root_entry();

}

int type1_hypervisor() {

	printk("Starting Jailhouse loader...\n");
	printk("Jailhouse hypervisor = 0x%x (+0x%x)\n",
		jhl->jailhouse_bin, jhl->jailhouse_size);

	printk(" num cpus = %d\n", jhl->num_cpus);
	printk(" num VMs = %d\n", jhl->num_vms);

	load_hypervisor();

	start_nonroot_cells();

	start_root_cell();

	printk("Waiting in a loop...\n");
	while(1)
		;
}
