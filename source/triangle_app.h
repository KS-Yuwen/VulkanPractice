#pragma once
#include "core/sample_app.h"
#include "core/buffer_resource.h"
#include <glm.hpp>
#include <vulkan/vulkan_core.h>

class TriangleApp : public ISampleApp
{
public:
	virtual void OnInitialize() override;
	virtual void OnDrawFrame() override;
	virtual void OnCleanup() override;

	struct Vertex
	{
		glm::vec3 position;
		glm::vec3 color;
	};

private:
	void InitializeTriangleVertexBuffer();
	void InitializeGraphicsPileline();

	std::shared_ptr<VertexBuffer> m_vertexBuffer;
	VkPipeline m_pipeline = VK_NULL_HANDLE;
	VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
};
