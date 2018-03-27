/*
 * jailhouse-loader.lds - Linker script for booting Jailhouse as type-1 hypervisor
 *
 * Copyright (C) 2016 Texas Instruments Incorporated - http://www.ti.com/
 * Copyright (C) 2012 ARM Limited. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE.txt file.
 */

OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)

INPUT(boot.o)
INPUT(loader.o)
INPUT(hypervisor/lib.o)
TARGET(binary)
INPUT(hypervisor/jailhouse.bin)

INPUT(configs/arm64/qemu-arm64.cell)
INPUT(../linux/arch/arm64/boot/Image)
INPUT(../generated.dtb)

INPUT(configs/arm64/qemu-arm64-linux-demo.cell)
INPUT(configs/arm64/dts/inmate-qemu-arm64.dtb)

INPUT(inmates/tools/arm64/linux-loader.bin)

SECTIONS
{
	. = 0x80000000;
	text = .;
	.text : { *(.text*) }

	. = 0x80080000;
	kernel0 = .;
	.kernel0 : { ../linux/arch/arm64/boot/Image }
	kernel0_size = . - kernel0;

	. = 0xc0280000;
	kernel1 = .;
	.kernel1 : { ../linux/arch/arm64/boot/Image }
	kernel1_size = . - kernel1;

	. = 0x82000000;
	dtb0 = .;
	.dtb0 : { ../generated.dtb }
	dtb0_size = . - dtb0;

	. = 0xc0000000;
	dtb1 = .;
	.dtb1 : { configs/arm64/dts/inmate-qemu-arm64.dtb }
	dtb1_size = . - dtb1;

	. = 0x90000000;
	hypervisor = .;
	.hypervisor : { hypervisor/jailhouse.bin }
	hyp_size = . - hypervisor;

	vmconfig0 = .;
	.vmconfig0 : { configs/arm64/qemu-arm64.cell }
	vmconfig0_size = . - vmconfig0;

	vmconfig1 = .;
	.vmconfig1 : { configs/arm64/qemu-arm64-linux-demo.cell }
	vmconfig1_size = . - vmconfig1;

	linux_loader = .;
	.linux-loader : { inmates/tools/arm64/linux-loader.bin }
	linux_loader_size = . - linux_loader;

	.data : { *(.data*) }
	.bss : { *(.bss*) }
}
