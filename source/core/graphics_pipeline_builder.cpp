#include "graphics_pipeline_builder.h"
#include "vulkan_context.h"

GraphicsPipelineBuilder::GraphicsPipelineBuilder()
{
	m_vertexInputState = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
	};

	m_inputAssemblyState = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		.primitiveRestartEnable = VK_FALSE,
	};

	m_rasterizerState = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.depthClampEnable = VK_FALSE,
		.rasterizerDiscardEnable = VK_FALSE,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_NONE,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.depthBiasEnable = VK_FALSE,
		.lineWidth = 1.0f,
	};

	m_multiSampleState = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
		.sampleShadingEnable = VK_FALSE,
	};

	m_colorBlendAttachment = {
		.blendEnable = VK_FALSE,
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
		VK_COLOR_COMPONENT_G_BIT |
		VK_COLOR_COMPONENT_B_BIT |
		VK_COLOR_COMPONENT_A_BIT,
	};

	m_colorBlendState = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &m_colorBlendAttachment,
	};

	m_depthStencilState = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_FALSE,
		.depthWriteEnable = VK_FALSE,
		.depthCompareOp = VK_COMPARE_OP_LESS,
		.depthBoundsTestEnable = VK_FALSE,
		.stencilTestEnable = VK_FALSE,
	};
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::AddShaderStage(VkShaderStageFlagBits stage, VkShaderModule module, const char* entry)
{
	m_shaderStages.push_back({
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = stage,
		.module = module,
		.pName = entry
		});

	return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::SetVertexInput(
	const VkVertexInputBindingDescription* bindings,
	uint32_t bindingCount,
	const VkVertexInputAttributeDescription* attributes,
	uint32_t attributeCount)
{
	m_bindingDescriptions.assign(bindings, bindings + bindingCount);
	m_attributeDescriptions.assign(attributes, attributes + attributeCount);

	m_vertexInputState = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = bindingCount,
		.pVertexBindingDescriptions = m_bindingDescriptions.data(),
		.vertexAttributeDescriptionCount = attributeCount,
		.pVertexAttributeDescriptions = m_attributeDescriptions.data(),
	};

	return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::SetViewport(VkExtent2D extent)
{
	m_viewPort = {
		.x = 0.0f,
		.y = 0.0f,
		.width = float(extent.width),
		.height = float(extent.height),
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};

	// VK_KHR_Maintenance1 による上下反転
	m_viewPort.y = float(extent.height);
	m_viewPort.height = -float(extent.height);

	m_scissor = {
		.offset = {0, 0},
		.extent = extent,
	};

	m_viewportState = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.pViewports = &m_viewPort,
		.scissorCount = 1,
		.pScissors = &m_scissor,
	};

	return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::SetViewport(VkViewport& viewport, VkRect2D scissor)
{
	m_viewPort = viewport;
	m_scissor = scissor;
	m_viewportState = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.pViewports = &m_viewPort,
		.scissorCount = 1,
		.pScissors = &m_scissor,
	};

	return *this;
}

void GraphicsPipelineBuilder::SetColorBlendAttachmentState(const VkPipelineColorBlendAttachmentState& state)
{
	m_colorBlendAttachment = state;
}

void GraphicsPipelineBuilder::SetRasterizationState(const VkPipelineRasterizationStateCreateInfo& state)
{
	m_rasterizerState = state;
}

void GraphicsPipelineBuilder::SetDepthStencilState(const VkPipelineDepthStencilStateCreateInfo& state)
{
	m_depthStencilState = state;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::SetPipelineLayout(VkPipelineLayout layout)
{
	m_pipelineLayout = layout;
	return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::UseRenderPass(VkRenderPass renderPass, uint32_t subpass)
{
	m_useRenderPass = true;
	m_renderPass = renderPass;
	m_subpass = subpass;

	return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::UserDynamicRendering(VkFormat colorFormat, VkFormat depthFormat)
{
	m_useRenderPass = false;
	m_colorFormat = colorFormat;
	m_depthFormat = depthFormat;

	return *this;
}

VkPipeline GraphicsPipelineBuilder::Build()
{
	VkGraphicsPipelineCreateInfo pipelineInfo{
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = static_cast<uint32_t>(m_shaderStages.size()),
		.pStages = m_shaderStages.data(),
		.pVertexInputState = &m_vertexInputState,
		.pInputAssemblyState = &m_inputAssemblyState,
		.pViewportState = &m_viewportState,
		.pRasterizationState = &m_rasterizerState,
		.pMultisampleState = &m_multiSampleState,
		.pDepthStencilState = &m_depthStencilState,
		.pColorBlendState = &m_colorBlendState,
		.layout = m_pipelineLayout,
	};

	VkPipelineRenderingCreateInfo renderingInfo{};
	if (m_useRenderPass)
	{
		pipelineInfo.renderPass = m_renderPass;
		pipelineInfo.subpass = m_subpass;
	}
	else
	{
		renderingInfo = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
			.colorAttachmentCount = 1,
			.pColorAttachmentFormats = &m_colorFormat,
			.depthAttachmentFormat = m_depthFormat,
		};
		pipelineInfo.pNext = &renderingInfo;
		pipelineInfo.renderPass = VK_NULL_HANDLE;
		pipelineInfo.subpass = 0;
	}

	if (m_tessellationEnable)
	{
		pipelineInfo.pTessellationState = &m_tessellationState;
	};

	auto device = VulkanContext::Get().GetVkDevice();
	VkPipeline pipeline = VK_NULL_HANDLE;
	if (vkCreateGraphicsPipelines(
		device,
		VK_NULL_HANDLE,
		1,
		&pipelineInfo,
		nullptr,
		&pipeline) != VK_SUCCESS)
	{
		return VK_NULL_HANDLE;
	}

	return pipeline;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::SetInputAssembly(const VkPipelineInputAssemblyStateCreateInfo& state)
{
	m_inputAssemblyState = state;
	return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::SetTessellation(bool enable, const VkPipelineTessellationStateCreateInfo& state)
{
	m_tessellationEnable = enable;
	m_tessellationState = state;

	return *this;
}
