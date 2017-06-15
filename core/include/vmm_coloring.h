/**
 * Copyright (c) 2017 Paolo Modica.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * @file vmm_coloring.h
 * @author Paolo Modica
 * @brief header file for cache coloring operations.
 */

#ifndef __VMM_COLORING_H__
#define __VMM_COLORING_H__

#include <vmm_types.h>
#include <vmm_stdio.h>
#include <vmm_macros.h>

//#define COLORING_DEBUG
#ifdef COLORING_DEBUG
#define VMM_PRINTF_CDEBUG(msg...)  vmm_printf(msg)
#else
#define VMM_PRINTF_CDEBUG(msg...)
#endif

/** Page shift (or order), mask, and size */
#define VMM_COLOR_SHIFT		CONFIG_COLOR_SHIFT //13
#define VMM_COLOR_SIZE		order_size(VMM_COLOR_SHIFT)
#define VMM_COLOR_MASK		order_mask(VMM_COLOR_SHIFT)

#define VMM_MAX_COLOR_NUM   CONFIG_MAX_COLOR_NUMBER //256
#define VMM_MASK_COLOR_BIT  CONFIG_MASK_COLOR_BIT //0x7

/** Return the number of setted colors*/
int number_of_colors(u32 colors);

/** Check if pa (physical address) is of colors */
bool check_color(physical_addr_t pa, u32 colors);


#endif /* __VMM_COLORING_H__ */
