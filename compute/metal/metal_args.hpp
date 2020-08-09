/*
 *  Flo's Open libRary (floor)
 *  Copyright (C) 2004 - 2020 Florian Ziesche
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
using namespace llvm_toolchain;

#include <floor/compute/metal/metal_buffer.hpp>
#include <floor/compute/metal/metal_image.hpp>
#include <Metal/MTLComputeCommandEncoder.h>
#include <Metal/MTLRenderCommandEncoder.h>
#include <Metal/MTLArgumentEncoder.h>

//! Metal compute/vertex/fragment/argument-buffer argument handler/setter
//! NOTE: do not include manually
namespace metal_args {
	enum class ENCODER_TYPE {
		COMPUTE,
		SHADER,
		ARGUMENT,
	};
	
	template <ENCODER_TYPE enc_type>
	using encoder_selector_t = conditional_t<(enc_type == ENCODER_TYPE::COMPUTE), id <MTLComputeCommandEncoder>,
											 conditional_t<(enc_type == ENCODER_TYPE::SHADER), id <MTLRenderCommandEncoder>, id <MTLArgumentEncoder>>>;
	
	struct idx_handler {
		//! actual argument index (directly corresponding to the c++ source code)
		uint32_t arg { 0 };
		//! flag if this is an implicit arg
		bool is_implicit { false };
		//! current implicit argument index
		uint32_t implicit { 0 };
		//! current buffer index
		uint32_t buffer_idx { 0 };
		//! current texture index
		uint32_t texture_idx { 0 };
		//! current kernel/shader entry
		uint32_t entry { 0 };
	};
	
	//! actual kernel argument setters
	template <ENCODER_TYPE enc_type>
	static void set_argument(const idx_handler& idx,
							 encoder_selector_t<enc_type> encoder, const function_info& entry,
							 const void* ptr, const size_t& size) {
		if constexpr (enc_type == ENCODER_TYPE::COMPUTE) {
			[encoder setBytes:ptr length:size atIndex:idx.buffer_idx];
		} else if constexpr (enc_type == ENCODER_TYPE::SHADER) {
			if (entry.type == FUNCTION_TYPE::VERTEX) {
				[encoder setVertexBytes:ptr length:size atIndex:idx.buffer_idx];
			} else {
				[encoder setFragmentBytes:ptr length:size atIndex:idx.buffer_idx];
			}
		} else if constexpr (enc_type == ENCODER_TYPE::ARGUMENT) {
			memcpy([encoder constantDataAtIndex:idx.buffer_idx], ptr, size);
		}
	}
	
	template <ENCODER_TYPE enc_type>
	static void set_argument(const idx_handler& idx,
							 encoder_selector_t<enc_type> encoder, const function_info& entry,
							 const compute_buffer* arg) {
		const metal_buffer* mtl_buffer = nullptr;
		if (has_flag<COMPUTE_MEMORY_FLAG::METAL_SHARING>(arg->get_flags())) {
			mtl_buffer = arg->get_shared_metal_buffer();
			if (mtl_buffer == nullptr) {
				mtl_buffer = (const metal_buffer*)arg;
#if defined(FLOOR_DEBUG)
				if (auto test_cast_mtl_buffer = dynamic_cast<const metal_buffer*>(arg); !test_cast_mtl_buffer) {
					log_error("specified buffer is neither a Metal buffer nor a shared Metal buffer");
					return;
				}
#endif
			} else {
				if (has_flag<COMPUTE_MEMORY_FLAG::METAL_SHARING_SYNC_SHARED>(arg->get_flags())) {
					arg->sync_metal_buffer();
				}
			}
		} else {
			mtl_buffer = (const metal_buffer*)arg;
		}
		
		if constexpr (enc_type == ENCODER_TYPE::COMPUTE || enc_type == ENCODER_TYPE::ARGUMENT) {
			[encoder setBuffer:mtl_buffer->get_metal_buffer()
						offset:0
					   atIndex:idx.buffer_idx];
		} else if constexpr (enc_type == ENCODER_TYPE::SHADER) {
			if (entry.type == FUNCTION_TYPE::VERTEX) {
				[encoder setVertexBuffer:mtl_buffer->get_metal_buffer()
								  offset:0
								 atIndex:idx.buffer_idx];
			} else {
				[encoder setFragmentBuffer:mtl_buffer->get_metal_buffer()
									offset:0
								   atIndex:idx.buffer_idx];
			}
		}
	}
	
	template <ENCODER_TYPE enc_type>
	static void set_argument(const idx_handler& idx,
							 encoder_selector_t<enc_type> encoder,
							 const function_info& entry floor_unused,
							 const vector<shared_ptr<compute_buffer>>& arg) {
		const auto count = arg.size();
		if (count < 1) return;
		
		vector<id <MTLBuffer>> mtl_buf_array(count, nil);
		vector<NSUInteger> offsets(count, 0);
		for (size_t i = 0; i < count; ++i) {
			mtl_buf_array[i] = ((metal_buffer*)arg[i].get())->get_metal_buffer();
		}
		
		if constexpr (enc_type == ENCODER_TYPE::ARGUMENT) {
			[encoder setBuffers:mtl_buf_array.data()
						offsets:offsets.data()
					  withRange:NSRange { idx.buffer_idx, count }];
		} else {
			static_assert([]() constexpr { return false; }, "only supported for argument buffers");
			log_error("buffer arrays are only supported for argument buffers");
		}
	}
	
	template <ENCODER_TYPE enc_type>
	static void set_argument(const idx_handler& idx,
							 encoder_selector_t<enc_type> encoder,
							 const function_info& entry floor_unused,
							 const vector<compute_buffer*>& arg) {
		const auto count = arg.size();
		if (count < 1) return;
		
		vector<id <MTLBuffer>> mtl_buf_array(count, nil);
		vector<NSUInteger> offsets(count, 0);
		for (size_t i = 0; i < count; ++i) {
			mtl_buf_array[i] = ((metal_buffer*)arg[i])->get_metal_buffer();
		}
		
		if constexpr (enc_type == ENCODER_TYPE::ARGUMENT) {
			[encoder setBuffers:mtl_buf_array.data()
						offsets:offsets.data()
					  withRange:NSRange { idx.buffer_idx, count }];
		} else {
			static_assert([]() constexpr { return false; }, "only supported for argument buffers");
			log_error("buffer arrays are only supported for argument buffers");
		}
	}
	
	template <ENCODER_TYPE enc_type>
	static void set_argument(const idx_handler& idx,
							 encoder_selector_t<enc_type> encoder,
							 const function_info& entry,
							 const argument_buffer* arg_buf) {
		const auto buf = arg_buf->get_storage_buffer();
		const metal_buffer* mtl_buffer = nullptr;
		if (has_flag<COMPUTE_MEMORY_FLAG::METAL_SHARING>(buf->get_flags())) {
			mtl_buffer = buf->get_shared_metal_buffer();
			if (mtl_buffer == nullptr) {
				mtl_buffer = (const metal_buffer*)buf;
	#if defined(FLOOR_DEBUG)
				if (auto test_cast_mtl_buffer = dynamic_cast<const metal_buffer*>(buf); !test_cast_mtl_buffer) {
					log_error("specified buffer is neither a Metal buffer nor a shared Metal buffer");
					return;
				}
	#endif
			} else {
				if (has_flag<COMPUTE_MEMORY_FLAG::METAL_SHARING_SYNC_SHARED>(buf->get_flags())) {
					buf->sync_metal_buffer();
				}
			}
		} else {
			mtl_buffer = (const metal_buffer*)buf;
		}
		
		if constexpr (enc_type == ENCODER_TYPE::COMPUTE || enc_type == ENCODER_TYPE::ARGUMENT) {
			[encoder setBuffer:mtl_buffer->get_metal_buffer()
						offset:0
					   atIndex:idx.buffer_idx];
		} else if constexpr (enc_type == ENCODER_TYPE::SHADER) {
			if (entry.type == FUNCTION_TYPE::VERTEX) {
				[encoder setVertexBuffer:mtl_buffer->get_metal_buffer()
								  offset:0
								 atIndex:idx.buffer_idx];
			} else {
				[encoder setFragmentBuffer:mtl_buffer->get_metal_buffer()
									offset:0
								   atIndex:idx.buffer_idx];
			}
		}
	}
	
	template <ENCODER_TYPE enc_type>
	static void set_argument(const idx_handler& idx,
							 encoder_selector_t<enc_type> encoder,
							 const function_info& entry,
							 const compute_image* arg) {
		const metal_image* mtl_image = nullptr;
		if (has_flag<COMPUTE_MEMORY_FLAG::METAL_SHARING>(arg->get_flags())) {
			mtl_image = arg->get_shared_metal_image();
			if (mtl_image == nullptr) {
				mtl_image = (const metal_image*)arg;
#if defined(FLOOR_DEBUG)
				if (auto test_cast_mtl_image = dynamic_cast<const metal_image*>(arg); !test_cast_mtl_image) {
					log_error("specified image is neither a Metal image nor a shared Metal image");
					return;
				}
#endif
			} else {
				if (has_flag<COMPUTE_MEMORY_FLAG::METAL_SHARING_SYNC_SHARED>(arg->get_flags())) {
					arg->sync_metal_image();
				}
			}
		} else {
			mtl_image = (const metal_image*)arg;
		}
		
		
		if constexpr (enc_type == ENCODER_TYPE::COMPUTE || enc_type == ENCODER_TYPE::ARGUMENT) {
			[encoder setTexture:mtl_image->get_metal_image()
						atIndex:idx.texture_idx];
		} else if constexpr (enc_type == ENCODER_TYPE::SHADER) {
			if (entry.type == FUNCTION_TYPE::VERTEX) {
				[encoder setVertexTexture:mtl_image->get_metal_image()
								  atIndex:idx.texture_idx];
			} else {
				[encoder setFragmentTexture:mtl_image->get_metal_image()
									atIndex:idx.texture_idx];
			}
		}
		
		// if this is a read/write image, add it again (one is read-only, the other is write-only)
		if (entry.args[idx.arg].image_access == ARG_IMAGE_ACCESS::READ_WRITE) {
			if constexpr (enc_type == ENCODER_TYPE::COMPUTE || enc_type == ENCODER_TYPE::ARGUMENT) {
				[encoder setTexture:mtl_image->get_metal_image()
							atIndex:(idx.texture_idx + 1)];
			} else if constexpr (enc_type == ENCODER_TYPE::SHADER) {
				if (entry.type == FUNCTION_TYPE::VERTEX) {
					[encoder setVertexTexture:mtl_image->get_metal_image()
									  atIndex:(idx.texture_idx + 1)];
				} else {
					[encoder setFragmentTexture:mtl_image->get_metal_image()
										atIndex:(idx.texture_idx + 1)];
				}
			}
		}
	}
	
	template <ENCODER_TYPE enc_type>
	static void set_argument(const idx_handler& idx,
							 encoder_selector_t<enc_type> encoder,
							 const function_info& entry,
							 const vector<shared_ptr<compute_image>>& arg) {
		const auto count = arg.size();
		if (count < 1) return;
		
		vector<id <MTLTexture>> mtl_img_array(count, nil);
		for (size_t i = 0; i < count; ++i) {
			mtl_img_array[i] = ((metal_image*)arg[i].get())->get_metal_image();
		}
		
		if constexpr (enc_type == ENCODER_TYPE::COMPUTE || enc_type == ENCODER_TYPE::ARGUMENT) {
			[encoder setTextures:mtl_img_array.data()
					   withRange:NSRange { idx.texture_idx, count }];
		} else if constexpr (enc_type == ENCODER_TYPE::SHADER) {
			if (entry.type == FUNCTION_TYPE::VERTEX) {
				[encoder setVertexTextures:mtl_img_array.data()
								 withRange:NSRange { idx.texture_idx, count }];
			} else {
				[encoder setFragmentTextures:mtl_img_array.data()
								   withRange:NSRange { idx.texture_idx, count }];
			}
		}
	}
	
	template <ENCODER_TYPE enc_type>
	static void set_argument(const idx_handler& idx,
							 encoder_selector_t<enc_type> encoder,
							 const function_info& entry,
							 const vector<compute_image*>& arg) {
		const auto count = arg.size();
		if (count < 1) return;
		
		vector<id <MTLTexture>> mtl_img_array(count, nil);
		for (size_t i = 0; i < count; ++i) {
			mtl_img_array[i] = ((metal_image*)arg[i])->get_metal_image();
		}
		
		if constexpr (enc_type == ENCODER_TYPE::COMPUTE || enc_type == ENCODER_TYPE::ARGUMENT) {
			[encoder setTextures:mtl_img_array.data()
					   withRange:NSRange { idx.texture_idx, count }];
		} else if constexpr (enc_type == ENCODER_TYPE::SHADER) {
			if (entry.type == FUNCTION_TYPE::VERTEX) {
				[encoder setVertexTextures:mtl_img_array.data()
								 withRange:NSRange { idx.texture_idx, count }];
			} else {
				[encoder setFragmentTextures:mtl_img_array.data()
								   withRange:NSRange { idx.texture_idx, count }];
			}
		}
	}
	
	//! returns the entry for the current indices and makes sure that stage_input args are ignored
	static inline const function_info* arg_pre_handler(const vector<const function_info*>& entries, idx_handler& idx) {
		// make sure we have a usable entry
		const function_info* entry = nullptr;
		for (;;) {
			// get the next non-nullptr entry or use the current one if it's valid
			while (entries[idx.entry] == nullptr) {
				++idx.entry;
#if defined(FLOOR_DEBUG)
				if (idx.entry >= entries.size()) {
					log_error("shader/kernel entry out of bounds");
					return nullptr;
				}
#endif
			}
			entry = entries[idx.entry];
			
			// ignore any stage input args
			while (idx.arg < entry->args.size() &&
				   entry->args[idx.arg].special_type == SPECIAL_TYPE::STAGE_INPUT) {
				++idx.arg;
			}
			
			// have all args been specified for this entry?
			if (idx.arg >= entry->args.size()) {
				// implicit args at the end
				const auto implicit_arg_count = (has_flag<FUNCTION_FLAGS::USES_SOFT_PRINTF>(entry->flags) ? 1u : 0u);
				if (idx.arg < entry->args.size() + implicit_arg_count) {
					idx.is_implicit = true;
				} else { // actual end
					// get the next entry
					++idx.entry;
					// reset
					idx.arg = 0;
					idx.is_implicit = false;
					idx.implicit = 0;
					idx.buffer_idx = 0;
					idx.texture_idx = 0;
					continue;
				}
			}
			break;
		}
		return entry;
	}
	
	//! increments indicies dependent on the arg
	static inline void arg_post_handler(const function_info& entry, idx_handler& idx, const compute_kernel_arg& arg) {
		// advance all indices
		if (idx.is_implicit) {
			++idx.implicit;
			// always a buffer for now
			++idx.buffer_idx;
		} else {
			if (entry.args[idx.arg].image_type == ARG_IMAGE_TYPE::NONE) {
				// buffer
				if (auto vec_buf_ptrs = get_if<const vector<compute_buffer*>*>(&arg.var)) {
					idx.buffer_idx += (*vec_buf_ptrs)->size();
				} else if (auto vec_buf_sptrs = get_if<const vector<shared_ptr<compute_buffer>>*>(&arg.var)) {
					idx.buffer_idx += (*vec_buf_sptrs)->size();
				} else {
					++idx.buffer_idx;
				}
			} else {
				// texture
				size_t tex_arg_count = 0;
				if (auto vec_img_ptrs = get_if<const vector<compute_image*>*>(&arg.var)) {
					tex_arg_count = (*vec_img_ptrs)->size();
				} else if (auto vec_img_sptrs = get_if<const vector<shared_ptr<compute_image>>*>(&arg.var)) {
					tex_arg_count = (*vec_img_sptrs)->size();
				} else {
					tex_arg_count = 1;
				}
				
				idx.texture_idx += tex_arg_count;
				if (entry.args[idx.arg].image_access == ARG_IMAGE_ACCESS::READ_WRITE) {
					// read/write images are implemented as two images -> add twice
					idx.texture_idx += tex_arg_count;
				}
			}
		}
		// finally
		++idx.arg;
	}
	
	//! sets and handles all arguments in the compute/vertex/fragment function
	template <ENCODER_TYPE enc_type>
	bool set_and_handle_arguments(encoder_selector_t<enc_type> encoder,
								  const vector<const function_info*>& entries,
								  const vector<compute_kernel_arg>& args,
								  const vector<compute_kernel_arg>& implicit_args) {
		const size_t arg_count = args.size() + implicit_args.size();
		idx_handler idx;
		size_t explicit_idx = 0, implicit_idx = 0;
		for (size_t i = 0; i < arg_count; ++i) {
			auto entry = arg_pre_handler(entries, idx);
			const auto& arg = (!idx.is_implicit ? args[explicit_idx++] : implicit_args[implicit_idx++]);
			
			if (auto buf_ptr = get_if<const compute_buffer*>(&arg.var)) {
				set_argument<enc_type>(idx, encoder, *entry, *buf_ptr);
			} else if (auto vec_buf_ptrs = get_if<const vector<compute_buffer*>*>(&arg.var)) {
				set_argument<enc_type>(idx, encoder, *entry, **vec_buf_ptrs);
			} else if (auto vec_buf_sptrs = get_if<const vector<shared_ptr<compute_buffer>>*>(&arg.var)) {
				set_argument<enc_type>(idx, encoder, *entry, **vec_buf_sptrs);
			} else if (auto img_ptr = get_if<const compute_image*>(&arg.var)) {
				set_argument<enc_type>(idx, encoder, *entry, *img_ptr);
			} else if (auto vec_img_ptrs = get_if<const vector<compute_image*>*>(&arg.var)) {
				set_argument<enc_type>(idx, encoder, *entry, **vec_img_ptrs);
			} else if (auto vec_img_sptrs = get_if<const vector<shared_ptr<compute_image>>*>(&arg.var)) {
				set_argument<enc_type>(idx, encoder, *entry, **vec_img_sptrs);
			} else if (auto arg_buf_ptr = get_if<const argument_buffer*>(&arg.var)) {
				set_argument<enc_type>(idx, encoder, *entry, *arg_buf_ptr);
			} else if (auto generic_arg_ptr = get_if<const void*>(&arg.var)) {
				set_argument<enc_type>(idx, encoder, *entry, *generic_arg_ptr, arg.size);
			} else {
				log_error("encountered invalid arg");
				return false;
			}
			
			arg_post_handler(*entry, idx, arg);
		}
		return true;
	}
	
} // namespace metal_args
