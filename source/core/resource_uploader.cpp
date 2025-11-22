#include "resource_uploader.h"

bool ResourceUploader::Initialize()
{
    VkDevice device = VulkanContext::Get().GetVkDevice();

    // フェンスの作成（1回限りで使い回し）
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    auto result = vkCreateFence(device, &fenceInfo, nullptr, &m_transferFence);
    return result == VK_SUCCESS;
}

void ResourceUploader::Cleanup()
{
    VkDevice device = VulkanContext::Get().GetVkDevice();
    vkDestroyFence(device, m_transferFence, nullptr);
    m_transferFence = VK_NULL_HANDLE;
}

bool ResourceUploader::UploadBuffer(IBufferResource* target, const void* pData, size_t size, VkAccessFlags nextAccessMask)
{
    VulkanContext& context = VulkanContext::Get();
    VkDevice device = context.GetVkDevice();

    if (target->IsHostAccessible())
    {
        // 直接書込みが可能なため、ここで処理
        if (void* p = target->Map(); p != nullptr)
        {
            memcpy(p, pData, size);
            target->Unmap();
            return true;
        }
        return false;
    }

    auto stagingBuffer = StagingBuffer::Create(size);
    if (!stagingBuffer)
    {
        return false;
    }

    void* mapped = stagingBuffer->Map();
    std::memcpy(mapped, pData, size);
    stagingBuffer->Unmap();

    m_transferEntries.emplace_back(PendingTransfer{
        .stagingBuffer = std::move(stagingBuffer),
        .destinationBuffer = target,
        .dstAccessMask = nextAccessMask,
        });
    return true;
}

void ResourceUploader::SubmitAndWait()
{
    if (m_transferEntries.empty() && m_transferImageEntries.empty())
    {
        return;
    }
    VulkanContext& vulkanCtx = VulkanContext::Get();
    VkDevice device = vulkanCtx.GetVkDevice();
    VkCommandPool pool = vulkanCtx.GetCommandPool();
    VkQueue queue = vulkanCtx.GetGraphicsQueue();

    // コマンドバッファ確保
    auto commandBuffer = vulkanCtx.CreateCommandBuffer();
    commandBuffer->Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    // 転送処理を先にすべて記録
    for (auto& entry : m_transferEntries)
    {
        IBufferResource* dst = entry.destinationBuffer;
        auto& src = entry.stagingBuffer;

        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = dst->GetBufferSize();

        vkCmdCopyBuffer(*commandBuffer, src->GetVkBuffer(), dst->GetVkBuffer(), 1, &copyRegion);
    }

    // 転送後バリアをまとめて1回発行
    std::vector<VkBufferMemoryBarrier2> barriers;
    for (auto& entry : m_transferEntries)
    {
        IBufferResource* dst = entry.destinationBuffer;
        auto dstAccessMask = dst->GetAccessFlags();

        VkBufferMemoryBarrier2 barrier{
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .dstAccessMask = dstAccessMask,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = dst->GetVkBuffer(),
            .offset = 0,
            .size = dst->GetBufferSize()
        };
        barriers.push_back(barrier);
    }

    if (!barriers.empty())
    {
        VkDependencyInfo depInfo{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .bufferMemoryBarrierCount = static_cast<uint32_t>(barriers.size()),
            .pBufferMemoryBarriers = barriers.data()
        };
        vkCmdPipelineBarrier2(*commandBuffer, &depInfo);
    }

    // 最終的なリソース状態を保存
    for (auto& entry : m_transferEntries) {
        entry.destinationBuffer->SetAccessFlags(entry.dstAccessMask);
    }

    // イメージの処理
    for (auto& entry : m_transferImageEntries)
    {
        auto dst = entry.destinationTexture;
        auto src = entry.stagingBuffer;

        VkImageSubresourceRange range{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        };

        VkImageMemoryBarrier2 barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = dst->GetVkImage(),
            .subresourceRange = range
        };
        VkDependencyInfo dependencyInfo{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier,
        };
        vkCmdPipelineBarrier2(*commandBuffer, &dependencyInfo);

        // イメージを転送して書込み
        vkCmdCopyBufferToImage(
            *commandBuffer,
            src->GetVkBuffer(),
            dst->GetVkImage(),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            uint32_t(entry.copyRegions.size()),
            entry.copyRegions.data()
        );

        // Mipmap生成処理
        if (entry.genMipmaps)
        {
            CreateMipmap(commandBuffer, entry);
        }
        else
        {
            barrier = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstStageMask = entry.dstStageFlags,
                .dstAccessMask = entry.dstAccessMask,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout = entry.dstImageLayout,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = dst->GetVkImage(),
                .subresourceRange = range
            };
            dependencyInfo.pImageMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(*commandBuffer, &dependencyInfo);

            dst->SetLayout(entry.dstImageLayout);
            dst->SetAccessFlag(entry.dstAccessMask);
        }
    }

    commandBuffer->End();

    auto cmd = commandBuffer->Get();
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkResetFences(device, 1, &m_transferFence);
    vkQueueSubmit(queue, 1, &submitInfo, m_transferFence);
    vkWaitForFences(device, 1, &m_transferFence, VK_TRUE, UINT64_MAX);

    m_transferEntries.clear();
    m_transferImageEntries.clear();
    commandBuffer.reset();
}

