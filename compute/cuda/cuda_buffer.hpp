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

#ifndef __FLOOR_CUDA_BUFFER_HPP__
#define __FLOOR_CUDA_BUFFER_HPP__

#include <floor/compute/cuda/cuda_common.hpp>

#if !defined(FLOOR_NO_CUDA)

#include <floor/compute/compute_buffer.hpp>

class cuda_device;
class cuda_buffer final : public compute_buffer {
public:
	cuda_buffer(const cuda_device* device,
				const size_t& size_,
				void* host_ptr,
				const COMPUTE_BUFFER_FLAG flags_ = (COMPUTE_BUFFER_FLAG::READ_WRITE |
													COMPUTE_BUFFER_FLAG::HOST_READ_WRITE));
	
	cuda_buffer(const cuda_device* device,
				const size_t& size_,
				const COMPUTE_BUFFER_FLAG flags_ = (COMPUTE_BUFFER_FLAG::READ_WRITE |
													COMPUTE_BUFFER_FLAG::HOST_READ_WRITE)) :
	cuda_buffer(device, size_, nullptr, flags_) {}
	
	template <typename data_type>
	cuda_buffer(const cuda_device* device,
				const vector<data_type>& data,
				const COMPUTE_BUFFER_FLAG flags_ = (COMPUTE_BUFFER_FLAG::READ_WRITE |
													COMPUTE_BUFFER_FLAG::HOST_READ_WRITE)) :
	cuda_buffer(device, sizeof(data_type) * data.size(), (void*)&data[0], flags_) {}
	
	template <typename data_type, size_t n>
	cuda_buffer(const cuda_device* device,
				const array<data_type, n>& data,
				const COMPUTE_BUFFER_FLAG flags_ = (COMPUTE_BUFFER_FLAG::READ_WRITE |
													COMPUTE_BUFFER_FLAG::HOST_READ_WRITE)) :
	cuda_buffer(device, sizeof(data_type) * n, (void*)&data[0], flags_) {}
	
	~cuda_buffer() override;
	
	//! reads "size" bytes (or the complete buffer if 0) from "offset" onwards
	//! back to the previously specified host pointer
	void read(shared_ptr<compute_queue> cqueue, const size_t size = 0, const size_t offset = 0) override;
	//! reads "size" bytes (or the complete buffer if 0) from "offset" onwards
	//! back to the specified "dst" pointer
	void read(shared_ptr<compute_queue> cqueue, void* dst, const size_t size = 0, const size_t offset = 0) override;
	
	//! writes "size" bytes (or the complete buffer if 0) from "offset" onwards
	//! from the previously specified host pointer to this buffer
	void write(shared_ptr<compute_queue> cqueue, const size_t size = 0, const size_t offset = 0) override;
	//! writes "size" bytes (or the complete buffer if 0) from "offset" onwards
	//! from the specified "src" pointer to this buffer
	void write(shared_ptr<compute_queue> cqueue, const void* src, const size_t size = 0, const size_t offset = 0) override;
	
	//!
	void copy(shared_ptr<compute_queue> cqueue,
			  shared_ptr<compute_buffer> src,
			  const size_t size = 0, const size_t src_offset = 0, const size_t dst_offset = 0) override;
	
	//!
	void fill(shared_ptr<compute_queue> cqueue,
			  const void* pattern, const size_t& pattern_size,
			  const size_t size = 0, const size_t offset = 0) override;
	
	//! zeros/clears the complete buffer
	void zero(shared_ptr<compute_queue> cqueue) override;
	
	//! resizes (recreates) the buffer to "size" and either copies the old data from the old buffer if specified,
	//! or copies the data (again) from the previously specified host pointer or the one provided to this call,
	//! and will also update the host memory pointer (if used!) to "new_host_ptr" if set to non-nullptr
	bool resize(shared_ptr<compute_queue> cqueue,
				const size_t& size,
				const bool copy_old_data = false,
				const bool copy_host_data = false,
				void* new_host_ptr = nullptr) override;
	
	//!
	//! NOTE: this will always be a blocking call!
	void* __attribute__((aligned(128))) map(shared_ptr<compute_queue> cqueue,
											const COMPUTE_BUFFER_MAP_FLAG flags =
											(COMPUTE_BUFFER_MAP_FLAG::READ_WRITE |
											 COMPUTE_BUFFER_MAP_FLAG::BLOCK),
											const size_t size = 0, const size_t offset = 0) override;
	
	//!
	//! NOTE: this will always be a blocking call!
	void unmap(shared_ptr<compute_queue> cqueue, void* __attribute__((aligned(128))) mapped_ptr) override;
	
	//!
	const CUdeviceptr& get_cuda_buffer() const {
		return buffer;
	}
	
protected:
	CUdeviceptr buffer { 0ull };
	
	struct cuda_mapping {
		const size_t size;
		const size_t offset;
		const COMPUTE_BUFFER_MAP_FLAG flags;
	};
	// stores all mapped pointers and the mapped buffer
	unordered_map<void*, cuda_mapping> mappings;
	
	// separate create buffer function, b/c it's called by the constructor and resize
	bool create_internal(const bool copy_host_data);
	
};

#endif

#endif