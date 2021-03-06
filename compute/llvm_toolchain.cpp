/*
 *  Flo's Open libRary (floor)
 *  Copyright (C) 2004 - 2021 Florian Ziesche
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

#include <floor/compute/llvm_toolchain.hpp>
#include <floor/floor/floor.hpp>
#include <regex>
#include <climits>

#include <floor/compute/opencl/opencl_device.hpp>
#include <floor/compute/cuda/cuda_device.hpp>
#include <floor/compute/metal/metal_device.hpp>
#include <floor/compute/vulkan/vulkan_device.hpp>
#include <floor/compute/host/host_device.hpp>

#if defined(__APPLE__)
#include <floor/darwin/darwin_helper.hpp>
#endif

namespace llvm_toolchain {

bool create_floor_function_info(const string& ffi_file_name,
								vector<function_info>& functions,
								const uint32_t toolchain_version floor_unused) {
	string ffi = "";
	if (!file_io::file_to_string(ffi_file_name, ffi)) {
		log_error("failed to retrieve floor function info from \"%s\"", ffi_file_name);
		return false;
	}
	
	const auto lines = core::tokenize(ffi, '\n');
	functions.reserve(max(lines.size(), size_t(1)) - 1);
	for (const auto& line : lines) {
		if (line.empty()) continue;
		const auto tokens = core::tokenize(line, ',');
		
		// at least 7 w/o any args:
		// functions : <version>,<func_name>,<type>,<flags>,<local_size_x>,<local_size_y>,<local_size_z>,<args...>
		// arg-buffer: <version>,<func_name>,<type>,<flags>,<arg # in func/arg-buffer>,0,0,<args...>
		// NOTE: arg-buffer entry must come after the function entry that uses it
		// NOTE: for an argument buffer struct local_size_* is 0
		if (tokens.size() < 7) {
			log_error("invalid function info entry: %s", line);
			return false;
		}
		const auto& token_version = tokens[0];
		const auto& token_name = tokens[1];
		const auto& token_type = tokens[2];
		const auto& token_flags = tokens[3];
		// -> functions
		const auto& token_local_size_x = tokens[4];
		const auto& token_local_size_y = tokens[5];
		const auto& token_local_size_z = tokens[6];
		// -> argument buffer
		const auto& token_arg_num = tokens[4];
		
		//
		static constexpr const char floor_functions_version[] { "4" };
		if (token_version != floor_functions_version) {
			log_error("invalid floor function info version, expected %u, got %u!",
					  floor_functions_version, token_version);
			return false;
		}
		
		FUNCTION_TYPE func_type = FUNCTION_TYPE::NONE;
		if (token_type == "1") func_type = FUNCTION_TYPE::KERNEL;
		else if (token_type == "2") func_type = FUNCTION_TYPE::VERTEX;
		else if (token_type == "3") func_type = FUNCTION_TYPE::FRAGMENT;
		else if (token_type == "4") func_type = FUNCTION_TYPE::GEOMETRY;
		else if (token_type == "5") func_type = FUNCTION_TYPE::TESSELLATION_CONTROL;
		else if (token_type == "6") func_type = FUNCTION_TYPE::TESSELLATION_EVALUATION;
		else if (token_type == "100") func_type = FUNCTION_TYPE::ARGUMENT_BUFFER_STRUCT;
		
		if (func_type != FUNCTION_TYPE::KERNEL &&
			func_type != FUNCTION_TYPE::VERTEX &&
			func_type != FUNCTION_TYPE::FRAGMENT &&
			func_type != FUNCTION_TYPE::ARGUMENT_BUFFER_STRUCT) {
			log_error("unsupported function type: %s", token_type);
			return false;
		}
		
		const auto func_flags = (FUNCTION_FLAGS)strtoull(token_flags.c_str(), nullptr, 10);
		
		uint3 local_size {};
		if (func_type != FUNCTION_TYPE::ARGUMENT_BUFFER_STRUCT) {
			local_size = {
				(uint32_t)strtoull(token_local_size_x.c_str(), nullptr, 10),
				(uint32_t)strtoull(token_local_size_y.c_str(), nullptr, 10),
				(uint32_t)strtoull(token_local_size_z.c_str(), nullptr, 10),
			};
		}
		
		function_info info {
			.name = token_name,
			.local_size = local_size,
			.type = func_type,
			.flags = func_flags,
		};
		if (info.type == FUNCTION_TYPE::ARGUMENT_BUFFER_STRUCT && !info.local_size.is_null()) {
			log_error("local size must be 0 for argument buffer struct info");
			return false;
		}
		
		for (size_t i = 7, count = tokens.size(); i < count; ++i) {
			if (tokens[i].empty()) continue;
			// function arg info: #elem_idx size, address space, image type, image access
			const auto data = strtoull(tokens[i].c_str(), nullptr, 10);
			
			if (data == ULLONG_MAX || data == 0) {
				log_error("invalid arg info (in %s): %s", ffi_file_name, tokens[i]);
			}
			
			info.args.emplace_back(arg_info {
				.size			= (uint32_t)
				((data & uint64_t(FLOOR_METADATA::ARG_SIZE_MASK)) >> uint64_t(FLOOR_METADATA::ARG_SIZE_SHIFT)),
				.address_space	= (ARG_ADDRESS_SPACE)
				((data & uint64_t(FLOOR_METADATA::ADDRESS_SPACE_MASK)) >> uint64_t(FLOOR_METADATA::ADDRESS_SPACE_SHIFT)),
				.image_type		= (ARG_IMAGE_TYPE)
				((data & uint64_t(FLOOR_METADATA::IMAGE_TYPE_MASK)) >> uint64_t(FLOOR_METADATA::IMAGE_TYPE_SHIFT)),
				.image_access	= (ARG_IMAGE_ACCESS)
				((data & uint64_t(FLOOR_METADATA::IMAGE_ACCESS_MASK)) >> uint64_t(FLOOR_METADATA::IMAGE_ACCESS_SHIFT)),
				.special_type	= (SPECIAL_TYPE)
				((data & uint64_t(FLOOR_METADATA::SPECIAL_TYPE_MASK)) >> uint64_t(FLOOR_METADATA::SPECIAL_TYPE_SHIFT)),
			});
		}
		
		if (info.type == FUNCTION_TYPE::ARGUMENT_BUFFER_STRUCT) {
			const auto arg_idx = (uint32_t)strtoull(token_arg_num.c_str(), nullptr, 10);
			
			bool found_func = false;
			for (auto riter = functions.rbegin(); riter != functions.rend(); ++riter) {
				if (riter->name == info.name) {
					found_func = true;
					
					if (arg_idx >= riter->args.size()) {
						log_error("argument index %u is out-of-bounds for function %s with %u args", arg_idx, info.name, riter->args.size());
						return false;
					}
					
					auto& arg = riter->args[arg_idx];
					if (arg.special_type != SPECIAL_TYPE::ARGUMENT_BUFFER) {
						log_error("argument index %u in function %s is not an argument buffer", arg_idx, info.name);
						return false;
					}
					
					arg.argument_buffer_info = move(info);
					break;
				}
			}
			if (!found_func) {
				log_error("didn't find function %s for argument buffer", info.name);
				return false;
			}
		} else {
			functions.emplace_back(info);
		}
	}
	
	// check if all argument buffer info has been set for all functions
	for (const auto& func : functions) {
		for (size_t i = 0, count = func.args.size(); i < count; ++i) {
			const auto& arg = func.args[i];
			if (arg.special_type == SPECIAL_TYPE::ARGUMENT_BUFFER && !arg.argument_buffer_info) {
				log_error("missing argument buffer info for argument #%u in function %s", i, func.name);
				return false;
			}
		}
	}

	return true;
}

program_data compile_program(const compute_device& device,
							 const string& code,
							 const compile_options options) {
	const string printable_code { "printf \"" + core::str_hex_escape(code) + "\" | " };
	return compile_input("-", printable_code, device, options);
}

program_data compile_program_file(const compute_device& device,
								  const string& filename,
								  const compile_options options) {
	return compile_input("\"" + filename + "\"", "", device, options);
}

program_data compile_input(const string& input,
						   const string& cmd_prefix,
						   const compute_device& device,
						   const compile_options options) {
	// create the initial clang compilation command
	string clang_cmd = cmd_prefix;
	string libcxx_path = " -isystem \"", clang_path = " -isystem \"", floor_path = " -isystem \"";
	string sm_version = "20"; // handle cuda sm version (default to fermi/sm_20)
	uint32_t ptx_version = max(43u, options.cuda.ptx_version); // handle cuda ptx version (default to at least ptx 4.3)
	string output_file_type = "bc"; // can be overwritten by target
	uint32_t toolchain_version = 0;
	bool disable_sub_groups = false; // in case something needs to override device capabilities
	switch(options.target) {
		case TARGET::SPIR:
			toolchain_version = floor::get_opencl_toolchain_version();
			clang_cmd += {
				"\"" + floor::get_opencl_compiler() + "\"" +
				" -x cl -Xclang -cl-std=CL1.2" \
				" -target spir64-unknown-unknown" \
				" -llvm-bc-32" \
				" -Xclang -cl-sampler-type -Xclang i32" \
				" -Xclang -cl-kernel-arg-info" \
				" -Xclang -cl-mad-enable" \
				" -Xclang -cl-fast-relaxed-math" \
				" -Xclang -cl-unsafe-math-optimizations" \
				" -Xclang -cl-finite-math-only" \
				" -DFLOOR_COMPUTE_OPENCL" \
				" -DFLOOR_COMPUTE_SPIR" \
				" -DFLOOR_COMPUTE_OPENCL_MAJOR=1" \
				" -DFLOOR_COMPUTE_OPENCL_MINOR=2" +
				(!device.double_support ? " -DFLOOR_COMPUTE_NO_DOUBLE" : "") +
				(floor::get_opencl_verify_spir() ? " -Xclang -cl-verify-spir" : "") +
				(device.platform_vendor == COMPUTE_VENDOR::INTEL &&
				 device.vendor == COMPUTE_VENDOR::INTEL ? " -Xclang -cl-spir-intel-workarounds" : "")
			};
			libcxx_path += floor::get_opencl_base_path() + "libcxx";
			clang_path += floor::get_opencl_base_path() + "clang";
			floor_path += floor::get_opencl_base_path() + "floor";
			break;
		case TARGET::AIR: {
			toolchain_version = floor::get_metal_toolchain_version();
			output_file_type = "metallib";
			
			const auto& mtl_dev = (const metal_device&)device;
			auto metal_version = mtl_dev.metal_language_version;
			const auto metal_force_version = (!options.ignore_runtime_info ? floor::get_metal_force_version() : 0);
			if (metal_force_version != 0) {
				switch (metal_force_version) {
					case 11:
						metal_version = METAL_VERSION::METAL_1_1;
						break;
					case 12:
						metal_version = METAL_VERSION::METAL_1_2;
						break;
					case 20:
						metal_version = METAL_VERSION::METAL_2_0;
						break;
					case 21:
						metal_version = METAL_VERSION::METAL_2_1;
						break;
					case 22:
						metal_version = METAL_VERSION::METAL_2_2;
						break;
					case 23:
						metal_version = METAL_VERSION::METAL_2_3;
						break;
					default:
						log_error("invalid force_version: %u", metal_force_version);
						break;
				}
				
				if (metal_version < METAL_VERSION::METAL_2_0) {
					// no sub-group and shuffle support here
					disable_sub_groups = true;
				}
			}
			if (metal_version > METAL_VERSION::METAL_2_3) {
				log_error("unsupported Metal language version: %u", metal_version_to_string(metal_version));
				return {};
			}
			
			string os_target;
			if (mtl_dev.family_type == metal_device::FAMILY_TYPE::APPLE) {
				// -> iOS 9.0+
				switch (metal_version) {
					default:
						os_target = "ios9.0.0";
						break;
					case METAL_VERSION::METAL_1_2:
						os_target = "ios10.0.0";
						break;
					case METAL_VERSION::METAL_2_0:
						os_target = "ios11.0.0";
						break;
					case METAL_VERSION::METAL_2_1:
						os_target = "ios12.0.0";
						break;
					case METAL_VERSION::METAL_2_2:
						os_target = "ios13.0.0";
						break;
					case METAL_VERSION::METAL_2_3:
						os_target = "ios14.0.0";
						break;
				}
			} else if (mtl_dev.family_type == metal_device::FAMILY_TYPE::MAC) {
				// -> OS X 10.11+
				switch (metal_version) {
					default:
						os_target = "macosx10.11.0";
						break;
					case METAL_VERSION::METAL_1_2:
						os_target = "macosx10.12.0";
						break;
					case METAL_VERSION::METAL_2_0:
						os_target = "macosx10.13.0";
						break;
					case METAL_VERSION::METAL_2_1:
						os_target = "macosx10.14.0";
						break;
					case METAL_VERSION::METAL_2_2:
						os_target = "macosx10.15.0";
						break;
					case METAL_VERSION::METAL_2_3:
						os_target = "macosx11.0.0";
						break;
				}
			} else {
				log_error("unsupported Metal device family type: %u", uint32_t(mtl_dev.family_type));
				return {};
			}
			
			string metal_std = "metal1.1";
			switch (metal_version) {
				case METAL_VERSION::METAL_1_2:
					metal_std = "metal1.2";
					break;
				case METAL_VERSION::METAL_2_0:
					metal_std = "metal2.0";
					break;
				case METAL_VERSION::METAL_2_1:
					metal_std = "metal2.1";
					break;
				case METAL_VERSION::METAL_2_2:
					metal_std = "metal2.2";
					break;
				case METAL_VERSION::METAL_2_3:
					metal_std = "metal2.3";
					break;
				default: break;
			}
			
			bool soft_printf = false;
			if (options.metal.soft_printf) {
				soft_printf = *options.metal.soft_printf;
			} else {
				soft_printf = floor::get_metal_soft_printf();
			}
			
			clang_cmd += {
				"\"" + floor::get_metal_compiler() + "\"" +
				" -x metal -std=" + metal_std + " -target air64-apple-" + os_target +
#if defined(__APPLE__)
				// always enable intel workarounds (conversion problems)
				(device.vendor == COMPUTE_VENDOR::INTEL ? " -Xclang -metal-intel-workarounds" : "") +
				// enable nvidia workarounds on osx 10.12+ (array load/store problems)
				(!options.ignore_runtime_info &&
				 device.vendor == COMPUTE_VENDOR::NVIDIA &&
				 darwin_helper::get_system_version() >= 101200 ?
				 " -Xclang -metal-nvidia-workarounds" : "") +
#endif
				(soft_printf ? " -Xclang -metal-soft-printf -DFLOOR_COMPUTE_HAS_SOFT_PRINTF=1" : "") +
				" -Xclang -cl-mad-enable" \
				" -Xclang -cl-fast-relaxed-math" \
				" -Xclang -cl-unsafe-math-optimizations" \
				" -Xclang -cl-finite-math-only" \
				" -mllvm -slp-max-vec-dim=4" /* vector dims > 4 are unsupported */ \
				" -DFLOOR_COMPUTE_NO_DOUBLE" \
				" -DFLOOR_COMPUTE_METAL" \
				" -llvm-metallib" +
				" -DFLOOR_COMPUTE_METAL_MAJOR=" + metal_major_version_to_string(metal_version) +
				" -DFLOOR_COMPUTE_METAL_MINOR=" + metal_minor_version_to_string(metal_version)
			};
			libcxx_path += floor::get_metal_base_path() + "libcxx";
			clang_path += floor::get_metal_base_path() + "clang";
			floor_path += floor::get_metal_base_path() + "floor";
		} break;
		case TARGET::PTX: {
			// handle sm version
			const auto& cuda_dev = (const cuda_device&)device;
			const auto& force_sm = floor::get_cuda_force_compile_sm();
			const auto& sm = cuda_dev.sm;
			sm_version = (force_sm.empty() || options.ignore_runtime_info ? to_string(sm.x * 10 + sm.y) : force_sm);

			// handle ptx version:
			// * 4.3 is the minimum requirement for floor
			// * 5.0 for sm_6x
			// * 6.0 for sm_7x < 75
			// * 6.3 for sm_75+
			// * 7.0 for sm_80/sm_82
			// * 7.1 for sm_86+
			switch(cuda_dev.sm.x) {
				case 2:
				case 3:
				case 5:
					// already 43
					break;
				case 6:
					ptx_version = max(50u, ptx_version);
					break;
				case 7:
					ptx_version = max(cuda_dev.sm.y < 5 ? 60u : 63u, ptx_version);
					break;
				case 8:
					ptx_version = max(cuda_dev.sm.y < 6 ? 70u : 71u, ptx_version);
					break;
				default:
					ptx_version = max(71u, ptx_version);
					break;
			}
			if(!floor::get_cuda_force_ptx().empty() && !options.ignore_runtime_info) {
				const auto forced_version = stou(floor::get_cuda_force_ptx());
				if(forced_version >= 43 && forced_version < numeric_limits<uint32_t>::max()) {
					ptx_version = forced_version;
				}
			}
			
			toolchain_version = floor::get_cuda_toolchain_version();
			clang_cmd += {
				"\"" + floor::get_cuda_compiler() + "\"" +
				" -x cuda -std=cuda" \
				" -target x86_64--" +
				" -nocudalib -nocudainc --cuda-device-only --cuda-gpu-arch=sm_" + sm_version +
				" -Xclang -fcuda-is-device" \
				" -DFLOOR_COMPUTE_CUDA"
			};
			if (toolchain_version >= 80000 && options.cuda.short_ptr) {
				clang_cmd += " -fcuda-short-ptr -mllvm --nvptx-short-ptr";
			}
			libcxx_path += floor::get_cuda_base_path() + "libcxx";
			clang_path += floor::get_cuda_base_path() + "clang";
			floor_path += floor::get_cuda_base_path() + "floor";
		} break;
		case TARGET::SPIRV_VULKAN: {
			toolchain_version = floor::get_vulkan_toolchain_version();
			output_file_type = "spvc";
			
			const auto& vk_device = (const vulkan_device&)device;
			string vulkan_std = "vulkan1.0";
			switch(vk_device.vulkan_version) {
				case VULKAN_VERSION::VULKAN_1_0:
					vulkan_std = "vulkan1.0";
					break;
				case VULKAN_VERSION::VULKAN_1_1:
					vulkan_std = "vulkan1.1";
					break;
				case VULKAN_VERSION::VULKAN_1_2:
					vulkan_std = "vulkan1.2";
					break;
				default: break;
			}
			
			bool soft_printf = false;
			if (options.vulkan.soft_printf) {
				soft_printf = *options.vulkan.soft_printf;
			} else {
				soft_printf = floor::get_vulkan_soft_printf();
			}
			
			// still compiling this as opencl for now
			clang_cmd += {
				"\"" + floor::get_vulkan_compiler() + "\"" +
				" -x vulkan -std=" + vulkan_std +
				" -llvm-spirv-container" \
				" -target spir64-unknown-unknown-vulkan" +
				" -Xclang -cl-sampler-type -Xclang i32" \
				" -Xclang -cl-kernel-arg-info" \
				" -Xclang -cl-mad-enable" \
				" -Xclang -cl-fast-relaxed-math" \
				" -Xclang -cl-unsafe-math-optimizations" \
				" -Xclang -cl-finite-math-only" +
				" -Xclang -vulkan-iub-size=" + to_string(vk_device.max_inline_uniform_block_size) +
				" -Xclang -vulkan-iub-count=" + to_string(vk_device.max_inline_uniform_block_count) +
				(soft_printf ? " -Xclang -vulkan-soft-printf -DFLOOR_COMPUTE_HAS_SOFT_PRINTF=1" : "") +
				" -DFLOOR_COMPUTE_VULKAN" \
				" -DFLOOR_COMPUTE_SPIRV" \
				" -DFLOOR_COMPUTE_NO_DOUBLE"
				// TODO: fix Vulkan double support
				//(!device.double_support ? " -DFLOOR_COMPUTE_NO_DOUBLE" : "")
			};
			libcxx_path += floor::get_vulkan_base_path() + "libcxx";
			clang_path += floor::get_vulkan_base_path() + "clang";
			floor_path += floor::get_vulkan_base_path() + "floor";
		} break;
		case TARGET::SPIRV_OPENCL: {
			toolchain_version = floor::get_opencl_toolchain_version();
			output_file_type = "spv";
			const auto& cl_device = (const opencl_device&)device;
			if(cl_device.spirv_version == SPIRV_VERSION::NONE) {
				log_error("SPIR-V is not supported by this device!");
				return {};
			}
			
			clang_cmd += {
				"\"" + floor::get_opencl_compiler() + "\"" +
				// compile to the max opencl standard that is supported by the device
				" -x cl -Xclang -cl-std=CL" + cl_version_to_string(cl_device.cl_version) +
				" -target spir64-unknown-unknown" \
				" -llvm-spirv" \
				" -Xclang -cl-sampler-type -Xclang i32" \
				" -Xclang -cl-kernel-arg-info" \
				" -Xclang -cl-mad-enable" \
				" -Xclang -cl-fast-relaxed-math" \
				" -Xclang -cl-unsafe-math-optimizations" \
				" -Xclang -cl-finite-math-only" \
				" -DFLOOR_COMPUTE_OPENCL" \
				" -DFLOOR_COMPUTE_SPIRV" \
				" -DFLOOR_COMPUTE_OPENCL_MAJOR=" + cl_major_version_to_string(cl_device.cl_version) +
				" -DFLOOR_COMPUTE_OPENCL_MINOR=" + cl_minor_version_to_string(cl_device.cl_version) +
				(!device.double_support ? " -DFLOOR_COMPUTE_NO_DOUBLE" : "")
			};
			libcxx_path += floor::get_opencl_base_path() + "libcxx";
			clang_path += floor::get_opencl_base_path() + "clang";
			floor_path += floor::get_opencl_base_path() + "floor";
		} break;
		case TARGET::HOST_COMPUTE_CPU: {
			toolchain_version = floor::get_host_toolchain_version();
			output_file_type = "bin";
			const auto& cpu_device = (const host_device&)device;
			
			string arch;
			switch (cpu_device.cpu_tier) {
#if !defined(FLOOR_IOS)
				default:
#endif
				case HOST_CPU_TIER::X86_TIER_1:
					arch = "corei7";
					break;
				case HOST_CPU_TIER::X86_TIER_2:
					arch = "corei7-avx";
					break;
				case HOST_CPU_TIER::X86_TIER_3:
					arch = "core-avx2";
					break;
				case HOST_CPU_TIER::X86_TIER_4:
					arch = "skylake-avx512";
					break;
					
#if defined(FLOOR_IOS)
				default:
#endif
				case HOST_CPU_TIER::ARM_TIER_1:
					arch = "arm64";
					break;
				case HOST_CPU_TIER::ARM_TIER_2:
					arch = "arm64e";
					break;
			}
			
			clang_cmd += {
				"\"" + floor::get_host_compiler() + "\"" +
				" -x c++ -std=gnu++2a" +
				" -target x86_64-pc-none-floor_host_compute" \
				" -nostdinc -fbuiltin -fno-math-errno" \
				" -fPIC" /* must be relocatable */ \
				" -march=" + arch +
				" -mcmodel=large" +
				" -DFLOOR_COMPUTE_HOST_DEVICE -DFLOOR_COMPUTE_HOST" +
				// TODO/NOTE: for now, doubles are not supported
				//(!device.double_support ? " -DFLOOR_COMPUTE_NO_DOUBLE" : "")
				" -DFLOOR_COMPUTE_NO_DOUBLE"
				" -fno-stack-protector"
			};
			libcxx_path += floor::get_host_base_path() + "libcxx";
			clang_path += floor::get_host_base_path() + "clang";
			floor_path += floor::get_host_base_path() + "floor";
		} break;
	}
	libcxx_path += "\"";
	clang_path += "\"";
	floor_path += "\"";
	
	// set toolchain version define
	clang_cmd += " -DFLOOR_TOOLCHAIN_VERSION=" + to_string(toolchain_version) + "u";
	
	// add device information
	// -> this adds both a "=" value definiton (that is used for enums in device_info.hpp) and a non-valued "_" defintion (used for #ifdef's)
	const auto vendor_str = compute_vendor_to_string(device.vendor);
	const auto platform_vendor_str = compute_vendor_to_string(device.platform_vendor);
	const auto type_str = (device.is_gpu() ? "GPU" : (device.is_cpu() ? "CPU" : "UNKNOWN"));
	string os_str;
	if (!options.ignore_runtime_info) {
		os_str = (options.target != TARGET::AIR ?
				  // non metal/air targets (aka the usual/proper handling)
#if defined(__APPLE__)
#if defined(FLOOR_IOS)
				  "IOS"
#else
				  "OSX"
#endif
#elif defined(__WINDOWS__)
				  "WINDOWS"
#elif defined(__LINUX__)
				  "LINUX"
#elif defined(__FreeBSD__)
				  "FREEBSD"
#elif defined(__OpenBSD__)
				  "OPENBSD"
#else
				  "UNKNOWN"
#endif
				  // metal/air specific handling, target os is dependent on feature set
				  : (((const metal_device&)device).family_type == metal_device::FAMILY_TYPE::APPLE ? "IOS" : "OSX"));
	} else {
		os_str = "UNKNOWN";
	}
	
	clang_cmd += " -DFLOOR_COMPUTE_INFO_VENDOR="s + vendor_str;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_VENDOR_"s + vendor_str;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_PLATFORM_VENDOR="s + platform_vendor_str;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_PLATFORM_VENDOR_"s + platform_vendor_str;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_TYPE="s + type_str;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_TYPE_"s + type_str;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_OS="s + os_str;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_OS_"s + os_str;
	
	string os_version_str = " -DFLOOR_COMPUTE_INFO_OS_VERSION=0";
	os_version_str += " -DFLOOR_COMPUTE_INFO_OS_VERSION_0";
