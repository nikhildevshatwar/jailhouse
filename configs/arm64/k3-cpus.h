/*
 * Jailhouse, a Linux-based partitioning hypervisor
 *
 * Copyright (c) 2018 Texas Instruments Incorporated - http://www.ti.com/
 *
 * Authors:
 *  Lokesh Vutla <lokeshvutla@ti.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#define BIT(x)		(1 << x)

#define K3_CORE0	BIT(0)
#define K3_CORE1	BIT(1)
#define K3_CLUSTER0	(K3_CORE0 | K3_CORE1)

#define K3_DRA8_CPUSET	K3_CLUSTER0
