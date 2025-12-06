#pragma once
#include <vector>
#include <memory>

#include "../core/buffer_resource.h"
#include "../core/image_resource.h"
#include "../core/sampler.h"
#include "../core/resource_uploader.h"

#include "model_data.h"

namespace render
{
	class DrawObject;

	class ModelResource {
	public:
		ModelResource() = default;
		~ModelResource() = default;

		enum class VertexAttributeIndex : int {
			Position = 0,
			Normal,
			Texcoord0,
			Tangent,
			Binormal,
			Count
		};
		struct VertexAttribute
		{
			std::shared_ptr<VertexBuffer> buffer;
			VkFormat format = VK_FORMAT_UNDEFINED;
			uint32_t stride = 0;
		};

		struct MeshPrimitive
		{
			uint32_t firstIndex = 0;
			uint32_t indexCount = 0;
			uint32_t vertexCount = 0;
			uint32_t vertexOffset = 0;

			int material = -1;

			MeshPrimitive(const ModelData::MeshPrimitive& src)
				: firstIndex(src.firstIndex), indexCount(src.indexCount), vertexCount(src.vertexCount), vertexOffset(src.vertexOffset), material(src.material)
			{
			}
		};

		struct Mesh
		{
			std::string name;
			std::vector<MeshPrimitive> primitives;

			Mesh(const ModelData::MeshData& src) : name(src.name)
			{
				primitives.reserve(src.primiteves.size());
				for (const auto& primitive : src.primiteves)
				{
					primitives.emplace_back(primitive);
				}
			}
		};

		struct Node
		{
			std::string name;
			glm::mat4 mtxLocal{ 1.0f };
			int parent = -1;
			std::vector<int> children;
			std::vector<int> meshes;

			Node(const ModelData::NodeData& src)
				: name(src.name), mtxLocal(src.mtxLocal), parent(src.parent), children(src.children), meshes(src.meshes)
			{
			}
		};

		struct Material
		{
			std::string name;
			glm::vec3 baseColorFactor{ 1.0f };
			float alpha = 1.0f;
			float alphaCutoff = 0.5f;
			enum class AlphaMode { Opaque, Mask, Blend } alphaMode = AlphaMode::Opaque;

			float roughnessFactor = 1.0f;
			float metallicFactor = -1;

			int baseColorTexture = -1;
			int metallicRoughnessTexture = -1;
			int normalMap = -1;

			std::shared_ptr<UniformBuffer> materialUbo;

			Material(const ModelData::MaterialData& src)
				: name(src.name), baseColorFactor(src.baseColorFactor), alpha(src.alpha), alphaCutoff(src.alphaCutoff),
				alphaMode(AlphaMode(src.alphaMode)), roughnessFactor(src.roughnessFactor), metallicFactor(src.metallicFactor),
				baseColorTexture(src.baseColorTexture), metallicRoughnessTexture(src.metallicRoughnessTexture), normalMap(src.normalMap)
			{
			}
		};

		struct TextureBinding
		{
			std::shared_ptr<Texture2D> texture;
			std::shared_ptr<Sampler> sampler;
			int textureImageIndex;
		};

		// GPUに渡す用のマテリアル構造体
		struct GPUMaterialParameters
		{
			glm::vec4 baseColorFactor;
			glm::vec2 metallicRoughness;		// x:metallic, y:rouughness
			float alphaCutoff;
			int alphaMode;

			int hasNormalMap;		// 法線マップ有効時1となる
			uint32_t _padd0;
			uint32_t _padd1;
			uint32_t _padd2;
		};

		void Cleanup();

		static std::shared_ptr<ModelResource> CreateFromModelData(const ModelData& data, ResourceUploader& uploader);

		const VertexAttribute& GetVertexAttribute(uint32_t index) const { return m_vertexAttribs[index]; }
		std::shared_ptr<IndexBuffer> GetIndexBuffer() const { return m_indexBuffer; }
		const std::vector<Mesh>& GetMeshes() const { return m_meshes; }
		const std::vector<Material>& GetMaterials() const { return m_materials; }

		// Helper Funcs
		VkDescriptorImageInfo GetBaseColorImageInfo(const Material& material) const;
		VkDescriptorImageInfo GetMetallicRoughnessImageInfo(const Material& material) const;
		VkDescriptorImageInfo GetNormalMapImageInfo(const Material& material) const;

	private:
		std::vector<VertexAttribute> m_vertexAttribs;
		std::shared_ptr<IndexBuffer> m_indexBuffer;
		std::vector<std::shared_ptr<Texture2D>> m_textureImages;
		std::vector<TextureBinding> m_textureBindings;
		std::vector<std::shared_ptr<Sampler>> m_samplers;

		// モデル構成情報
		std::vector<Material> m_materials;
		std::vector<Mesh> m_meshes;
		std::vector<Node> m_nodes;

		// ダミーテクスチャ
		std::shared_ptr<Texture2D> m_defaultWhiteTex;
		std::shared_ptr<Sampler> m_defaultSampler;

		friend class DrawObject;
	};
}