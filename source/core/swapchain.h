#pragma once
#include <map>
#include <string>
#include "vulkan_context.h"
#include "surface_provider.h"

class VulkanContext;
class Swapchain
{
public:
	Swapchain() = default;

	bool Recreate(uint32_t newWidth, uint32_t newHeight);
	void Cleanup();

	VkResult AcquireNextImage();
	VkResult QueuePresent(VkQueue queuePresent);

	operator const VkSwapchainKHR() { return m_swapchain; }

	VkSurfaceFormatKHR GetFormat() const { return m_imageFormat; }
	VkExtent2D GetExtent() const { return m_imageExtent; }

	uint32_t GetCurrentIndex() const { return m_currentIndex; }
	uint32_t GetImageCount() const { return uint32_t(m_images.size()); }
	VkImage GetCurrentImage() const { return m_images[m_currentIndex]; }
	VkImageView GetCurrentView() const { return m_imageViews[m_currentIndex]; }

	VkSemaphore GetPresentCompleteSemaphore() const;
	VkSemaphore GetRenderCompleteSemaphore() const;
	std::vector<VkImageView> GetImageViews() const { return m_imageViews; }

private:
	void CreateFrameContext();
	void DestroyFrameContext();

	VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
	uint32_t m_currentIndex = 0;

	VkSurfaceFormatKHR m_imageFormat{};
	VkExtent2D m_imageExtent{};
	std::vector<VkImage> m_images;
	std::vector<VkImageView> m_imageViews;

	struct FrameContext
	{
		VkSemaphore renderComplete = VK_NULL_HANDLE;
		VkSemaphore presentComplete = VK_NULL_HANDLE;
	};

	std::vector<FrameContext> m_frames;
	std::vector<VkSemaphore> m_presentSemaphoreList;

	friend class VulkanContext;
};
