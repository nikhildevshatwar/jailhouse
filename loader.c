#include <stdarg.h>
#include <jailhouse/types.h>
#include <jailhouse/header.h>
#include <jailhouse/string.h>
#include <jailhouse/hypercall.h>
#include <asm/spinlock.h>


#define UART_WRITE_ADDR	0x09000000
#define GICD_BASE		0x08000000
static DEFINE_SPINLOCK(loader_printk_lock);

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

/********************** Hypervisor Loader ***************************/


#define PSCI_FN64_CPU_ON	0xc4000003
#define MAX_NUM_CPUS		16

/* This is an assembly wrapper which will setup stack and jump to the bootwrapper_entry */
extern void entry_wrapper();

void *jailhouse_fw = NULL, *linux_loader = NULL;
unsigned long linux_loader_size = 0;

struct vmload_info {
	unsigned long long *kernel;
	unsigned long kernel_size;

	unsigned long long *dtb;
	unsigned long dtb_size;

	unsigned long long *initramfs;
	unsigned long fs_size;

	char args[256];
};

struct loader_info {
	int cpus[8];
	unsigned long long *jailhouse_fw;
	unsigned long jailhouse_size;

	unsigned long long *linux_loader;
	unsigned long linux_loader_size;

	struct vmload_info vm[8];
};

void *vmconfig0 = NULL, *vmkernel0 = NULL, *vmdtb0 = NULL, *vmfs0 = NULL;
void *vmconfig1 = NULL, *vmkernel1 = NULL, *vmdtb1 = NULL, *vmfs1 = NULL;
unsigned long vmconfig0_size = 0, vmkernel0_size = 0, vmdtb0_size = 0, vmfs0_size = 0;
unsigned long vmconfig1_size = 0, vmkernel1_size = 0, vmdtb1_size = 0, vmfs1_size = 0;

int (*hyp_entry)(unsigned int);
char loader_args[64] = "kernel=0xc0280000 dtb=0xc0000000";

int enter_hyp(unsigned long cpu_id) {

	asm volatile(
		"mov	x11, 0x0\n" \
	);
	hyp_entry(cpu_id);
	return 0;
}

