#include "compute_app.h"
#include "core/shader_loader.h"
#include "core/asset_path.h"
#include "core/graphics_pipeline_builder.h"

#include <thread>
#include <chrono>

#define COMPUTE_SHADER_VECTOR_ADD_SPV "computeshader/vectorAdd_simple.comp.spv"
//#define COMPUTE_SHADER_VECTOR_ADD_SPV "computeshader/vectorAdd_reduction.comp.spv"
//#define COMPUTE_SHADER_VECTOR_ADD_SPV "computeshader/vectorAdd_shuffle.comp.spv"

#define COMPUTE_SHADER_FILTER_V_SPV "computeshader/imageFilterV.comp.spv"
#define COMPUTE_SHADER_FILTER_H_SPV "computeshader/imageFilterH.comp.spv"

void ComputeApp::OnInitialize()
{
	m_resourceUploader.Initialize();
	CreateDepthBuffer();

	CreatePlaneGeometry();	// テクスチャ表示用ジオメトリ

	PrepareVectorAddData(); // コンピュートシェーダー配列加算サンプル用
	PrepareImageFilterProcessData();	// 画像フィルター実行向けデータの準備

	CreateDescriptorSetLayout();
	CreateSceneUniformBuffer();
	CreateDescriptorSets();

	CreateGraphicsPipeline();
}

void ComputeApp::OnDrawFrame()
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

	static int frameCount = 0; frameCount++;

	// シーン用ユニフォームバッファの更新
	SceneConstants sceneConstants{};
	glm::vec3 eyePos = glm::vec3(0.0, 0.0f, 2.0f);

	sceneConstants.mtxView = glm::lookAtRH<float>(eyePos, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0, 1, 0));
	sceneConstants.mtxProj = glm::perspectiveFov(glm::radians(45.0f), float(extent.width), float(extent.height), 0.1f, 100.0f);
	sceneConstants.lightDir = glm::vec4(0.1f, 2.0f, 1.0f, 0.0f);
	sceneConstants.eyePosition = glm::vec4(eyePos, 0);

	// DynamicUniformBuffer の所定の位置に書き込む
	if (void* p = m_sceneUniform->Map(); p != nullptr)
	{
		memcpy(p, &sceneConstants, sizeof(sceneConstants));
		m_sceneUniform->Unmap();
	}

	// ワールド行列を更新
	auto mtxWorld = glm::rotate(glm::mat4(1.0f), frameCount * 0.01f, glm::vec3(0, 1, 0));

	// 描画処理

	auto commandBuffer = frameCtx->commandBuffer;
	commandBuffer->Begin();

	ComputeVectorAdd(commandBuffer);

	// 画像フィルター処理
	ApplyImageFilter(commandBuffer);

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
		VK_SHADER_STAGE_VERTEX_BIT,
		0, sizeof(mtxWorld), glm::value_ptr(mtxWorld));

	vkCmdDrawIndexed(*commandBuffer, m_plane.indexCount, 1, 0, 0, 0);

	EndScene(commandBuffer);

	commandBuffer->End();

	vulkanCtx.SubmitPresent();
}

void ComputeApp::OnCleanup()
{
	auto& vulkanCtx = VulkanContext::Get();
	auto device = vulkanCtx.GetVkDevice();

	// GPU状態がアイドルになるのを待ってから後始末を開始
	vkDeviceWaitIdle(device);

	// パイプライン破棄
	vkDestroyPipeline(device, m_pipeline, nullptr);

	m_vectorAdd.srcBuffer->Cleanup();
	m_vectorAdd.resultBuffer->Cleanup();
	m_vectorAdd.cpuAccessBuffer->Cleanup();
	vulkanCtx.FreeDescriptorSet(m_vectorAdd.descriptorSet);
	vkDestroyPipeline(device, m_vectorAdd.pipeline, nullptr);
	vkDestroyPipelineLayout(device, m_vectorAdd.pipelineLayout, nullptr);
	vkDestroyDescriptorSetLayout(device, m_vectorAdd.descriptorSetLayout, nullptr);
	m_vectorAdd.descriptorSetLayout = VK_NULL_HANDLE;
	m_vectorAdd.descriptorSet = VK_NULL_HANDLE;

	m_imageFilter.filteredImage->Cleanup();
	m_imageFilter.workImage->Cleanup();
	m_imageFilter.srcImage->Cleanup();
	vulkanCtx.FreeDescriptorSet(m_imageFilter.descriptorSet1);
	vulkanCtx.FreeDescriptorSet(m_imageFilter.descriptorSet2);
	vkDestroyPipeline(device, m_imageFilter.pipelineFilterH, nullptr);
	vkDestroyPipeline(device, m_imageFilter.pipelineFilterV, nullptr);
	vkDestroyPipelineLayout(device, m_imageFilter.pipelineLayout, nullptr);
	vkDestroyDescriptorSetLayout(device, m_imageFilter.descriptorSetLayout, nullptr);


	m_sampler->Cleanup();
	m_sampler.reset();

	// ジオメトリの破棄
	m_plane.vertexBuffer->Cleanup();
	m_plane.indexBuffer->Cleanup();

	m_sceneUniform->Cleanup();
	m_sceneUniform.reset();

	m_depthBuffer->Cleanup();

	vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
	vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);

	m_resourceUploader.Cleanup();
}

