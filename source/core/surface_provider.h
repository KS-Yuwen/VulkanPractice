#pragma once
#include <vulkan/vulkan.h>

class ISurfaceProvider {
public:
	virtual ~ISurfaceProvider() = default;
	virtual VkSurfaceKHR CreateSurface(VkInstance instance) = 0;
	virtual uint32_t GetFramebufferWidth() const = 0;
	virtual uint32_t GetFramebufferHeight() const = 0;
};
