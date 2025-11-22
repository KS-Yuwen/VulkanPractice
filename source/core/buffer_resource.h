#pragma once
#include "vulkan_context.h"
#include "gpu_resource_base.h"

class IBufferResource
{
public:
	virtual bool IsHostAccessible() const = 0;
	virtual VkBuffer GetVkBuffer() const = 0;
	virtual VkDeviceSize GetBufferSize() const = 0;

	virtual void SetAccessFlags(const VkAccessFlags flags) = 0;
	virtual VkAccessFlags GetAccessFlags() const = 0;

	virtual void* Map() = 0;
	virtual void Unmap() = 0;

	virtual VkDescriptorBufferInfo GetDescriptorInfo() const = 0;
};

template<typename T>
class BufferResource : public GpuResourceBase<T>, public IBufferResource
{
public:
	BufferResource(const BufferResource&) = delete;
	BufferResource& operator=(const BufferResource&) = delete;

	virtual ~BufferResource() { Cleanup(); }
	virtual void Cleanup();

	bool IsHostAccessible() const override { return (m_memProps & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0; }
	VkAccessFlags GetAccessFlags() const override { return m_accessFlags; }
	void SetAccessFlags(const VkAccessFlags flags) override { m_accessFlags = flags; }

	VkBuffer GetVkBuffer() const override { return m_buffer; }
	VkDeviceSize GetBufferSize() const override { return m_size; }

	VkDescriptorBufferInfo GetDescriptorInfo() const override;

protected:
	BufferResource() = default;

	bool CreateBuffer(const VkBufferCreateInfo& createInfo, VkMemoryPropertyFlags memProps);
	VkBuffer m_buffer{};
	VkDeviceMemory m_memory{};
	VkDeviceSize m_size{};
	VkMemoryPropertyFlags m_memProps{};
	VkAccessFlags m_accessFlags = VK_ACCESS_NONE;

};

class VertexBuffer : public BufferResource<VertexBuffer>
{
	friend class GpuResourceBase<VertexBuffer>;
private:
	VertexBuffer() = default;
public:
	virtual ~VertexBuffer() = default;
	virtual void* Map() override;
	virtual void Unmap() override;

	bool Initialize(VkDeviceSize size, VkMemoryPropertyFlags memProps);

	// Create, Initializeを1度で処理するための作成関数
	static std::shared_ptr<VertexBuffer> Create(VkDeviceSize size, VkMemoryPropertyFlags memProps)
	{
		auto buffer = GpuResourceBase::Create();
		if (!buffer->Initialize(size, memProps))
		{
			return nullptr;
		}
		return buffer;
	}
};

class IndexBuffer : public BufferResource<IndexBuffer>
{
    friend class GpuResourceBase<IndexBuffer>;
private:
    IndexBuffer() = default;
public:
    virtual ~IndexBuffer() = default;

    virtual void* Map() override;
    virtual void Unmap() override;

    bool Initialize(VkDeviceSize size, VkMemoryPropertyFlags memProps);

    // Create, Initialize を1度で処理するための作成関数
    static std::shared_ptr<IndexBuffer> Create(VkDeviceSize size, VkMemoryPropertyFlags memProps)
    {
        auto buffer = GpuResourceBase::Create();
        if (!buffer->Initialize(size, memProps)) { return nullptr; }
        return buffer;
    }
};

class UniformBuffer : public BufferResource<UniformBuffer>
{
    friend class GpuResourceBase<UniformBuffer>;
public:
    UniformBuffer() = default;
    virtual ~UniformBuffer() = default;

    virtual void* Map() override;
    virtual void Unmap() override;

    bool Initialize(VkDeviceSize size);

    // Create, Initialize を1度で処理するための作成関数
    static std::shared_ptr<UniformBuffer> Create(VkDeviceSize size)
    {
        auto buffer = GpuResourceBase::Create();
        if (!buffer->Initialize(size)) { return nullptr; }
        return buffer;
    }
};

// ステージングバッファ
class StagingBuffer : public BufferResource<StagingBuffer>
{
    friend class GpuResourceBase<StagingBuffer>;
private:
    StagingBuffer() = default;

public:
    virtual ~StagingBuffer() = default;

    virtual void* Map() override;
    virtual void Unmap() override;

    bool Initialize(VkDeviceSize size);

    // Create, Initialize を1度で処理するための作成関数
    static std::shared_ptr<StagingBuffer> Create(VkDeviceSize size)
    {
        auto buffer = GpuResourceBase::Create();
        if (!buffer->Initialize(size)) { return nullptr; }
        return buffer;
    }
};