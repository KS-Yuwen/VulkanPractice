#include "tessellation_app.h"
#include "core/shader_loader.h"
#include "core/asset_path.h"
#include "core/graphics_pipeline_builder.h"

#include <thread>
#include <chrono>

// サンプルプロジェクトでは実装が必要だが、自身のプロジェクト構成では他cppファイルに存在するのでコメントアウト
//uint32_t toAlign(uint32_t size, uint32_t align)
//{
//    return (size + align - 1) & ~(align - 1);
//}

void TessellationApp::OnInitialize()
{
	m_resourceUploader.Initialize();
	CreateDepthBuffer();

	CreatePlateGeometry();

	CreateDescriptorSetLayout();
	CreateSceneUniformBuffer();
	CreateDescriptorSets();

	CreateGraphicsPipeline();

	m_resourceUploader.SubmitAndWait();
}

void TessellationApp::OnDrawFrame()
{
    auto& vulkanCtx = VulkanContext::Get();
    auto& swapchain = vulkanCtx.GetSwapchain();
    auto device = vulkanCtx.GetVkDevice();
    auto extent = swapchain->GetExtent();

    if (vulkanCtx.AcquireNextImage() != VK_SUCCESS)
    {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        return;
    }

    auto frameIndex = vulkanCtx.GetCurrentFrameIndex();
    auto* frameCtx = vulkanCtx.GetCurrentFrameContext();

    static int frameCount = 0;
    frameCount++;

    // シーン用ユニフォームバッファの更新
    SceneConstants sceneConstants{};
    glm::vec3 eyePos = glm::vec3(0.0f, 3.0f, 4.0f);
    sceneConstants.mtxView = glm::lookAtRH<float>(eyePos, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0, 1, 0));
    sceneConstants.mtxProj = glm::perspectiveFov(glm::radians(45.0f), float(extent.width), float(extent.height), 0.1f, 100.0f);
    sceneConstants.lightDir = glm::vec4(eyePos, 0);

    // テセレーション係数を設定
    sceneConstants.tessParameters.x = 48.0f;
    sceneConstants.tessParameters.y = 48.0f;
    sceneConstants.frameTime = frameCount;

    // DynamicUniformBufferの所定の位置へ書き込む
    if (void* p = m_sceneUniform->Map(); p != nullptr)
    {
        memcpy(p, &sceneConstants, sizeof(sceneConstants));
        m_sceneUniform->Unmap();
    }

    // ワールド行列を更新
    auto mtxWorld = glm::rotate(glm::mat4(1.0f), frameCount * 0.001f, glm::vec3(0, 1, 0));

    // 描画処理
    auto commandBuffer = frameCtx->commandBuffer;
    commandBuffer->Begin();
    BeginScene(commandBuffer);

    vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    VkBuffer vertexBuffers[] = { m_plane.vertexBuffer->GetVkBuffer() };
    VkDeviceSize vertexOffsets[] = { 0 };
    vkCmdBindVertexBuffers(*commandBuffer, 0, 1, vertexBuffers, vertexOffsets);
    vkCmdBindIndexBuffer(*commandBuffer, m_plane.indexBuffer->GetVkBuffer(), 0, VK_INDEX_TYPE_UINT32);

    // シーン用ユニフォームバッファ/ディスクリプタセットのセット
    uint32_t sceneUniformOffsets[] = { uint32_t(m_sceneUniform->GetCurrentOffset()) };
    vkCmdBindDescriptorSets(*commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
        0, 1, &m_sceneDescriptorSet,
        1, sceneUniformOffsets);
    vkCmdPushConstants(*commandBuffer,
        m_pipelineLayout,
        VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
        0, sizeof(mtxWorld), glm::value_ptr(mtxWorld));

    vkCmdDrawIndexed(*commandBuffer, m_plane.indexCount, 1, 0, 0, 0);

    EndSecne(commandBuffer);
    commandBuffer->End();

    vulkanCtx.SubmitPresent();
}

