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
#include "core/acceleration_structure.h"

#include "Render/rt/model_resource_rt.h"
#include "Render/rt/draw_object_rt.h"

class MeshShaderTriangleApp : public ISampleApp
{
public:
	virtual void OnInitialize() override;
	virtual void OnDrawFrame() override;
	virtual void OnCleanup() override;

	struct Vertex
	{
		glm::vec4 position;
		glm::vec4 color;
	};

private:
	void CreateTriangleGeometry();
	void CreateDescriptorSetLayout();

	void CreateDescriptorSet();
	void CreatePipeline();

	void BeginScene(std::shared_ptr<CommandBuffer> commandBuffer);
	void EndScene(std::shared_ptr<CommandBuffer> commandBuffer);

	ResourceUploader m_resourceUploader{};

	struct {
		std::shared_ptr<StorageBuffer> vertexBuffer;
		std::shared_ptr<StorageBuffer> indexBuffer;
		uint32_t indexCount;
		uint32_t vertexCount;
	} m_triangle;

	VkPipeline m_pipeline = VK_NULL_HANDLE;
	VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

	VkDescriptorSet m_sceneDescriptorSet = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
};