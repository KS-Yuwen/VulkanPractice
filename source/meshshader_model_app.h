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

#include "Render/ms/draw_object_ms.h"
#include "Render/ms/model_resource_ms.h"
#include "Render/ms/basic_pbr_render_ms.h"

class MeshShaderModelApp : public ISampleApp
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
	void PrepareModelData();

	ResourceUploader m_resourceUploader{};

	std::shared_ptr<DynamicUniformBuffer> m_sceneUniform;
	VkDescriptorSet m_sceneDescriptorSet;

	std::shared_ptr<Render::ms::ModelResource> m_modelResource;
	std::shared_ptr<Render::ms::DrawObject> m_drawObject;
	std::shared_ptr<Render::ms::BasicPBRRender> m_renderPBR;
};
