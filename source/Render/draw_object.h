#pragma once
#include <memory>
#include <string>
#include <glm.hpp>

#include "../core/vulkan_context.h"
#include "../core/buffer_resource.h"

#include "model_resource.h"

namespace render
{
	class ModelResource;

	// 描画用インスタンスクラス
	class DrawObject {
	public:
		DrawObject() = default;
		~DrawObject() = default;

		void Initialize(std::shared_ptr<ModelResource>& modelResource, VkDescriptorSetLayout dsMaterialLayout);
		void Cleanup();

		void UpdateWorldMatrices(const glm::mat4& mtxTransform);

		std::shared_ptr<ModelResource> GetModel() const { return m_model; }

		glm::mat4 GetNodeWordMatrix(int index) const { return m_nodes[index].mtxWorld; }

		struct Material
		{
			std::string name;
			ModelResource::Material::AlphaMode alphaMode;
			VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
		};

		struct Node
		{
			std::string name;
			glm::mat4 mtxLocal;
			glm::mat4 mtxWorld;
			int parent = -1;
			std::vector<int> children;
			std::vector<int> meshes;

			Node(const ModelResource::Node& src)
				: name(src.name), mtxLocal(src.mtxLocal), mtxWorld(1.0f), parent(src.parent), children(src.children), meshes(src.meshes)
			{
			}
		};

		const std::vector<Node>& GetNodes() const { return m_nodes; }
		const std::vector<Material>& GetMaterials() const { return m_materials; }

	private:
		std::shared_ptr<ModelResource> m_model;
		glm::mat4 m_baseWorldMatrix = glm::mat4(1.0f);
		std::vector<Node> m_nodes;
		std::vector<Material> m_materials;
	};
}