void ComputeApp::CreatePlaneGeometry()
{
	std::vector<Vertex> vertices = {
  {{-0.5f, -0.5f, 0.0f},  { 0.0f, 1.0f }},
  {{-0.5f,  0.5f, 0.0f},  { 0.0f, 0.0f }},
  {{ 0.5f, -0.5f, 0.0f},  { 1.0f, 1.0f }},
  {{ 0.5f,  0.5f, 0.0f},  { 1.0f, 0.0f }},
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

void ComputeApp::CreateDepthBuffer()
{
	auto& vulkanCtx = VulkanContext::Get();
	auto& swapchain = vulkanCtx.GetSwapchain();
	auto extent = swapchain->GetExtent();
	m_depthBuffer = std::make_shared<DepthBuffer>();
	m_depthBuffer->Initialize(extent, VK_FORMAT_D32_SFLOAT);
}

void ComputeApp::CreateDescriptorSetLayout()
{
	auto& vulkanCtx = VulkanContext::Get();
	auto device = vulkanCtx.GetVkDevice();

	// ディスクリプタセットレイアウトの作成
	VkDescriptorSetLayoutCreateInfo createInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
	};

	// DescriptorSet=0:
	//   シーンのユニフォームバッファ(動的) レイアウト. 
	//   テクスチャ
	std::vector<VkDescriptorSetLayoutBinding> dsLayoutBindings = {
		{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
			.pImmutableSamplers = nullptr,
		},
		{
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
			.pImmutableSamplers = nullptr,
		}
	};
	createInfo.bindingCount = dsLayoutBindings.size();
	createInfo.pBindings = dsLayoutBindings.data();
	auto result = vkCreateDescriptorSetLayout(device, &createInfo, nullptr, &m_descriptorSetLayout);

	if (result != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create descriptor set layout!");
	}
}

void ComputeApp::CreateGraphicsPipeline()
{
	auto& vulkanCtx = VulkanContext::Get();
	auto& swapchain = vulkanCtx.GetSwapchain();
	auto device = vulkanCtx.GetVkDevice();

	// パイプラインレイアウトを先に構成する
	// 本サンプルにおいて、ワールド行列を PushConstantで渡す
	VkPushConstantRange pushConstantRange{
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
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

	VkShaderModule vertShaderModule = loader::LoadShaderModule(GetAssetPath(AssetType::Shader, "computeshader/shader.vert.spv"));
	VkShaderModule fragShaderModule = loader::LoadShaderModule(GetAssetPath(AssetType::Shader, "computeshader/shader.frag.spv"));

	GraphicsPipelineBuilder builder{};
	builder.AddShaderStage(VK_SHADER_STAGE_VERTEX_BIT, vertShaderModule);
	builder.AddShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, fragShaderModule);

	// バインディング情報
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
		.cullMode = VK_CULL_MODE_NONE,
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

void ComputeApp::CreateSceneUniformBuffer()
{
	m_sceneUniform = DynamicUniformBuffer::Create(sizeof(SceneConstants));
}

void ComputeApp::CreateDescriptorSets()
{
	auto& vulkanCtx = VulkanContext::Get();
	auto device = vulkanCtx.GetVkDevice();

	m_sceneDescriptorSet = vulkanCtx.AllocateDescriptorSet(m_descriptorSetLayout);

	auto sceneUniformInfo = m_sceneUniform->GetDescriptorInfo();
	auto imageInfo = m_imageFilter.filteredImage->GetTextureReadDescriptorInfo(*m_sampler);

	std::vector<VkWriteDescriptorSet> writes = {
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = m_sceneDescriptorSet,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.pBufferInfo = &sceneUniformInfo,
		},
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = m_sceneDescriptorSet,
			.dstBinding = 1,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &imageInfo,
		},
	};
	vkUpdateDescriptorSets(device, uint32_t(writes.size()), writes.data(), 0, nullptr);
}

