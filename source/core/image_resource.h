#pragma once
#include "vulkan_context.h"
#include "gpu_resource_base.h"

class IImageResource
{
public:
    virtual VkFormat GetFormat() const = 0;
    virtual VkExtent2D GetExtent() const = 0;
    virtual uint32_t GetMipmapCount() const = 0;

    virtual VkImage GetVkImage() const = 0;
    virtual void SetAccessFlag(const VkAccessFlags flags) = 0;
    virtual VkAccessFlags GetAccessFlags() const = 0;

    virtual void SetLayout(VkImageLayout layout) = 0;
    virtual VkImageLayout GetLayout() const = 0;
};

template<typename T>
class ImageResource : public GpuResourceBase<T>, public IImageResource
{
public:
    ImageResource(const ImageResource&) = delete;
    ImageResource& operator=(const ImageResource&) = delete;

    virtual ~ImageResource() = default;
    virtual void Cleanup() = 0;
    virtual VkFormat GetFormat() const override { return m_format; }
    virtual VkExtent2D GetExtent() const override { return m_extent; }
    virtual uint32_t GetMipmapCount() const override { return m_mipLevels; }

    virtual VkImage GetVkImage() const override { return m_image; }
    virtual void SetAccessFlag(const VkAccessFlags flags) override { m_accessFlags = flags; }
    virtual VkAccessFlags GetAccessFlags() const override { return m_accessFlags; }

    virtual void SetLayout(VkImageLayout layout) override { m_layout = layout; }
    virtual VkImageLayout GetLayout() const override { return m_layout; }

protected:
    ImageResource() = default;

    VkImage m_image = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    VkImageSubresourceRange m_subresourceRange{};
    VkAccessFlags m_accessFlags = VK_ACCESS_NONE;
    VkImageLayout m_layout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkFormat m_format = VK_FORMAT_UNDEFINED;
    VkExtent2D m_extent{};
    uint32_t m_mipLevels{};
};

class DepthBuffer : public ImageResource<DepthBuffer>
{
    friend class GpuResourceBase<DepthBuffer>;
public:
    virtual ~DepthBuffer() { Cleanup(); }
    virtual void Cleanup() override;

    bool Initialize(VkExtent2D extent, VkFormat depthFormat);

    VkImageView GetVkImageView() const { return m_imageView; }

    // Create, Initialize を1度で処理するための作成関数
    static std::shared_ptr<DepthBuffer> Create(VkExtent2D extent, VkFormat depthFormat)
    {
        auto image = GpuResourceBase::Create();
        if (!image->Initialize(extent, depthFormat)) { return nullptr; }
        return image;
    }
private:
    VkImageView m_imageView{};
};

class Texture2D : public ImageResource<Texture2D>
{
    friend class GpuResourceBase<Texture2D>;
public:
    virtual ~Texture2D() { Cleanup(); }
    virtual void Cleanup() override;

    bool Initialize(VkExtent2D extent, VkFormat format, uint32_t mipLevels);

    VkImageView GetVkImageView() const { return m_imageView; }
    VkImageSubresourceRange GetSubresourceRange() const { return m_subresourceRange; }

    VkDescriptorImageInfo GetDescriptorInfo(VkSampler sampler) const;

    // Create, Initialize を1度で処理するための作成関数
    static std::shared_ptr<Texture2D> Create(VkExtent2D extent, VkFormat format, uint32_t mipLevels)
    {
        auto image = GpuResourceBase::Create();
        if (!image->Initialize(extent, format, mipLevels)) { return nullptr; }
        return image;
    }

private:
    VkImageView m_imageView{};

    VkImageSubresourceRange m_subresourceRange{};
};

class StorageImage2D : public ImageResource<StorageImage2D>
{
    friend class GpuResourceBase<StorageImage2D>;
public:
    virtual ~StorageImage2D() { Cleanup(); }
    virtual void Cleanup() override;

    bool Initialize(VkExtent2D extent, VkFormat format, uint32_t mipLevels);

    VkImageView GetVkImageView() const { return m_imageView; }
    VkImageSubresourceRange GetSubresourceRange() const { return m_subresourceRange; }

    VkDescriptorImageInfo GetTextureReadDescriptorInfo(VkSampler sampler) const;
    VkDescriptorImageInfo GetStorageReadWriteDescriptorInfo(VkSampler sampler) const;

    // Create, Initialize を1度で処理するための作成関数
    static std::shared_ptr<StorageImage2D> Create(VkExtent2D extent, VkFormat format, uint32_t mipLevels)
    {
        auto image = GpuResourceBase::Create();
        if (!image->Initialize(extent, format, mipLevels)) { return nullptr; }
        return image;
    }
private:
    VkImageView m_imageView{};

    VkImageSubresourceRange m_subresourceRange{};
};
