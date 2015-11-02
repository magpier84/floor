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

#include <floor/compute/metal/metal_kernel.hpp>

#if !defined(FLOOR_NO_METAL)

#include <floor/compute/metal/metal_queue.hpp>
#include <floor/compute/metal/metal_buffer.hpp>
#include <floor/compute/metal/metal_image.hpp>

struct metal_kernel::metal_encoder {
	id <MTLCommandBuffer> cmd_buffer;
	id <MTLComputeCommandEncoder> encoder;
	unordered_set<metal_buffer*> buffers;
	unordered_set<metal_image*> images;
};

metal_kernel::metal_kernel(kernel_map_type&& kernels_) : kernels(kernels_) {
}

metal_kernel::~metal_kernel() {}

void metal_kernel::execute_internal(shared_ptr<metal_encoder> encoder,
									const uint3& grid_dim,
									const uint3& block_dim) {
	// TODO/NOTE: guarantee that all buffers have finished their prior processing
	const MTLSize metal_grid_dim { grid_dim.x, grid_dim.y, grid_dim.z };
	const MTLSize metal_block_dim { block_dim.x, block_dim.y, block_dim.z };
	[encoder->encoder dispatchThreadgroups:metal_grid_dim threadsPerThreadgroup:metal_block_dim];
	[encoder->encoder endEncoding];
	[encoder->cmd_buffer commit];
}

typename metal_kernel::kernel_map_type::const_iterator metal_kernel::get_kernel(const compute_queue* queue) const {
	return kernels.find((metal_device*)queue->get_device().get());
}

shared_ptr<metal_kernel::metal_encoder> metal_kernel::create_encoder(compute_queue* queue, const kernel_entry& entry) {
	id <MTLCommandBuffer> cmd_buffer = ((metal_queue*)queue)->make_command_buffer();
	auto ret = make_shared<metal_encoder>(metal_encoder {
		cmd_buffer, [cmd_buffer computeCommandEncoder], {}, {}
	});
	[ret->encoder setComputePipelineState:(__bridge id <MTLComputePipelineState>)entry.kernel_state];
	return ret;
}

void metal_kernel::set_const_parameter(metal_encoder* encoder, const uint32_t& idx,
									   const void* ptr, const size_t& size) {
	[encoder->encoder setBytes:ptr length:size atIndex:idx];
}

void metal_kernel::set_kernel_argument(uint32_t&, uint32_t& buffer_idx, uint32_t&,
									   metal_encoder* encoder, const kernel_entry&,
									   shared_ptr<compute_buffer> arg) {
	[encoder->encoder setBuffer:((metal_buffer*)arg.get())->get_metal_buffer()
						 offset:0
						atIndex:buffer_idx++];
	encoder->buffers.emplace((metal_buffer*)arg.get());
}

void metal_kernel::set_kernel_argument(uint32_t& total_idx, uint32_t&, uint32_t& texture_idx,
									   metal_encoder* encoder, const kernel_entry& entry,
									   shared_ptr<compute_image> arg) {
	[encoder->encoder setTexture:((metal_image*)arg.get())->get_metal_image()
						 atIndex:texture_idx++];
	
	// if this is a read/write image, add it again (one is read-only, the other is write-only)
	if(entry.info->args[total_idx].image_access == llvm_compute::kernel_info::ARG_IMAGE_ACCESS::READ_WRITE) {
		[encoder->encoder setTexture:((metal_image*)arg.get())->get_metal_image()
							 atIndex:texture_idx++];
	}
	
	encoder->images.emplace((metal_image*)arg.get());
}

#endif
