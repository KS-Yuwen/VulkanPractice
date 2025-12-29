#pragma once
#include <memory>
#include <glm.hpp>

#include "../../core/vulkan_context.h"
#include "../../core/buffer_resource.h"
#include "../../core/acceleration_structure.h"

#include "model_resource_rt.h"
#include <string>

namespace Render::rt
{
	class ModelResource;

	// 描画用インスタンスクラス
	class DrawObject {
	public:
		DrawObject() = default;
		~DrawObject() = default;

		using BLAS = std::shared_ptr<AccelerationStructure>;

		void Initialize(std::shared_ptr<ModelResource>& modelResource, glm::mat4 mtxWorld);
		void Cleanup();

		struct RaytraceMaterial {
			glm::vec4 baseColor;
			float metallicFactor;
			float roughnessFactor;
			uint32_t materialKind;
			uint32_t _padd0 = 0;

			uint32_t baseTexture;
			uint32_t metallicRoughnessTexture;
			uint32_t normalTexture;
			uint32_t _padd1 = 0;
		};

		std::shared_ptr<ModelResource> GetModel() const { return m_model; }
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
		struct MeshPrimitive
		{
			BLAS accelerationStructure;
			int nodeIndex;
			uint32_t materialIndex;

			struct
			{
				VkDeviceAddress vbPosition;
				VkDeviceAddress vbNormal;
				VkDeviceAddress vbTexcoord;
				VkDeviceAddress indexBuffer;
			} geometry;
			struct
			{
				uint32_t indexCount;
				uint32_t vertexCount;
				uint32_t indexOffset;
				uint32_t vertexOffset;
			} primitive;
		};

		const std::vector<Node>& GetNodes() const { return m_nodes; }
		const std::vector<RaytraceMaterial>& GetMaterials() const { return m_materials; }

		// TLAS を構築する際に必要となる行列とBLASの配列を返す
		//const std::tuple<std::vector<glm::mat4>, std::vector<BLAS>> GetBlasList() const;
		const std::vector<MeshPrimitive> GetMeshPrimitiveList() const { return m_meshPrimitiveList; }
	private:
		void UpdateWorldMatrices(const glm::mat4& mtxTransform);
		void CreateMeshPrimitiveList();
		void BuildBlas(int nodeIndex, const rt::ModelResource::Mesh& mesh);

		std::shared_ptr<ModelResource> m_model;
		std::vector<RaytraceMaterial> m_materials;

		glm::mat4 m_baseWorldMatrix = glm::mat4(1.0f);
		std::vector<Node> m_nodes;
		std::vector<MeshPrimitive> m_meshPrimitiveList;
	};
}