void ResourceUploader::CreateMipmap(std::shared_ptr<CommandBuffer> commandBuffer, PendingImageTransfer& entry)
{
    auto dst = entry.destinationTexture;
    auto extent = dst->GetExtent();
    int32_t mipWidth = extent.width, mipHeight = extent.height;
    auto image = dst->GetVkImage();

    VkImageSubresourceRange range{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1
    };

    // Mip:0 を転送元に変更する
    ImageLayoutTransition mip0ToSrc{
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT,
        .dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT,
    };
    commandBuffer->TransitionLayout(image, range, mip0ToSrc);

    // Mip: 1 - N を生成する
    for (uint32_t i = 1; i < dst->GetMipmapCount(); ++i)
    {
        int32_t dstWidth = std::max(1, mipWidth >> 1);
        int32_t dstHeight = std::max(1, mipHeight >> 1);

        // mip i へ書き込むためにレイアウト変更
        auto toDst = ImageLayoutTransition::FromUndefToTransferDst();
        range.baseMipLevel = i;
        commandBuffer->TransitionLayout(image, range, toDst);

        // 縮小転送
        VkImageBlit blit{
            .srcSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = i - 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            },
            .srcOffsets = {
                {0, 0, 0}, {mipWidth, mipHeight, 1}
            },
            .dstSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = i,
                .baseArrayLayer = 0,
                .layerCount = 1
            },
            .dstOffsets = {
                {0, 0, 0}, {dstWidth, dstHeight, 1}
            }
        };
        vkCmdBlitImage(*commandBuffer,
            image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit,
            VK_FILTER_LINEAR
        );

        // mip i を次のBlit用のsrcに遷移させておく
        auto toNextSrc = ImageLayoutTransition::FromTransferDstToTransferSrc();
        commandBuffer->TransitionLayout(image, range, toNextSrc);

        mipWidth = dstWidth;
        mipHeight = dstHeight;
    }

    // 全ミップレベルを最終レイアウトへ変更する
    range.baseMipLevel = 0;
    range.levelCount = dst->GetMipmapCount();

    ImageLayoutTransition toLast{
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .newLayout = entry.dstImageLayout,
        .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .dstAccessMask = entry.dstAccessMask,
        .srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT,
        .dstStage = entry.dstStageFlags,
    };

    commandBuffer->TransitionLayout(image, range, toLast);
    // テクスチャとして使用可能な状態へ
    dst->SetLayout(entry.dstImageLayout);
    dst->SetAccessFlag(entry.dstAccessMask);
}