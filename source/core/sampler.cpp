#include "sampler.h"
#include <stdexcept>

void Sampler::Initialize(
	VkFilter minFilter,
	VkFilter magFilter,
	VkSamplerMipmapMode mipMapMode,
	VkSamplerAddressMode addrModeU,
	VkSamplerAddressMode addrModeV,
	float minLod,
	float maxLod)
{
	auto& vulkanCtx = VulkanContext::Get();
	VkSamplerCreateInfo createInfo{
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = magFilter,
		.minFilter = minFilter,
		.mipmapMode = mipMapMode,
		.addressModeU = addrModeU,
		.addressModeV = addrModeV,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.maxAnisotropy = 1.0f,
		.minLod = minLod,
		.maxLod = maxLod,
	};

	auto result = vkCreateSampler(
		vulkanCtx.GetVkDevice(),
		&createInfo,
		nullptr,
		&m_sampler);

	if (result != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to vkCreateSampler.");
	}
}

void Sampler::Cleanup()
{
	auto& vulkanCtx = VulkanContext::Get();
	if (m_sampler != VK_NULL_HANDLE)
	{
		vkDestroySampler(
			vulkanCtx.GetVkDevice(),
			m_sampler,
			nullptr);
	}

	m_sampler = VK_NULL_HANDLE;
}
