#include "model_loader.h"
#include <stdexcept>
#include <fstream>
#include <queue>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/GltfMaterial.h>

// assimp から Vulkan用に変換するための関数
glm::mat4 ConvertMatrix(const aiMatrix4x4 from)
{
	glm::mat4 to;
	to[0][0] = from.a1; to[1][0] = from.a2;
	to[2][0] = from.a3; to[3][0] = from.a4;
	to[0][1] = from.b1; to[1][1] = from.b2;
	to[2][1] = from.b3; to[3][1] = from.b4;
	to[0][2] = from.c1; to[1][2] = from.c2;
	to[2][2] = from.c3; to[3][2] = from.c4;
	to[0][3] = from.d1; to[1][3] = from.d2;
	to[2][3] = from.d3; to[3][3] = from.d4;

	return to;
}

glm::vec2 Convert(const aiVector2D& v) { return glm::vec2(v.x, v.y); }
glm::vec3 Convert(const aiVector3D& v) { return glm::vec3(v.x, v.y, v.z); }
glm::vec3 Convert(const aiColor3D& v) { return glm::vec3(v.r, v.g, v.b); }

VkSamplerAddressMode ConvertAddressMode(aiTextureMapMode mode)
{
	switch (mode)
	{
	default:
	case aiTextureMapMode_Wrap:
		return VK_SAMPLER_ADDRESS_MODE_REPEAT;
	case aiTextureMapMode_Clamp:
		return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	case aiTextureMapMode_Mirror:
		return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	}
}

VkFilter ConvertTexMagFilter(int filter)
{
	switch (filter)
	{
	case 9728:
		return VK_FILTER_NEAREST;
	case 9729:
		return VK_FILTER_LINEAR;
	default:
		return VK_FILTER_LINEAR;
	}
}

VkFilter ConvertTexMinFilter(int filter)
{
	switch (filter)
	{
	case 9728:
		return VK_FILTER_NEAREST;
	case 9729:
		return VK_FILTER_LINEAR;

	case 9984:
		return VK_FILTER_NEAREST;
	case 9985:
		return VK_FILTER_LINEAR;
	case 9986:
		return VK_FILTER_NEAREST;
	case 9987:
		return VK_FILTER_LINEAR;

	default:
		return VK_FILTER_LINEAR;
	}
}

VkSamplerMipmapMode ConvertTexMipmapFilter(int minFilter)
{
	switch (minFilter)
	{
	case 9728:
	case 9729:
	case 9984:
	case 9985:
		return VK_SAMPLER_MIPMAP_MODE_NEAREST;

	default:
		return VK_SAMPLER_MIPMAP_MODE_LINEAR;
	}
}

static bool LoadEmbeddedTexture(ModelData::TextureImageData* textureImage, const aiTexture* srcTex)
{
	size_t size = srcTex->mWidth;
	if (srcTex->mHeight != 0)
	{
		return false;	// 非対応処理
	}

	textureImage->imageData.resize(srcTex->mWidth);
	memcpy(textureImage->imageData.data(), srcTex->pcData, size);
	textureImage->isEmbedded = true;
	return true;
}

static bool LoadFileTexture(ModelData::TextureImageData* textureImage, const std::filesystem::path& filePath)
{
	std::ifstream file(filePath, std::ios::binary | std::ios::ate);
	if (!file)
	{
		return false;
	}

	std::streamsize size = file.tellg();
	file.seekg(0, std::ios::beg);
	textureImage->imageData.resize(size);
	file.read(reinterpret_cast<char*>(textureImage->imageData.data()), size);
	textureImage->isEmbedded = true;
	return true;
}

