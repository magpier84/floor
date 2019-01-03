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

#ifndef __FLOOR_COMPUTE_BUFFER_HPP__
#define __FLOOR_COMPUTE_BUFFER_HPP__

#include <floor/compute/compute_memory.hpp>

FLOOR_PUSH_WARNINGS()
FLOOR_IGNORE_WARNING(weak-vtables)

class compute_buffer : public compute_memory {
public:
	//! constructs a buffer of the specified size, using the host pointer as specified by the flags
	compute_buffer(compute_device* device,
				   const size_t& size_,
				   void* host_ptr,
				   const COMPUTE_MEMORY_FLAG flags_ = (COMPUTE_MEMORY_FLAG::READ_WRITE |
													   COMPUTE_MEMORY_FLAG::HOST_READ_WRITE),
				   const uint32_t opengl_type = 0,
				   const uint32_t external_gl_object_ = 0);
	
	//! constructs an uninitialized buffer of the specified size
	compute_buffer(compute_device* device,
				   const size_t& size_,
				   const COMPUTE_MEMORY_FLAG flags_ = (COMPUTE_MEMORY_FLAG::READ_WRITE |
													   COMPUTE_MEMORY_FLAG::HOST_READ_WRITE),
				   const uint32_t opengl_type_ = 0) :
	compute_buffer(device, size_, nullptr, flags_, opengl_type_) {}
	
	//! constructs a buffer of the specified data (under consideration of the specified flags)
	template <typename data_type>
	compute_buffer(compute_device* device,
				   const vector<data_type>& data,
				   const COMPUTE_MEMORY_FLAG flags_ = (COMPUTE_MEMORY_FLAG::READ_WRITE |
													   COMPUTE_MEMORY_FLAG::HOST_READ_WRITE),
				   const uint32_t opengl_type_ = 0) :
	compute_buffer(device, sizeof(data_type) * data.size(), (void*)&data[0], flags_, opengl_type_) {}
	
	//! constructs a buffer of the specified data (under consideration of the specified flags)
	template <typename data_type, size_t n>
	compute_buffer(compute_device* device,
				   const array<data_type, n>& data,
				   const COMPUTE_MEMORY_FLAG flags_ = (COMPUTE_MEMORY_FLAG::READ_WRITE |
													   COMPUTE_MEMORY_FLAG::HOST_READ_WRITE),
				   const uint32_t opengl_type_ = 0) :
	compute_buffer(device, sizeof(data_type) * n, (void*)&data[0], flags_, opengl_type_) {}
	
	virtual ~compute_buffer() = 0;
	
	//! reads "size" bytes (or the complete buffer if 0) from "offset" onwards
	//! back to the previously specified host pointer
	virtual void read(shared_ptr<compute_queue> cqueue, const size_t size = 0, const size_t offset = 0) = 0;
	//! reads "size" bytes (or the complete buffer if 0) from "offset" onwards
	//! back to the specified "dst" pointer
	virtual void read(shared_ptr<compute_queue> cqueue, void* dst, const size_t size = 0, const size_t offset = 0) = 0;
	
	//! reads sizeof("dst") bytes from "offset" onwards to "dst"
	template <typename T>
	floor_inline_always void read_to(T& dst, shared_ptr<compute_queue> cqueue, const size_t offset = 0) {
		read(cqueue, &dst, sizeof(T), offset);
	}
	
	//! writes "size" bytes (or the complete buffer if 0) from "offset" onwards
	//! from the previously specified host pointer to this buffer
	virtual void write(shared_ptr<compute_queue> cqueue, const size_t size = 0, const size_t offset = 0) = 0;
	//! writes "size" bytes (or the complete buffer if 0) from "offset" onwards
	//! from the specified "src" pointer to this buffer
	virtual void write(shared_ptr<compute_queue> cqueue, const void* src, const size_t size = 0, const size_t offset = 0) = 0;
	//! writes all of "src" to this buffer, from "offset" on onwards
	template <typename data_type>
	void write(shared_ptr<compute_queue> cqueue, const vector<data_type>& src, const size_t offset = 0) {
		write(cqueue, (const void*)&src[0], sizeof(data_type) * src.size(), offset);
	}
	//! writes all of "src" to this buffer, from "offset" on onwards
	template <typename data_type, size_t n>
	void write(shared_ptr<compute_queue> cqueue, const array<data_type, n>& src, const size_t offset = 0) {
		write(cqueue, (const void*)&src[0], sizeof(data_type) * n, offset);
	}
	
