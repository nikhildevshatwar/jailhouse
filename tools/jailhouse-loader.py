#!/usr/bin/python
import os, sys, re
import argparse
from argparse import Namespace
import pexpect
import collections

GDB = "aarch64-linux-gnu-gdb"
CFG_OFFSET = 0x30000

templ_linker_script = """
OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)

INPUT(boot.o)
INPUT(loader.o)
INPUT(hypervisor/lib.o)
TARGET(binary)
INPUT(hypervisor/jailhouse.bin)

%s

SECTIONS
{
	. = 0x%x;
	text = .;
	.text : { *(.text*) }
	.bss : { *.o(.bss*) }
	.data : { *.o(.data*) }

	. = ALIGN(4096);
	. = . + 0x10000;
	loader_stack_top = .;

	. = text + 0x%x;
%s
}
"""

linker_input_files = ""
linker_section = ""
linker_section = ""

loader_strings = ""
templ_loader_header = """
struct jailhouse_loader_data jh_loader = {
	.mpidr_cpus = {  %s },
	.num_cpus = %d,

	.jailhouse_bin = (void *)0x%x,
	.jailhouse_size = 0x%x,

	.num_vms = %d,
%s
};

"""

templ_vm = """
	.vms[%d] = {
		.name = "%s",
		.config = (void *)0x%x,
		.config_size = 0x%x,
		.num_preload_images = %d,
%s
	},
"""
templ_img = """
		.imgs[%d] = {
			.source_address = %s,
			.target_address = 0x%x,
			.size = 0x%x,
		},
"""

############################### argument parsing ###############################
def copy_config(args):
	loadables = Namespace(string = args.string, binary = args.binary)
	args.config[args.config.keys()[-1]] = loadables
	args.string = []
	args.binary = []
	return args

class parse_config(argparse.Action):
	def __call__(self, parser, args, values, option_string=None):
		if (args.config == None):
			args.config = collections.OrderedDict({})
		else:
			args = copy_config(args)
		args.config[values] = None

parser = argparse.ArgumentParser(prog='jailhouse-loader',
	description='Jailhouse loader - Load multiple VMs with Jailhouse from baremetal app',
	epilog='After running this, build the jailhouse-loader.bin.\n')

parser.add_argument('-r', '--reset', dest='reset', action='store', required=True,
	help='Reset address for the baremetal loader')
parser.add_argument('--cpulist', dest='cpulist', action='store', required=True, metavar='CORESxCLUSTERS',
	help='CPU list in the format NUM-CORExNUM-CLUSTER')
parser.add_argument('-c', '--config', dest='config', action=parse_config, required=True,
	help='Cell config file')

loadables = parser.add_argument_group('loadables items')
loadables.add_argument('-s', '--string', dest='string', metavar=('STRING', 'ADDR'),
	action='append', type=str, nargs=2,
	help='Provide a loadables string. Used for loading cmdline args')
loadables.add_argument('-b', '--binary', dest='binary', metavar=('IMAGE', 'ADDR'),
	action='append', type=str, nargs=2,
	help='Provide a binary to load. No ELF intelligence, just objcopy')

args = parser.parse_args()
args = copy_config(args)

reset_addr = int(args.reset, 16)
num_vms = len(args.config)
vmconfigs = args.config.keys()
cells = {}
match = re.match("([0-9]+)x([0-9]+)$", args.cpulist)
if (match == None):
	parser.error("Invalid cpulist")
(num_cores, num_clusters) = (int(match.groups()[0]), int(match.groups()[1]))

################################################################################

############ Helper functions to extract data from cell configs ################
# Invoke GDB process to extract data from cell objects
def setup_cell_procs(cell_list):
	proclist = {}
	for cell in cell_list:
		cell_object = cell.replace(".cell", ".o")
		proc = pexpect.spawn(GDB + " " + cell_object)
		proc.expect("\(gdb\) ")
		proclist[cell] = proc
	return proclist

# Talk to the GDB process to read data structures
def read_var(proc, var):
	proc.sendline("p/x %s" % var)
	proc.expect("\(gdb\) ")
	line = proc.before.split('\r')[-2]
	val = line.split('=')[-1]
	return int(val, 16)

################################################################################

################ Helper functions to add files in Linker script ################
def fsize(fname):
	return os.stat(fname).st_size

