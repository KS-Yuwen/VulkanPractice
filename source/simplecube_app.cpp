#include "simplecube_app.h"
#include "core/asset_path.h"
#include "core/shader_loader.h"
#include "core/graphics_pipeline_builder.h"

#include <thread>
#include <chrono>


void SimpleCubeApp::OnInitialize()
{
    m_resourceUploader.Initialize();

    CreateDepthBuffer();

#if 01 // Cube
    CreateCubeGeometry();
#else
    CreateSphereGeometry();
#endif
    CreateDescriptorSetLayout();

    CreateUniformBuffers();
    CreateDescriptorSets();

    CreateGraphicsPipeline();
}

void SimpleCubeApp::OnDrawFrame()
{
    auto& vulkanCtx = VulkanContext::Get();
    auto& swapchain = vulkanCtx.GetSwapchain();
    auto device = vulkanCtx.GetVkDevice();
    auto extent = swapchain->GetExtent();

    if (vulkanCtx.AcquireNextImage() != VK_SUCCESS)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return;
    }


    auto frameIndex = vulkanCtx.GetCurrentFrameIndex();
    auto* frameCtx = vulkanCtx.GetCurrentFrameContext();

    // SceneConstantsを更新する
    SceneConstants sceneConstants{};
    auto& ubo = m_uniformBuffers[frameIndex];

    auto eyePos = glm::vec3(2, 1, 4);
    sceneConstants.mtxWorld = glm::mat4(1.0f);
    sceneConstants.mtxView = glm::lookAt(eyePos, glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    sceneConstants.mtxProj = glm::perspectiveFov(
        glm::radians(45.0f),
        float(extent.width), float(extent.height),
        0.1f, 100.0f);
    sceneConstants.lightDir = glm::normalize(glm::vec4(0.5, 2, 1, 0));
    sceneConstants.eyePosition = glm::vec4(eyePos, 0);

    if (void* p = ubo->Map(); p != nullptr)
    {
        memcpy(p, &sceneConstants, sizeof(sceneConstants));
        ubo->Unmap();
    }


    auto& commandBuffer = frameCtx->commandBuffer;
    commandBuffer->Begin();

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

    // --- バインド＆描画
    vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    auto vb = m_cube.vertexBuffer->GetVkBuffer();
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(*commandBuffer, 0, 1, &vb, offsets);
    vkCmdBindIndexBuffer(*commandBuffer, m_cube.indexBuffer->GetVkBuffer(), 0, VK_INDEX_TYPE_UINT32);

    vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_pipelineLayout,
        0, 1, &m_descriptorSets[frameIndex],
        0, nullptr);
    vkCmdDrawIndexed(*commandBuffer, m_cube.indexCount, 1, 0, 0, 0);

    vkCmdEndRendering(*commandBuffer);

    commandBuffer->TransitionLayout(
        swapchain->GetCurrentImage(), range,
        ImageLayoutTransition::FromColorToPresent()
    );
    commandBuffer->End();
    vulkanCtx.SubmitPresent();
}

void SimpleCubeApp::OnCleanup()
{
    auto& vulkanCtx = VulkanContext::Get();
    auto device = vulkanCtx.GetVkDevice();

    // GPU状態がアイドルになるのを待ってから後始末を開始
    vkDeviceWaitIdle(device);

    // パイプライン破棄
    vkDestroyPipeline(device, m_pipeline, nullptr);

    // Cubeジオメトリの破棄
    m_cube.vertexBuffer.reset();
    m_cube.indexBuffer.reset();

    // ディスクリプタ破棄
    for (auto& ds : m_descriptorSets)
    {
        vulkanCtx.FreeDescriptorSet(ds);
    }

    // Uniformバッファ破棄
    for (auto& ubo : m_uniformBuffers)
    {
        ubo->Cleanup();
    }

    // デプスバッファ破棄
    m_depthBuffer->Cleanup();
    m_depthBuffer.reset();

    vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
    vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);

    m_resourceUploader.Cleanup();
}

