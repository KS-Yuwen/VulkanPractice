#include "meshshader_triangle_app.h"
#include "core/shader_loader.h"
#include "Render/model_loader.h"
#include "core/asset_path.h"
#include "core/graphics_pipeline_builder.h"

#include <thread>
#include <chrono>

constexpr uint32_t MaxRaytraceTextureCount = 4096;

#if false
// この関数は他のファイルで作成しているので、コメントアウト
uint32_t toAlign(uint32_t size, uint32_t align)
{
	return (size + align - 1) & ~(align - 1);
}
#endif

void MeshShaderTriangleApp::OnInitialize()
{
	m_resourceUploader.Initialize();

	CreateDescriptorSetLayout();
	CreateTriangleGeometry();
	CreateDescriptorSet();
	CreatePipeline();

	m_resourceUploader.SubmitAndWait();
}

void MeshShaderTriangleApp::OnDrawFrame()
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

	// 描画処理

	auto commandBuffer = frameCtx->commandBuffer;
	commandBuffer->Begin();
	BeginScene(commandBuffer);

	vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
	vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
		0, 1, &m_sceneDescriptorSet, 0, nullptr);
	vkCmdDrawMeshTasksEXT(*commandBuffer, 1, 1, 1);

	EndScene(commandBuffer);
	commandBuffer->End();

	vulkanCtx.SubmitPresent();

}

void MeshShaderTriangleApp::OnCleanup()
{
	auto& vulkanCtx = VulkanContext::Get();
	auto device = vulkanCtx.GetVkDevice();

	// GPU状態がアイドルになるのを待ってから後始末を開始
	vkDeviceWaitIdle(device);

	vkDestroyPipeline(device, m_pipeline, nullptr);

	m_triangle.vertexBuffer->Cleanup();
	m_triangle.vertexBuffer.reset();

	m_triangle.indexBuffer->Cleanup();
	m_triangle.indexBuffer.reset();

	vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
	vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);

	m_resourceUploader.Cleanup();
}

void MeshShaderTriangleApp::CreateTriangleGeometry()
{
	const std::vector<Vertex> vertices = {
		  { { -0.5f, -0.5f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
		  { {  0.5f, -0.5f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
		  { {  0.0f,  0.5f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
	};
	const uint32_t indices[] = { 0, 1, 2 };

	VkDeviceSize bufferSize = sizeof(Vertex) * vertices.size();
	auto gpuAccess = StorageBuffer::AccessMode::GPUOnlyAccess;

	// 頂点データとインデックスデータはストレージバッファとして作る
	m_triangle.vertexBuffer = StorageBuffer::Create(bufferSize, gpuAccess);
	m_resourceUploader.UploadBuffer(m_triangle.vertexBuffer.get(), vertices.data(), bufferSize, VK_ACCESS_SHADER_READ_BIT);

	m_triangle.indexBuffer = StorageBuffer::Create(sizeof(indices), gpuAccess);
	m_resourceUploader.UploadBuffer(m_triangle.indexBuffer.get(), indices, sizeof(indices), VK_ACCESS_SHADER_READ_BIT);

	m_triangle.vertexCount = uint32_t(vertices.size());
	m_triangle.indexCount = std::size(indices);
}

void MeshShaderTriangleApp::CreateDescriptorSetLayout()
{
	auto& vulkanCtx = VulkanContext::Get();
	auto device = vulkanCtx.GetVkDevice();

	// ディスクリプタセットレイアウトの作成
	VkDescriptorSetLayoutCreateInfo createInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
	};

	std::vector<VkDescriptorSetLayoutBinding> dsLayoutBindings = {
		{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT,
			.pImmutableSamplers = nullptr,
		},
		{
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT,
			.pImmutableSamplers = nullptr,
		},
	};
	createInfo.bindingCount = dsLayoutBindings.size();
	createInfo.pBindings = dsLayoutBindings.data();
	auto result = vkCreateDescriptorSetLayout(device, &createInfo, nullptr, &m_descriptorSetLayout);
	if (result != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create descriptor set layout!");
	}
}

void MeshShaderTriangleApp::CreateDescriptorSet()
{
	auto& vulkanCtx = VulkanContext::Get();
	auto device = vulkanCtx.GetVkDevice();

	m_sceneDescriptorSet = vulkanCtx.AllocateDescriptorSet(m_descriptorSetLayout);

	auto vertexInfo = m_triangle.vertexBuffer->GetDescriptorInfo();
	auto indexInfo = m_triangle.indexBuffer->GetDescriptorInfo();

	std::vector<VkWriteDescriptorSet> writes = {
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = m_sceneDescriptorSet,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.pBufferInfo = &vertexInfo,
		},
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = m_sceneDescriptorSet,
			.dstBinding = 1,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.pBufferInfo = &indexInfo,
		},
	};
	vkUpdateDescriptorSets(device, uint32_t(writes.size()), writes.data(), 0, nullptr);
}

void MeshShaderTriangleApp::CreatePipeline()
{
	auto& vulkanCtx = VulkanContext::Get();
	auto& swapchain = vulkanCtx.GetSwapchain();
	auto device = vulkanCtx.GetVkDevice();

	VkPipelineLayoutCreateInfo layoutCreateInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &m_descriptorSetLayout,
	};
	vkCreatePipelineLayout(device, &layoutCreateInfo, nullptr, &m_pipelineLayout);

	VkShaderModule meshShaderModule = loader::LoadShaderModule(GetAssetPath(AssetType::Shader, "meshshader-triangle/shader.mesh.spv"));
	VkShaderModule fragShaderModule = loader::LoadShaderModule(GetAssetPath(AssetType::Shader, "meshshader-triangle/shader.frag.spv"));

	GraphicsPipelineBuilder builder{};
	builder.AddShaderStage(VK_SHADER_STAGE_MESH_BIT_EXT, meshShaderModule);
	builder.AddShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, fragShaderModule);

	auto swapchainExtent = swapchain->GetExtent();
	builder.SetViewport(swapchainExtent);
	builder.SetPipelineLayout(m_pipelineLayout);

	builder.UseDynamicRendering(swapchain->GetFormat().format);
	m_pipeline = builder.Build();
	vkDestroyShaderModule(device, meshShaderModule, nullptr);
	vkDestroyShaderModule(device, fragShaderModule, nullptr);
}

void MeshShaderTriangleApp::BeginScene(std::shared_ptr<CommandBuffer> commandBuffer)
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
	VkRenderingInfo renderingInfo{
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea = { {0, 0}, extent },
		.layerCount = 1,
		.colorAttachmentCount = 1,
		.pColorAttachments = &colorAttachment,
	};
	vkCmdBeginRendering(*commandBuffer, &renderingInfo);
}

void MeshShaderTriangleApp::EndScene(std::shared_ptr<CommandBuffer> commandBuffer)
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
