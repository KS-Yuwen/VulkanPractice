#pragma once
#include <vector>
#include <memory>

#include "../../core/buffer_resource.h"
#include "../../core/image_resource.h"
#include "../../core/sampler.h"
#include "../../core/resource_uploader.h"

#include "../model_data.h"

namespace Render::ms
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
			Count,
		};
		struct VertexAttribute
		{
			std::shared_ptr<StorageBuffer> buffer;
			VkFormat format = VK_FORMAT_UNDEFINED;
			uint32_t stride = 0;
		};

		struct MeshPrimitive
		{
			uint32_t firstIndex = 0;
			uint32_t indexCount = 0;
			uint32_t vertexCount = 0;
			uint32_t vertexOffset = 0;

			uint32_t material = 0;

			MeshPrimitive(const ModelData::MeshPrimitive& src)
				: firstIndex(src.firstIndex), indexCount(src.indexCount), vertexCount(src.vertexCount), vertexOffset(src.vertexOffset), material(src.material)
			{
			}
		};

		struct MeshletPrimitive
		{
			struct MeshletInfo
			{
				uint32_t vertexOffset;
				uint32_t triangleOffset;
				uint32_t vertexCount;
				uint32_t triangleCount;

				uint64_t positionBufferAddress;
				uint64_t normalBufferAddress;
				uint64_t uv0BufferAddress;
				uint64_t tangentBufferAddress;
				uint64_t binormalBufferAddress;
				uint32_t materialIndex;
				uint32_t _padd0;
			};

			// メッシュレット情報(MeshletInfo)
			struct {
				std::shared_ptr<StorageBuffer> buffer;
				std::vector<MeshletInfo> data;
			} meshletInfo;
			// メッシュレット：頂点インデックスバッファ
			struct {
				std::shared_ptr<StorageBuffer> buffer;
				std::vector<uint32_t> data;
			} meshletVertexIndexBuffer;
			// メッシュレット：プリミティブインデックスバッファ
			struct {
				std::shared_ptr<StorageBuffer> buffer;
				std::vector<uint8_t> data;
			} meshletPrimitiveIndexBuffer;

			uint64_t meshletCount;
			uint32_t materialIndex;
		};

		struct Mesh
		{
			std::string name;
			std::vector<MeshPrimitive> primitives;
			std::vector<MeshletPrimitive> meshletPrimitives;

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
			float metallicFactor = 0.0f;

			int baseColorTexture = -1;
			int metallicRoughnessTexture = -1;
			int normalMap = -1;

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


		void Cleanup();

		static std::shared_ptr<ModelResource> CreateFromModelData(const ModelData& data, ResourceUploader& uploader);

		const VertexAttribute& GetVertexAttribute(uint32_t index) const { return m_vertexAttribs[index]; }
		std::shared_ptr<StorageBuffer> GetIndexBuffer() const { return m_indexBuffer; }
		const std::vector<Mesh>& GetMeshes()const { return m_meshes; }
		const std::vector<Material>& GetMaterials() const { return m_materials; }

		const std::vector<Node>& GetNodes() const { return m_nodes; }
		const std::vector<TextureBinding>& GetTextureBindings() const { return m_textureBindings; }
		const std::vector<std::shared_ptr<Texture2D>>& GetTextureImageList() const { return m_textureImages; }
		const std::vector<std::shared_ptr<Sampler>>& GetSamplers() const { return m_samplers; }

	private:
		std::vector<VertexAttribute> m_vertexAttribs;
		std::shared_ptr<StorageBuffer> m_indexBuffer;
		std::vector<std::shared_ptr<Texture2D>> m_textureImages;
		std::vector<TextureBinding> m_textureBindings;
		std::vector<std::shared_ptr<Sampler>> m_samplers;

		// モデル構成情報
		std::vector<Material> m_materials;
		std::vector<Mesh> m_meshes;
		std::vector<Node> m_nodes;

		friend class Render::ms::DrawObject;
	};
}