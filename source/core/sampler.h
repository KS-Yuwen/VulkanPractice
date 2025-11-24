#pragma once
#include "vulkan_context.h"
#include "gpu_resource_base.h"

class Sampler : public GpuResourceBase<Sampler>
{
	friend class GpuResourceBase<Sampler>;

private:
	Sampler() = default;

public:
	Sampler(const Sampler&) = delete;
	Sampler& operator=(const Sampler&) = delete;

	virtual ~Sampler() { Cleanup(); }
	void Initialize(
		VkFilter minFilter,
		VkFilter magFilter,
		VkSamplerMipmapMode mipMapMode,
		VkSamplerAddressMode addrModeU,
		VkSamplerAddressMode addrModeV,
		float minLod = 0,
		float maxLod = VK_LOD_CLAMP_NONE);
	void Cleanup();

	VkSampler GetVkSampler() const { return m_sampler; }
	operator const VkSampler& () { return m_sampler; }

protected:
	VkSampler m_sampler = VK_NULL_HANDLE;
};
