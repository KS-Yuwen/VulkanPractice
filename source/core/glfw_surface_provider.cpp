#include "glfw_surface_provider.h"
#include <stdexcept>

GLFWSurfaceProvider::GLFWSurfaceProvider(GLFWwindow* window) : m_window(window)
{
}

VkSurfaceKHR GLFWSurfaceProvider::CreateSurface(VkInstance instance)
{
	VkSurfaceKHR surface;
	if (glfwCreateWindowSurface(
		instance,
		m_window,
		nullptr,
		&surface) != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to create GLFW window surface");
	}

	return surface;
}

uint32_t GLFWSurfaceProvider::GetFramebufferWidth() const
{
	int width;
	glfwGetFramebufferSize(m_window, &width, nullptr);
	return width;
}

uint32_t GLFWSurfaceProvider::GetFramebufferHeight() const
{
	int height;
	glfwGetFramebufferSize(m_window, nullptr, &height);
	return height;
}
