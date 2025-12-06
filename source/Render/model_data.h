#pragma once

#include "../core/vulkan_context.h"

#include <vector>
#include <string>
#include <glm.hpp>

struct ModelData
{
	struct VertexBufferData
	{
		std::vector<uint8_t> data; // バイナリとして保持
		VkFormat format = VK_FORMAT_UNDEFINED;
		uint32_t stride = 0;
	};

	struct IndexBufferData
	{
		std::vector<uint32_t> indices;
	};

	struct SamplerData
	{
		VkFilter magFilter = VK_FILTER_LINEAR;
		VkFilter minFilter = VK_FILTER_LINEAR;
		VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		VkSamplerAddressMode addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		VkSamplerAddressMode addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;

		auto operator<=>(const SamplerData&) const = default; // C++20
	};

	struct TextureImageData
	{
		std::string name;
		std::vector<uint8_t> imageData;
		bool isEmbedded = false;
	};

	struct TextureBinding
	{
		int imageIndex = -1;
		int samplerIndex = -1;
	};

	struct MaterialData
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
	};

	struct MeshPrimitive
	{
		uint32_t firstIndex = 0;
		uint32_t indexCount = 0;
		uint32_t vertexCount = 0;
		uint32_t vertexOffset = 0;

		int position = -1;
		int normal = -1;
		int texcoord0 = -1;
		int tangent = -1;
		int binormal = -1;
		int indices = -1;

		int material = -1;
	};

	struct MeshData
	{
		std::string name;
		std::vector<MeshPrimitive> primiteves;
	};

	struct NodeData
	{
		std::string name;
		glm::mat4 mtxLocal{ 1.0f };
		int parent = -1;
		std::vector<int> children;
		std::vector<int> meshes;
	};

	std::vector<NodeData> nodes;
	std::vector<MeshData> meshes;
	std::vector<MaterialData> materials;
	std::vector<TextureImageData> textureImages;
	std::vector<SamplerData> samplers;
	std::vector<TextureBinding> textures;
	std::vector<VertexBufferData> vertexBuffers;
	std::vector<IndexBufferData> indexBuffers;

	int defaultSceneRoot = 0;	// ルートノード
};
