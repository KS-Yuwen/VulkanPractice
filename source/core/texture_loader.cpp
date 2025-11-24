#include "texture_loader.h"
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <stdexcept>
#include <cmath>

namespace {
    uint32_t CalcMipLevels(VkExtent2D extent)
    {
        auto w = extent.width;
        auto h = extent.height;
        return uint32_t(std::floor(std::log2(std::max(w, h)))) + 1;
    }
}

namespace loader
{
    std::tuple<std::shared_ptr<Texture2D>, TextureUploadRequest>
        LoadTexture2DFromFile(const std::filesystem::path& filePath, bool generateMips)
    {
        int width, height, channnels;
        auto fullPath = std::filesystem::absolute(filePath);
        auto* pixels = stbi_load(
            fullPath.string().c_str(),
            &width,
            &height,
            &channnels,
            STBI_rgb_alpha);

        if (!pixels)
        {
            throw std::runtime_error("Failed to load image file: " + fullPath.string());
        }

        VkExtent2D extent{
            .width = uint32_t(width),
            .height = uint32_t(height),
        };

        VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
        auto mipLevels = generateMips ? CalcMipLevels(extent) : 1;
        size_t imageSize = width * height * sizeof(uint32_t);

        // 転送先となるテクスチャの準備
        auto texture = std::make_shared<Texture2D>();
        texture->Initialize(
            extent,
            format,
            mipLevels);

        // ステージングバッファの作成
        auto staging = StagingBuffer::Create(imageSize);
        memcpy(
            staging->Map(),
            pixels,
            imageSize);
        staging->Unmap();

        stbi_image_free(pixels);
        pixels = nullptr;

        // 転送領域指定
        VkBufferImageCopy region{
            .bufferOffset = 0,
            .bufferRowLength = 0,   // 隙間なく格納されていることを示す
            .bufferImageHeight = 0, // 隙間なく格納されていることを示す
            .imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .imageOffset = {0, 0, 0},
            .imageExtent = {
                .width = extent.width,
                .height = extent.height,
                .depth = 1,
            },
        };

        TextureUploadRequest request{
            .staging = staging,
            .copyRegions = {region},
            .nextAccessFlags = VK_ACCESS_SHADER_READ_BIT,
            .nextLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .nextStageFlags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        };

        return { std::move(texture), request };
    }
}
