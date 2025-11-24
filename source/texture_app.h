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
#include "core/sampler.h"

class TextureApp : public ISampleApp
{
public:
	virtual void OnInitialize() override;
	virtual void OnDrawFrame() override;
	virtual void OnCleanup() override;

	struct Vertex
	{
		glm::vec3 position;
		glm::vec2 uv;
	};

	struct SceneConstants
	{
		glm::mat4 mtxWorld;
		glm::mat4 mtxView;
		glm::mat4 mtxProj;
	};

private:
	void CreatePlaneGeometry();
	void CreateTextureResource();
	void CreateDescriptorSetLayout();
	void CreateUniformBuffers();
	void CreateDescriptorSets();
	void CreateDepthBuffer();
	void CreateGraphicsPipeline();

	ResourceUploader m_resourceUploader{};

	VkPipeline m_pipeline = VK_NULL_HANDLE;
	VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

	struct {
		std::shared_ptr<VertexBuffer> vertexBuffer;
		std::shared_ptr<IndexBuffer> indexBuffer;

		uint32_t indexCount;
	} m_plane{};
	std::shared_ptr<Texture2D> m_texture;
	std::shared_ptr<Sampler> m_sampler;

	VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;

	std::array<std::shared_ptr<UniformBuffer>, 2> m_uniformBuffers;
	std::array<VkDescriptorSet, 2> m_descriptorSets;

	std::shared_ptr<DepthBuffer> m_depthBuffer;
};