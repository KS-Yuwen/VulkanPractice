#pragma once
#include "vulkan_context.h"
#include <filesystem>

namespace loader
{
	VkShaderModule LoadShaderModule(const std::filesystem::path& shaderSpvPath);
};
