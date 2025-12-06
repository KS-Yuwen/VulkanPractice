#include "basic_pbr_render.h"
#include "../core/swapchain.h"
#include "../core/graphics_pipeline_builder.h"
#include "../core/asset_path.h"
#include "../core/shader_loader.h"

#include <stdexcept>

namespace render
{
	void BasicPBRRender::Initialize()
	{
		CreateDepthBuffer();
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
		m_layouts.materialLayout = VK_NULL_HANDLE;
		m_layouts.sceneLayout = VK_NULL_HANDLE;
		m_layouts.pipeline = VK_NULL_HANDLE;
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
	}

	void BasicPBRRender::EndScene(std::shared_ptr<CommandBuffer> commandBuffer)
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

	void BasicPBRRender::Draw(std::shared_ptr<CommandBuffer> commandBuffer, std::shared_ptr<DrawObject> drawObject)
	{
		const auto& model = drawObject->GetModel();

		// 頂点バッファインデックスバッファのバインド
		std::vector<VkBuffer> vertexBuffers;
		std::vector<VkDeviceSize> offsets;

		for (uint32_t i = 0; i< int(ModelResource::VertexAttributeIndex::Count); ++i)
		{
			const auto& attrib = model->GetVertexAttribute(i);
			vertexBuffers.push_back(attrib.buffer->GetVkBuffer());
			offsets.push_back(0);
		}
		auto indexBuffer = model->GetIndexBuffer();

		vkCmdBindVertexBuffers(*commandBuffer, 0, vertexBuffers.size(), vertexBuffers.data(), offsets.data());
		vkCmdBindIndexBuffer(*commandBuffer, indexBuffer->GetVkBuffer(), 0, VK_INDEX_TYPE_UINT32);

		struct DrawInfo
		{
			glm::mat4 worldMatrix;
			int materialIndex;
			uint32_t indexCount;
			uint32_t firstIndex;
			uint32_t vertexOffset;
		};
		std::vector<DrawInfo> opaqueList, transparentList;
		// ノードの行列とメッシュプリミティブを探索し、描画リストを作成
		for (const auto& node : drawObject->GetNodes())
		{
			for (auto meshIndex : node.meshes)
			{
				const auto& mesh = model->GetMeshes()[meshIndex];
				const auto& materials = drawObject->GetMaterials();

				for (auto& meshPrimitive : mesh.primitives)
				{
					const auto& material = materials[meshPrimitive.material];
					DrawInfo info{
						.worldMatrix = node.mtxWorld,
						.materialIndex = meshPrimitive.material,
						.indexCount = meshPrimitive.indexCount,
						.firstIndex = meshPrimitive.firstIndex,
						.vertexOffset = meshPrimitive.vertexOffset,
					};

					using AlphaMode = ModelResource::Material::AlphaMode;
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
		const auto& materials = drawObject->GetMaterials();
		auto sceneDescriptorSet = m_scene.descriptorSet;
		uint32_t uboOffsets[] = { m_scene.dynamicOffset };

		// 不透明、アルファマスクのものを描画する
		vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelines[RenderPassTag::Opaque]);
		for (const auto& info : opaqueList)
		{
			vkCmdPushConstants(
				*commandBuffer, m_layouts.pipeline,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				0, sizeof(glm::mat4), &info.worldMatrix);

			VkDescriptorSet descriptorSets[] = {
				sceneDescriptorSet,
				materials[info.materialIndex].descriptorSet
			};

			vkCmdBindDescriptorSets(
				*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layouts.pipeline,
				0, std::size(descriptorSets), descriptorSets,
				std::size(uboOffsets), uboOffsets);

			vkCmdDrawIndexed(*commandBuffer, info.indexCount, 1, info.firstIndex, info.vertexOffset, 0);
		}

		// 半透明のものを描画する
		vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelines[RenderPassTag::Transparent]);
		for (const auto& info : transparentList)
		{
			vkCmdPushConstants(
				*commandBuffer, m_layouts.pipeline,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				0, sizeof(glm::mat4), &info.worldMatrix);

			VkDescriptorSet descriptorSets[] = {
				sceneDescriptorSet,
				materials[info.materialIndex].descriptorSet
			};

			vkCmdBindDescriptorSets(
				*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layouts.pipeline,
				0, std::size(descriptorSets), descriptorSets,
				std::size(uboOffsets), uboOffsets);

			vkCmdDrawIndexed(*commandBuffer, info.indexCount, 1, info.firstIndex, info.vertexOffset, 0);
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
		std::vector<VkDescriptorSetLayoutBinding> dsLayoutBindings;
		VkDescriptorSetLayoutCreateInfo createInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		};

		// シーンのユニフォームバッファ(動的)レイアウト (DescriptorSet=0)
		dsLayoutBindings = {
			{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				.pImmutableSamplers = nullptr,
			},
		};
		createInfo.bindingCount = dsLayoutBindings.size();
		createInfo.pBindings = dsLayoutBindings.data();
		vkCreateDescriptorSetLayout(device, &createInfo, nullptr, &m_layouts.sceneLayout);

		// マテリアル情報用レイアウト (Descriptorset=1)
		dsLayoutBindings = {
			{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			},
			{	// BaseColor
				.binding = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			},
			{	// MetallicRoughness
				.binding = 2,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			},
			{	// NormalMap
				.binding = 3,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			},
		};
		createInfo.bindingCount = dsLayoutBindings.size();
		createInfo.pBindings = dsLayoutBindings.data();
		vkCreateDescriptorSetLayout(device, &createInfo, nullptr, &m_layouts.materialLayout);
	}
	void BasicPBRRender::CreateGraphicsPipelines()
	{
		auto& vulkanCtx = VulkanContext::Get();
		auto device = vulkanCtx.GetVkDevice();
		auto& swapchain = vulkanCtx.GetSwapchain();

		// パイプラインレイアウトを作成する
		// 本レンダラでは各処理パスで共通のパイプラインレイアウトを使うため1種のみ作成
		// ds[0]:シーンUniformBuffer
		// ds[1]:マテリアルUniformBuffer,各種テクスチャ
		// + PushConstant(WorldMatrix)
		std::vector<VkDescriptorSetLayout> dsLayouts = { m_layouts.sceneLayout, m_layouts.materialLayout };
		VkPushConstantRange pushConstantRange{
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			.offset = 0,
			.size = sizeof(glm::mat4),
		};
		VkPipelineLayoutCreateInfo layoutCreateInfo{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = uint32_t(dsLayouts.size()),
			.pSetLayouts = dsLayouts.data(),
			.pushConstantRangeCount = 1,
			.pPushConstantRanges = &pushConstantRange,
		};
		vkCreatePipelineLayout(device, &layoutCreateInfo, nullptr, &m_layouts.pipeline);

		// 頂点入力情報構築
		std::vector<VkVertexInputBindingDescription> bindingDescription = {
		  {.binding = 0, .stride = sizeof(glm::vec3), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX },   // Position
		  {.binding = 1, .stride = sizeof(glm::vec3), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX },   // Normal
		  {.binding = 2, .stride = sizeof(glm::vec2), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX },   // Texcoord0
		  {.binding = 3, .stride = sizeof(glm::vec3), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX },   // Tangent
		  {.binding = 4, .stride = sizeof(glm::vec3), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX },   // Binormal
		};
		std::vector<VkVertexInputAttributeDescription> attributeDescription = {
		  {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0, },
		  {.location = 1, .binding = 1, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0, },
		  {.location = 2, .binding = 2, .format = VK_FORMAT_R32G32_SFLOAT, .offset = 0,    },
		  {.location = 3, .binding = 3, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0, },
		  {.location = 4, .binding = 4, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0, },
		};

		GraphicsPipelineBuilder builder{};
		builder.SetVertexInput(
			bindingDescription.data(), bindingDescription.size(),
			attributeDescription.data(), attributeDescription.size()
		);
		auto swapchainExtent = swapchain->GetExtent();
		builder.SetViewport(swapchainExtent);
		builder.SetPipelineLayout(m_layouts.pipeline);

		// シェーダーの読み込み
		VkShaderModule vertShaderModule = loader::LoadShaderModule(GetAssetPath(AssetType::Shader, "basic_pbr/basic_pbr.vert.spv"));
		VkShaderModule fragShaderModule = loader::LoadShaderModule(GetAssetPath(AssetType::Shader, "basic_pbr/basic_pbr.frag.spv"));
		builder.AddShaderStage(VK_SHADER_STAGE_VERTEX_BIT, vertShaderModule);
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

		auto colorFormat = swapchain->GetFormat().format;
		auto depthFormat = m_depthBuffer->GetFormat();
		builder.UseDynamicRendering(colorFormat, depthFormat);

		// Opaque のパイプライン作成.( Maskモードでもこのパイプラインを使用する)
		m_pipelines[RenderPassTag::Opaque] = builder.Build();

		// 半透明描画用に設定を更新
		//  デプスバッファはテストは有効だが書込みをしない
		depthStencilState.depthTestEnable = VK_TRUE;
		depthStencilState.depthWriteEnable = VK_FALSE;

		// 半透明処理においては裏面でも描画対象とする
		rasterizerState = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
			.depthClampEnable = VK_FALSE,
			.rasterizerDiscardEnable = VK_FALSE,
			.polygonMode = VK_POLYGON_MODE_FILL,
			.cullMode = VK_CULL_MODE_NONE,
			.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
			.depthBiasEnable = VK_FALSE,
			.lineWidth = 1.0f,
		};

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

		// 半透明のパイプライン作成
		builder.SetDepthStencilState(depthStencilState);
		builder.SetRasterizationState(rasterizerState);
		builder.SetColorBlendAttachmentState(colorBlendAttachmentState);

		m_pipelines[RenderPassTag::Transparent] = builder.Build();


		// シェーダーモジュールはもう不要のため解放
		vkDestroyShaderModule(device, vertShaderModule, nullptr);
		vkDestroyShaderModule(device, fragShaderModule, nullptr);
	}
}