void ComputeApp::BeginScene(std::shared_ptr<CommandBuffer> commandBuffer)
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

void ComputeApp::EndScene(std::shared_ptr<CommandBuffer> commandBuffer)
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

void ComputeApp::PrepareVectorAddData()
{
	auto& vulkanCtx = VulkanContext::Get();
	auto device = vulkanCtx.GetVkDevice();

	m_vectorAdd.sampleData.resize(ADD_ARRAY_LENGTH);
	for (uint32_t i = 0; i < ADD_ARRAY_LENGTH; ++i)
	{
		m_vectorAdd.sampleData[i] = 1;
	}

	// データのソースバッファ(32bit整数の配列データを格納)
	VkDeviceSize bufferSize = sizeof(uint32_t) * ADD_ARRAY_LENGTH;
	m_vectorAdd.srcBuffer = StorageBuffer::Create(bufferSize, StorageBuffer::AccessMode::GPUOnlyAccess);
	m_resourceUploader.UploadBuffer(m_vectorAdd.srcBuffer.get(),
		m_vectorAdd.sampleData.data(), bufferSize, VK_ACCESS_SHADER_READ_BIT);

	// 結果バッファの用意
	bufferSize = sizeof(uint32_t) * 2;
	m_vectorAdd.resultBuffer = StorageBuffer::Create(bufferSize, StorageBuffer::AccessMode::GPUOnlyAccess);

	// CPUで読み戻し確認用のバッファを用意
	bufferSize = sizeof(uint32_t) * 2 * vulkanCtx.MaxInflightFrames;
	m_vectorAdd.cpuAccessBuffer = StorageBuffer::Create(bufferSize, StorageBuffer::AccessMode::CPUAccessible);

	m_resourceUploader.SubmitAndWait();

	// ディスクリプタセットレイアウトの作成
	// ストレージバッファ2つをset=0,binding=0,1へ割当
	std::vector<VkDescriptorSetLayoutBinding> dsLayoutBindings = {
		{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
		},
		{
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
		},
	};
	VkDescriptorSetLayoutCreateInfo dsLayoutCreateInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = uint32_t(dsLayoutBindings.size()),
		.pBindings = dsLayoutBindings.data(),
	};
	auto result = vkCreateDescriptorSetLayout(device, &dsLayoutCreateInfo, nullptr, &m_vectorAdd.descriptorSetLayout);
	if (result != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create descriptor set layout!");
	}

	VkPipelineLayoutCreateInfo layoutCreateInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &(m_vectorAdd.descriptorSetLayout),
	};
	result = vkCreatePipelineLayout(device, &layoutCreateInfo, nullptr, &m_vectorAdd.pipelineLayout);
	if (result != VK_SUCCESS)
	{
		throw std::runtime_error("failed to pipeline layout !");
	}

	VkShaderModule compShaderModule = loader::LoadShaderModule(GetAssetPath(AssetType::Shader, COMPUTE_SHADER_VECTOR_ADD_SPV));
	VkPipelineShaderStageCreateInfo shaderStageCreateInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.module = compShaderModule,
		.pName = "main"
	};

	// パイプラインの準備
	VkComputePipelineCreateInfo pipelineCreateInfo{
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.stage = shaderStageCreateInfo,
		.layout = m_vectorAdd.pipelineLayout,
	};
	result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &m_vectorAdd.pipeline);
	if (result != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create compute pipeline!");
	}
	vkDestroyShaderModule(device, compShaderModule, nullptr);

	// ディスクリプタセットの準備
	// コンピュートシェーダーではsrcBufferの内容を加算してresultBufferに書き込む
	// CPU で結果を見るためにはresultBufferからcpuAccessBufferへコピーをする
	m_vectorAdd.descriptorSet = vulkanCtx.AllocateDescriptorSet(m_vectorAdd.descriptorSetLayout);

	auto srcBufferSSBOInfo = m_vectorAdd.srcBuffer->GetDescriptorInfo();
	auto dstBufferSSBOInfo = m_vectorAdd.resultBuffer->GetDescriptorInfo();
	std::vector<VkWriteDescriptorSet> writes = {
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = m_vectorAdd.descriptorSet,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.pBufferInfo = &srcBufferSSBOInfo,
		},
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = m_vectorAdd.descriptorSet,
			.dstBinding = 1,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.pBufferInfo = &dstBufferSSBOInfo,
		},
	};
	vkUpdateDescriptorSets(device, uint32_t(writes.size()), writes.data(), 0, nullptr);
}

