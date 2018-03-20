#define UART_WRITE_ADDR		0x02800000

#define PSCI_FN64_CPU_ON	0xc4000003

#ifndef __ASSEMBLER__

#define JHL_MAX_CPUS		16
#define JHL_MAX_VMS		8
#define JHL_MAX_IMGS		8

struct jailhouse_loader_vminfo {
	char name[JAILHOUSE_CELL_NAME_MAXLEN+1];
	void *config;
	u32 config_size;

	u32 num_preload_images;
	struct jailhouse_preload_image imgs[JHL_MAX_IMGS];
};

struct jailhouse_loader_data {
	u32 mpidr_cpus[JHL_MAX_CPUS];
	u32 num_cpus;

	void *jailhouse_bin;
	u32 jailhouse_size;

	struct jailhouse_loader_vminfo vms[JHL_MAX_VMS];
	u32 num_vms;

	int (*hyp_entry)(unsigned int);
};

#endif