static void LoadTextures(ModelData* modelData, const aiScene* scene, const std::filesystem::path& baseDir)
{
	using TextureImageData = ModelData::TextureImageData;
	auto textureImages = modelData->textureImages;
	auto& samplers = modelData->samplers;
	auto& textureBindings = modelData->textures;

	// テクスチャ名からインデックス検索
	auto findExistTextureIndex = [&](const std::string& name) {
		auto it = std::find_if(textureImages.begin(), textureImages.end(), [&](const auto& tex) {return tex.name == name; });
		int index = -1;
		if (it != textureImages.end())
		{
			index = int(std::distance(textureImages.begin(), it));
		}
		return index;
	};

	auto addTexture = [&](const aiMaterial* material, aiTextureType type, int index = 0) -> int {
		aiString path;
		if (material->GetTexture(type, index, &path) != aiReturn_SUCCESS)
		{
			return -1;
		}

		std::string texName = path.C_Str();
		// 登録済みであればインデックス取得
		if (auto texFindIndex = findExistTextureIndex(texName); texFindIndex >= 0)
		{
			return texFindIndex;	// 見つかったインデックスを返す
		}

		// 登録の必要あり
		TextureImageData texImageData{};
		texImageData.name = texName;
		if (!texName.empty() && texName[0] == '*')
		{
			int embeddedIndex = std::stoi(texName.substr(1));	// インデックス部分取り出し

			if (embeddedIndex >= 0 && embeddedIndex < int(scene->mNumTextures))
			{
				const auto* texture = scene->mTextures[embeddedIndex];
				if (!LoadEmbeddedTexture(&texImageData, texture))
				{
					return -1;
				}
			}
			else
			{
				auto filePath = baseDir / texName;
				if (!LoadFileTexture(&texImageData, filePath))
				{
					return -1;
				}
			}

			int newIndex = int(modelData->textureImages.size());
			modelData->textureImages.push_back(std::move(texImageData));
			return newIndex;
		}
	};
}

static ModelData::MaterialData::AlphaMode ConvertAlphaMode(const aiString& str)
{
	using AlphaMode = ModelData::MaterialData::AlphaMode;
	std::string mode = str.C_Str();
	if (mode == "OPAQUE") return AlphaMode::Opaque;
	if (mode == "MASK")   return AlphaMode::Mask;
	if (mode == "BLEND")  return AlphaMode::Blend;
	return AlphaMode::Opaque;
}

static ModelData::MaterialData ExtractMaterialProperty(const aiMaterial* mat, const ModelData* modelData)
{
	using MaterialData = ModelData::MaterialData;
	MaterialData material{};
	auto check = [](aiReturn r) { return r == aiReturn_SUCCESS; };

	if (aiString name; check(mat->Get(AI_MATKEY_NAME, name)))
	{
		material.name = name.C_Str();
	}
	if (aiColor3D c; check(mat->Get(AI_MATKEY_BASE_COLOR, c)))
	{
		material.baseColorFactor = Convert(c);
	}
	if (float alpha; check(mat->Get(AI_MATKEY_OPACITY, alpha)))
	{
		material.alpha = alpha;
	}
	if (aiString alphaMode; check(mat->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode)))
	{
		material.alphaMode = ConvertAlphaMode(alphaMode);
	}
	if (float cutoff = 0.5f; check(mat->Get(AI_MATKEY_GLTF_ALPHACUTOFF, cutoff)))
	{
		material.alphaCutoff = cutoff;
	}
	if (float roughness = 1.0f; check(mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness)))
	{
		material.roughnessFactor = roughness;
	}
	if (float metallic = 0.0f; check(mat->Get(AI_MATKEY_METALLIC_FACTOR, metallic)))
	{
		material.metallicFactor = metallic;
	}
	return material;
}


