#pragma once

#include "vulkan_context.h"
#include "buffer_resource.h"
#include <vector>
#include <memory>

class AccelerationStructure
{
public:
	AccelerationStructure();
	void Destory();

	struct Input {
		std::vector<VkAccelerationStructureGeometryKHR> asGeometry;
		std::vector<VkAccelerationStructureBuildRangeInfoKHR> asBuildRangeInfo;
	};

	// AccelerationStructureの構築
	void BuildAS(VkAccelerationStructureTypeKHR type, const Input& input, VkBuildAccelerationStructureFlagsKHR buildFlags);

	VkAccelerationStructureKHR GetHandle() const { return m_accelerationStructure.handle; }
	VkDeviceAddress GetDeviceAddress() const { return m_accelerationStructure.deviceAddress; }

private:
	void Build(
		const VkAccelerationStructureBuildGeometryInfoKHR& asBuildGeometryInfo,
		const std::vector<VkAccelerationStructureBuildRangeInfoKHR>& asBuildRangeInfo);

	// AccelerationStructure本体データ
	struct {
		VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
		std::shared_ptr<AccelerationStructureBuffer> buffer;
		VkDeviceAddress deviceAddress;
	} m_accelerationStructure;

	// AccelerationStructure構築のための作業バッファ
	std::shared_ptr<StorageBuffer> m_scratchBuffer;
};
