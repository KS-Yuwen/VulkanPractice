#pragma once
#include "../../core/vulkan_context.h"
#include <memory>
#include <vector>
#include <unordered_map>
#include "../../core/buffer_resource.h"
#include "../../core/image_resource.h"

#include "glm.hpp"
#include "ext.hpp"

#include "draw_object_ms.h"

//#define USE_RENDERPASS

namespace Render::ms
{
	class DrawObject;

	class BasicPBRRender
	{
	public:
		BasicPBRRender() = default;
		~BasicPBRRender() = default;

		void Initialize();
		void Cleanup();

		void SetSceneDescriptor(VkDescriptorSet set, uint32_t dynamicOffset);

		void BeginScene(std::shared_ptr<CommandBuffer> commandBuffer);
		void EndScene(std::shared_ptr<CommandBuffer> commandBuffer);

		void Draw(std::shared_ptr<CommandBuffer> commandBuffer, std::shared_ptr<DrawObject>);

		VkDescriptorSetLayout GetSceneLayout() const { return m_layouts.sceneLayout; }
		VkDescriptorSetLayout GetMaterialLayout() const { return m_layouts.materialLayout; }

	private:
		enum class RenderPassTag { Opaque, Transparent };
		struct PushConstantData
		{
			glm::mat4 mtxWorld;
			uint64_t meshletInfoAddr;
			uint64_t meshletVertexIndexBufferAddr;
			uint64_t meshletPrimitiveIndexBufferAddr;
		};

		void CreateDepthBuffer();
		void CreateDescriptorSetLayout();
		void CreateGraphicsPipelines();

		struct {
			VkPipelineLayout pipeline = VK_NULL_HANDLE;
			VkDescriptorSetLayout sceneLayout = VK_NULL_HANDLE;
			VkDescriptorSetLayout materialLayout = VK_NULL_HANDLE;
		} m_layouts;
		std::unordered_map<RenderPassTag, VkPipeline> m_pipelines;

		std::shared_ptr<DepthBuffer> m_depthBuffer;

		struct {
			VkDescriptorSet descriptorSet;
			uint32_t dynamicOffset;
		} m_scene{};

#if defined(USE_RENDERPASS)
	private:
		void InitializeRenderPass();
		VkRenderPass m_renderPass = VK_NULL_HANDLE;
		std::vector<VkFramebuffer> m_framebuffers;
#endif
	};
}