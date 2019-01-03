/*
 *  Flo's Open libRary (floor)
 *  Copyright (C) 2004 - 2019 Florian Ziesche
 *  
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License only.
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __FLOOR_COMPUTE_DEVICE_VULKAN_PRE_HPP__
#define __FLOOR_COMPUTE_DEVICE_VULKAN_PRE_HPP__

#if defined(FLOOR_COMPUTE_VULKAN)

// still need to include opencl stuff
#include <floor/compute/device/opencl_pre.hpp>

// enable optional capabilities
#if defined(FLOOR_COMPUTE_INFO_VULKAN_HAS_INT16_SUPPORT_1)
#pragma OPENCL EXTENSION vk_capability_int16 : enable
#endif

#if defined(FLOOR_COMPUTE_INFO_VULKAN_HAS_INT64_SUPPORT_1)
#pragma OPENCL EXTENSION vk_capability_int64 : enable
#endif

#if defined(FLOOR_COMPUTE_INFO_VULKAN_HAS_FLOAT16_SUPPORT_1)
#pragma OPENCL EXTENSION vk_capability_float16 : enable
#endif

#if !defined(FLOOR_COMPUTE_NO_DOUBLE)
#pragma OPENCL EXTENSION vk_capability_float64 : enable
#endif

#endif

#endif
