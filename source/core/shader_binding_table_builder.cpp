#include "shader_binding_table_builder.h"
#include <exception>
#include <stdexcept>
#include <algorithm>
#include <cassert>

namespace {
	size_t toAlign(size_t size, size_t align)
	{
		return (size + align - 1) & ~(align - 1);
	}
}

ShaderBindingTableBuilder::ShaderBindingTableBuilder(VkPipeline rtPipeline, uint32_t totalShaderGroupCount)
	: m_pipeline(rtPipeline), m_totalShaderGroupCount(totalShaderGroupCount)
{
	FetchShaderHandles();
}

ShaderBindingTableBuilder& ShaderBindingTableBuilder::AddShaderGroup(
	RaytraceShaderGroupType type, uint32_t groupIndex,
	const void* recordData, size_t recordSize)
{
	ShaderGroup entry{ type, groupIndex };
	if (recordData != nullptr && recordSize > 0)
	{
		entry.recordData.resize(recordSize);
		memcpy(entry.recordData.data(), recordData, recordSize);
	}

	switch (type)
	{
	case RaytraceShaderGroupType::RayGen:
		m_rayGenGroups.push_back(std::move(entry));
		break;
	case RaytraceShaderGroupType::Miss:
		m_missGroups.push_back(std::move(entry));
		break;
	case RaytraceShaderGroupType::HitGroup:
		m_hitGroups.push_back(std::move(entry));
		break;
	}

	return *this;
}

std::shared_ptr<ShaderBindingTableBuffer> ShaderBindingTableBuilder::Build()
{
    size_t offset = 0;

    auto alignSection = [this, &offset](auto groupCount) {
        if (groupCount > 0) {
            offset = toAlign(offset, m_shaderGroupBaseAlignment);
        }
    };
    // 同グループにあるレコードサイズは同一で揃っているかを確認
    auto checkSameRecordSize = [this](const auto& groups) {
        if (groups.empty()) { return; }
        size_t firstSize = groups.front().recordData.size();
        assert(std::all_of(groups.begin(), groups.end(), [=](const auto& e) { return e.recordData.size() == firstSize; }));
    };

    // Ray Generation Section
    alignSection(m_rayGenGroups.size());
    checkSameRecordSize(m_rayGenGroups);
    size_t rayGenOffset = offset;
    for (auto& e : m_rayGenGroups)
    {
        auto entrySize = toAlign(m_shaderGroupHandleSize + e.recordData.size(), m_shaderGroupHandleAlignment);
        offset += entrySize;
    }

    // Miss Section
    alignSection(m_missGroups.size());
    checkSameRecordSize(m_missGroups);
    size_t missOffset = offset;
    for (auto& e : m_missGroups)
    {
        auto entrySize = toAlign(m_shaderGroupHandleSize + e.recordData.size(), m_shaderGroupHandleAlignment);
        offset += entrySize;
    }

    // HitGroup Section
    alignSection(m_hitGroups.size());
    checkSameRecordSize(m_hitGroups);
    size_t hitOffset = offset;
    for (auto& e : m_hitGroups)
    {
        auto entrySize = toAlign(m_shaderGroupHandleSize + e.recordData.size(), m_shaderGroupHandleAlignment);
        offset += entrySize;
    }

    // ShaderBindingTable のバッファを作成
    size_t shaderBindingTableSize = offset;
    auto sbtBuffer = ShaderBindingTableBuffer::Create(shaderBindingTableSize);
    uint8_t* mapped = reinterpret_cast<uint8_t*>(sbtBuffer->Map());

    // データを書き込む
    auto writeSection = [this, &mapped](const std::vector<ShaderGroup>& groups, size_t startOffset) {
        size_t localOffset = startOffset;
        for (auto& e : groups)
        {
            auto dst = mapped + localOffset;
            // 目的のシェーダーハンドルを取得
            auto handle = m_shaderHandles.data() + (m_shaderGroupHandleSize * e.groupIndex);

            memcpy(dst, handle, m_shaderGroupHandleSize);
            if (!e.recordData.empty())
            {
                memcpy(dst + m_shaderGroupHandleSize, e.recordData.data(), e.recordData.size());
            }
            localOffset += toAlign(m_shaderGroupHandleSize + e.recordData.size(), m_shaderGroupHandleAlignment);
        }
    };

    writeSection(m_rayGenGroups, rayGenOffset);
    writeSection(m_missGroups, missOffset);
    writeSection(m_hitGroups, hitOffset);

    sbtBuffer->Unmap();

    // リージョン情報を構築
    VkDeviceAddress baseAddress = sbtBuffer->GetDeviceAddress();
    auto createRegion = [this, baseAddress](size_t offset, const std::vector<ShaderGroup>& groups) {
        VkStridedDeviceAddressRegionKHR region{};
        if (groups.empty()) {
            return region;
        }
        region.deviceAddress = baseAddress + offset;
        region.stride = toAlign(m_shaderGroupHandleSize + groups.front().recordData.size(), m_shaderGroupHandleAlignment);
        region.size = region.stride * groups.size();
        return region;
    };
    m_bindingRegions.raygen = createRegion(rayGenOffset, m_rayGenGroups);
    m_bindingRegions.miss = createRegion(missOffset, m_missGroups);
    m_bindingRegions.rchit = createRegion(hitOffset, m_hitGroups);
    m_bindingRegions.callable = { };

    return sbtBuffer;
}

void ShaderBindingTableBuilder::FetchShaderHandles()
{
	auto& vulkanCtx = VulkanContext::Get();
	auto device = vulkanCtx.GetVkDevice();

	VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProps{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR
	};
	VkPhysicalDeviceProperties2 physDevProps2{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
		.pNext = &rtProps,
	};
	vkGetPhysicalDeviceProperties2(vulkanCtx.GetVkPhysicalDevice(), &physDevProps2);

	// 各サイズ・アライメント情報を取得
	m_shaderGroupHandleSize = rtProps.shaderGroupHandleSize;
	m_shaderGroupHandleAlignment = rtProps.shaderGroupHandleAlignment;
	m_shaderGroupBaseAlignment = rtProps.shaderGroupBaseAlignment;

	size_t totalHandleSize = m_totalShaderGroupCount * m_shaderGroupHandleSize;
	m_shaderHandles.resize(totalHandleSize);

	auto result = vkGetRayTracingShaderGroupHandlesKHR(
		device, m_pipeline, 0, m_totalShaderGroupCount,
		totalHandleSize, m_shaderHandles.data()
	);
	if (result != VK_SUCCESS)
	{
		throw std::runtime_error("failed to fetch shader group handles");
	}
}
