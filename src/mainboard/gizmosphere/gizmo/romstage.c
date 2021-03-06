/*
 * This file is part of the coreboot project.
 *
 * Copyright (C) 2011 Advanced Micro Devices, Inc.
 * Copyright (C) 2013 Sage Electronic Engineering, LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc.
 */

#include <stdint.h>
#include <string.h>
#include <device/pci_def.h>
#include <device/pci_ids.h>
#include <arch/acpi.h>
#include <arch/io.h>
#include <arch/stages.h>
#include <device/pnp_def.h>
#include <arch/cpu.h>
#include <cpu/x86/lapic.h>
#include <console/console.h>
#include <commonlib/loglevel.h>
#include <cpu/x86/mtrr.h>
#include <cpu/amd/car.h>
#include <northbridge/amd/agesa/agesawrapper.h>
#include <cpu/x86/bist.h>
#include <cpu/x86/cache.h>
#include <sb_cimx.h>
#include "SBPLATFORM.h"
#include "cbmem.h"
#include <cpu/amd/mtrr.h>
#include <cpu/amd/agesa/s3_resume.h>

#define MSR_MTRR_VARIABLE_BASE6   0x020C
#define MSR_MTRR_VARIABLE_MASK6   0x020D
#define MSR_PSTATE_CONTROL        0xC0010062


void cache_as_ram_main(unsigned long bist, unsigned long cpu_init_detectedx)
{
	u32 val;
	msr_t msr;

	/*
	 * All cores: allow caching of flash chip code and data
	 * (there are no cache-as-ram reliability concerns with family 14h)
	 */
	msr.lo = ((0x0100000000ull - CACHE_ROM_SIZE) | 5) & 0xFFFFFFFF;
	msr.hi = ((0x0100000000ull - CACHE_ROM_SIZE) | 5) >> 32;
	wrmsr (MSR_MTRR_VARIABLE_BASE6, msr);

	msr.lo = ((0x1000000000ull - CACHE_ROM_SIZE) | 0x800) & 0xFFFFFFFF;
	msr.hi = ((0x1000000000ull - CACHE_ROM_SIZE) | 0x800) >> 32;
	wrmsr (MSR_MTRR_VARIABLE_MASK6, msr);

	/* All cores: set pstate 0 (1600 MHz) early to save a few ms of boot time */
	msr.lo = 0;
	msr.hi = 0;
	wrmsr (MSR_PSTATE_CONTROL, msr);

	amd_initmmio();

	if (!cpu_init_detectedx && boot_cpu()) {
		post_code(0x30);
		sb_Poweron_Init();

		post_code(0x31);

		console_init();
	}

	/* Halt if there was a built in self test failure */
	post_code(0x34);
	report_bist_failure(bist);

	/* Load MPB */
	val = cpuid_eax(1);
	printk(BIOS_DEBUG, "BSP Family_Model: %08x\n", val);
	printk(BIOS_DEBUG, "cpu_init_detectedx = %08lx\n", cpu_init_detectedx);

	post_code(0x37);
	agesawrapper_amdinitreset();

	post_code(0x39);
	agesawrapper_amdinitearly();

	int s3resume = acpi_is_wakeup_s3();
	if (!s3resume) {
		post_code(0x40);
		agesawrapper_amdinitpost();

		post_code(0x42);
		agesawrapper_amdinitenv();
		amd_initenv();

	} else { 			/* S3 detect */
		printk(BIOS_INFO, "S3 detected\n");

		post_code(0x60);
		agesawrapper_amdinitresume();

		agesawrapper_amds3laterestore();

		post_code(0x61);
		prepare_for_resume();
	}

	post_code(0x50);
	copy_and_run();
	printk(BIOS_ERR, "Error: copy_and_run() returned!\n");

	post_code(0x54);	/* Should never see this post code. */
}