static void LoadMaterials(ModelData* modelData, const aiScene* scene)
{
	using SamplerData = ModelData::SamplerData;
	using TextureBinding = ModelData::TextureBinding;
	auto check = [](aiReturn r) {return r == aiReturn_SUCCESS; };
	auto& textureImages = modelData->textureImages;
	auto& samplers = modelData->samplers;
	auto& textureBindings = modelData->textures;

	auto findTexImageIndex = [&](const std::string& name) {
		auto it = std::find_if(textureImages.begin(), textureImages.end(), [&](const auto& tex) { return tex.name == name; });
		int index = -1;
		if (it != textureImages.end())
		{
			index = int(std::distance(textureImages.begin(), it));
		}
		return index;
	};

	auto resolveTexture = [&](const aiMaterial* material, aiTextureType type, int index) -> int {
		if (material->GetTextureCount(type) == 0)
		{
			return -1;
		}

		aiString texPath;
		if (check(material->GetTexture(type, 0, &texPath)) == false)
		{
			return -1;
		}

		std::string texName = texPath.C_Str();
		auto texImageIndex = findTexImageIndex(texName);
		if (texImageIndex < 0)
		{
			return -1;
		}

		// サンプラー作成
		SamplerData sampler{};
		aiTextureMapMode modeU = aiTextureMapMode_Wrap, modeV = aiTextureMapMode_Wrap;
		material->Get(AI_MATKEY_MAPPINGMODE_U(type, 0), modeU);
		material->Get(AI_MATKEY_MAPPINGMODE_V(type, 0), modeV);
		sampler.addressModeU = ConvertAddressMode(modeU);
		sampler.addressModeV = ConvertAddressMode(modeV);

		int minFilter = 0, magFilter = 0;
		material->Get(AI_MATKEY_GLTF_MAPPINGFILTER_MIN(type, 0), minFilter);
		material->Get(AI_MATKEY_GLTF_MAPPINGFILTER_MAG(type, 0), magFilter);
		sampler.minFilter = ConvertTexMinFilter(minFilter);
		sampler.magFilter = ConvertTexMagFilter(magFilter);
		sampler.mipmapMode = ConvertTexMipmapFilter(minFilter);

		auto samplerIt = std::find(samplers.begin(), samplers.end(), sampler);
		int samplerIndex = -1;
		if (samplerIt != samplers.end())
		{
			samplerIndex = int(std::distance(samplers.begin(), samplerIt));
		}
		else
		{
			samplerIndex = int(samplers.size());
			samplers.push_back(sampler);
		}

		// TextureBinding構築
		TextureBinding binding{
			.imageIndex = texImageIndex,
			.samplerIndex = samplerIndex
		};
		int bindingIndex = int(textureBindings.size());
		textureBindings.push_back(binding);
		return bindingIndex;
	};

	for (uint32_t i = 0; i < scene->mNumMaterials; ++i)
	{
		const auto mat = scene->mMaterials[i];
		auto material = ExtractMaterialProperty(mat, modelData);

		// ベースカラーテクスチャ
		material.baseColorTexture = resolveTexture(mat, aiTextureType_BASE_COLOR, 0);
		// 法線マップ
		material.normalMap = resolveTexture(mat, aiTextureType_NORMALS, 0);
		// メタリックラクネステクスチャ
		material.metallicRoughnessTexture = resolveTexture(mat, AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLICROUGHNESS_TEXTURE);

		modelData->materials.push_back(std::move(material));
	}
}

static void LoadMeshes(ModelData* modelData, const aiScene* scene)
{
	using MeshData = ModelData::MeshData;
	using MeshPrimitive = ModelData::MeshPrimitive;

	assert(modelData != nullptr);
	assert(scene != nullptr);

	// モデルファイル内で穴業した頂点・インデックスバッファ
	std::vector<glm::vec3> positionData;
	std::vector<glm::vec3> normalData;
	std::vector<glm::vec2> texcoordData;
	std::vector<glm::vec3> tangentData;
	std::vector<glm::vec3> binormalData;
	std::vector<uint32_t> indexData;

	int vertexOffset = 0;
	int indexOffset = 0;
	for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
	{
		const aiMesh* mesh = scene->mMeshes[meshIndex];
		MeshData meshData{};
		meshData.name = mesh->mName.C_Str();

		MeshPrimitive primitive{};
		primitive.firstIndex = indexOffset;
		primitive.vertexCount = mesh->mNumVertices;
		primitive.indexCount = mesh->mNumFaces * 3;	// Triangleate済のため
		primitive.material = mesh->mMaterialIndex;

		// 頂点属性ごとのバッファを構築
		for (uint32_t i = 0; i < mesh->mNumVertices; ++i)
		{
			positionData.push_back(Convert(mesh->mVertices[i]));
			// 法線はあるものとして考える
			normalData.push_back(Convert(mesh->mNormals[i]));

			if (mesh->HasTextureCoords(0))
			{
				auto uv = mesh->mTextureCoords[0][i];
				texcoordData.push_back(glm::vec2(uv.x, uv.y));
			}
			else
			{
				texcoordData.push_back(glm::vec2(0.0f));
			}

			if (mesh->HasTangentsAndBitangents())
			{
				tangentData.push_back(Convert(mesh->mTangents[i]));
				binormalData.push_back(Convert(mesh->mBitangents[i]));
			}
			else
			{
				tangentData.push_back(glm::vec3(0.0f));
				binormalData.push_back(glm::vec3(0.0f));
			}
		}

		// インデックスバッファ
		for (uint32_t i = 0; i < mesh->mNumFaces; ++i)
		{
			const auto& face = mesh->mFaces[i];
			indexData.push_back(face.mIndices[0]);
			indexData.push_back(face.mIndices[1]);
			indexData.push_back(face.mIndices[2]);
		}

		primitive.position = 0;
		primitive.normal = 1;
		primitive.texcoord0 = 2;
		primitive.tangent = 3;
		primitive.binormal = 4;
		primitive.indices = int(modelData->indexBuffers.size());
		primitive.vertexOffset = vertexOffset;

		meshData.primiteves.push_back(primitive);
		modelData->meshes.push_back(meshData);

		vertexOffset += mesh->mNumVertices;
		indexOffset += primitive.indexCount;
	}

	// VertexBufferDataへの登録
	auto CreateVertexBufferData = [](const auto& srcVector, size_t stride, VkFormat format) {
		ModelData::VertexBufferData buf;
		buf.stride = uint32_t(stride);
		buf.format = format;
		buf.data.resize(stride * srcVector.size());
		memcpy(buf.data.data(), srcVector.data(), buf.data.size());

		return buf;
	};

	modelData->vertexBuffers.push_back(CreateVertexBufferData(positionData, sizeof(glm::vec3), VK_FORMAT_R32G32B32_SFLOAT));
	modelData->vertexBuffers.push_back(CreateVertexBufferData(normalData, sizeof(glm::vec3), VK_FORMAT_R32G32B32_SFLOAT));
	modelData->vertexBuffers.push_back(CreateVertexBufferData(texcoordData, sizeof(glm::vec2), VK_FORMAT_R32G32_SFLOAT));
	modelData->vertexBuffers.push_back(CreateVertexBufferData(tangentData, sizeof(glm::vec3), VK_FORMAT_R32G32B32_SFLOAT));
	modelData->vertexBuffers.push_back(CreateVertexBufferData(binormalData, sizeof(glm::vec3), VK_FORMAT_R32G32B32_SFLOAT));

	ModelData::IndexBufferData ib;
	ib.indices = std::move(indexData);
	modelData->indexBuffers.push_back(std::move(ib));
}

