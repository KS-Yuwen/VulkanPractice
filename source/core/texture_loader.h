#pragma once
#include <memory>
#include <vector>
#include <filesystem>

#include "buffer_resource.h"
#include "image_resource.h"

namespace loader
{
	// テクスチャローダー
	struct TextureUploadRequest
	{
		std::shared_ptr<StagingBuffer> staging;
		std::vector<VkBufferImageCopy> copyRegions;

		VkAccessFlags nextAccessFlags;
		VkImageLayout nextLayout;
		VkPipelineStageFlags nextStageFlags;
	};

	std::tuple<std::shared_ptr<Texture2D>, TextureUploadRequest>
		LoadTexture2DFromFile(const std::filesystem::path& filePath, bool generateMips = true);
}
