#
# This file is part of the coreboot project.
#
# Copyright (C) 2007-2009 coresystems GmbH
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc.
#

ifeq ($(CONFIG_NORTHBRIDGE_INTEL_I5000),y)

ramstage-y += northbridge.c
romstage-y += raminit.c
cpu_incs-y += src/northbridge/intel/i5000/halt_second_bsp.S

endif