void ComputeApp::ComputeVectorAdd(std::shared_ptr<CommandBuffer> commandBuffer)
{
	// ストレージバッファに記録した配列データを全加算する
	vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_vectorAdd.pipeline);
	vkCmdBindDescriptorSets(*commandBuffer,
		VK_PIPELINE_BIND_POINT_COMPUTE, m_vectorAdd.pipelineLayout,
		0, 1, &m_vectorAdd.descriptorSet,
		0, nullptr);
	const uint32_t threadNum = 512; // ComputeShaderで設定しているスレッド数
	uint32_t dispatchX = uint32_t(std::ceil(float(ADD_ARRAY_LENGTH) / float(threadNum)));
	vkCmdDispatch(*commandBuffer, dispatchX, 1, 1);

	// 結果をCPUで読み取れるバッファにコピーする
	// コピーする位置については描画インデックスによって変更する
	auto frameIndex = VulkanContext::Get().GetCurrentFrameIndex();
	uint32_t blockSize = sizeof(float) * 2;
	VkBufferCopy resultCopy{
		.srcOffset = 0,
		.dstOffset = blockSize * frameIndex,
		.size = blockSize,
	};
	auto srcBuffer = m_vectorAdd.resultBuffer->GetVkBuffer();
	auto dstBuffer = m_vectorAdd.cpuAccessBuffer->GetVkBuffer();
	vkCmdCopyBuffer(*commandBuffer, srcBuffer, dstBuffer, 1, &resultCopy);

	{   // 加算結果の確認用コード
		auto result = m_vectorAdd.cpuAccessBuffer->MapTyped<uint32_t>();
		auto totalAdded = result[0];
		auto methodType = result[1];
		m_vectorAdd.cpuAccessBuffer->Unmap();
	}
}

void ComputeApp::PrepareImageFilterProcessData()
{
	auto& vulkanCtx = VulkanContext::Get();
	auto device = vulkanCtx.GetVkDevice();

	bool bMipmap = false;
	auto textureFilePath = GetAssetPath(AssetType::Texture, "test-texture.png");
	auto [texture, request] = loader::LoadStorageImage2DFromFile(textureFilePath, bMipmap);
	m_imageFilter.srcImage = texture;
	m_resourceUploader.UploadImage(m_imageFilter.srcImage, request);

	// フィルター(1回目)適応結果格納用
	m_imageFilter.workImage = std::make_shared<StorageImage2D>();
	m_imageFilter.workImage->Initialize(texture->GetExtent(), texture->GetFormat(), texture->GetMipmapCount());

	// フィルター(2回目)適応結果格納用
	m_imageFilter.filteredImage = std::make_shared<StorageImage2D>();
	m_imageFilter.filteredImage->Initialize(texture->GetExtent(), texture->GetFormat(), texture->GetMipmapCount());

	m_resourceUploader.SubmitAndWait();

	// フィルター書き出し先となるイメージデータのレイアウトを変更する
	{
		auto commandBuffer = vulkanCtx.CreateCommandBuffer();
		commandBuffer->Begin();
		auto nextState = ImageLayoutTransition::ToStorageImageGeneralLayout(m_imageFilter.workImage.get());
		commandBuffer->TransitionLayout(m_imageFilter.workImage, nextState);
		commandBuffer->TransitionLayout(m_imageFilter.filteredImage, nextState);
		commandBuffer->End();

		vulkanCtx.SubmitAndWait(commandBuffer);
	}

	// ディスクリプタセットレイアウトの作成
	// ストレージイメージ2つをset=0, binding=0,1へ割当
	std::vector<VkDescriptorSetLayoutBinding> dsLayoutBindings = {
		{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
		},
		{
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
		},
	};
	VkDescriptorSetLayoutCreateInfo dsLayoutCreateInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = uint32_t(dsLayoutBindings.size()),
		.pBindings = dsLayoutBindings.data(),
	};
	auto result = vkCreateDescriptorSetLayout(device, &dsLayoutCreateInfo, nullptr, &m_imageFilter.descriptorSetLayout);
	if (result != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create descriptor set layout!");
	}

	// パイプラインレイアウトの作成
	VkPipelineLayoutCreateInfo layoutCreateInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &(m_imageFilter.descriptorSetLayout),
	};
	result = vkCreatePipelineLayout(device, &layoutCreateInfo, nullptr, &m_imageFilter.pipelineLayout);
	if (result != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create pipeline set layout!");
	}

	VkShaderModule compShaderModuleH = loader::LoadShaderModule(GetAssetPath(AssetType::Shader, COMPUTE_SHADER_FILTER_H_SPV));
	VkShaderModule compShaderModuleV = loader::LoadShaderModule(GetAssetPath(AssetType::Shader, COMPUTE_SHADER_FILTER_V_SPV));
	VkPipelineShaderStageCreateInfo shaderStageCreateInfoH{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.module = compShaderModuleH,
		.pName = "main"
	};
	VkPipelineShaderStageCreateInfo shaderStageCreateInfoV{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.module = compShaderModuleV,
		.pName = "main"
	};
		
	// パイプラインの準備
	VkComputePipelineCreateInfo pipelineCreateInfo{
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.stage = shaderStageCreateInfoH,
		.layout = m_imageFilter.pipelineLayout,
	};
	result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &m_imageFilter.pipelineFilterH);
	if (result != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create compute pipeline !");
	}

	pipelineCreateInfo.stage = shaderStageCreateInfoV;
	result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &m_imageFilter.pipelineFilterV);
	if (result != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create compute pipeline !");
	}
	vkDestroyShaderModule(device, compShaderModuleH, nullptr);
	vkDestroyShaderModule(device, compShaderModuleV, nullptr);


	// ディスクリプタセットの準備
	m_imageFilter.descriptorSet1 = vulkanCtx.AllocateDescriptorSet(m_imageFilter.descriptorSetLayout);
	m_imageFilter.descriptorSet2 = vulkanCtx.AllocateDescriptorSet(m_imageFilter.descriptorSetLayout);

	m_sampler = Sampler::Create();
	m_sampler->Initialize(
		VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
	);
	auto imageSSBOInfo = m_imageFilter.srcImage->GetStorageReadWriteDescriptorInfo(*m_sampler);
	auto workSSBOInfo = m_imageFilter.workImage->GetStorageReadWriteDescriptorInfo(*m_sampler);
	auto filteredSSBOInfo = m_imageFilter.filteredImage->GetStorageReadWriteDescriptorInfo(*m_sampler);

	std::vector<VkWriteDescriptorSet> writes = {
		// 1回目のフィルター処理で使うリソースをセット
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = m_imageFilter.descriptorSet1,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.pImageInfo = &imageSSBOInfo,
		},
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = m_imageFilter.descriptorSet1,
			.dstBinding = 1,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.pImageInfo = &workSSBOInfo,
		},
		// 2回目のフィルター処理で使うリソースをセット
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = m_imageFilter.descriptorSet2,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.pImageInfo = &workSSBOInfo,
		},
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = m_imageFilter.descriptorSet2,
			.dstBinding = 1,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.pImageInfo = &filteredSSBOInfo,
		},
	};
	vkUpdateDescriptorSets(device, uint32_t(writes.size()), writes.data(), 0, nullptr);
}