#if defined(__APPLE__) // TODO: do this on linux as well? if so, how/what?
	if (!options.ignore_runtime_info) {
		const auto osx_version_str = to_string(darwin_helper::get_system_version());
		os_version_str = " -DFLOOR_COMPUTE_INFO_OS_VERSION="s + osx_version_str;
		os_version_str += " -DFLOOR_COMPUTE_INFO_OS_VERSION_"s + osx_version_str;
	}
#elif defined(__WINDOWS__)
	if (!options.ignore_runtime_info) {
		const auto win_version = core::get_windows_version();
		// set as: 61 (Win 7), 62 (Win 8), 63 (Win 8.1), 100 (Win 10)
		const auto win_version_str = to_string(win_version.x * 10 + win_version.y);
		os_version_str = " -DFLOOR_COMPUTE_INFO_OS_VERSION="s + win_version_str;
		os_version_str += " -DFLOOR_COMPUTE_INFO_OS_VERSION_"s + win_version_str;
	}
#endif
	clang_cmd += os_version_str;
	
	// assume all gpus have fma support
	bool has_fma = device.is_gpu();
	if(device.is_cpu() && !options.ignore_runtime_info) {
		// if device is a cpu, need to check cpuid on x86, or check if cpu is armv8
		has_fma = core::cpu_has_fma();
	}
	const auto has_fma_str = to_string(has_fma);
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_FMA="s + has_fma_str;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_FMA_"s + has_fma_str;
	
	// base and extended 64-bit atomics support
	const auto has_base_64_bit_atomics_str = to_string(device.basic_64_bit_atomics_support);
	const auto has_extended_64_bit_atomics_str = to_string(device.extended_64_bit_atomics_support);
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_64_BIT_ATOMICS="s + has_base_64_bit_atomics_str;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_64_BIT_ATOMICS_"s + has_base_64_bit_atomics_str;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_NATIVE_EXTENDED_64_BIT_ATOMICS="s + has_extended_64_bit_atomics_str;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_NATIVE_EXTENDED_64_BIT_ATOMICS_"s + has_extended_64_bit_atomics_str;
	
	// has device actual dedicated local memory
	const auto has_dedicated_local_memory_str = to_string(device.local_mem_dedicated);
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_DEDICATED_LOCAL_MEMORY="s + has_dedicated_local_memory_str;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_DEDICATED_LOCAL_MEMORY_"s + has_dedicated_local_memory_str;
	
	// id/size ranges
	// NOTE: ranges are specified as [min, max), i.e. min is inclusive, max is exclusive
	uint2 global_id_range { 0u, ~0u };
	uint2 global_size_range { 1u, ~0u };
	// NOTE: nobody supports a local size > 2048 (or 1536) right now, if that changes, this needs to be updated
	uint2 local_id_range { 0u, 2048u };
	uint2 local_size_range { 1u, 2049u };
	uint2 group_id_range { 0u, ~0u };
	uint2 group_size_range { 1u, ~0u };
	
	// if the device has actual info about this, use that instead of the defaults
	const auto max_global_size = device.max_global_size.max_element();
	if(max_global_size > 0) {
		global_id_range.y = (max_global_size >= 0xFFFFFFFFull ? ~0u : uint32_t(max_global_size));
		global_size_range.y = (max_global_size >= 0xFFFFFFFFull ? ~0u : uint32_t(max_global_size + 1));
	}
	
	if(device.max_total_local_size != 0) {
		local_id_range.y = device.max_total_local_size;
		local_size_range.y = device.max_total_local_size + 1;
	}
	
	const auto max_group_size = device.max_group_size.max_element();
	if(max_group_size > 0) {
		group_id_range.y = max_group_size;
		group_size_range.y = (max_group_size != ~0u ? max_group_size + 1 : ~0u);
	}
	
	clang_cmd += " -DFLOOR_COMPUTE_INFO_GLOBAL_ID_RANGE_MIN=" + to_string(global_id_range.x) + "u";
	clang_cmd += " -DFLOOR_COMPUTE_INFO_GLOBAL_ID_RANGE_MAX=" + to_string(global_id_range.y) + "u";
	clang_cmd += " -DFLOOR_COMPUTE_INFO_GLOBAL_SIZE_RANGE_MIN=" + to_string(global_size_range.x) + "u";
	clang_cmd += " -DFLOOR_COMPUTE_INFO_GLOBAL_SIZE_RANGE_MAX=" + to_string(global_size_range.y) + "u";
	clang_cmd += " -DFLOOR_COMPUTE_INFO_LOCAL_ID_RANGE_MIN=" + to_string(local_id_range.x) + "u";
	clang_cmd += " -DFLOOR_COMPUTE_INFO_LOCAL_ID_RANGE_MAX=" + to_string(local_id_range.y) + "u";
	clang_cmd += " -DFLOOR_COMPUTE_INFO_LOCAL_SIZE_RANGE_MIN=" + to_string(local_size_range.x) + "u";
	clang_cmd += " -DFLOOR_COMPUTE_INFO_LOCAL_SIZE_RANGE_MAX=" + to_string(local_size_range.y) + "u";
	clang_cmd += " -DFLOOR_COMPUTE_INFO_GROUP_ID_RANGE_MIN=" + to_string(group_id_range.x) + "u";
	clang_cmd += " -DFLOOR_COMPUTE_INFO_GROUP_ID_RANGE_MAX=" + to_string(group_id_range.y) + "u";
	clang_cmd += " -DFLOOR_COMPUTE_INFO_GROUP_SIZE_RANGE_MIN=" + to_string(group_size_range.x) + "u";
	clang_cmd += " -DFLOOR_COMPUTE_INFO_GROUP_SIZE_RANGE_MAX=" + to_string(group_size_range.y) + "u";
	
	// handle device simd width
	uint32_t simd_width = device.simd_width;
	uint2 simd_range = device.simd_range;
	if(simd_width == 0 && !options.ignore_runtime_info) {
		// try to figure out the simd width of the device if it's 0
		if(device.is_gpu()) {
			switch(device.vendor) {
				case COMPUTE_VENDOR::NVIDIA: simd_width = 32; simd_range = { simd_width, simd_width }; break;
				case COMPUTE_VENDOR::AMD: simd_width = 64; simd_range = { simd_width, simd_width }; break;
				case COMPUTE_VENDOR::INTEL: simd_width = 16; simd_range = { 8, 32 }; break;
				case COMPUTE_VENDOR::APPLE: simd_width = 32; simd_range = { simd_width, simd_width }; break;
				// else: unknown gpu
				default: break;
			}
		}
		else if(device.is_cpu()) {
			// always at least 4 (SSE, newer NEON), 8-wide if avx/avx, 16-wide if avx-512
			simd_width = (core::cpu_has_avx() ? (core::cpu_has_avx512() ? 16 : 8) : 4);
			simd_range = { 1, simd_width };
		}
	}
	const auto simd_width_str = to_string(simd_width);
	clang_cmd += " -DFLOOR_COMPUTE_INFO_SIMD_WIDTH="s + simd_width_str + "u";
	clang_cmd += " -DFLOOR_COMPUTE_INFO_SIMD_WIDTH_MIN="s + to_string(simd_range.x) + "u";
	clang_cmd += " -DFLOOR_COMPUTE_INFO_SIMD_WIDTH_MAX="s + to_string(simd_range.y) + "u";
	clang_cmd += " -DFLOOR_COMPUTE_INFO_SIMD_WIDTH_"s + simd_width_str;
	
	if (device.sub_group_support && !disable_sub_groups) {
		// sub-group range handling, now that we know local sizes and SIMD-width
		// NOTE: no known device currently has a SIMD-width larger than 64
		// NOTE: correspondence if a sub-group was a work-group:
		//       sub-group local id ^= local id
		//       sub-group size     ^= local size
		//       sub-group id       ^= group id
		//       num sub-groups     ^= group size
		uint2 sub_group_local_id_range { 0u, 64u };
		uint2 sub_group_size_range { 1u, local_size_range.y /* not larger than this */ };
		uint2 sub_group_id_range { 0u, local_id_range.y /* not larger than this */ };
		uint2 num_sub_groups_range { 1u, local_size_range.y /* not larger than this */ };
		
		if (device.simd_width > 1u) {
			sub_group_local_id_range.y = device.simd_range.y; // [0, SIMD-width)
			sub_group_size_range = { device.simd_range.x, device.simd_range.y + 1 }; // [min SIMD-width, max SIMD-width]
			if (device.simd_range.x == device.simd_range.y) {
				// device with constant SIMD-width
				sub_group_id_range.y = (local_id_range.y / device.simd_range.y); // [0, max-local-size / max-SIMD-width)
				num_sub_groups_range.y = local_id_range.y / device.simd_range.y + 1; // [1, max-local-size / max-SIMD-width + 1)
			} else {
				// device with variable SIMD-width
				sub_group_id_range.y = (local_id_range.y / device.simd_range.x); // [0, max-local-size / min-SIMD-width)
				num_sub_groups_range.y = local_id_range.y / device.simd_range.x + 1; // [1, max-local-size / min-SIMD-width + 1)
			}
		}
		
		clang_cmd += " -DFLOOR_COMPUTE_INFO_SUB_GROUP_ID_RANGE_MIN=" + to_string(sub_group_id_range.x) + "u";
		clang_cmd += " -DFLOOR_COMPUTE_INFO_SUB_GROUP_ID_RANGE_MAX=" + to_string(sub_group_id_range.y) + "u";
		clang_cmd += " -DFLOOR_COMPUTE_INFO_SUB_GROUP_LOCAL_ID_RANGE_MIN=" + to_string(sub_group_local_id_range.x) + "u";
		clang_cmd += " -DFLOOR_COMPUTE_INFO_SUB_GROUP_LOCAL_ID_RANGE_MAX=" + to_string(sub_group_local_id_range.y) + "u";
		clang_cmd += " -DFLOOR_COMPUTE_INFO_SUB_GROUP_SIZE_RANGE_MIN=" + to_string(sub_group_size_range.x) + "u";
		clang_cmd += " -DFLOOR_COMPUTE_INFO_SUB_GROUP_SIZE_RANGE_MAX=" + to_string(sub_group_size_range.y) + "u";
		clang_cmd += " -DFLOOR_COMPUTE_INFO_NUM_SUB_GROUPS_RANGE_MIN=" + to_string(num_sub_groups_range.x) + "u";
		clang_cmd += " -DFLOOR_COMPUTE_INFO_NUM_SUB_GROUPS_RANGE_MAX=" + to_string(num_sub_groups_range.y) + "u";
	}
	
	
	// handle sub-group support
	if(device.sub_group_support && !disable_sub_groups) {
		clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_SUB_GROUPS=1 -DFLOOR_COMPUTE_INFO_HAS_SUB_GROUPS_1";
	} else {
		clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_SUB_GROUPS=0 -DFLOOR_COMPUTE_INFO_HAS_SUB_GROUPS_0";
	}
	
	// handle sub-group shuffle support
	if(device.sub_group_shuffle_support && !disable_sub_groups) {
		clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_SUB_GROUP_SHUFFLE=1 -DFLOOR_COMPUTE_INFO_HAS_SUB_GROUP_SHUFFLE_1";
	} else {
		clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_SUB_GROUP_SHUFFLE=0 -DFLOOR_COMPUTE_INFO_HAS_SUB_GROUP_SHUFFLE_0";
	}
	
	// handle cooperative kernel support
	if(device.cooperative_kernel_support) {
		clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_COOPERATIVE_KERNEL=1 -DFLOOR_COMPUTE_INFO_HAS_COOPERATIVE_KERNEL_1";
	} else {
		clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_COOPERATIVE_KERNEL=0 -DFLOOR_COMPUTE_INFO_HAS_COOPERATIVE_KERNEL_0";
	}
	
	// handle image support
	const auto has_image_support = to_string(device.image_support);
	const auto has_image_depth_support = to_string(device.image_depth_support);
	const auto has_image_depth_write_support = to_string(device.image_depth_write_support);
	const auto has_image_msaa_support = to_string(device.image_msaa_support);
	const auto has_image_msaa_write_support = to_string(device.image_msaa_write_support);
	const auto has_image_msaa_array_support = to_string(device.image_msaa_array_support);
	const auto has_image_msaa_array_write_support = to_string(device.image_msaa_array_write_support);
	const auto has_image_cube_support = to_string(device.image_cube_support);
	const auto has_image_cube_write_support = to_string(device.image_cube_write_support);
	const auto has_image_cube_array_support = to_string(device.image_cube_array_support);
	const auto has_image_cube_array_write_support = to_string(device.image_cube_array_write_support);
	const auto has_image_mipmap_support = to_string(device.image_mipmap_support);
	const auto has_image_mipmap_write_support = to_string(device.image_mipmap_write_support);
	const auto has_image_offset_read_support = to_string(device.image_offset_read_support);
	const auto has_image_offset_write_support = to_string(device.image_offset_write_support);
	const auto has_image_depth_compare_support = to_string(device.image_depth_compare_support);
	const auto has_image_gather_support = to_string(device.image_gather_support);
	const auto has_image_read_write_support = to_string(device.image_read_write_support);
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_IMAGE_SUPPORT="s + has_image_support;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_IMAGE_SUPPORT_"s + has_image_support;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_IMAGE_DEPTH_SUPPORT="s + has_image_depth_support;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_IMAGE_DEPTH_SUPPORT_"s + has_image_depth_support;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_IMAGE_DEPTH_WRITE_SUPPORT="s + has_image_depth_write_support;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_IMAGE_DEPTH_WRITE_SUPPORT_"s + has_image_depth_write_support;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_IMAGE_MSAA_SUPPORT="s + has_image_msaa_support;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_IMAGE_MSAA_SUPPORT_"s + has_image_msaa_support;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_IMAGE_MSAA_WRITE_SUPPORT="s + has_image_msaa_write_support;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_IMAGE_MSAA_WRITE_SUPPORT_"s + has_image_msaa_write_support;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_IMAGE_MSAA_ARRAY_SUPPORT="s + has_image_msaa_array_support;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_IMAGE_MSAA_ARRAY_SUPPORT_"s + has_image_msaa_array_support;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_IMAGE_MSAA_ARRAY_WRITE_SUPPORT="s + has_image_msaa_array_write_support;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_IMAGE_MSAA_ARRAY_WRITE_SUPPORT_"s + has_image_msaa_array_write_support;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_IMAGE_CUBE_SUPPORT="s + has_image_cube_support;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_IMAGE_CUBE_SUPPORT_"s + has_image_cube_support;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_IMAGE_CUBE_WRITE_SUPPORT="s + has_image_cube_write_support;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_IMAGE_CUBE_WRITE_SUPPORT_"s + has_image_cube_write_support;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_IMAGE_CUBE_ARRAY_SUPPORT="s + has_image_cube_array_support;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_IMAGE_CUBE_ARRAY_SUPPORT_"s + has_image_cube_array_support;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_IMAGE_CUBE_ARRAY_WRITE_SUPPORT="s + has_image_cube_array_write_support;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_IMAGE_CUBE_ARRAY_WRITE_SUPPORT_"s + has_image_cube_array_write_support;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_IMAGE_MIPMAP_SUPPORT="s + has_image_mipmap_support;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_IMAGE_MIPMAP_SUPPORT_"s + has_image_mipmap_support;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_IMAGE_MIPMAP_WRITE_SUPPORT="s + has_image_mipmap_write_support;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_IMAGE_MIPMAP_WRITE_SUPPORT_"s + has_image_mipmap_write_support;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_IMAGE_OFFSET_READ_SUPPORT="s + has_image_mipmap_support;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_IMAGE_OFFSET_READ_SUPPORT_"s + has_image_mipmap_support;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_IMAGE_OFFSET_WRITE_SUPPORT="s + has_image_mipmap_write_support;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_IMAGE_OFFSET_WRITE_SUPPORT_"s + has_image_mipmap_write_support;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_IMAGE_DEPTH_COMPARE_SUPPORT="s + has_image_depth_compare_support;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_IMAGE_DEPTH_COMPARE_SUPPORT_"s + has_image_depth_compare_support;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_IMAGE_GATHER_SUPPORT="s + has_image_gather_support;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_IMAGE_GATHER_SUPPORT_"s + has_image_gather_support;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_IMAGE_READ_WRITE_SUPPORT="s + has_image_read_write_support;
	clang_cmd += " -DFLOOR_COMPUTE_INFO_HAS_IMAGE_READ_WRITE_SUPPORT_"s + has_image_read_write_support;
	
	IMAGE_CAPABILITY img_caps { IMAGE_CAPABILITY::NONE };
	if(device.image_support) img_caps |= IMAGE_CAPABILITY::BASIC;
	if(device.image_depth_support) img_caps |= IMAGE_CAPABILITY::DEPTH_READ;
	if(device.image_depth_write_support) img_caps |= IMAGE_CAPABILITY::DEPTH_WRITE;
	if(device.image_msaa_support) img_caps |= IMAGE_CAPABILITY::MSAA_READ;
	if(device.image_msaa_write_support) img_caps |= IMAGE_CAPABILITY::MSAA_WRITE;
	if(device.image_msaa_array_support) img_caps |= IMAGE_CAPABILITY::MSAA_ARRAY_READ;
	if(device.image_msaa_array_write_support) img_caps |= IMAGE_CAPABILITY::MSAA_ARRAY_WRITE;
	if(device.image_cube_support) img_caps |= IMAGE_CAPABILITY::CUBE_READ;
	if(device.image_cube_write_support) img_caps |= IMAGE_CAPABILITY::CUBE_WRITE;
	if(device.image_cube_array_support) img_caps |= IMAGE_CAPABILITY::CUBE_ARRAY_READ;
	if(device.image_cube_array_write_support) img_caps |= IMAGE_CAPABILITY::CUBE_ARRAY_WRITE;
	if(device.image_mipmap_support) img_caps |= IMAGE_CAPABILITY::MIPMAP_READ;
	if(device.image_mipmap_write_support) img_caps |= IMAGE_CAPABILITY::MIPMAP_WRITE;
	if(device.image_offset_read_support) img_caps |= IMAGE_CAPABILITY::OFFSET_READ;
	if(device.image_offset_write_support) img_caps |= IMAGE_CAPABILITY::OFFSET_WRITE;
	if(device.image_depth_compare_support) img_caps |= IMAGE_CAPABILITY::DEPTH_COMPARE;
	if(device.image_gather_support) img_caps |= IMAGE_CAPABILITY::GATHER;
	if(device.image_read_write_support) img_caps |= IMAGE_CAPABILITY::READ_WRITE;
	clang_cmd += " -Xclang -floor-image-capabilities=" + to_string((underlying_type_t<IMAGE_CAPABILITY>)img_caps);
	
	clang_cmd += " -DFLOOR_COMPUTE_INFO_MAX_MIP_LEVELS="s + to_string(device.max_mip_levels) + "u";
	
	// set param workaround define
	if(device.param_workaround) {
		clang_cmd += " -DFLOOR_COMPUTE_PARAM_WORKAROUND=1";
	}
	
	// floor function info
	const auto function_info_file_name = core::create_tmp_file_name("ffi", ".txt");
	clang_cmd += " -Xclang -floor-function-info=" + function_info_file_name;
	
	// target specific compute info
	switch(options.target) {
		case TARGET::PTX:
			// set cuda sm and ptx version
			clang_cmd += " -DFLOOR_COMPUTE_INFO_CUDA_SM=" + sm_version;
			clang_cmd += " -DFLOOR_COMPUTE_INFO_CUDA_PTX=" + to_string(ptx_version);
			break;
		case TARGET::SPIRV_VULKAN: {
			const auto& vk_device = (const vulkan_device&)device;
			const auto has_int16_support = to_string(vk_device.int16_support);
			const auto has_float16_support = to_string(vk_device.float16_support);
			clang_cmd += " -DFLOOR_COMPUTE_INFO_VULKAN_HAS_INT16_SUPPORT="s + has_int16_support;
			clang_cmd += " -DFLOOR_COMPUTE_INFO_VULKAN_HAS_INT16_SUPPORT_"s + has_int16_support;
			clang_cmd += " -DFLOOR_COMPUTE_INFO_VULKAN_HAS_FLOAT16_SUPPORT="s + has_float16_support;
			clang_cmd += " -DFLOOR_COMPUTE_INFO_VULKAN_HAS_FLOAT16_SUPPORT_"s + has_float16_support;
			break;
		}
		default: break;
	}

	// emit line info if debug mode is enabled (unless this is spir where we'd better not emit this)
	if(((floor::get_toolchain_debug() && !options.ignore_runtime_info) || options.emit_debug_line_info) &&
		options.target != TARGET::SPIR) {
		clang_cmd += " -gline-tables-only";
	}
	
	// default warning flags (note that these cost a significant amount of compilation time)
	const char* warning_flags {
		// let's start with everything
		" -Weverything"
		// remove compat warnings
		" -Wno-c++98-compat -Wno-c++98-compat-pedantic"
		" -Wno-c++11-compat -Wno-c++11-compat-pedantic"
		" -Wno-c++14-compat -Wno-c++14-compat-pedantic"
		" -Wno-c++17-compat -Wno-c++17-compat-pedantic"
		" -Wno-c++2a-compat -Wno-c++2a-compat-pedantic -Wno-c++2a-extensions"
		" -Wno-c99-extensions -Wno-c11-extensions"
		" -Wno-gcc-compat -Wno-gnu"
		// in case we're using warning options that aren't supported by other clang versions
		" -Wno-unknown-warning-option"
		// really don't want to be too pedantic
		" -Wno-old-style-cast -Wno-date-time -Wno-system-headers -Wno-header-hygiene -Wno-documentation"
		// again: not too pedantic, also useful language features
		" -Wno-nested-anon-types -Wno-global-constructors -Wno-exit-time-destructors"
		// usually conflicting with the other switch/case warning, so disable it
		" -Wno-switch-enum"
		// don't warn when using macros prefixed with "__" or "_"
		" -Wno-reserved-id-macro"
		// ignore "explicit move to avoid copying on older compilers" warning
		" -Wno-return-std-move-in-c++11"
		// end
		" "
	};
	
	// add generic flags/options that are always used
	auto compiled_file_or_code = core::create_tmp_file_name("", '.' + output_file_type);
	clang_cmd += {
#if defined(FLOOR_DEBUG)
		" -DFLOOR_DEBUG"
#endif
		" -DFLOOR_COMPUTE"
		" -DFLOOR_NO_MATH_STR" +
		libcxx_path + clang_path + floor_path +
		" -include floor/compute/device/common.hpp" +
		(options.target != TARGET::HOST_COMPUTE_CPU ? " -fno-pic" : "") +
		" -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-addrsig"
		" -fno-rtti -fstrict-aliasing -ffast-math -funroll-loops -Ofast -ffp-contract=fast"
		// increase limit from 16 to 64, this "fixes" some forced unrolling
		" -mllvm -rotation-max-header-size=64" +
		(options.enable_warnings ? warning_flags : " ") +
		options.cli +
		" -m64" +
		(options.target != TARGET::HOST_COMPUTE_CPU ? " -emit-llvm" : "") +
		" -c -o " + compiled_file_or_code + " " + input
	};
	
	// on sane systems, redirect errors to stdout so that we can grab them
