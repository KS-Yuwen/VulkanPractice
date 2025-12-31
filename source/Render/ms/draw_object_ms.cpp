#include "draw_object_ms.h"
#include <queue>

void Render::ms::DrawObject::Initialize(std::shared_ptr<ModelResource>& modelResource, ResourceUploader& uploader, VkDescriptorSetLayout dsMaterialLayout, glm::mat4 mtxWorld)
{
	auto& vulkanCtx = VulkanContext::Get();
	auto device = vulkanCtx.GetVkDevice();

	// マテリアルのコピー
	m_materials.reserve(modelResource->m_materials.size());
	for (const auto& src : modelResource->m_materials)
	{
		Material material{};
		material.baseColor = glm::vec4(src.baseColorFactor, src.alpha);
		material.metallicFactor = src.metallicFactor;
		material.roughnessFactor = src.roughnessFactor;
		material.alphaMode = uint32_t(src.alphaMode);

		material.baseTexture = src.baseColorTexture;
		material.metallicRoughnessTexture = src.metallicRoughnessTexture;
		material.normalTexture = src.normalMap;
		material.alphaCutoff = src.alphaCutoff;

		m_materials.push_back(material);
	}

	auto bufferSize = sizeof(Material) * m_materials.size();
	m_materialBuffer = StorageBuffer::Create(bufferSize, StorageBuffer::AccessMode::GPUOnlyAccess);
	uploader.UploadBuffer(m_materialBuffer.get(), m_materials.data(), bufferSize, VK_ACCESS_SHADER_READ_BIT);

	std::vector<VkDescriptorImageInfo> textureList;
	const auto& textureImageList = modelResource->GetTextureImageList();
	const auto& textureBIndings = modelResource->GetTextureBindings();

	for (auto& binding : modelResource->GetTextureBindings())
	{
		const auto& image = textureImageList[binding.textureImageIndex];
		textureList.push_back(std::move(image->GetDescriptorInfo(*binding.sampler)));
	}

	m_materialDescriptorSet = vulkanCtx.AllocateDescriptorSet(dsMaterialLayout);
	auto materialBufferSsbo = m_materialBuffer->GetDescriptorInfo();
	std::vector<VkWriteDescriptorSet> writes = {
		{	// [0]:マテリアルバッファ
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = m_materialDescriptorSet,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.pBufferInfo = &materialBufferSsbo,
		},
		{	// [1]:テクスチャリスト
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = m_materialDescriptorSet,
			.dstBinding = 1,
			.dstArrayElement = 0,
			.descriptorCount = uint32_t(textureList.size()),
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = textureList.data(),
		},
	};
	vkUpdateDescriptorSets(device, uint32_t(writes.size()), writes.data(), 0, nullptr);

	// ノードのコピー
	for (const auto& node : modelResource->m_nodes)
	{
		m_nodes.push_back(node);
	}

	// 単位行列でワールド行列を更新
	UpdateWorldMatrices(mtxWorld);

	// モデルリソースへの参照をもたせる
	m_model = modelResource;
}

void Render::ms::DrawObject::Cleanup()
{
	auto& vulkanCtx = VulkanContext::Get();
}

void Render::ms::DrawObject::UpdateWorldMatrices(const glm::mat4& mtxTransform)
{
	std::queue<int> nodeQueue;

	// 幅優先探索でノードを処理
	nodeQueue.push(0);
	while (!nodeQueue.empty())
	{
		auto index = nodeQueue.front();
		nodeQueue.pop();
		auto& node = m_nodes[index];

		auto mtxParent = mtxTransform;
		if (node.parent >= 0)
		{
			mtxParent = m_nodes[node.parent].mtxWorld;
		}
		node.mtxWorld = mtxParent * node.mtxLocal;


		// 子供ノードを追加
		for (auto& i : node.children)
		{
			nodeQueue.push(i);
		}
	}
}