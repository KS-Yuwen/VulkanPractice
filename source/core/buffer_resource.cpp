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

// 各クラスのテンプレートをインスタンス化
template class BufferResource<VertexBuffer>;