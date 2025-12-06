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

#include "Render/model_resource.h"
#include "Render/draw_object.h"
#include "Render/basic_pbr_render.h"

class DrawModelApp : public ISampleApp
{
public:
	virtual void OnInitialize() override;
	virtual void OnDrawFrame() override;
	virtual void OnCleanup() override;

	struct SceneConstants
	{
		glm::mat4 mtxView;
		glm::mat4 mtxProj;
		glm::vec4 lightDir;
		glm::vec3 eyePosition;
		float exposure;
	};

private:
	void CreateSceneUniformBuffer();
	void CreateDescriptorSets();

	ResourceUploader m_resourceUploader{};

	std::shared_ptr<DynamicUniformBuffer> m_sceneUniform;
	std::shared_ptr<render::ModelResource> m_modelResource;
	std::shared_ptr<render::DrawObject> m_drawObject;

	VkDescriptorSet m_sceneDescriptorSet = VK_NULL_HANDLE;
	std::shared_ptr<render::BasicPBRRender> m_renderPBR;
};