void bootwrapper_entry(unsigned long mpidr, unsigned long cpu_id) {

	unsigned int state = 1;
	unsigned int param;

	printk("Core %d is up\n", cpu_id);
	enter_hyp(cpu_id);

	printk("Core %d called entrypoint\n", cpu_id);
	while(1)
		;
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

int bringup_one_core(int cpu_id) {

	void *entry = entry_wrapper;
	unsigned long mpidr = ((cpu_id / 2) << 8) + (cpu_id % 2);

	mpidr = cpu_id;

	printk("Bringing up core: 0x%3x\n", mpidr);
	arm_smccc_smc(PSCI_FN64_CPU_ON, mpidr, (unsigned long)entry, 0,
		0, 0, 0, 0, NULL, NULL);
}

void load_hypervisor(int cpu_count) {

	struct jailhouse_header *hdr;
	unsigned long el2_vectors = 0x0;
	char *jailhoue_mem;
	void *cfg;
	int i;

	hdr = jailhouse_fw;
	hdr->max_cpus = cpu_count;
	hdr->arm_linux_hyp_vectors = el2_vectors;
	hdr->arm_linux_hyp_abi = HYP_STUB_ABI_OPCODE;
	hdr->debug_console_base = (void *)UART_WRITE_ADDR;
	hdr->online_cpus = cpu_count;

	hyp_entry = hdr->entry + (unsigned long)jailhouse_fw;

	printk("Jailhouse header signature = '%s'\n", hdr->signature);
	printk("arch_entry = 0x%x\n", hyp_entry);

	cfg = (char *)jailhouse_fw + hdr->core_size + cpu_count * hdr->percpu_size;

	/* HACK, TODO get the size correctly */
	memcpy(cfg, vmconfig0, 0x244);
	/* HACK, TODO get the hypervisor_memory correctly */
	*((unsigned long long *)((char *)cfg + 0x8)) = (unsigned long long)jailhouse_fw;

	for (i = 1; i < cpu_count; i++) {
		bringup_one_core(i);
	}

	enter_hyp(0);
	printk("Hypervisor enabled\n");

}

#define JAILHOUSE_HC_DISABLE                    0
#define JAILHOUSE_HC_CELL_CREATE                1
#define JAILHOUSE_HC_CELL_START                 2
#define JAILHOUSE_HC_CELL_SET_LOADABLE          3
#define JAILHOUSE_HC_CELL_DESTROY               4
#define JAILHOUSE_HC_HYPERVISOR_GET_INFO        5
#define JAILHOUSE_HC_CELL_GET_STATE             6
#define JAILHOUSE_HC_CPU_GET_INFO               7
#define JAILHOUSE_HC_DEBUG_CONSOLE_PUTC         8

int load_vms() {
	int ret = -1;

	printk("Ready to load VMs\n");

	ret = jailhouse_call_arg1(JAILHOUSE_HC_CELL_CREATE, (unsigned long long)vmconfig1);
	printk("Cell1 created %d\n", ret);

	ret = jailhouse_call_arg1(JAILHOUSE_HC_CELL_SET_LOADABLE, 0x1);
	printk("Cell1 can be loaded %d\n", ret);

	/* TODO change this harcoding */
	memcpy((void *)0xDFFF0000, linux_loader, linux_loader_size);
	memcpy((void *)0xDFFF1000, loader_args, sizeof(loader_args));
	memcpy((void *)0xc0280000, vmkernel1, vmkernel1_size);
	memcpy((void *)0xc1f63000, vmfs1, vmfs1_size);
	memcpy((void *)0xc0000000, vmdtb1, vmdtb1_size);

	printk("Cell1 binaries loaded\n");

	ret = jailhouse_call_arg1(JAILHOUSE_HC_CELL_START, 0x1);
	printk("Cell1 started %d\n", ret);


}

void gic_v3_init() {
	volatile unsigned long *gicd_ctrl = (void *)(GICD_BASE + 0x0);
	unsigned long val;

	val = *gicd_ctrl;
	val |= 1 << 1;
	*gicd_ctrl = val;
}

void init_binaries() {

	asm volatile (
	"ldr	x0, =hypervisor\n" \
	"mov	%0, x0" : "=r" ((jailhouse_fw)) \
	);

	asm volatile (
	"ldr	x0, =vmconfig0\n" \
	"mov	%0, x0" : "=r" ((vmconfig0)) \
	);

	asm volatile (
	"ldr	x0, =vmconfig0_size\n" \
	"mov	%0, x0" : "=r" ((vmconfig0_size)) \
	);

	asm volatile (
	"ldr	x0, =kernel0\n" \
	"mov	%0, x0" : "=r" ((vmkernel0)) \
	);

	asm volatile (
	"ldr	x0, =kernel0_size\n" \
	"mov	%0, x0" : "=r" ((vmkernel0_size)) \
	);

	asm volatile (
	"ldr	x0, =dtb0\n" \
	"mov	%0, x0" : "=r" ((vmdtb0)) \
	);

	asm volatile (
	"ldr	x0, =dtb0_size\n" \
	"mov	%0, x0" : "=r" ((vmdtb0_size)) \
	);

	asm volatile (
	"ldr	x0, =vmconfig1\n" \
	"mov	%0, x0" : "=r" ((vmconfig1)) \
	);

	asm volatile (
	"ldr	x0, =vmconfig1_size\n" \
	"mov	%0, x0" : "=r" ((vmconfig1_size)) \
	);

	asm volatile (
	"ldr	x0, =kernel1\n" \
	"mov	%0, x0" : "=r" ((vmkernel1)) \
	);

	asm volatile (
	"ldr	x0, =kernel1_size\n" \
	"mov	%0, x0" : "=r" ((vmkernel1_size)) \
	);

	asm volatile (
	"ldr	x0, =dtb1\n" \
	"mov	%0, x0" : "=r" ((vmdtb1)) \
	);

	asm volatile (
	"ldr	x0, =dtb1_size\n" \
	"mov	%0, x0" : "=r" ((vmdtb1_size)) \
	);

	asm volatile (
	"ldr	x0, =linux_loader\n" \
	"mov	%0, x0" : "=r" ((linux_loader)) \
	);

	asm volatile (
	"ldr	x0, =linux_loader_size\n" \
	"mov	%0, x0" : "=r" ((linux_loader_size)) \
	);

	printk("Jailhouse = 0x%x, linux_loader = 0x%x\n",
		jailhouse_fw, linux_loader);

	printk("VM0 Config = 0x%x, Kernel = 0x%x, dtb = 0x%x, fs = 0x%x\n",
		vmconfig0, vmkernel0, vmdtb0, vmfs0);

	printk("VM1 Config = 0x%x, Kernel = 0x%x, dtb = 0x%x, fs = 0x%x\n",
		vmconfig1, vmkernel1, vmdtb1, vmfs1);

}

void load_root_cell() {
	void (*entry)(unsigned long long dtb, unsigned long long x1, unsigned long long x2, unsigned long long x3);
	entry = vmkernel1;

	printk("Jumping to root cell kernel\n");
	entry((unsigned long long)vmdtb0, 0, 0, 0);
}


int type1_hypervisor() {

	int cpu_count = 4;

	printk("printk is working\n");

	gic_v3_init();

	init_binaries();

	load_hypervisor(cpu_count);

	load_vms();

	load_root_cell();

	printk("Waiting in a loop...\n");
	while(1)
		;
}
