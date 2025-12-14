#pragma once
#include "core/sample_app.h"

#include <string>
#include <vector>
#include <array>
#include <stdexcept>

#include "glm.hpp"
#include "ext.hpp"

#include "core/vulkan_context.h"
#include "core/swapchain.h"
#include "core/resource_uploader.h"
#include "core/buffer_resource.h"
#include "core/image_resource.h"

class TessellationApp : public ISampleApp
{
public:
	virtual void OnInitialize() override;
	virtual void OnDrawFrame() override;
	virtual void OnCleanup() override;

	struct Vertex
	{
		glm::vec3 position;
	};
	struct SceneConstants
	{
		glm::mat4 mtxView;
		glm::mat4 mtxProj;
		glm::vec4 lightDir;
		glm::vec4 eyePosition;
		glm::vec2 tessParameters;
		uint32_t frameTime;
		uint32_t padding;
	};

private:
	void CreatePlateGeometry();
	void CreateDepthBuffer();

	void CreateDescriptorSetLayout();
	void CreateGraphicsPipeline();
	void CreateSceneUniformBuffer();
	void CreateDescriptorSets();

	void BeginScene(std::shared_ptr<CommandBuffer> commandBuffer);
	void EndSecne(std::shared_ptr<CommandBuffer> commandBuffer);

	ResourceUploader m_resourceUploader{};

	struct
	{
		std::shared_ptr<VertexBuffer> vertexBuffer;
		std::shared_ptr<IndexBuffer> indexBuffer;
		uint32_t indexCount;
	} m_plane;

	std::shared_ptr<DynamicUniformBuffer> m_sceneUniform;

	VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
	VkDescriptorSet m_sceneDescriptorSet = VK_NULL_HANDLE;

	VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
	VkPipeline m_pipeline = VK_NULL_HANDLE;

	std::shared_ptr<DepthBuffer> m_depthBuffer;
};