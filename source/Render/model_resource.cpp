#include "model_resource.h"
#include "../core/asset_path.h"
#include "../core/texture_loader.h"

namespace render
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

	std::shared_ptr<ModelResource> render::ModelResource::CreateFromModelData(const ModelData& data, ResourceUploader& uploader)
	{
		auto resource = std::make_shared<ModelResource>();
		VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

		// 頂点バッファ
		resource->m_vertexAttribs.resize(int(ModelResource::VertexAttributeIndex::Count));

		for (int i = 0; i < int(resource->m_vertexAttribs.size()); ++i)
		{
			const auto& src = data.vertexBuffers[i];
			if (src.data.empty())
			{
				continue;
			}
			auto& dst = resource->m_vertexAttribs[i];
			dst.format = src.format;
			dst.stride = src.stride;
			dst.buffer = VertexBuffer::Create(src.data.size(), memProps);
			uploader.UploadBuffer(dst.buffer.get(), src.data.data(), src.data.size(), VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT);
		}

		// インデックスバッファ
		if (!data.indexBuffers.empty())
		{
			const auto& indexData = data.indexBuffers[0];
			auto size = sizeof(uint32_t) * indexData.indices.size();
			resource->m_indexBuffer = IndexBuffer::Create(size, memProps);

			auto& dst = resource->m_indexBuffer;
			uploader.UploadBuffer(dst.get(), indexData.indices.data(), size, VK_ACCESS_INDEX_READ_BIT);
		}

		// メッシュ構造のコピー
		for (const auto& meshData : data.meshes)
		{
			resource->m_meshes.push_back(meshData);
		}

		// マテリアルのコピー
		resource->m_materials.reserve(data.materials.size());
		for (uint32_t i = 0; i < data.materials.size(); ++i)
		{
			const auto& src = data.materials[i];
			resource->m_materials.emplace_back(src);

			GPUMaterialParameters gpuParams{};
			gpuParams.baseColorFactor = glm::vec4(src.baseColorFactor, src.alpha);
			gpuParams.metallicRoughness.x = src.metallicFactor;
			gpuParams.metallicRoughness.y = src.roughnessFactor;
			gpuParams.alphaCutoff = src.alphaCutoff;
			gpuParams.alphaMode = int(src.alphaMode);
			gpuParams.hasNormalMap = (src.normalMap >= 0) ? 1 : 0;

			// UniformBufferへ書き込み
			auto size = sizeof(GPUMaterialParameters);
			auto ubo = UniformBuffer::Create(size);
			if (void* p = ubo->Map(); p != nullptr)
			{
				memcpy(p, &gpuParams, size);
				ubo->Unmap();
			}
			resource->m_materials[i].materialUbo = ubo;
		}

		// テクスチャのコピー
		for (const auto& src : data.textureImages)
		{
			auto [texture, uploadRequest] = loader::LoadTexture2DFromMemory(src.imageData.data(), src.imageData.size(), true);
			uploader.UploadImage(texture, uploadRequest);
			resource->m_textureImages.push_back(texture);
		}

		// サンプラのコピー
		for (const auto& src : data.samplers)
		{
			auto sampler = Sampler::Create();
			sampler->Initialize(
				src.minFilter, src.magFilter, src.mipmapMode,
				src.addressModeU, src.addressModeV
			);
			resource->m_samplers.push_back(std::move(sampler));
		}

		// テクスチャバインディングのコピー
		for (const auto& src : data.textures)
		{
			TextureBinding texBinding{};
			texBinding.sampler = resource->m_samplers[src.samplerIndex];
			texBinding.texture = resource->m_textureImages[src.imageIndex];
			texBinding.textureImageIndex = src.imageIndex;
			resource->m_textureBindings.push_back(std::move(texBinding));
		}

		// ノードのコピー
		for (const auto& nodeData : data.nodes)
		{
			resource->m_nodes.push_back(nodeData);
		}

		// デフォルト(ダミー)として使うためのオブジェクトを用意
		resource->m_defaultSampler = Sampler::Create();
		resource->m_defaultSampler->Initialize(
			VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR,
			VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT
		);
		{
			auto filePath = GetAssetPath(AssetType::Texture, "white.png");
			auto [texture, uploadRequest] = loader::LoadTexture2DFromFile(filePath);
			uploader.UploadImage(texture, uploadRequest);
			resource->m_defaultWhiteTex = texture;
		}

		return resource;
	}

	VkDescriptorImageInfo ModelResource::GetBaseColorImageInfo(const Material& material) const
	{
		if (material.baseColorTexture < 0)
		{
			return VkDescriptorImageInfo{
				.sampler = m_defaultSampler->GetVkSampler(),
				.imageView = m_defaultWhiteTex->GetVkImageView(),
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};
		}

		const auto& bindings = m_textureBindings[material.baseColorTexture];
		return VkDescriptorImageInfo{
			.sampler = bindings.sampler->GetVkSampler(),
			.imageView = bindings.texture->GetVkImageView(),
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};
	}

	VkDescriptorImageInfo ModelResource::GetMetallicRoughnessImageInfo(const Material& material) const
	{
		if (material.metallicRoughnessTexture < 0)
		{
			return VkDescriptorImageInfo{
				.sampler = m_defaultSampler->GetVkSampler(),
				.imageView = m_defaultWhiteTex->GetVkImageView(),
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};
		}

		const auto& bindings = m_textureBindings[material.metallicRoughnessTexture];
		return VkDescriptorImageInfo{
			.sampler = bindings.sampler->GetVkSampler(),
			.imageView = bindings.texture->GetVkImageView(),
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};
	}

	VkDescriptorImageInfo ModelResource::GetNormalMapImageInfo(const Material& material) const
	{
		if (material.normalMap < 0)
		{
			return VkDescriptorImageInfo{
				.sampler = m_defaultSampler->GetVkSampler(),
				.imageView = m_defaultWhiteTex->GetVkImageView(),
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};
		}

		const auto& bindings = m_textureBindings[material.normalMap];
		return VkDescriptorImageInfo{
			.sampler = bindings.sampler->GetVkSampler(),
			.imageView = bindings.texture->GetVkImageView(),
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};
	}
}