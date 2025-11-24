#pragma once
#include <memory>
#include <vector>

#include "vulkan_context.h"
#include "command_buffer.h"
#include "buffer_resource.h"
#include "image_resource.h"
#include "texture_loader.h"

class ResourceUploader
{
public:
    ResourceUploader() = default;
    ~ResourceUploader() = default;

    bool Initialize();
    void Cleanup();

    bool UploadBuffer(IBufferResource* target, const void* pData, size_t size, VkAccessFlags nextAccessMask);

    bool UploadImage(std::shared_ptr<IImageResource> target, loader::TextureUploadRequest& request);

    // 登録されている転送処理をまとめて実行する
    // 同期実行を行い、全ての転送処理が完了後に処理が戻る
    void SubmitAndWait();
private:
    struct PendingTransfer
    {
        std::shared_ptr<StagingBuffer> stagingBuffer;
        IBufferResource* destinationBuffer;
        VkAccessFlags    dstAccessMask;
    };
    std::vector<PendingTransfer> m_transferEntries;

    struct PendingImageTransfer
    {
        std::shared_ptr<StagingBuffer> stagingBuffer;
        std::shared_ptr<IImageResource> destinationTexture;
        std::vector<VkBufferImageCopy> copyRegions;
        bool genMipmaps = false;
        VkAccessFlags dstAccessMask;
        VkImageLayout dstImageLayout;
        VkPipelineStageFlags dstStageFlags;
    };
    std::vector<PendingImageTransfer> m_transferImageEntries;

    void CreateMipmap(std::shared_ptr<CommandBuffer> commandBuffer, PendingImageTransfer& entry);

    VkFence m_transferFence = VK_NULL_HANDLE;
};
