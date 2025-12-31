#include "basic_pbr_render_ms.h"
#include <stdexcept>
#include "../../core/swapchain.h"
#include "../../core/graphics_pipeline_builder.h"

#include  "../../core/asset_path.h"
#include  "../../core/shader_loader.h"

constexpr uint32_t MaxTextureCount = 4096;

namespace Render::ms
{
    void BasicPBRRender::Initialize()
    {
        auto& vulkanCtx = VulkanContext::Get();
        CreateDepthBuffer();

#if defined(USE_RENDERPASS)
        InitializeRenderPass();
#endif

        CreateDescriptorSetLayout();
        CreateGraphicsPipelines();
    }

    void BasicPBRRender::Cleanup()
    {
        auto& vulkanCtx = VulkanContext::Get();
        auto device = vulkanCtx.GetVkDevice();

        if (m_depthBuffer)
        {
            m_depthBuffer->Cleanup();
        }
        m_depthBuffer.reset();

        for (auto [key, value] : m_pipelines)
        {
            vkDestroyPipeline(device, value, nullptr);
        }
        m_pipelines.clear();

        if (m_layouts.sceneLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(device, m_layouts.sceneLayout, nullptr);
        }
        if (m_layouts.materialLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(device, m_layouts.materialLayout, nullptr);
        }

        if (m_layouts.pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(device, m_layouts.pipeline, nullptr);
        }
        m_layouts.sceneLayout = VK_NULL_HANDLE;
        m_layouts.materialLayout = VK_NULL_HANDLE;
        m_layouts.pipeline = VK_NULL_HANDLE;

#if defined(USE_RENDERPASS)
        vkDestroyRenderPass(device, m_renderPass, nullptr);
        for (auto framebuffer : m_framebuffers)
        {
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        }
        m_renderPass = VK_NULL_HANDLE;
        m_framebuffers.clear();
#endif
    }

    void BasicPBRRender::SetSceneDescriptor(VkDescriptorSet set, uint32_t dynamicOffset)
    {
        m_scene.descriptorSet = set;
        m_scene.dynamicOffset = dynamicOffset;
    }
    void BasicPBRRender::BeginScene(std::shared_ptr<CommandBuffer> commandBuffer)
    {
        auto& vulkanCtx = VulkanContext::Get();
        auto& swapchain = vulkanCtx.GetSwapchain();
        auto device = vulkanCtx.GetVkDevice();
        auto extent = swapchain->GetExtent();
#if !defined(USE_RENDERPASS)
        // 描画前：UNDEFINED → COLOR_ATTACHMENT_OPTIMAL
        // VK_ATTACHMENT_LOAD_OP_CLEARを指定のため、常にUNDEFINED指定遷移で問題なし
        VkImageSubresourceRange range{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0, .levelCount = 1,
            .baseArrayLayer = 0, .layerCount = 1,
        };
        commandBuffer->TransitionLayout(
            swapchain->GetCurrentImage(), range,
            ImageLayoutTransition::FromUndefinedToColorAttachment()
        );

        // Color
        VkRenderingAttachmentInfo colorAttachment{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = swapchain->GetCurrentView(),
            .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = VkClearValue{.color = {{0.2f, 0.1f, 0.1f, 0.0f}} }
        };
        // Depth
        VkRenderingAttachmentInfo depthAttachment{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = m_depthBuffer->GetVkImageView(),
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {.depthStencil = { 1.0f, 0 } }
        };
        VkRenderingInfo renderingInfo{
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = { {0, 0}, extent },
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachment,
            .pDepthAttachment = &depthAttachment,
        };
        vkCmdBeginRendering(*commandBuffer, &renderingInfo);
#else
        VkClearValue clearValues[2] = {
            {.color = {0.2f, 0.1f, 0.1f, 0.0f} },
            {.depthStencil = {.depth = 1.0f, .stencil = 0 } }
        };

        VkRenderPassBeginInfo beginInfo{
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = m_renderPass,
            .framebuffer = m_framebuffers[swapchain->GetCurrentIndex()],
            .renderArea = { {0,0}, extent },
            .clearValueCount = 2,
            .pClearValues = clearValues,
        };
        VkSubpassBeginInfo subpassBeginInfo{
            .sType = VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO,
            .contents = VK_SUBPASS_CONTENTS_INLINE,
        };
        vkCmdBeginRenderPass2(*commandBuffer, &beginInfo, &subpassBeginInfo);
#endif
    }

