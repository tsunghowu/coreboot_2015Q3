##
## This file is part of the coreboot project.
##
## Copyright (C) 2014 The ChromiumOS Authors.  All rights reserved.
##
## This program is free software; you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation; version 2 of the License.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, write to the Free Software
## Foundation, Inc.
##

libverstage-generic-ccopts += -D__PRE_RAM__ -D__VERSTAGE__
verstage-generic-ccopts += -D__PRE_RAM__ -D__VERSTAGE__

bootblock-y += vboot_loader.c
romstage-y += vboot_loader.c
ramstage-y += vboot_loader.c
verstage-y += vboot_loader.c

bootblock-y += ../vboot_common.c
verstage-y += ../vboot_common.c
romstage-y += ../vboot_common.c
ramstage-y += ../vboot_common.c

bootblock-y += common.c
libverstage-y += vboot_logic.c
verstage-y += common.c
verstage-y += verstage.c
ifeq (${CONFIG_VBOOT2_MOCK_SECDATA},y)
libverstage-y += secdata_mock.c
else
libverstage-y += antirollback.c
endif
romstage-y += vboot_handoff.c common.c

ramstage-y += common.c

ifeq ($(CONFIG_SEPARATE_VERSTAGE),y)
VB_FIRMWARE_ARCH := $(ARCHDIR-$(ARCH-verstage-y))
else
ifeq ($(CONFIG_VBOOT_STARTS_IN_BOOTBLOCK),y)
VB_FIRMWARE_ARCH := $(ARCHDIR-$(ARCH-bootblock-y))
else
VB_FIRMWARE_ARCH := $(ARCHDIR-$(ARCH-romstage-y))
endif
endif # CONFIG_SEPARATE_VERSTAGE

VB2_LIB = $(obj)/external/vboot_reference/vboot_fw20.a
VBOOT_CFLAGS += $(patsubst -I%,-I$(top)/%, $(filter-out -I$(obj), $(filter-out -include $(src)/include/kconfig.h, $(CPPFLAGS_libverstage))))
VBOOT_CFLAGS += $(CFLAGS_libverstage)
VBOOT_CFLAGS += $(libverstage-c-ccopts)
VBOOT_CFLAGS += -I$(abspath $(obj)) -include $(top)/src/include/kconfig.h -Wno-missing-prototypes
VBOOT_CFLAGS += -DVBOOT_DEBUG

$(VB2_LIB): $(obj)/config.h
	@printf "    MAKE       $(subst $(obj)/,,$(@))\n"
	$(Q)FIRMWARE_ARCH=$(VB_FIRMWARE_ARCH) \
	CC="$(CC_verstage)" \
	CFLAGS="$(VBOOT_CFLAGS)" VBOOT2="y" \
	$(MAKE) -C $(VB_SOURCE) \
		BUILD=$(abspath $(dir $(VB2_LIB))) \
		V=$(V) \
		fwlib20

libverstage-srcs += $(VB2_LIB)

ifeq ($(CONFIG_SEPARATE_VERSTAGE),y)
cbfs-files-y += $(call strip_quotes,$(CONFIG_CBFS_PREFIX))/verstage
$(call strip_quotes,$(CONFIG_CBFS_PREFIX))/verstage-file = $(objcbfs)/verstage.elf
$(call strip_quotes,$(CONFIG_CBFS_PREFIX))/verstage-type = stage
$(call strip_quotes,$(CONFIG_CBFS_PREFIX))/verstage-compression = none
else
ifeq ($(CONFIG_VBOOT_STARTS_IN_BOOTBLOCK),y)
bootblock-srcs += $(objgenerated)/libverstage.a
else
romstage-srcs += $(objgenerated)/libverstage.a
endif
endif # CONFIG_SEPARATE_VERSTAGE
