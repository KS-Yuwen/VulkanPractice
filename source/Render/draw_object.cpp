#include "draw_object.h"
#include <queue>

namespace render
{
	void DrawObject::Initialize(std::shared_ptr<ModelResource>& modelResource, VkDescriptorSetLayout dsMaterialLayout)
	{
		auto& vulkanCtx = VulkanContext::Get();
		auto device = vulkanCtx.GetVkDevice();

		// マテリアルのコピー
		m_materials.reserve(modelResource->m_materials.size());
		for (const auto& src : modelResource->m_materials)
		{
			auto& material = m_materials.emplace_back();
			material.name = src.name;
			material.descriptorSet = vulkanCtx.AllocateDescriptorSet(dsMaterialLayout);
			material.alphaMode = src.alphaMode;

			auto baseColorInfo = modelResource->GetBaseColorImageInfo(src);
			auto metallicRoughnessInfo = modelResource->GetMetallicRoughnessImageInfo(src);
			auto normalMapInfo = modelResource->GetNormalMapImageInfo(src);
			auto descriptorBufferInfo = src.materialUbo->GetDescriptorInfo();

			std::vector<VkWriteDescriptorSet> writes;

			// index[0] : マテリアルUniformBuffer
			writes.push_back({
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = material.descriptorSet,
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.pBufferInfo = &descriptorBufferInfo,
			});

			// index[1] : BaseColor(TEX)
			writes.push_back({
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = material.descriptorSet,
				.dstBinding = 1,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = &baseColorInfo,
			});

			// index[2] : MetallicRoughness(TEX)
			writes.push_back({
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = material.descriptorSet,
				.dstBinding = 2,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = &metallicRoughnessInfo,
			});

			// index[3] : NormalMap(TEX)
			writes.push_back({
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = material.descriptorSet,
				.dstBinding = 3,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = &normalMapInfo,
				});

			vkUpdateDescriptorSets(device, uint32_t(writes.size()), writes.data(), 0, nullptr);
		}

		// ノードのコピー
		for (const auto& node : modelResource->m_nodes)
		{
			m_nodes.push_back(node);
		}

		// 単位行列でワールド行列を更新
		UpdateWorldMatrices(glm::mat4(1.0f));

		// モデルリソースへの参照を持たせる
		m_model = modelResource;
	}

	void DrawObject::Cleanup()
	{
		auto& vulkanCtx = VulkanContext::Get();

		for (auto& material : m_materials)
		{
			vulkanCtx.FreeDescriptorSet(material.descriptorSet);
			material.descriptorSet = VK_NULL_HANDLE;
		}
		m_materials.clear();
	}

	void DrawObject::UpdateWorldMatrices(const glm::mat4& mtxTransform)
	{
		std::queue<int> nodeQueue;

		// 幅優先検索でノードを処理
		nodeQueue.push(0);
		while (!nodeQueue.empty())
		{
			auto index = nodeQueue.front();
			nodeQueue.pop();
			auto& node = m_nodes[index];

			auto mtxParent = mtxTransform;
			if (node.parent > 0)
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
}