    void BasicPBRRender::EndScene(std::shared_ptr<CommandBuffer> commandBuffer)
    {
        auto& vulkanCtx = VulkanContext::Get();
        auto& swapchain = vulkanCtx.GetSwapchain();

#if !defined(USE_RENDERPASS)
        vkCmdEndRendering(*commandBuffer);

        VkImageSubresourceRange range{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0, .levelCount = 1,
            .baseArrayLayer = 0, .layerCount = 1,
        };
        commandBuffer->TransitionLayout(
            swapchain->GetCurrentImage(), range,
            ImageLayoutTransition::FromColorToPresent());
#else
        // レンダーパスの終了
        VkSubpassEndInfo subpassEndInfo{
            .sType = VK_STRUCTURE_TYPE_SUBPASS_END_INFO,
        };
        vkCmdEndRenderPass2(*commandBuffer, &subpassEndInfo);
#endif
    }

    void BasicPBRRender::Draw(std::shared_ptr<CommandBuffer> commandBuffer, std::shared_ptr<DrawObject> drawObject)
    {
        const auto& model = drawObject->GetModel();

        struct DrawInfo
        {
            glm::mat4 worldMatrix;
            int meshIndex;
            uint32_t meshletPrimitiveIndex;
            uint32_t materialIndex;
        };
        std::vector<DrawInfo> opaqueList, transparentList;
        const auto& materials = model->GetMaterials();
        auto materialDescriptorSet = drawObject->GetMaterialDescriptorSet();

        // ノードの行列とメッシュプリミティブを探索し、描画リストを作成
        for (const auto& node : drawObject->GetNodes())
        {
            for (auto meshIndex : node.meshes)
            {
                const auto& mesh = model->GetMeshes()[meshIndex];

                for (uint32_t i = 0; i < mesh.meshletPrimitives.size(); ++i)
                {
                    using AlphaMode = ms::ModelResource::Material::AlphaMode;
                    const auto& meshletPrimitive = mesh.meshletPrimitives[i];
                    const auto& material = materials[meshletPrimitive.materialIndex];

                    DrawInfo info{
                        .worldMatrix = node.mtxWorld,
                        .meshIndex = meshIndex,
                        .meshletPrimitiveIndex = i,
                        .materialIndex = meshletPrimitive.materialIndex,
                    };

                    switch (material.alphaMode)
                    {
                    case AlphaMode::Opaque:
                    case AlphaMode::Mask:
                        opaqueList.push_back(std::move(info));
                        break;
                    case AlphaMode::Blend:
                        transparentList.push_back(std::move(info));
                        break;
                    }
                }
            }
        }

        auto sceneDescriptorSet = m_scene.descriptorSet;
        uint32_t uboOffsets[] = { m_scene.dynamicOffset };

        // 不透明、アルファマスクのものを描画する
        vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelines[RenderPassTag::Opaque]);
        for (const auto& info : opaqueList)
        {
            const auto& mesh = model->GetMeshes()[info.meshIndex];
            const auto& meshletPrimitive = mesh.meshletPrimitives[info.meshletPrimitiveIndex];

            PushConstantData pushData{
                .mtxWorld = info.worldMatrix,
                .meshletInfoAddr = meshletPrimitive.meshletInfo.buffer->GetDeviceAddress(),
                .meshletVertexIndexBufferAddr = meshletPrimitive.meshletVertexIndexBuffer.buffer->GetDeviceAddress(),
                .meshletPrimitiveIndexBufferAddr = meshletPrimitive.meshletPrimitiveIndexBuffer.buffer->GetDeviceAddress(),
            };
            vkCmdPushConstants(
                *commandBuffer, m_layouts.pipeline,
                VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(pushData), &pushData);

            VkDescriptorSet descriptorSets[] = {
                sceneDescriptorSet, materialDescriptorSet,
            };

            vkCmdBindDescriptorSets(
                *commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layouts.pipeline,
                0, std::size(descriptorSets), descriptorSets,
                std::size(uboOffsets), uboOffsets);

            vkCmdDrawMeshTasksEXT(*commandBuffer, meshletPrimitive.meshletCount, 1, 1);
        }