	//! writes sizeof("src") bytes from "src" to "offset" onwards to this buffer
	template <typename T>
	floor_inline_always void write_from(const T& src, shared_ptr<compute_queue> cqueue, const size_t offset = 0) {
		write(cqueue, &src, sizeof(T), offset);
	}
	
	//! copies data from the specified "src" buffer to this buffer, of the specified "size" (complete buffer if size == 0),
	//! from "src_offset" in the "src" buffer to "dst_offset" in this buffer
	virtual void copy(shared_ptr<compute_queue> cqueue, compute_buffer& src,
					  const size_t size = 0, const size_t src_offset = 0, const size_t dst_offset = 0) = 0;
	
	//! copies data from the specified "src" buffer to this buffer, of the specified "size" (complete buffer if size == 0),
	//! from "src_offset" in the "src" buffer to "dst_offset" in this buffer
	void copy(shared_ptr<compute_queue> cqueue, shared_ptr<compute_buffer> src,
			  const size_t size_ = 0, const size_t src_offset_ = 0, const size_t dst_offset_ = 0) {
		return copy(cqueue, *src, size_, src_offset_, dst_offset_);
	}
	
	//! clones this buffer, optionally copying its contents as well
	virtual shared_ptr<compute_buffer> clone(shared_ptr<compute_queue> cqueue, const bool copy_contents = false,
											 const COMPUTE_MEMORY_FLAG flags_override = COMPUTE_MEMORY_FLAG::NONE);
	
	//! fills this buffer with the provided "pattern" of size "pattern_size" (in bytes)
	//! NOTE: filling the buffer with patterns that are 1 byte, 2 bytes or 4 bytes in size might be faster than other sizes
	virtual void fill(shared_ptr<compute_queue> cqueue,
					  const void* pattern, const size_t& pattern_size,
					  const size_t size = 0, const size_t offset = 0) = 0;
	
	//! resizes (recreates) the buffer to "size" and either copies the old data from the old buffer if specified,
	//! or copies the data (again) from the previously specified host pointer or the one provided to this call,
	//! and will also update the host memory pointer (if used!) to "new_host_ptr" if set to non-nullptr
	virtual bool resize(shared_ptr<compute_queue> cqueue,
						const size_t& size,
						const bool copy_old_data = false,
						const bool copy_host_data = false,
						void* new_host_ptr = nullptr) = 0;
	
	//! maps device memory into host accessible memory, of the specified "size" (0 = complete buffer) and buffer "offset"
	//! NOTE: this might require a complete buffer copy on map and/or unmap (use READ, WRITE and WRITE_INVALIDATE appropriately)
	//! NOTE: this call might block regardless of if the BLOCK flag is set or not
	virtual void* __attribute__((aligned(128))) map(shared_ptr<compute_queue> cqueue,
													const COMPUTE_MEMORY_MAP_FLAG flags =
													(COMPUTE_MEMORY_MAP_FLAG::READ_WRITE |
													 COMPUTE_MEMORY_MAP_FLAG::BLOCK),
													const size_t size = 0, const size_t offset = 0) = 0;
	
	//! maps device memory into host accessible memory, of the specified "size" (0 = complete buffer) and buffer "offset",
	//! returning the mapped pointer as an array<> of "data_type" with "n" elements
	//! NOTE: this might require a complete buffer copy on map and/or unmap (use READ, WRITE and WRITE_INVALIDATE appropriately)
	//! NOTE: this call might block regardless of if the BLOCK flag is set or not
	template <typename data_type, size_t n>
	array<data_type, n>* map(shared_ptr<compute_queue> cqueue,
							 const COMPUTE_MEMORY_MAP_FLAG flags_ =
							 (COMPUTE_MEMORY_MAP_FLAG::READ_WRITE |
							  COMPUTE_MEMORY_MAP_FLAG::BLOCK),
							 const size_t size_ = 0, const size_t offset = 0) {
		return (array<data_type, n>*)map(cqueue, flags_, size_, offset);
	}
	
	//! unmaps a previously mapped memory pointer
	//! NOTE: this might require a complete buffer copy on map and/or unmap (use READ, WRITE and WRITE_INVALIDATE appropriately)
	//! NOTE: this call might block regardless of if the BLOCK flag is set or not
	virtual void unmap(shared_ptr<compute_queue> cqueue, void* __attribute__((aligned(128))) mapped_ptr) = 0;
	
	//! returns the size of this buffer (in bytes)
	const size_t& get_size() const { return size; }
	