void TessellationApp::OnCleanup()
{
	auto& vulkanCtx = VulkanContext::Get();
	auto vkDevice = vulkanCtx.GetVkDevice();

	// GPU状態がアイドルになるのを待ってから後始末を開始
	vkDeviceWaitIdle(vkDevice);

	// パイプライン破棄
	vkDestroyPipeline(vkDevice, m_pipeline, nullptr);

	// ジオメトリの破棄
	m_plane.vertexBuffer->Cleanup();
	m_plane.indexBuffer->Cleanup();

	m_sceneUniform->Cleanup();
	m_sceneUniform.reset();


	m_depthBuffer->Cleanup();

	vkDestroyDescriptorSetLayout(vkDevice, m_descriptorSetLayout, nullptr);
	vkDestroyPipelineLayout(vkDevice, m_pipelineLayout, nullptr);

	m_resourceUploader.Cleanup();
}

void TessellationApp::CreatePlateGeometry()
{
	// テセレーションで使用する XZ 平面を作成する
	// テセレーションパッチ(四角形)の作成である点に注意
	std::vector<Vertex> vertices = {
		{{ -4.0f, 0.0f, -4.0f }},
		{{  4.0f, 0.0f, -4.0f }},
		{{ -4.0f, 0.0f,  4.0f }},
		{{  4.0f, 0.0f,  4.0f }},
	};
	std::vector<uint32_t> indices = {
		0, 1, 2, 3,
	};
	VkDeviceSize bufferSize;
	VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	bufferSize = sizeof(Vertex) * vertices.size();
	m_plane.vertexBuffer = VertexBuffer::Create(bufferSize, memProps);
	m_resourceUploader.UploadBuffer(
		m_plane.vertexBuffer.get(), vertices.data(), bufferSize,
		VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT);

	bufferSize = sizeof(uint32_t) * indices.size();
	m_plane.indexBuffer = IndexBuffer::Create(bufferSize, memProps);
	m_plane.indexCount = uint32_t(indices.size());
	m_resourceUploader.UploadBuffer(
		m_plane.indexBuffer.get(), indices.data(), bufferSize,
		VK_ACCESS_INDEX_READ_BIT);

	m_resourceUploader.SubmitAndWait();
}

void TessellationApp::CreateDepthBuffer()
{
	auto& vulkanCtx = VulkanContext::Get();
	auto& swapchain = vulkanCtx.GetSwapchain();
	auto extent = swapchain->GetExtent();
	m_depthBuffer = std::make_shared<DepthBuffer>();
	m_depthBuffer->Initialize(extent, VK_FORMAT_D32_SFLOAT);
}

void TessellationApp::CreateDescriptorSetLayout()
{
	auto& vulkanCtx = VulkanContext::Get();
	auto vkDevice = vulkanCtx.GetVkDevice();

	// ディスクリプタセットレイアウトの作成
	VkDescriptorSetLayoutCreateInfo createInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
	};

	// シーンのユニフォームバッファ(動的)レイアウト.(DescriptorSet = 0)
    std::vector<VkDescriptorSetLayoutBinding> dsLayoutBindings = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
            .pImmutableSamplers = nullptr,
        },
    };
	createInfo.bindingCount = dsLayoutBindings.size();
	createInfo.pBindings = dsLayoutBindings.data();
	auto result = vkCreateDescriptorSetLayout(vkDevice, &createInfo, nullptr, &m_descriptorSetLayout);

	if (result != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create descriptor set layout!");
	}
}