#if !defined(_MSC_VER)
	clang_cmd += " 2>&1";
#endif
	
	// compile
	if(floor::get_toolchain_log_commands() &&
	   !options.silence_debug_output) {
		log_debug("clang cmd: %s", clang_cmd);
		logger::flush();
	}
	string compilation_output = "";
	core::system(clang_cmd, compilation_output);
	// check if the output contains an error string (yes, a bit ugly, but it works for now - can't actually check the return code)
	if(compilation_output.find(" error: ") != string::npos ||
	   compilation_output.find(" errors:") != string::npos) {
		log_error("compilation failed! failed cmd was:\n%s", clang_cmd);
		log_error("compilation errors:\n%s", compilation_output);
		return {};
	}
	// also print the output if it is non-empty
	if(compilation_output != "" &&
	   !options.silence_debug_output) {
		log_debug("compilation output:\n%s", compilation_output);
	}
	
	// grab floor function info and create the internal per-function info
	vector<function_info> functions;
	if(!create_floor_function_info(function_info_file_name, functions, toolchain_version)) {
		log_error("failed to create internal floor function info");
		return {};
	}
	if(!floor::get_toolchain_keep_temp()) {
		core::system("rm " + function_info_file_name);
	}
	
	// final target specific processing/compilation
	if(options.target == TARGET::SPIR) {
		string spir_bc_data = "";
		if(!file_io::file_to_string(compiled_file_or_code, spir_bc_data)) {
			log_error("failed to read SPIR 1.2 .bc file");
			return {};
		}
		
		// cleanup
		if(!floor::get_toolchain_keep_temp()) {
			core::system("rm " + compiled_file_or_code);
		}
		
		// move spir data
		compiled_file_or_code.swap(spir_bc_data);
	}
	else if(options.target == TARGET::AIR) {
		// nop, final processing will be done in metal_compute
	}
	else if(options.target == TARGET::PTX) {
		// compile llvm ir to ptx
		const string llc_cmd {
			"\"" + floor::get_cuda_llc() + "\"" +
			" -nvptx-fma-level=2 -nvptx-sched4reg -enable-unsafe-fp-math" \
			" -mcpu=sm_" + sm_version + " -mattr=ptx" + to_string(ptx_version) +
			(toolchain_version >= 80000 && options.cuda.short_ptr ? " -nvptx-short-ptr" : "") +
			" -o - " + compiled_file_or_code
#if !defined(_MSC_VER)
			+ " 2>&1"
#endif
		};
		if(floor::get_toolchain_log_commands() &&
		   !options.silence_debug_output) {
			log_debug("llc cmd: %s", llc_cmd);
		}
		string ptx_code = "";
		core::system(llc_cmd, ptx_code);
		ptx_code += '\0'; // make sure there is a \0 terminator
		
		// only output the compiled ptx code if this was specified in the config
		// NOTE: explicitly create this in the working directory (not in tmp)
		if(floor::get_toolchain_keep_temp()) {
			file_io::string_to_file("cuda.ptx", ptx_code);
		}
		
		// check if we have sane output
		if(ptx_code == "" || ptx_code.find("Generated by LLVM NVPTX Back-End") == string::npos) {
			log_error("llc/ptx compilation failed (%s)!\n%s", llc_cmd, ptx_code);
			return {};
		}
		
		// cleanup
		if(!floor::get_toolchain_keep_temp()) {
			core::system("rm " + compiled_file_or_code);
		}
		
		// move ptx code
		compiled_file_or_code.swap(ptx_code);
	}
	else if(options.target == TARGET::SPIRV_VULKAN ||
			options.target == TARGET::SPIRV_OPENCL) {
		const auto validate = (options.target == TARGET::SPIRV_VULKAN ?
							   floor::get_vulkan_validate_spirv() : floor::get_opencl_validate_spirv());
		const auto validator = (options.target == TARGET::SPIRV_VULKAN ?
								floor::get_vulkan_spirv_validator() : floor::get_opencl_spirv_validator());
		
		// run spirv-val if specified
		if(validate) {
			const string spirv_validator_cmd {
				"\"" + validator + "\" " +
				(options.target == TARGET::SPIRV_VULKAN ? "--uniform-buffer-standard-layout --scalar-block-layout " : "") +
				compiled_file_or_code
#if !defined(_MSC_VER)
				+ " 2>&1"
#endif
			};
			string spirv_validator_output = "";
			core::system(spirv_validator_cmd, spirv_validator_output);
			if(!spirv_validator_output.empty() && spirv_validator_output[spirv_validator_output.size() - 1] == '\n') {
				spirv_validator_output.pop_back(); // trim last newline
			}
			if(!options.silence_debug_output) {
				if(spirv_validator_output == "") {
					log_msg("spir-v validator: valid");
				}
				else {
					log_error("spir-v validator: %s", spirv_validator_output);
				}
			}
		}
		
		// NOTE: will cleanup the binary in opencl_compute/vulkan_compute
	}
	else if(options.target == TARGET::HOST_COMPUTE_CPU) {
		// nop, already a binary
	}
	
	return { true, compiled_file_or_code, functions, options };
}

} // llvm_toolchain
