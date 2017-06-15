/**
 * Copyright (c) 2017 Paolo Modica.
 * All rights reserved.
 *
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
 * @file vmm_coloring.c
 * @author Paolo Modica
 * @brief source code for cache coloring
 */

#include <vmm_error.h>
#include <vmm_coloring.h>


int number_of_colors(u32 colors)
{
    colors = colors - ((colors >> 1) & 0x55555555);
    colors = (colors & 0x33333333) + ((colors >> 2) & 0x33333333);
    return (((colors + (colors >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24;
}

bool check_color(physical_addr_t pa, u32 colors)
{
    physical_addr_t color = (pa >> VMM_COLOR_SHIFT) & VMM_MASK_COLOR_BIT;

    if ( (colors >> color) & 0x1)
        return true;
    else return false;
}
