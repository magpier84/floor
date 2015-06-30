/*
 *  Flo's Open libRary (floor)
 *  Copyright (C) 2004 - 2015 Florian Ziesche
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

#ifndef __FLOOR_COMPUTE_DEVICE_DEVICE_INFO_HPP__
#define __FLOOR_COMPUTE_DEVICE_DEVICE_INFO_HPP__

#if defined(FLOOR_COMPUTE)

//! this namespace contains device, compile and platform information that can be queried at compile-time.
//! note that most of this information can also be accessed through macros/defines, but this provides an
//! easier to use interface when using it with c++ constructs (templates enable_if, type_traits, etc).
namespace device_info {
	//! device vendors
	enum class VENDOR : uint32_t {
		NVIDIA,
		INTEL,
		AMD,
		APPLE,
		POCL,
		HOST,
		UNKNOWN
	};
	//! returns the device vendor
	constexpr VENDOR vendor() {
		return VENDOR::FLOOR_COMPUTE_INFO_VENDOR;
	}
	
	//! device bitness (32 or 64)
	constexpr uint32_t bitness() {
#if defined(PLATFORM_X32)
		return 32u;
#elif defined(PLATFORM_X64)
		return 64u;
#endif
	}
	
	//! device hardware types
	enum class TYPE : uint32_t {
		GPU,
		CPU,
		UNKNOWN
	};
	//! returns the device type
	constexpr TYPE type() {
		return TYPE::FLOOR_COMPUTE_INFO_TYPE;
	}
	
	//! operating systems
	enum class OS : uint32_t {
		IOS,
		OSX,
		WINDOWS,
		LINUX,
		FREEBSD,
		OPENBSD,
		UNKNOWN
	};
	//! returns the operating system this is compiled with/for
	constexpr OS os() {
		return OS::FLOOR_COMPUTE_INFO_OS;
	}
	
	//! returns the operating system version
	//! NOTE: only returns a valid value on OS X and iOS
	//! OS X: identical to MAC_OS_X_VERSION_10_* macro
	//! iOS: identical to __IPHONE_*_* macro
	constexpr size_t os_version() {
		return FLOOR_COMPUTE_INFO_OS_VERSION;
	}
	
	//! returns true if the device has native fma instruction support
	constexpr bool has_fma() {
		return (FLOOR_COMPUTE_INFO_HAS_FMA != 0);
	}
	
	//! returns true if the device has native 64-bit atomics support
	//! NOTE: for OpenCL this is true if cl_khr_int64_base_atomics is supported
	//! NOTE: for CUDA this is true for all devices
	//! NOTE: for Metal this is false for all devices
	constexpr bool has_64_bit_atomics() {
		return (FLOOR_COMPUTE_INFO_HAS_64_BIT_ATOMICS != 0);
	}
	
	//! returns true if the device has native support for extended 64-bit atomics (min, max, and, or, xor),
	//! note that if this is false, these functions are still supported, but implemented through a CAS loop
	//! if the device has support for basic 64-bit atomics of course.
	//! NOTE: for OpenCL this is true if cl_khr_int64_extended_atomics is supported
	//! NOTE: for CUDA this is true for all sm_32+ devices
	//! NOTE: for Metal this is false for all devices
	constexpr bool has_native_extended_64_bit_atomics() {
		return (FLOOR_COMPUTE_INFO_HAS_NATIVE_EXTENDED_64_BIT_ATOMICS != 0);
	}
	
	//! returns true if the device supports atomic operations on pointer types
	constexpr bool has_pointer_atomics() {
#if defined(PLATFORM_X32)
		return true; // always true
#elif defined(PLATFORM_X64)
		return has_64_bit_atomics(); // must support 64-bit atomics otherwise
#endif
	}
	
}

#endif

#endif