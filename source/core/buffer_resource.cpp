#include "buffer_resource.h"
#include "vulkan_context.h"

template<typename T>
void BufferResource<T>::Cleanup()
{
    VulkanContext& context = VulkanContext::Get();
    VkDevice device = context.GetVkDevice();

    if (m_buffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(
            device,
            m_buffer,
            nullptr);
        m_buffer = VK_NULL_HANDLE;
    }

    if (m_memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(
            device,
            m_memory,
            nullptr);
        m_memory = VK_NULL_HANDLE;
    }

    m_size = 0;
}

template<typename T>
VkDescriptorBufferInfo BufferResource<T>::GetDescriptorInfo() const
{
    return VkDescriptorBufferInfo{
        .buffer = m_buffer,
        .offset = 0,
        .range = m_size,
    };
}

template<typename T>
bool BufferResource<T>::CreateBuffer(const VkBufferCreateInfo& createInfo, VkMemoryPropertyFlags memProps)
{
    VulkanContext& context = VulkanContext::Get();
    VkDevice device = context.GetVkDevice();

    auto result = vkCreateBuffer(
        device,
        &createInfo,
        nullptr,
        &m_buffer);
    if (result != VK_SUCCESS)
    {
        return false;
    }

    // メモリ要件を取得
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(
        device,
        m_buffer,
        &memRequirements);

    VkMemoryAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = context.FindMemoryType(memRequirements, memProps),
    };

    result = vkAllocateMemory(
        device,
        &allocInfo,
        nullptr,
        &m_memory);
    if (result != VK_SUCCESS)
    {
        return false;
    }

    vkBindBufferMemory(
        device,
        m_buffer,
        m_memory,
        0);
    m_size = createInfo.size;
    m_memProps = memProps;

    return true;
}


void* VertexBuffer::Map()
{
    if (!(m_memProps & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
    {
        return nullptr;
    }

    void* mapped = nullptr;
    vkMapMemory(VulkanContext::Get().GetVkDevice(), m_memory, 0, m_size, 0, &mapped);
    return mapped;
}

void VertexBuffer::Unmap()
{
    if (!(m_memProps & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
    {
        return;
    }

    vkUnmapMemory(VulkanContext::Get().GetVkDevice(), m_memory);
}

bool VertexBuffer::Initialize(VkDeviceSize size, VkMemoryPropertyFlags memProps)
{
    VulkanContext& context = VulkanContext::Get();
    VkBufferCreateInfo bufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    SetAccessFlags(VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT);
    return CreateBuffer(bufferInfo, memProps);
}

bool IndexBuffer::Initialize(VkDeviceSize size, VkMemoryPropertyFlags memProps)
{
    VkBufferCreateInfo bufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    SetAccessFlags(VK_ACCESS_INDEX_READ_BIT);
    return CreateBuffer(bufferInfo, memProps);
}

void* IndexBuffer::Map()
{
    if (!(m_memProps & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) return nullptr;

    void* mapped = nullptr;
    vkMapMemory(VulkanContext::Get().GetVkDevice(), m_memory, 0, m_size, 0, &mapped);
    return mapped;
}

void IndexBuffer::Unmap()
{
    if (!(m_memProps & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) return;

    vkUnmapMemory(VulkanContext::Get().GetVkDevice(), m_memory);
}


bool UniformBuffer::Initialize(VkDeviceSize size)
{
    VkBufferCreateInfo bufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    SetAccessFlags(VK_ACCESS_SHADER_READ_BIT);
    return CreateBuffer(bufferInfo, memProps);
}

void* UniformBuffer::Map()
{
    void* mapped = nullptr;
    vkMapMemory(VulkanContext::Get().GetVkDevice(), m_memory, 0, m_size, 0, &mapped);
    return mapped;
}

void UniformBuffer::Unmap()
{
    vkUnmapMemory(VulkanContext::Get().GetVkDevice(), m_memory);
}

bool StagingBuffer::Initialize(VkDeviceSize size)
{
    VkBufferCreateInfo bufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    SetAccessFlags(VK_ACCESS_HOST_WRITE_BIT);
    return CreateBuffer(bufferInfo, memProps);
}

void* StagingBuffer::Map()
{
    void* mapped = nullptr;
    VkDevice device = VulkanContext::Get().GetVkDevice();
    vkMapMemory(device, m_memory, 0, m_size, 0, &mapped);
    return mapped;
}

void StagingBuffer::Unmap()
{
    VkDevice device = VulkanContext::Get().GetVkDevice();
    vkUnmapMemory(device, m_memory);
}

void* DynamicUniformBuffer::Map()
{
    auto& vulkanCtx = VulkanContext::Get();
    VkDevice device = vulkanCtx.GetVkDevice();
    auto frameIndex = vulkanCtx.GetCurrentFrameIndex();
    auto offset = frameIndex * m_blockSize;

    void* mapped = nullptr;
	vkMapMemory(device, m_memory, offset, m_blockSize, 0, &mapped);
    return mapped;
}

void DynamicUniformBuffer::Unmap()
{
    auto& vulkanCtx = VulkanContext::Get();
    VkDevice device = vulkanCtx.GetVkDevice();
    auto frameIndex = vulkanCtx.GetCurrentFrameIndex();
    auto offset = frameIndex * m_blockSize;

    VkMappedMemoryRange mappedRange{
        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .memory = m_memory,
        .offset = offset,
        .size = m_blockSize,
    };
	vkFlushMappedMemoryRanges(device, 1, &mappedRange);
    vkUnmapMemory(device, m_memory);
}

bool DynamicUniformBuffer::Initialize(VkDeviceSize size)
{
    auto& vulkanCtx = VulkanContext::Get();
    auto uboOffsetAlignment = vulkanCtx.MinUniformOffsetAlignment();
    auto nonCoherntAtomSize = vulkanCtx.NonCoherentAtomSize();
    auto alignSize = std::max(uboOffsetAlignment, nonCoherntAtomSize);

    // バッファ開始アライメントとサイズのアライメント要件を考慮して、1ブロックサイズを計算
    m_blockSize = (size + alignSize - 1ULL) & ~(alignSize - 1ULL);
    VkDeviceSize bufferSize = m_blockSize * vulkanCtx.MaxInflightFrames;
    VkBufferCreateInfo bufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = bufferSize,
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    SetAccessFlags(VK_ACCESS_SHADER_READ_BIT);
    return CreateBuffer(bufferInfo, memProps);

}

VkDescriptorBufferInfo DynamicUniformBuffer::GetDescriptorInfo() const
{
    return VkDescriptorBufferInfo{
        .buffer = m_buffer,
        .offset = 0,
        .range = m_blockSize,
    };
}

uint32_t DynamicUniformBuffer::GetCurrentOffset() const
{
    auto& vulkanCtx = VulkanContext::Get();
    VkDeviceSize offset = m_blockSize * vulkanCtx.GetCurrentFrameIndex();
    return uint32_t(offset);
}


// 各クラスのテンプレートをインスタンス化
template class BufferResource<VertexBuffer>;
template class BufferResource<IndexBuffer>;
template class BufferResource<UniformBuffer>;
template class BufferResource<StagingBuffer>;
template class BufferResource<DynamicUniformBuffer>;