void TessellationApp::CreateGraphicsPipeline()
{
    auto& vulkanCtx = VulkanContext::Get();
    auto& swapchain = vulkanCtx.GetSwapchain();
    auto device = vulkanCtx.GetVkDevice();

    // パイプラインレイアウトを先に構成する
    // 本サンプルにおいて、ワールド行列は TESE ステージで使用する
    VkPushConstantRange pushConstantRange{
        .stageFlags = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
        .offset = 0,
        .size = sizeof(glm::mat4),
    };
    VkPipelineLayoutCreateInfo layoutInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &m_descriptorSetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstantRange
    };

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create pipeline layout!");
    }

    VkShaderModule vertShaderModule = loader::LoadShaderModule(GetAssetPath(AssetType::Shader, "tessellation/tessellate.vert.spv"));
    VkShaderModule fragShaderModule = loader::LoadShaderModule(GetAssetPath(AssetType::Shader, "tessellation/tessellate.frag.spv"));
    VkShaderModule tescShaderModule = loader::LoadShaderModule(GetAssetPath(AssetType::Shader, "tessellation/tessellate.tesc.spv"));
    VkShaderModule teseShaderModule = loader::LoadShaderModule(GetAssetPath(AssetType::Shader, "tessellation/tessellate.tese.spv"));

    GraphicsPipelineBuilder builder{};
    builder.AddShaderStage(VK_SHADER_STAGE_VERTEX_BIT, vertShaderModule);
    builder.AddShaderStage(VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, tescShaderModule);
    builder.AddShaderStage(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, teseShaderModule);
    builder.AddShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, fragShaderModule);

    // バインディング情報
    VkVertexInputBindingDescription bindingDescription{
        .binding = 0,
        .stride = sizeof(Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };
    // 属性情報（location 0: position）
    std::array<VkVertexInputAttributeDescription, 1> attributeDescriptions{
        VkVertexInputAttributeDescription{
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(Vertex, position)
        },
    };
    builder.SetVertexInput(
        &bindingDescription, 1,
        attributeDescriptions.data(), uint32_t(attributeDescriptions.size())
    );

    // テッセレーションのため入力をパッチリストへ変更する
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST
    };
    builder.SetInputAssembly(inputAssembly);

    // テッセレーションの設定
    VkPipelineTessellationStateCreateInfo tessellationState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,
        .patchControlPoints = 4,    // コントロールポイントは4つ
    };
    builder.SetTessellation(true, tessellationState);

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
    rasterizerState.polygonMode = VK_POLYGON_MODE_LINE; // ワイヤフレーム描画を試す場合
    builder.SetRasterizationState(rasterizerState);

    auto colorFormat = swapchain->GetFormat().format;
    auto depthFormat = m_depthBuffer->GetFormat();
    builder.UseDynamicRendering(colorFormat, depthFormat);

    m_pipeline = builder.Build();

    vkDestroyShaderModule(device, vertShaderModule, nullptr);
    vkDestroyShaderModule(device, tescShaderModule, nullptr);
    vkDestroyShaderModule(device, teseShaderModule, nullptr);
    vkDestroyShaderModule(device, fragShaderModule, nullptr);
}

void TessellationApp::CreateSceneUniformBuffer()
{
    m_sceneUniform = DynamicUniformBuffer::Create(sizeof(SceneConstants));
}

void TessellationApp::CreateDescriptorSets()
{
    auto& vulkanCtx = VulkanContext::Get();
    auto vkDevice = vulkanCtx.GetVkDevice();

    m_sceneDescriptorSet = vulkanCtx.AllocateDescriptorSet(m_descriptorSetLayout);

    auto sceneUniformInfo = m_sceneUniform->GetDescriptorInfo();
    VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = m_sceneDescriptorSet,
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
        .pBufferInfo = &sceneUniformInfo,
    };
    vkUpdateDescriptorSets(vkDevice, 1, &write, 0, nullptr);
}

void TessellationApp::BeginScene(std::shared_ptr<CommandBuffer> commandBuffer)
{
    auto& vulkanCtx = VulkanContext::Get();
    auto& swapchain = vulkanCtx.GetSwapchain();
    auto device = vulkanCtx.GetVkDevice();
    auto extent = swapchain->GetExtent();

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
        .clearValue = VkClearValue{.color = {{0.0f, 0.0f, 0.0f, 0.0f}} }
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
}

void TessellationApp::EndSecne(std::shared_ptr<CommandBuffer> commandBuffer)
{
    auto& vulkanCtx = VulkanContext::Get();
    auto& swapchain = vulkanCtx.GetSwapchain();

    vkCmdEndRendering(*commandBuffer);

    VkImageSubresourceRange range{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0, .levelCount = 1,
        .baseArrayLayer = 0, .layerCount = 1,
    };
    commandBuffer->TransitionLayout(
        swapchain->GetCurrentImage(), range,
        ImageLayoutTransition::FromColorToPresent());
}
