/*
 * This file is part of the coreboot project.
 *
 * Copyright (C) 2012 Google Inc.
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
#include <bootstate.h>
#include <console/console.h>
#include <cbfs.h>
#include <fmap.h>
#include <ip_checksum.h>
#include <device/device.h>
#include <cbmem.h>
#include "pei_data.h"
#include "haswell.h"
#include <spi-generic.h>
#include <spi_flash.h>

/* convert a pointer to flash area into the offset inside the flash */
static inline u32 to_flash_offset(struct spi_flash *flash, void *p) {
	return ((u32)p + flash->size);
}

static struct mrc_data_container *next_mrc_block(
	struct mrc_data_container *mrc_cache)
{
	/* MRC data blocks are aligned within the region */
	u32 mrc_size = sizeof(*mrc_cache) + mrc_cache->mrc_data_size;
	if (mrc_size & (MRC_DATA_ALIGN - 1UL)) {
		mrc_size &= ~(MRC_DATA_ALIGN - 1UL);
		mrc_size += MRC_DATA_ALIGN;
	}

	u8 *region_ptr = (u8*)mrc_cache;
	region_ptr += mrc_size;
	return (struct mrc_data_container *)region_ptr;
}

static int is_mrc_cache(struct mrc_data_container *mrc_cache)
{
	return (!!mrc_cache) && (mrc_cache->mrc_signature == MRC_DATA_SIGNATURE);
}

/* Right now, the offsets for the MRC cache area are hard-coded in the
 * northbridge Kconfig if CONFIG_CHROMEOS is not set. In order to make
 * this more flexible, there are two of options:
 *  - Have each mainboard Kconfig supply a hard-coded offset
 *  - Use CBFS
 */
static u32 get_mrc_cache_region(struct mrc_data_container **mrc_region_ptr)
{
	size_t region_size = 0;
	*mrc_region_ptr = NULL;

	if (IS_ENABLED(CONFIG_CHROMEOS)) {
		struct region_device rdev;

		if (fmap_locate_area_as_rdev("RW_MRC_CACHE", &rdev) == 0) {
			region_size = region_device_sz(&rdev);
			*mrc_region_ptr = rdev_mmap_full(&rdev);
		}
	} else {
		*mrc_region_ptr = cbfs_boot_map_with_leak("mrc.cache",
							CBFS_TYPE_MRC_CACHE,
							&region_size);
	}
	return region_size;
}

/*
 * Find the largest index block in the MRC cache. Return NULL if non is
 * found.
 */
static struct mrc_data_container *find_current_mrc_cache_local
	(struct mrc_data_container *mrc_cache, u32 region_size)
{
	u32 region_end;
	u32 entry_id = 0;
	struct mrc_data_container *mrc_next = mrc_cache;

	region_end = (u32) mrc_cache + region_size;

	/* Search for the last filled entry in the region */
	while (is_mrc_cache(mrc_next)) {
		entry_id++;
		mrc_cache = mrc_next;
		mrc_next = next_mrc_block(mrc_next);
		if ((u32)mrc_next >= region_end) {
			/* Stay in the MRC data region */
			break;
		}
	}

	if (entry_id == 0) {
		printk(BIOS_ERR, "%s: No valid MRC cache found.\n", __func__);
		return NULL;
	}

	/* Verify checksum */
	if (mrc_cache->mrc_checksum !=
	    compute_ip_checksum(mrc_cache->mrc_data,
				mrc_cache->mrc_data_size)) {
		printk(BIOS_ERR, "%s: MRC cache checksum mismatch\n", __func__);
		return NULL;
	}

	printk(BIOS_DEBUG, "%s: picked entry %u from cache block\n", __func__,
	       entry_id - 1);

	return mrc_cache;
}

/* SPI code needs malloc/free.
 * Also unknown if writing flash from XIP-flash code is a good idea
 */

