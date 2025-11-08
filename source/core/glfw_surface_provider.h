#pragma once
#include "surface_provider.h"
#define GLFW_INCULDE_VALKAN
#include <GLFW/glfw3.h>

class GLFWSurfaceProvider : public ISurfaceProvider
{
public:
	explicit GLFWSurfaceProvider(GLFWwindow* window);
	VkSurfaceKHR CreateSurface(VkInstance instance) override;
	uint32_t GetFramebufferWidth() const override;
	uint32_t GetFramebufferHeight() const override;

private:
	GLFWwindow* m_window;
};