def linker_add_configs(cell_list):
	global linker_input_files
	global linker_section
	global reset_addr
	config_info = {}
	ldaddr = reset_addr + CFG_OFFSET;
	for cell in cell_list:
		size = fsize(cell)
		config_info[cell] = (ldaddr, size)
		ldaddr = ldaddr + size

		name = os.path.basename(cell).replace(".cell", "")
		linker_input_files += "INPUT(%s)\n" % cell
		linker_section += "\t.%s : { %s }\n" % (name, cell)
	return config_info

def get_imgdata(cell):
	global vmconfigs
	global linker_input_files
	global linker_section
	global loader_strings
	loadables = args.config[cell]
	vmid = vmconfigs.index(cell)
	imgdata = ""
	idx = 0

	def translate_addr(addr, img):
		proc = cells[cell]
		if (proc == rootcell):
			nmem = read_var(proc, "config.header.root_cell.num_memory_regions")
		else:
			nmem = read_var(proc, "config.cell.num_memory_regions")
		for i in range(0, nmem):
			phys = read_var(proc, "config.mem_regions[%d].phys_start" % i)
			virt = read_var(proc, "config.mem_regions[%d].virt_start" % i)
			size = read_var(proc, "config.mem_regions[%d].size" % i)
			#print "0x%x 0x%x 0x%x" % (phys, virt, size)
			if (addr >= virt and addr < virt + size):
				addr = (addr - virt) + phys
				return addr
		print "ERROR: Can't load '%s'. Virtual memory 0x%x not mapped in cell %s" % (img, addr, cell)
		exit(1)

	if (loadables.binary == None):
		loadables.binary = []
	for item in loadables.binary:
		(binfile, virt_addr) = item
		virt_addr = int(virt_addr, 16)
		size = fsize(binfile)

		dst_addr = translate_addr(virt_addr, binfile)
		name = "img%d%d" % (vmid, idx)
		src_addr = dst_addr

		linker_input_files += "INPUT(%s)\n" % binfile
		linker_section += "\t. = 0x%x;\n\t.%s : { %s }\n\n" % (src_addr, name, binfile)

		if (vmid == 0):
			dst_addr = virt_addr
		source = "0x%x" % dst_addr
		imgdata += templ_img % (idx, source, dst_addr, size)
		idx += 1

	if (loadables.string == None):
		loadables.string = []
	for item in loadables.string:
		(text, virt_addr) = item
		virt_addr = int(virt_addr, 16)
		size = len(text)

		dst_addr = translate_addr(virt_addr, text)
		name = "str%d%d" % (vmid, idx)

		loader_strings += "char %s[] = \"%s\";\n" % (name, text)

		if (vmid == 0):
			dst_addr = virt_addr
		source = "(u64)&%s" % name
		imgdata += templ_img % (idx, source, dst_addr, size)
		idx += 1

	return (imgdata, idx)
################################################################################

################################# Main #########################################

cells = setup_cell_procs(vmconfigs)
linker_config_info = linker_add_configs(vmconfigs)

rootcell = cells[vmconfigs[0]]

# Populate CPU list
# Basic info
cpulist = ""
count = 0
for i in range(0, num_clusters):
	for j in range(0, num_cores):
		cpulist += " 0x%03x," % (i * 0x100 + j)
		count += 1

# Populate Hypervisor
hyp_addr = read_var(rootcell, "config.header.hypervisor_memory.phys_start")
hyp_size = read_var(rootcell, "config.header.hypervisor_memory.size")
linker_section += "\n\t. = 0x%x;\n\t.%s : { %s }\n\n" % (hyp_addr, "hyp", "hypervisor/jailhouse.bin")

# Populate VM details
print
print "Parsing config files..."

vmdata = ""
id = 0
for cell in vmconfigs:
	(cfg_addr, cfg_size) = linker_config_info[cell]
	(imgdata, num_imgs) = get_imgdata(cell)
	name = os.path.basename(cell).replace(".cell", "")
	vmdata += templ_vm % (id, name, cfg_addr, cfg_size, num_imgs, imgdata)
	id += 1
################################################################################

linker_script = templ_linker_script % (linker_input_files, reset_addr, CFG_OFFSET, linker_section)
loader_header = loader_strings + templ_loader_header % (cpulist, count, hyp_addr, hyp_size, num_vms, vmdata)

print
print "#" * 80
print "Generating Linker script..."
f = open('jailhouse-loader.lds', 'w+')
f.write(linker_script)
f.close()
print "#" * 80
print "Generating Loader data..."
f = open('loader-data.h', 'w+')
f.write(loader_header)
f.close()
print "#" * 80