void SimpleCubeApp::CreateCubeGeometry()
{
    const glm::vec3 A(-0.5f, 0.5f, 0.5f), B(-0.5f, -0.5f, 0.5f),
        C(0.5f, 0.5f, 0.5f), D(0.5f, -0.5f, 0.5f),
        E(-0.5f, 0.5f, -0.5f), F(-0.5f, -0.5f, -0.5f),
        G(0.5f, 0.5f, -0.5f), H(0.5f, -0.5f, -0.5f);

    std::vector<Vertex> vertices =
    {
        // front
        { A, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 1.0f } },
        { B, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },
        { C, { 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f } },
        { D, { 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f, 0.0f } },
        // back
        { E, { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f, 1.0f } },
        { F, { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f, 0.0f } },
        { G, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 1.0f } },
        { H, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } },
        // right
        { C, { 1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } },
        { D, { 1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } },
        { G, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 1.0f } },
        { H, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
        // left
        { E, { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
        { F, { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
        { A, { -1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 1.0f } },
        { B, { -1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
        // top
        { E, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
        { A, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 1.0f } },
        { G, { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 1.0f } },
        { C, { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } },
        // bottom
        { B, { 0.0f, -1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
        { F, { 0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
        { D, { 0.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } },
        { H, { 0.0f, -1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
    };

    std::vector<uint32_t> indices = {
      0, 1, 2, 2, 1, 3,       // front
      6, 7, 4, 4, 7, 5,       // back
      8, 9, 10, 10, 9, 11,    // right
      12, 13, 14, 14, 13, 15, // left
      16, 17, 18, 18, 17, 19, // top
      20, 21, 22, 22, 21, 23, // bottom
    };

    VkDeviceSize bufferSize;
    VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    bufferSize = sizeof(Vertex) * vertices.size();
    m_cube.vertexBuffer = VertexBuffer::Create(bufferSize, memProps);

    m_resourceUploader.UploadBuffer(m_cube.vertexBuffer.get(), vertices.data(), bufferSize, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT);
    bufferSize = sizeof(uint32_t) * indices.size();
    m_cube.indexBuffer = IndexBuffer::Create(bufferSize, memProps);
    m_resourceUploader.UploadBuffer(m_cube.indexBuffer.get(), indices.data(), bufferSize, VK_ACCESS_INDEX_READ_BIT);
    m_cube.indexCount = indices.size();

    m_resourceUploader.SubmitAndWait();
}

void SimpleCubeApp::CreateSphereGeometry()
{
    const int stackCount = 32;
    const int sliceCount = 48;
    constexpr auto PI = glm::pi<float>();
    const auto sliceStep = PI * 2.0f / sliceCount;
    const auto stackStep = PI / stackCount;

    std::vector<Vertex> vertices;
    for (int stack = 0; stack <= stackCount; ++stack)
    {
        auto stackAngle = (float)PI / 2 - stack * stackStep;

        for (int slice = 0; slice <= sliceCount; ++slice)
        {
            auto sliceAngle = slice * sliceStep;

            auto x = std::cosf(stackAngle) * std::cosf(sliceAngle);
            auto y = std::sinf(stackAngle);
            auto z = std::cosf(stackAngle) * std::sinf(sliceAngle);

            Vertex v;
            v.position = glm::vec3(x, y, z);
            v.normal = normalize(v.position);
            v.color = glm::vec3(0.7f, 0.85f, 0.9f);
            vertices.push_back(v);
        }
    }
    std::vector<uint32_t> indices;
    for (int stack = 0; stack < stackCount; ++stack)
    {
        uint32_t k1 = stack * (sliceCount + 1);
        uint32_t k2 = k1 + sliceCount + 1;

        for (int slice = 0; slice < sliceCount; ++slice, ++k1, ++k2)
        {
            if (stack != 0)
            {
                indices.insert(indices.end(), { k1, k1 + 1, k2 });
            }
            if (stack != (stackCount - 1))
            {
                indices.insert(indices.end(), { k1 + 1, k2 + 1, k2 });
            }
        }
    }
    VkDeviceSize bufferSize;
    VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    bufferSize = sizeof(Vertex) * vertices.size();
    m_cube.vertexBuffer = VertexBuffer::Create(bufferSize, memProps);
    m_resourceUploader.UploadBuffer(m_cube.vertexBuffer.get(), vertices.data(), bufferSize, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT);

    bufferSize = sizeof(uint32_t) * indices.size();
    m_cube.indexBuffer = IndexBuffer::Create(bufferSize, memProps);
    m_resourceUploader.UploadBuffer(m_cube.indexBuffer.get(), indices.data(), bufferSize, VK_ACCESS_INDEX_READ_BIT);
    m_cube.indexCount = indices.size();

    m_resourceUploader.SubmitAndWait();
}

void SimpleCubeApp::CreateDescriptorSetLayout()
{
    VkDescriptorSetLayoutBinding uboLayoutBinding{
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .pImmutableSamplers = nullptr,
    };
    VkDescriptorSetLayoutCreateInfo layoutInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &uboLayoutBinding,
    };
    auto device = VulkanContext::Get().GetVkDevice();
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create descriptor set layout!");
    }
}

void SimpleCubeApp::CreateUniformBuffers()
{
    auto& vulkanCtx = VulkanContext::Get();
    assert(vulkanCtx.MaxInflightFrames == m_uniformBuffers.size());

    for (uint32_t i = 0; i < m_uniformBuffers.size(); ++i)
    {
        m_uniformBuffers[i] = UniformBuffer::Create(sizeof(SceneConstants));
    }
}

void SimpleCubeApp::CreateDescriptorSets()
{
    auto& vulkanCtx = VulkanContext::Get();
    for (uint32_t i = 0; i < m_descriptorSets.size(); ++i)
    {
        m_descriptorSets[i] = vulkanCtx.AllocateDescriptorSet(m_descriptorSetLayout);

        VkDescriptorBufferInfo bufferInfo{
            .buffer = m_uniformBuffers[i]->GetVkBuffer(),
            .offset = 0,
            .range = sizeof(SceneConstants),
        };

        VkWriteDescriptorSet write{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = m_descriptorSets[i],
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &bufferInfo,
        };
        vkUpdateDescriptorSets(vulkanCtx.GetVkDevice(), 1, &write, 0, nullptr);
    }
}

void SimpleCubeApp::CreateDepthBuffer()
{
    auto& vulkanCtx = VulkanContext::Get();
    auto& swapchain = vulkanCtx.GetSwapchain();
    auto extent = swapchain->GetExtent();
    m_depthBuffer = DepthBuffer::Create(extent, VK_FORMAT_D32_SFLOAT);
}

void SimpleCubeApp::CreateGraphicsPipeline()
{
    auto& vulkanCtx = VulkanContext::Get();
    auto& swapchain = vulkanCtx.GetSwapchain();
    auto device = vulkanCtx.GetVkDevice();

    // パイプラインレイアウトを先に構成する
    VkPipelineLayoutCreateInfo layoutInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &m_descriptorSetLayout,
        .pushConstantRangeCount = 0,
        .pPushConstantRanges = nullptr
    };
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create pipeline layout!");
    }

    VkShaderModule vertShaderModule = loader::LoadShaderModule(GetAssetPath(AssetType::Shader, "cube/cube.vert.spv"));
    VkShaderModule fragShaderModule = loader::LoadShaderModule(GetAssetPath(AssetType::Shader, "cube/cube.frag.spv"));

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertShaderModule,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragShaderModule,
            .pName = "main",
        }
    };
    // バインディング情報（1つの頂点バッファバインディング）
    VkVertexInputBindingDescription bindingDescription{
        .binding = 0,
        .stride = sizeof(Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };

    // 属性情報（location 0: position, location 1: normal, location 2: color）
    std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{
        VkVertexInputAttributeDescription{
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(Vertex, position)
        },
        VkVertexInputAttributeDescription{
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(Vertex, normal)
        },
        VkVertexInputAttributeDescription{
            .location = 2,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(Vertex, color)
        }
    };

    GraphicsPipelineBuilder builder{};
    builder.AddShaderStage(VK_SHADER_STAGE_VERTEX_BIT, vertShaderModule);
    builder.AddShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, fragShaderModule);
    builder.SetVertexInput(
        &bindingDescription, 1,
        attributeDescriptions.data(), uint32_t(attributeDescriptions.size())
    );
    auto swapchainExtent = swapchain->GetExtent();
    builder.SetViewport(swapchainExtent);
    builder.SetPipelineLayout(m_pipelineLayout);

    // デプスバッファに向けた設定
    VkPipelineDepthStencilStateCreateInfo depthStencilState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
    };
    builder.SetDepthStencilState(depthStencilState);

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

    auto colorFormat = swapchain->GetFormat().format;
    auto depthFormat = m_depthBuffer->GetFormat();
    builder.UseDynamicRendering(colorFormat, depthFormat);

    m_pipeline = builder.Build();

    vkDestroyShaderModule(device, vertShaderModule, nullptr);
    vkDestroyShaderModule(device, fragShaderModule, nullptr);
}
