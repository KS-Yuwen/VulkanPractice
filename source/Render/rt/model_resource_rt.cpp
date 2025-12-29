#include "model_resource_rt.h"
#include "../../core/asset_path.h"
#include "../../core/texture_loader.h"

namespace Render::rt
{
	void ModelResource::Cleanup()
	{
		m_vertexAttribs.clear();
		m_indexBuffer.reset();
		m_textureImages.clear();
		m_samplers.clear();
		m_textureBindings.clear();

		m_materials.clear();
		m_meshes.clear();
		m_nodes.clear();
	}

	std::shared_ptr<ModelResource> ModelResource::CreateFromModelData(const ModelData& modelData, ResourceUploader& uploader)
	{
		auto resource = std::make_shared<ModelResource>();
		VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

		// 頂点バッファ
		resource->m_vertexAttribs.resize(int(ModelResource::VertexAttributeIndex::Count));
		for (int i = 0; i< int(resource->m_vertexAttribs.size()); ++i)
		{
			const auto& src = modelData.vertexBuffers[i];
			if (src.data.empty())
			{
				continue;
			}
			auto& dst = resource->m_vertexAttribs[i];
			dst.format = src.format;
			dst.stride = src.stride;
			dst.buffer = VertexBuffer::Create(src.data.size(), memProps);
			if (!dst.buffer)
			{
				throw std::runtime_error("failed to create VertexBuffer");
			}
			uploader.UploadBuffer(dst.buffer.get(), src.data.data(), src.data.size(), VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT);
		}

		// インデックスバッファ
		if (!modelData.indexBuffers.empty())
		{
			const auto& indexData = modelData.indexBuffers[0];
			auto size = sizeof(uint32_t) * indexData.indices.size();
			resource->m_indexBuffer = IndexBuffer::Create(size, memProps);

			auto& dst = resource->m_indexBuffer;
			if (!dst)
			{
				throw std::runtime_error("failed to create IndexBuffer");
			}
			uploader.UploadBuffer(dst.get(), indexData.indices.data(), size, VK_ACCESS_INDEX_READ_BIT);
		}

		// メッシュ構造のコピー
		for (const auto& meshData : modelData.meshes)
		{
			resource->m_meshes.push_back(meshData);
		}
		// マテリアルのコピー
		resource->m_materials.reserve(modelData.materials.size());
		for (const auto& material : modelData.materials)
		{
			resource->m_materials.push_back(material);
		}

		// テクスチャのコピー
		for (const auto& src : modelData.textureImages)
		{
			auto [texture, uploadRequest] = loader::LoadTexture2DFromMemory(src.imageData.data(), src.imageData.size(), true);
			uploader.UploadImage(texture, uploadRequest);
			resource->m_textureImages.push_back(texture);
		}

		// サンプラのコピー
		for (const auto& src : modelData.samplers)
		{
			auto sampler = Sampler::Create();
			sampler->Initialize(
				src.minFilter, src.magFilter, src.mipmapMode,
				src.addressModeU, src.addressModeV
			);
			resource->m_samplers.push_back(std::move(sampler));
		}
		// テクスチャバインディングのコピー
		for (const auto& src : modelData.textures)
		{
			TextureBinding texBinding{};
			texBinding.sampler = resource->m_samplers[src.samplerIndex];
			texBinding.texture = resource->m_textureImages[src.imageIndex];
			texBinding.textureImageIndex = src.imageIndex;
			resource->m_textureBindings.push_back(std::move(texBinding));
		}

		// ノードのコピー
		for (const auto& nodeData : modelData.nodes)
		{
			resource->m_nodes.push_back(nodeData);
		}

		return resource;
	}
}