void ComputeApp::ApplyImageFilter(std::shared_ptr<CommandBuffer> commandBuffer)
{
	auto toGeneralLayout = ImageLayoutTransition::ToStorageImageGeneralLayout(m_imageFilter.filteredImage.get());
	commandBuffer->TransitionLayout(m_imageFilter.filteredImage, toGeneralLayout);

	auto texExtent = m_imageFilter.srcImage->GetExtent();
	// 水平方向にフィルター処理
	vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_imageFilter.pipelineFilterH);
	vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_imageFilter.pipelineLayout, 0, 1, &m_imageFilter.descriptorSet1, 0, nullptr);
	uint32_t groupX, groupY;
	// 64はローカル・ワークグループで使用しているスレッド数
	groupX = uint32_t(std::ceil(texExtent.width / double(64)));
	groupY = texExtent.height;
	vkCmdDispatch(*commandBuffer, groupX, groupY, 1);

	// 中間バッファの書き込みバリア
	auto toStorageImageBarrier = ImageLayoutTransition::ToStorageImageGeneralLayout(m_imageFilter.workImage.get());
	commandBuffer->TransitionLayout(m_imageFilter.workImage, toStorageImageBarrier);

	// 垂直方向にフィルター処理
	vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_imageFilter.pipelineFilterV);
	vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_imageFilter.pipelineLayout, 0, 1, &m_imageFilter.descriptorSet2, 0, nullptr);
	groupX = texExtent.width;
	groupY = uint32_t(std::ceil(texExtent.height / double(64)));
	vkCmdDispatch(*commandBuffer, groupX, groupY, 1);

	// 次のフェーズでテクスチャとしてアクセスするためレイアウトの変更
	auto toShaderReadonlyOptimal = ImageLayoutTransition::ToShaderReadonlyOptimal(m_imageFilter.filteredImage.get());
	commandBuffer->TransitionLayout(m_imageFilter.filteredImage, toShaderReadonlyOptimal);
}
