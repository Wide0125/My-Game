#ifndef MESHBUFFERS_HPP
#define MESHBUFFERS_HPP

#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>

struct MeshBuffers {
	vk::raii::Buffer vertexBuffer {nullptr};
	VmaAllocation vertexAllocation {};

	std::vector<vk::raii::Buffer> indexBuffers {};
	std::vector<VmaAllocation> indexAllocations {};

	uint32_t indicesCount {};

	std::vector<uint32_t> materialIndices {};
};

#endif // !MESHBUFFERS_HPP