	//! return struct of get_opengl_buffer_info
	struct opengl_buffer_info {
		uint32_t size { 0u };
		bool valid { false };
	};
	//! helper function to retrieve information from a pre-existing opengl buffer
	static opengl_buffer_info get_opengl_buffer_info(const uint32_t& opengl_buffer,
													 const uint32_t& opengl_type,
													 const COMPUTE_MEMORY_FLAG& flags);
	
protected:
	size_t size { 0u };
	
	// internal function to create/delete an opengl buffer if compute/opengl sharing is used
	bool create_gl_buffer(const bool copy_host_data);
	void delete_gl_buffer();
	
	// buffer size/offset checking (used for debugging/development purposes)
	// NOTE: this can also be enabled by simply defining FLOOR_DEBUG_COMPUTE_BUFFER elsewhere
#if defined(FLOOR_DEBUG) || defined(FLOOR_DEBUG_COMPUTE_BUFFER)
	floor_inline_always static bool read_check(const size_t& buffer_size,
											   const size_t& read_size,
											   const size_t& offset,
											   const COMPUTE_MEMORY_FLAG& buffer_flags) {
		if(read_size == 0) {
			log_warn("read: trying to read 0 bytes!");
		}
		if(offset >= buffer_size) {
			log_error("read: invalid offset (>= size): offset: %X, size: %X", offset, buffer_size);
			return false;
		}
		if(offset + read_size > buffer_size) {
			log_error("read: invalid offset/read size (offset + read size > buffer size): offset: %X, read size: %X, size: %X",
					  offset, read_size, buffer_size);
			return false;
		}
		// should buffer be readable from the host?
		if(!has_flag<COMPUTE_MEMORY_FLAG::HOST_READ>(buffer_flags)) {
			log_error("read: buffer is not readable by the host (HOST_READ buffer flag not set)");
			return false;
		}
		return true;
	}
	
	floor_inline_always static bool write_check(const size_t& buffer_size,
												const size_t& write_size,
												const size_t& offset,
												const COMPUTE_MEMORY_FLAG& buffer_flags) {
		if(write_size == 0) {
			log_warn("write: trying to write 0 bytes!");
		}
		if(offset >= buffer_size) {
			log_error("write: invalid offset (>= size): offset: %X, size: %X", offset, buffer_size);
			return false;
		}
		if(offset + write_size > buffer_size) {
			log_error("write: invalid offset/write size (offset + write size > buffer size): offset: %X, write size: %X, size: %X",
					  offset, write_size, buffer_size);
			return false;
		}
		// should buffer be writable from the host?
		if(!has_flag<COMPUTE_MEMORY_FLAG::HOST_WRITE>(buffer_flags)) {
			log_error("write: buffer is not writable by the host (HOST_WRITE buffer flag not set)");
			return false;
		}
		return true;
	}
	
	floor_inline_always static bool copy_check(const size_t& buffer_size,
											   const size_t& src_size,
											   const size_t& copy_size,
											   const size_t& dst_offset,
											   const size_t& src_offset) {
		if(copy_size == 0) {
			log_warn("copy: trying to copy 0 bytes!");
		}
		if(src_offset >= src_size) {
			log_error("copy: invalid src offset (>= size): offset: %X, size: %X", src_offset, src_size);
			return false;
		}
		if(dst_offset >= buffer_size) {
			log_error("copy: invalid dst offset (>= size): offset: %X, size: %X", dst_offset, buffer_size);
			return false;
		}
		if(src_offset + copy_size > src_size) {
			log_error("copy: invalid src offset/copy size (offset + copy size > buffer size): offset: %X, copy size: %X, size: %X",
					  src_offset, copy_size, src_size);
			return false;
		}
		if(dst_offset + copy_size > buffer_size) {
			log_error("copy: invalid dst offset/copy size (offset + copy size > buffer size): offset: %X, copy size: %X, size: %X",
					  dst_offset, copy_size, buffer_size);
			return false;
		}
		return true;
	}
	