/* find the first empty block in the MRC cache area.
 * If there's none, return NULL.
 *
 * @mrc_cache_base - base address of the MRC cache area
 * @mrc_cache - current entry (for which we need to find next)
 * @region_size - total size of the MRC cache area
 */
static struct mrc_data_container *find_next_mrc_cache
		(struct mrc_data_container *mrc_cache_base,
		 struct mrc_data_container *mrc_cache,
		 u32 region_size)
{
	u32 region_end = (u32) mrc_cache_base + region_size;

	mrc_cache = next_mrc_block(mrc_cache);
	if ((u32)mrc_cache >= region_end) {
		/* Crossed the boundary */
		mrc_cache = NULL;
		printk(BIOS_DEBUG, "%s: no available entries found\n",
		       __func__);
	} else {
		printk(BIOS_DEBUG,
		       "%s: picked next entry from cache block at %p\n",
		       __func__, mrc_cache);
	}

	return mrc_cache;
}

static void update_mrc_cache(void *unused)
{
	printk(BIOS_DEBUG, "Updating MRC cache data.\n");
	struct mrc_data_container *current = cbmem_find(CBMEM_ID_MRCDATA);
	struct mrc_data_container *cache, *cache_base;
	u32 cache_size;

	if (!current) {
		printk(BIOS_ERR, "No MRC cache in cbmem. Can't update flash.\n");
		return;
	}
	if (current->mrc_data_size == -1) {
		printk(BIOS_ERR, "MRC cache data in cbmem invalid.\n");
		return;
	}

	cache_size = get_mrc_cache_region(&cache_base);
	if (cache_base == NULL) {
		printk(BIOS_ERR, "%s: could not find MRC cache area\n",
		       __func__);
		return;
	}

	/*
	 * we need to:
	 */
	//  0. compare MRC data to last mrc-cache block (exit if same)
	cache = find_current_mrc_cache_local(cache_base, cache_size);

	if (cache && (cache->mrc_data_size == current->mrc_data_size) &&
			(memcmp(cache, current, cache->mrc_data_size) == 0)) {
		printk(BIOS_DEBUG,
			"MRC data in flash is up to date. No update.\n");
		return;
	}

	//  1. use spi_flash_probe() to find the flash, then
	spi_init();
	struct spi_flash *flash = spi_flash_probe(0, 0);
	if (!flash) {
		printk(BIOS_DEBUG, "Could not find SPI device\n");
		return;
	}

	//  2. look up the first unused block
	if (cache)
		cache = find_next_mrc_cache(cache_base, cache, cache_size);

	/*
	 * 3. if no such place exists, erase entire mrc-cache range & use
	 * block 0. First time around the erase is not needed, but this is a
	 * small overhead for simpler code.
	 */
	if (!cache) {
		printk(BIOS_DEBUG,
		       "Need to erase the MRC cache region of %d bytes at %p\n",
		       cache_size, cache_base);

		flash->erase(flash, to_flash_offset(flash, cache_base), cache_size);

		/* we will start at the beginning again */
		cache = cache_base;
	}
	//  4. write mrc data with flash->write()
	printk(BIOS_DEBUG, "Finally: write MRC cache update to flash at %p\n",
	       cache);
	flash->write(flash, to_flash_offset(flash, cache),
		     current->mrc_data_size + sizeof(*current), current);
}

BOOT_STATE_INIT_ENTRY(BS_WRITE_TABLES, BS_ON_ENTRY, update_mrc_cache, NULL);

struct mrc_data_container *find_current_mrc_cache(void)
{
	struct mrc_data_container *cache_base;
	u32 cache_size;

	cache_size = get_mrc_cache_region(&cache_base);
	if (cache_base == NULL) {
		printk(BIOS_ERR, "%s: could not find MRC cache area\n",
		       __func__);
		return NULL;
	}

	/*
	 * we need to:
	 */
	//  0. compare MRC data to last mrc-cache block (exit if same)
	return find_current_mrc_cache_local(cache_base, cache_size);
}
