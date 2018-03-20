#
# Jailhouse, a Linux-based partitioning hypervisor
#
# Copyright (c) Siemens AG, 2013, 2014
#
# Authors:
#  Jan Kiszka <jan.kiszka@siemens.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.  See
# the COPYING file in the top-level directory.
#

# Check make version
need := 3.82
ifneq ($(need),$(firstword $(sort $(MAKE_VERSION) $(need))))
$(error Too old make version $(MAKE_VERSION), at least $(need) required)
endif

# no recipes above this one (also no includes)
all: modules

# includes installation-related variables and definitions
include scripts/include.mk

# out-of-tree build for our kernel-module, firmware and inmates
KDIR ?= /lib/modules/`uname -r`/build

INSTALL_MOD_PATH ?= $(DESTDIR)
export INSTALL_MOD_PATH

DOXYGEN ?= doxygen

kbuild = -C $(KDIR) M=$$PWD $@

modules clean:
	$(Q)$(MAKE) $(kbuild)

CC := $(CROSS_COMPILE)gcc -I include -I include/arch/$(ARCH) -I hypervisor/include -I hypervisor/arch/$(ARCH)/include -I driver
jailhouse-loader.o: boot.S loader.c Makefile
	 $(CC) -g -c loader.c boot.S  hypervisor/lib.c

jailhouse-loader: hypervisor/jailhouse.bin jailhouse-loader.o
	echo "Creating loader"
	$(CROSS_COMPILE)$(LD) -T jailhouse-loader.lds -o $@

# documentation, build needs to be triggered explicitly
docs:
	$(DOXYGEN) Documentation/Doxyfile

modules_install: modules
	$(Q)$(MAKE) $(kbuild)

firmware_install: $(DESTDIR)$(firmwaredir) modules
	$(INSTALL_DATA) hypervisor/jailhouse*.bin $<

tool_inmates_install: $(DESTDIR)$(libexecdir)/jailhouse
	$(INSTALL_DATA) inmates/tools/$(ARCH)/*.bin $<

install: modules_install firmware_install tool_inmates_install
	$(Q)$(MAKE) -C tools $@ src=.

.PHONY: modules_install install clean firmware_install modules tools docs \
	docs_clean