        // 半透明のものを描画する
        vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelines[RenderPassTag::Transparent]);
        for (const auto& info : transparentList)
        {
            const auto& mesh = model->GetMeshes()[info.meshIndex];
            const auto& meshletPrimitive = mesh.meshletPrimitives[info.meshletPrimitiveIndex];

            PushConstantData pushData{
                .mtxWorld = info.worldMatrix,
                .meshletInfoAddr = meshletPrimitive.meshletInfo.buffer->GetDeviceAddress(),
                .meshletVertexIndexBufferAddr = meshletPrimitive.meshletVertexIndexBuffer.buffer->GetDeviceAddress(),
                .meshletPrimitiveIndexBufferAddr = meshletPrimitive.meshletPrimitiveIndexBuffer.buffer->GetDeviceAddress(),
            };
            vkCmdPushConstants(
                *commandBuffer, m_layouts.pipeline,
                VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(pushData), &pushData);


            VkDescriptorSet descriptorSets[] = {
                sceneDescriptorSet, materialDescriptorSet,
            };

            vkCmdBindDescriptorSets(
                *commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layouts.pipeline,
                0, std::size(descriptorSets), descriptorSets,
                std::size(uboOffsets), uboOffsets);

            vkCmdDrawMeshTasksEXT(*commandBuffer, meshletPrimitive.meshletCount, 1, 1);
        }

    }


    void BasicPBRRender::CreateDepthBuffer()
    {
        auto& vulkanCtx = VulkanContext::Get();
        auto& swapchain = vulkanCtx.GetSwapchain();
        auto extent = swapchain->GetExtent();
        m_depthBuffer = std::make_shared<DepthBuffer>();
        m_depthBuffer->Initialize(extent, VK_FORMAT_D32_SFLOAT);
    }

    void BasicPBRRender::CreateDescriptorSetLayout()
    {
        auto& vulkanCtx = VulkanContext::Get();
        auto device = vulkanCtx.GetVkDevice();

        // ディスクリプタセットレイアウトの作成
        std::vector<VkDescriptorSetLayoutBinding> dsLayoutBindings = {
            { // シーンのユニフォームバッファ(動的)
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
                .pImmutableSamplers = nullptr,
            },
        };
        VkDescriptorSetLayoutCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = uint32_t(dsLayoutBindings.size()),
            .pBindings = dsLayoutBindings.data(),
        };
        vkCreateDescriptorSetLayout(device, &createInfo, nullptr, &m_layouts.sceneLayout);

        dsLayoutBindings = {
            { // マテリアルバッファ
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
                .pImmutableSamplers = nullptr,
            },
            { // テクスチャリスト
                .binding = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = MaxTextureCount,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                .pImmutableSamplers = nullptr,
            },
        };
        createInfo.bindingCount = uint32_t(dsLayoutBindings.size());
        createInfo.pBindings = dsLayoutBindings.data();
        vkCreateDescriptorSetLayout(device, &createInfo, nullptr, &m_layouts.materialLayout);

        if (m_layouts.sceneLayout == VK_NULL_HANDLE || m_layouts.materialLayout == VK_NULL_HANDLE)
        {
            throw std::runtime_error("failed to create DescriptorSetLayout");
        }
    }

    void BasicPBRRender::CreateGraphicsPipelines()
    {
        auto& vulkanCtx = VulkanContext::Get();
        auto device = vulkanCtx.GetVkDevice();
        auto& swapchain = vulkanCtx.GetSwapchain();

        // パイプラインレイアウトを作成する
        // ds[0]: シーンUniformBuffer
        // ds[1]: マテリアル, 各種テクスチャ
        // + PushConstant (WorldMatrix)
        std::vector<VkDescriptorSetLayout> dsLayouts = { m_layouts.sceneLayout, m_layouts.materialLayout };
        VkPushConstantRange pushConstantRange{
            .stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0,
            .size = sizeof(PushConstantData),
        };
        VkPipelineLayoutCreateInfo layoutCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = uint32_t(dsLayouts.size()),
            .pSetLayouts = dsLayouts.data(),
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &pushConstantRange
        };
        vkCreatePipelineLayout(device, &layoutCreateInfo, nullptr, &m_layouts.pipeline);

        GraphicsPipelineBuilder builder{};
        auto swapchainExtent = swapchain->GetExtent();
        builder.SetViewport(swapchainExtent);
        builder.SetPipelineLayout(m_layouts.pipeline);

        // シェーダーの読み込み
        VkShaderModule meshShaderModule = loader::LoadShaderModule(GetAssetPath(AssetType::Shader, "meshshader-model/shader.mesh.spv"));
        VkShaderModule fragShaderModule = loader::LoadShaderModule(GetAssetPath(AssetType::Shader, "meshshader-model/shader.frag.spv"));
        builder.AddShaderStage(VK_SHADER_STAGE_MESH_BIT_EXT, meshShaderModule);
        builder.AddShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, fragShaderModule);

        // 背面をカリングする設定
        VkPipelineRasterizationStateCreateInfo rasterizerState{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_BACK_BIT,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .depthBiasEnable = VK_FALSE,
            .lineWidth = 1.0f,
        };
        builder.SetRasterizationState(rasterizerState);

        // デプスバッファの設定
        //  Opaque向けデプステスト有効
        VkPipelineDepthStencilStateCreateInfo depthStencilState{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .depthCompareOp = VK_COMPARE_OP_LESS,
        };
        builder.SetDepthStencilState(depthStencilState);

