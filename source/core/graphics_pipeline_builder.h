#pragma once
#include <vulkan/vulkan_core.h>
#include <vector>

class GraphicsPipelineBuilder
{
public:
	GraphicsPipelineBuilder();

	// 各ステージ追加
	GraphicsPipelineBuilder& AddShaderStage(VkShaderStageFlagBits stage, VkShaderModule module, const char* entry = "main");

	// 頂点入力レイアウト
	GraphicsPipelineBuilder& SetVertexInput(
		const VkVertexInputBindingDescription* bindings,
		uint32_t bindingCount,
		const VkVertexInputAttributeDescription* attributes,
		uint32_t attributeCount);

	// ビューポートとシザー
	GraphicsPipelineBuilder& SetViewport(VkExtent2D extent);
	GraphicsPipelineBuilder& SetViewport(VkViewport& viewport, VkRect2D scissor);

	// ブレンディング設定
	void SetColorBlendAttachmentState(const VkPipelineColorBlendAttachmentState& state);

	// ラスタライズ設定
	void SetRasterizationState(const VkPipelineRasterizationStateCreateInfo& state);

	// デプス・ステンシル設定
	void SetDepthStencilState(const VkPipelineDepthStencilStateCreateInfo& state);

	// レイアウト
	GraphicsPipelineBuilder& SetPipelineLayout(VkPipelineLayout layout);

	// VkRenderPassを使用する場合の設定
	GraphicsPipelineBuilder& UseRenderPass(VkRenderPass renderPass, uint32_t subpass);

	// DynamicRenderingを使用する場合の設定
	GraphicsPipelineBuilder& UserDynamicRendering(VkFormat colorFormat, VkFormat depthFormat = VK_FORMAT_UNDEFINED);

	// パイプライン作成
	VkPipeline Build();

	// 入力アセンブリを変更(テセレーションなどで使用)
	GraphicsPipelineBuilder& SetInputAssembly(const VkPipelineInputAssemblyStateCreateInfo& state);

	// テセレーション情報の設定
	GraphicsPipelineBuilder& SetTessellation(bool enable, const VkPipelineTessellationStateCreateInfo& state);

private:
	VkDevice m_device;

	std::vector<VkPipelineShaderStageCreateInfo> m_shaderStages;

	VkPipelineVertexInputStateCreateInfo m_vertexInputState{};
	std::vector<VkVertexInputBindingDescription> m_bindingDescriptions;
	std::vector<VkVertexInputAttributeDescription> m_attributeDescriptions;

	VkPipelineInputAssemblyStateCreateInfo m_inputAssemblyState{};
	VkPipelineViewportStateCreateInfo m_viewportState{};
	VkViewport m_viewPort{};
	VkRect2D m_scissor{};

	VkPipelineRasterizationStateCreateInfo m_rasterizerState{};
	VkPipelineMultisampleStateCreateInfo m_multiSampleState{};
	VkPipelineColorBlendAttachmentState m_colorBlendAttachment{};
	VkPipelineColorBlendStateCreateInfo m_colorBlendState{};
	VkPipelineDepthStencilStateCreateInfo m_depthStencilState{};

	bool m_tessellationEnable = false;
	VkPipelineTessellationStateCreateInfo m_tessellationState{};

	VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
	VkFormat m_colorFormat = VK_FORMAT_UNDEFINED;
	VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;

	bool m_useRenderPass = false;
	VkRenderPass m_renderPass = VK_NULL_HANDLE;
	uint32_t m_subpass = 0;
};