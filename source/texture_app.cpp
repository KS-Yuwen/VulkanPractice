#include "texture_app.h"
#include "core/asset_path.h"
#include "core/shader_loader.h"
#include "core/texture_loader.h"
#include "core/graphics_pipeline_builder.h"

#include <thread>
#include <chrono>

void TextureApp::OnInitialize()
{
	m_resourceUploader.Initialize();
	CreateDepthBuffer();

	CreatePlaneGeometry();
	CreateTextureResource();

	CreateDescriptorSetLayout();

	CreateUniformBuffers();
	CreateDescriptorSets();

	CreateGraphicsPipeline();

}

void TextureApp::OnDrawFrame()
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

	float aspect = float(extent.width) / float(extent.height);
	sceneConstants.mtxWorld = glm::mat4(1.0f);
	sceneConstants.mtxView = glm::mat4(1.0f);
	sceneConstants.mtxProj = glm::orthoRH(-1.0f * aspect, +1.0f * aspect, -1.0f, 1.0f, 100.f, -100.0f);

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

	auto vb = m_plane.vertexBuffer->GetVkBuffer();
	VkDeviceSize offsets[] = { 0 };
	vkCmdBindVertexBuffers(*commandBuffer, 0, 1, &vb, offsets);
	vkCmdBindIndexBuffer(*commandBuffer, m_plane.indexBuffer->GetVkBuffer(), 0, VK_INDEX_TYPE_UINT32);

	vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		m_pipelineLayout,
		0, 1, &m_descriptorSets[frameIndex],
		0, nullptr);
	vkCmdDrawIndexed(*commandBuffer, m_plane.indexCount, 1, 0, 0, 0);

	vkCmdEndRendering(*commandBuffer);

	commandBuffer->TransitionLayout(
		swapchain->GetCurrentImage(), range,
		ImageLayoutTransition::FromColorToPresent()
	);
	commandBuffer->End();
	vulkanCtx.SubmitPresent();
}

void TextureApp::OnCleanup()
{
	auto& vulkanCtx = VulkanContext::Get();
	auto device = vulkanCtx.GetVkDevice();

	// GPU状態がアイドルになるのを待ってから後始末を実行
	vkDeviceWaitIdle(device);

	// パイプライン破棄
	vkDestroyPipeline(
		device,
		m_pipeline,
		nullptr);

	m_plane.vertexBuffer->Cleanup();
	m_plane.indexBuffer->Cleanup();

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

	// テクスチャの破棄
	m_texture.reset();
	m_sampler.reset();

	// デプスバッファの破棄
	m_depthBuffer->Cleanup();
	m_depthBuffer.reset();

	vkDestroyDescriptorSetLayout(
		device,
		m_descriptorSetLayout,
		nullptr);
	vkDestroyPipelineLayout(
		device,
		m_pipelineLayout,
		nullptr);

	m_resourceUploader.Cleanup();
}

void TextureApp::CreatePlaneGeometry()
{
	std::vector<Vertex> vertices = {
		{{-0.5f, -0.5f, 0.5f},  { 0.0f, 1.0f }},
		{{-0.5f,  0.5f, 0.5f},  { 0.0f, 0.0f }},
		{{ 0.5f, -0.5f, 0.5f},  { 1.0f, 1.0f }},
		{{ 0.5f,  0.5f, 0.5f},  { 1.0f, 0.0f }},
	};
	std::vector<uint32_t> indices = {
		0, 3, 1,
		0, 2, 3,
	};

	VkDeviceSize bufferSize;
	VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	bufferSize = sizeof(Vertex) * vertices.size();
	m_plane.vertexBuffer = VertexBuffer::Create(bufferSize, memProps);
	m_resourceUploader.UploadBuffer(
		m_plane.vertexBuffer.get(),
		vertices.data(),
		bufferSize,
		VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT);

	bufferSize = sizeof(uint32_t) * indices.size();
	m_plane.indexBuffer = IndexBuffer::Create(bufferSize, memProps);
	m_resourceUploader.UploadBuffer(
		m_plane.indexBuffer.get(),
		indices.data(),
		bufferSize,
		VK_ACCESS_INDEX_READ_BIT);
	m_plane.indexCount = indices.size();
}