static void LoadNodes(ModelData* modelData, const aiScene* scene)
{
	using NodeData = ModelData::NodeData;
	std::queue<const aiNode*> nodeQueue;
	std::unordered_map<const aiNode*, int> nodeToIndex;
	auto& nodes = modelData->nodes;

	// 幅優先検索でノードを処理
	nodeQueue.push(scene->mRootNode);
	while (!nodeQueue.empty())
	{
		const auto current = nodeQueue.front();
		nodeQueue.pop();

		int currentIndex = int(nodes.size());
		nodeToIndex[current] = currentIndex;

		NodeData nodeData{};
		nodeData.name = current->mName.C_Str();
		nodeData.mtxLocal = ConvertMatrix(current->mTransformation);

		for (uint32_t i = 0; i < current->mNumMeshes; ++i)
		{
			nodeData.meshes.push_back(current->mMeshes[i]);
		}
		nodes.push_back(nodeData);

		// 子供ノードを追加
		for (uint32_t i = 0; i < current->mNumChildren; ++i)
		{
			nodeQueue.push(current->mChildren[i]);
		}
	}

	// 親子構造を作る
	for (const auto& [node, index] : nodeToIndex)
	{
		for (uint32_t i = 0; i < node->mNumChildren; ++i)
		{
			const auto* child = node->mChildren[i];
			int childIndex = nodeToIndex[child];
			nodes[index].children.push_back(childIndex);
			nodes[childIndex].parent = index;
		}
	}
}

namespace loader
{
	void LoadModelDataFromFile(ModelData* modelData, const std::filesystem::path& filePath)
	{
		Assimp::Importer inporter;
		uint32_t flags = 0;
		flags |= aiProcess_Triangulate;	// 三角形化する
		flags |= aiProcess_RemoveRedundantMaterials;	// 冗長なマテリアルを削除
		flags |= aiProcess_FlipUVs;	// テクスチャ座標系：左上を原点とする
		flags |= aiProcess_PreTransformVertices;
		flags |= aiProcess_GenSmoothNormals;
		flags |= aiProcess_OptimizeMeshes;
		const auto scene = inporter.ReadFile(filePath.string(), flags);

		// テクスチャの読み込み
		std::filesystem::path baseDir = filePath.parent_path();
		LoadTextures(modelData, scene, baseDir);

		// マテリアルの読み込み
		LoadMaterials(modelData, scene);

		// ポリゴンメッシュの読み込み
		LoadMeshes(modelData, scene);

		// シーンノード情報の読み込み
		LoadNodes(modelData, scene);

		// デフォルトのシーンノードを設定
		modelData->defaultSceneRoot = 0;
		std::string rootName = scene->mRootNode->mName.C_Str();
		auto it = std::find_if(modelData->nodes.begin(), modelData->nodes.end(), [&](const auto& n) { return n.name == rootName; });
		if (it != modelData->nodes.end())
		{
			modelData->defaultSceneRoot = int(std::distance(modelData->nodes.begin(), it));
		}
	}

}