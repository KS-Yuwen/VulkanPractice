#include "image_resource.h"
#include <stdexcept>

bool DepthBuffer::Initialize(VkExtent2D extent, VkFormat depthFormat)
{
    auto& vulkanCtx = VulkanContext::Get();
    auto device = vulkanCtx.GetVkDevice();

    m_format = depthFormat;
    m_extent = extent;
    m_mipLevels = 1;

    VkImageCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = m_format,
        .extent = { extent.width, extent.height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    if (vkCreateImage(device, &createInfo, nullptr, &m_image) != VK_SUCCESS)
    {
        return false;
    }

    // メモリ要件の取得
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, m_image, &memRequirements);

    VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkMemoryAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = vulkanCtx.FindMemoryType(memRequirements, memProps)
    };
    if (vkAllocateMemory(device, &allocInfo, nullptr, &m_memory) != VK_SUCCESS)
    {
        return false;
    }

    if (vkBindImageMemory(device, m_image, m_memory, 0) != VK_SUCCESS)
    {
        return false;
    }

    m_subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
        .baseMipLevel = 0, .levelCount = createInfo.mipLevels,
        .baseArrayLayer = 0, .layerCount = 1,
    };

    // ビューの作成
    VkImageViewCreateInfo viewCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = m_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = createInfo.format,
        .subresourceRange = m_subresourceRange,
    };
    if (vkCreateImageView(device, &viewCreateInfo, nullptr, &m_imageView) != VK_SUCCESS)
    {
        return false;
    }

    return true;
}

void DepthBuffer::Cleanup()
{
    auto& vulkanCtx = VulkanContext::Get();
    auto device = vulkanCtx.GetVkDevice();

    if (m_imageView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(device, m_imageView, nullptr);
    }
    if (m_image != VK_NULL_HANDLE)
    {
        vkDestroyImage(device, m_image, nullptr);
    }
    if (m_memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(device, m_memory, nullptr);
    }
    m_image = VK_NULL_HANDLE;
    m_imageView = VK_NULL_HANDLE;
    m_memory = VK_NULL_HANDLE;
}

bool Texture2D::Initialize(VkExtent2D extent, VkFormat format, uint32_t mipLevels)
{
    auto& vulkanCtx = VulkanContext::Get();
    auto device = vulkanCtx.GetVkDevice();

    m_format = format;
    m_extent = extent;
    m_mipLevels = mipLevels;

    VkImageUsageFlags usageFlags = VK_IMAGE_USAGE_SAMPLED_BIT;
    usageFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    VkImageCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = m_format,
        .extent = { extent.width, extent.height, 1 },
        .mipLevels = mipLevels,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usageFlags,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    if (vkCreateImage(device, &createInfo, nullptr, &m_image) != VK_SUCCESS)
    {
        return false;
    }

    // メモリ要件の取得
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, m_image, &memRequirements);
    VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkMemoryAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = vulkanCtx.FindMemoryType(memRequirements, memProps)
    };
    if (vkAllocateMemory(device, &allocInfo, nullptr, &m_memory) != VK_SUCCESS)
    {
        return false;
    }

    if (vkBindImageMemory(device, m_image, m_memory, 0) != VK_SUCCESS)
    {
        return false;
    }

    m_subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0, .levelCount = mipLevels,
        .baseArrayLayer = 0, .layerCount = 1,
    };

    // ビューの作成
    VkImageViewCreateInfo viewCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = m_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = m_format,
        .subresourceRange = m_subresourceRange,
    };
    if (vkCreateImageView(device, &viewCreateInfo, nullptr, &m_imageView) != VK_SUCCESS)
    {
        return false;
    }

    return true;
}

void Texture2D::Cleanup()
{
    auto& vulkanCtx = VulkanContext::Get();
    auto device = vulkanCtx.GetVkDevice();
    if (m_imageView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(device, m_imageView, nullptr);
    }
    if (m_image != VK_NULL_HANDLE)
    {
        vkDestroyImage(device, m_image, nullptr);
    }
    if (m_memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(device, m_memory, nullptr);
    }
    m_image = VK_NULL_HANDLE;
    m_imageView = VK_NULL_HANDLE;
    m_memory = VK_NULL_HANDLE;
}

VkDescriptorImageInfo Texture2D::GetDescriptorInfo(VkSampler sampler) const
{
    return VkDescriptorImageInfo{
        .sampler = sampler,
        .imageView = m_imageView,
        .imageLayout = m_layout,
    };
}

bool StorageImage2D::Initialize(VkExtent2D extent, VkFormat format, uint32_t mipLevels)
{
    auto& vulkanCtx = VulkanContext::Get();
    auto device = vulkanCtx.GetVkDevice();

    m_format = format;
    m_extent = extent;
    m_mipLevels = mipLevels;

    VkImageUsageFlags usageFlags = VK_IMAGE_USAGE_SAMPLED_BIT;
    usageFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    usageFlags |= VK_IMAGE_USAGE_STORAGE_BIT;

    VkImageCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = m_format,
        .extent = { extent.width, extent.height, 1 },
        .mipLevels = mipLevels,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usageFlags,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (vkCreateImage(device, &createInfo, nullptr, &m_image) != VK_SUCCESS)
    {
        return false;
    }

    // メモリ要件の取得
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, m_image, &memRequirements);
    VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkMemoryAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = vulkanCtx.FindMemoryType(memRequirements, memProps)
    };
    if (vkAllocateMemory(device, &allocInfo, nullptr, &m_memory) != VK_SUCCESS)
    {
        return false;
    }

    if (vkBindImageMemory(device, m_image, m_memory, 0) != VK_SUCCESS)
    {
        return false;
    }

    // ビューの作成
    m_subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0, .levelCount = mipLevels,
        .baseArrayLayer = 0, .layerCount = 1,
    };
    VkImageViewCreateInfo viewCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = m_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = m_format,
        .subresourceRange = m_subresourceRange,
    };
    if (vkCreateImageView(device, &viewCreateInfo, nullptr, &m_imageView) != VK_SUCCESS)
    {
        return false;
    }
    return true;
}
void StorageImage2D::Cleanup()
{
    auto& vulkanCtx = VulkanContext::Get();
    auto device = vulkanCtx.GetVkDevice();
    if (m_imageView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(device, m_imageView, nullptr);
    }
    if (m_image != VK_NULL_HANDLE)
    {
        vkDestroyImage(device, m_image, nullptr);
    }
    if (m_memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(device, m_memory, nullptr);
    }
    m_image = VK_NULL_HANDLE;
    m_imageView = VK_NULL_HANDLE;
    m_memory = VK_NULL_HANDLE;
}

VkDescriptorImageInfo StorageImage2D::GetTextureReadDescriptorInfo(VkSampler sampler) const
{
    return VkDescriptorImageInfo{
        .sampler = sampler,
        .imageView = m_imageView,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
}
VkDescriptorImageInfo StorageImage2D::GetStorageReadWriteDescriptorInfo(VkSampler sampler) const
{
    return VkDescriptorImageInfo{
        .sampler = sampler,
        .imageView = m_imageView,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    };
}


// 各クラスのテンプレートをインスタンス化
template class ImageResource<DepthBuffer>;
template class ImageResource<Texture2D>;
template class ImageResource<StorageImage2D>;