void TextureApp::CreateTextureResource()
{
	bool mipmapGen = true;
	auto filePath = GetAssetPath(AssetType::Texture, "test-texture.png");
	auto [texture, request] = loader::LoadTexture2DFromFile(filePath, mipmapGen);
	m_texture = texture;
	m_resourceUploader.UploadImage(m_texture, request);

	m_resourceUploader.SubmitAndWait();

	m_sampler = Sampler::Create();
	m_sampler->Initialize(
		VK_FILTER_LINEAR,
		VK_FILTER_LINEAR,
		VK_SAMPLER_MIPMAP_MODE_LINEAR,
		VK_SAMPLER_ADDRESS_MODE_REPEAT,
		VK_SAMPLER_ADDRESS_MODE_REPEAT);
}

void TextureApp::CreateDescriptorSetLayout()
{
	std::vector<VkDescriptorSetLayoutBinding> layoutBindings = {
		{   // 0: ubo
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr,
		},
		{   // 1: texture
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = nullptr,
		}
	};

	VkDescriptorSetLayoutCreateInfo layoutInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = uint32_t(layoutBindings.size()),
		.pBindings = layoutBindings.data(),
	};

	auto device = VulkanContext::Get().GetVkDevice();
	if (vkCreateDescriptorSetLayout(
		device,
		&layoutInfo,
		nullptr,
		&m_descriptorSetLayout) != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to create descriptor set layout!");
	}
}

void TextureApp::CreateUniformBuffers()
{
	auto& vulkanCtx = VulkanContext::Get();
	assert(vulkanCtx.MaxInflightFrames == m_uniformBuffers.size());

	for (uint32_t i = 0; i < m_uniformBuffers.size(); ++i)
	{
		m_uniformBuffers[i] = UniformBuffer::Create(sizeof(SceneConstants));
	}
}

void TextureApp::CreateDescriptorSets()
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

		VkDescriptorImageInfo imageInfo{
			.sampler = m_sampler->GetVkSampler(),
			.imageView = m_texture->GetVkImageView(),
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};

		std::vector<VkWriteDescriptorSet> writes{
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = m_descriptorSets[i],
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.pBufferInfo = &bufferInfo,
			},
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = m_descriptorSets[i],
				.dstBinding = 1,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = &imageInfo,
			},
		};
		vkUpdateDescriptorSets(
			vulkanCtx.GetVkDevice(),
			uint32_t(writes.size()),
			writes.data(),
			0,
			nullptr);
	}
}

void TextureApp::CreateDepthBuffer()
{
	auto& vulkanCtx = VulkanContext::Get();
	auto& swapchain = vulkanCtx.GetSwapchain();
	auto extent = swapchain->GetExtent();
	m_depthBuffer = DepthBuffer::Create(extent, VK_FORMAT_D32_SFLOAT);
}

void TextureApp::CreateGraphicsPipeline()
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

	if (vkCreatePipelineLayout(
		device,
		&layoutInfo,
		nullptr,
		&m_pipelineLayout) != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to create pipeline layout!");
	}

	VkShaderModule vertShaderModule = loader::LoadShaderModule(GetAssetPath(AssetType::Shader, "texture/texture.vert.spv"));
	VkShaderModule fragShaderModule = loader::LoadShaderModule(GetAssetPath(AssetType::Shader, "texture/texture.frag.spv"));

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

	// 属性情報（location 0: position, location 1: uv）
	std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{
		VkVertexInputAttributeDescription{
			.location = 0,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(Vertex, position)
		},
		VkVertexInputAttributeDescription{
			.location = 1,
			.binding = 0,
			.format = VK_FORMAT_R32G32_SFLOAT,
			.offset = offsetof(Vertex, uv)
		},
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