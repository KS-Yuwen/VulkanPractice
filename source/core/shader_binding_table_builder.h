#pragma once
#include "vulkan_context.h"
#include "buffer_resource.h"

enum class RaytraceShaderGroupType {
	RayGen,
	Miss,
	HitGroup,
};

class ShaderBindingTableBuilder
{
public:
	ShaderBindingTableBuilder(VkPipeline rtPipeline, uint32_t totalShaderGroupCount);

	ShaderBindingTableBuilder& AddShadowGroup(
		RaytraceShaderGroupType type, uint32_t groupIndex,
		const void* recordData = nullptr, size_t recordSize = 0);

	std::shared_ptr<ShaderBindingTableBuffer> Build();

	struct ShaderBindingRegions
	{
		VkStridedDeviceAddressRegionKHR raygen{};
		VkStridedDeviceAddressRegionKHR miss{};
		VkStridedDeviceAddressRegionKHR rchit{};
		VkStridedDeviceAddressRegionKHR callable{};
	};
	const ShaderBindingRegions& GetShaderBindingRegions() const { return m_bindingRegions; }
private:
	void FetchShaderHandles();

	VkPipeline m_pipeline = VK_NULL_HANDLE;
	uint32_t m_totalShaderGroupCount = 0;

	uint32_t m_shaderGroupHandleSize{};
	uint32_t m_shaderGroupHandleAlignment{};
	uint32_t m_shaderGroupBaseAlignment{};

	struct ShaderGroup{
		RaytraceShaderGroupType type;
		uint32_t groupIndex;	// パイプライン内でのシェーダーグループ番号
		std::vector<uint8_t> recordData;	// 任意のSBTレコード
	};

	std::vector<ShaderGroup> m_rayGenGroups;
	std::vector<ShaderGroup> m_missGroups;
	std::vector<ShaderGroup> m_hitGroups;

	std::vector<uint8_t> m_shaderHandles;
	ShaderBindingRegions m_bindingRegions{};
};
