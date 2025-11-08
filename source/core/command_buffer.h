#pragma once
#include "vulkan_context.h"
#include "image_barrier.h"

class ImageLayoutTransition;

class CommandBuffer
{
public:
	CommandBuffer(VkCommandBuffer commandBuffer);
	virtual ~CommandBuffer();

	void Begin(VkCommandBufferUsageFlags usageFlag = 0);
	void End();
	void Reset();

	VkCommandBuffer Get() const { return m_commandBuffer; }

	operator VkCommandBuffer() { return m_commandBuffer; }
	operator VkCommandBuffer() const { return m_commandBuffer; }

	void TransitionLayout(VkImage image, const VkImageSubresourceRange& range, const ImageLayoutTransition& transition);

private:
	VkCommandBuffer m_commandBuffer{};
};