	floor_inline_always static bool fill_check(const size_t& buffer_size,
											   const size_t& fill_size,
											   const size_t& pattern_size,
											   const size_t& offset) {
		if(fill_size == 0) {
			log_error("fill: trying to fill 0 bytes!");
			return false;
		}
		if((offset % pattern_size) != 0) {
			log_error("fill: fill offset must be a multiple of pattern size: offset: %X, pattern size: %X", offset, pattern_size);
			return false;
		}
		if((fill_size % pattern_size) != 0) {
			log_error("fill: fill size must be a multiple of pattern size: fille size: %X, pattern size: %X", fill_size, pattern_size);
			return false;
		}
		if(offset >= buffer_size) {
			log_error("fill: invalid fill offset (>= size): offset: %X, size: %X", offset, buffer_size);
			return false;
		}
		if(offset + fill_size > buffer_size) {
			log_error("fill: invalid fill offset/fill size (offset + size > buffer size): offset: %X, fill size: %X, size: %X",
					  offset, fill_size, buffer_size);
			return false;
		}
		return true;
	}
	
	floor_inline_always static bool map_check(const size_t& buffer_size,
											  const size_t& map_size,
											  const COMPUTE_MEMORY_FLAG& buffer_flags,
											  const COMPUTE_MEMORY_MAP_FLAG& map_flags,
											  const size_t& offset) {
		if(has_flag<COMPUTE_MEMORY_MAP_FLAG::WRITE_INVALIDATE>(map_flags) &&
		   (map_flags & COMPUTE_MEMORY_MAP_FLAG::READ_WRITE) != COMPUTE_MEMORY_MAP_FLAG::NONE) {
			log_error("map: WRITE_INVALIDATE map flag is mutually exclusive with the READ and WRITE flags!");
			return false;
		}
		if(!has_flag<COMPUTE_MEMORY_MAP_FLAG::WRITE_INVALIDATE>(map_flags) &&
		   (map_flags & COMPUTE_MEMORY_MAP_FLAG::READ_WRITE) == COMPUTE_MEMORY_MAP_FLAG::NONE) {
			log_error("map: neither read nor write flags set for buffer mapping!");
			return false;
		}
		if(map_size == 0) {
			log_error("map: trying to map 0 bytes!");
			return false;
		}
		if(offset >= buffer_size) {
			log_error("map: invalid offset (>= size): offset: %X, size: %X", offset, buffer_size);
			return false;
		}
		if(offset + map_size > buffer_size) {
			log_error("map: invalid offset/map size (offset + map size > buffer size): offset: %X, map size: %X, size: %X",
					  offset, map_size, buffer_size);
			return false;
		}
		// should buffer be accessible at all?
		if((buffer_flags & COMPUTE_MEMORY_FLAG::HOST_READ_WRITE) == COMPUTE_MEMORY_FLAG::NONE) {
			log_error("map: buffer has been created with no host access flags, buffer can not be mapped to host memory!");
			return false;
		}
		// read/write mismatch check (only if either read or write set)
		if((buffer_flags & COMPUTE_MEMORY_FLAG::HOST_READ_WRITE) != COMPUTE_MEMORY_FLAG::HOST_READ_WRITE) {
			if(has_flag<COMPUTE_MEMORY_FLAG::HOST_READ>(buffer_flags) &&
			   (has_flag<COMPUTE_MEMORY_MAP_FLAG::WRITE>(map_flags) || has_flag<COMPUTE_MEMORY_MAP_FLAG::WRITE_INVALIDATE>(map_flags))) {
				   log_error("map: buffer has been created with the HOST_READ flag, but map flags specify buffer must be writable!");
				   return false;
			   }
			if(has_flag<COMPUTE_MEMORY_FLAG::HOST_WRITE>(buffer_flags) &&
			   has_flag<COMPUTE_MEMORY_MAP_FLAG::READ>(map_flags)) {
				log_error("map: buffer has been created with the HOST_WRITE flag, but map flags specify buffer must be readable!");
				return false;
			}
		}
		return true;
	}
#else
	floor_inline_always static constexpr bool read_check(const size_t&, const size_t&, const size_t&,
														 const COMPUTE_MEMORY_FLAG&) {
		return true;
	}
	floor_inline_always static constexpr bool write_check(const size_t&, const size_t&, const size_t&,
														  const COMPUTE_MEMORY_FLAG&) {
		return true;
	}
	floor_inline_always static constexpr bool copy_check(const size_t&, const size_t&, const size_t&,
														 const size_t&, const size_t&) {
		return true;
	}
	floor_inline_always static constexpr bool fill_check(const size_t&, const size_t&, const size_t&,
														 const size_t&) {
		return true;
	}
	floor_inline_always static bool map_check(const size_t&, const size_t&,
											  const COMPUTE_MEMORY_FLAG&, const COMPUTE_MEMORY_MAP_FLAG&,
											  const size_t&) {
		return true;
	}
#endif
	
};

FLOOR_POP_WARNINGS()

#endif