#if !defined(USE_RENDERPASS)
        auto colorFormat = swapchain->GetFormat().format;
        auto depthFormat = m_depthBuffer->GetFormat();
        builder.UseDynamicRendering(colorFormat, depthFormat);
#else
        builder.UseRenderPass(m_renderPass, 0);
#endif

        // Opaque のパイプライン作成.( Maskモードでもこのパイプラインを使用する)
        m_pipelines[RenderPassTag::Opaque] = builder.Build();

        // 半透明描画用に設定を更新
        //  デプスバッファはテストは有効だが書込みをしない
        depthStencilState.depthTestEnable = VK_TRUE;
        depthStencilState.depthWriteEnable = VK_FALSE;
        builder.SetDepthStencilState(depthStencilState);
        //  アルファブレンド合成を有効にする
        VkPipelineColorBlendAttachmentState colorBlendAttachmentState{
            .blendEnable = VK_TRUE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                              VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT |
                              VK_COLOR_COMPONENT_A_BIT
        };
        builder.SetColorBlendAttachmentState(colorBlendAttachmentState);
        // Transparent のパイプライン作成
        m_pipelines[RenderPassTag::Transparent] = builder.Build();


        // シェーダーモジュールはもう不要のため解放
        vkDestroyShaderModule(device, meshShaderModule, nullptr);
        vkDestroyShaderModule(device, fragShaderModule, nullptr);
    }

#if defined(USE_RENDERPASS)
    void BasicPBRRender::InitializeRenderPass()
    {
        auto& vulkanCtx = VulkanContext::Get();
        auto& swapchain = vulkanCtx.GetSwapchain();
        auto device = vulkanCtx.GetVkDevice();

        VkAttachmentReference colorRefs{
          .attachment = 0,
          .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };
        VkAttachmentReference depthRefs{
          .attachment = 1,
          .layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        };

        std::vector<VkAttachmentDescription> attachmentDescs;
        attachmentDescs.push_back(VkAttachmentDescription{
          .format = swapchain->GetFormat().format,
          .samples = VK_SAMPLE_COUNT_1_BIT,
          .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
          .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
          .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
          .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
          .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
          .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            });
        attachmentDescs.push_back(VkAttachmentDescription{
          .format = VK_FORMAT_D32_SFLOAT,
          .samples = VK_SAMPLE_COUNT_1_BIT,
          .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
          .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
          .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
          .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
          .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
          .finalLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            });

        VkSubpassDescription subpass{
          .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
          .colorAttachmentCount = 1,
          .pColorAttachments = &colorRefs,
          .pDepthStencilAttachment = &depthRefs,
        };

        VkRenderPassCreateInfo renderpassCreateInfo{
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = uint32_t(attachmentDescs.size()),
            .pAttachments = attachmentDescs.data(),
            .subpassCount = 1, .pSubpasses = &subpass,
        };
        vkCreateRenderPass(device, &renderpassCreateInfo, nullptr, &m_renderPass);

        auto extent = swapchain->GetExtent();
        for (auto imageView : swapchain->GetImageViews())
        {
            std::vector<VkImageView> views;
            views.push_back(imageView);                           // Color
            views.push_back(m_depthBuffer->GetVkImageView());     // Depth

            VkFramebufferCreateInfo createInfo{
              .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
              .renderPass = m_renderPass,
              .attachmentCount = uint32_t(views.size()),
              .pAttachments = views.data(),
              .width = extent.width,
              .height = extent.height,
              .layers = 1,
            };
            VkFramebuffer framebuffer{};
            vkCreateFramebuffer(device, &createInfo, nullptr, &framebuffer);
            m_framebuffers.push_back(framebuffer);
        }
    }
#endif
}