#include "acceleration_structure.h"
#include <cassert>

AccelerationStructure::AccelerationStructure()
{
}

void AccelerationStructure::Destroy()
{
	auto& vulkanCtx = VulkanContext::Get();
	auto device = vulkanCtx.GetVkDevice();
	m_scratchBuffer->Cleanup();
	m_scratchBuffer.reset();

	vkDestroyAccelerationStructureKHR(device, m_accelerationStructure.handle, nullptr);
	m_accelerationStructure.handle = VK_NULL_HANDLE;
	m_accelerationStructure.buffer->Cleanup();
	m_accelerationStructure.buffer.reset();
}

void AccelerationStructure::BuildAS(
	VkAccelerationStructureTypeKHR type, const Input& input, VkBuildAccelerationStructureFlagsKHR buildFlags)
{
	auto& vulkanCtx = VulkanContext::Get();
	auto device = vulkanCtx.GetVkDevice();

	// サイズを求める
	VkAccelerationStructureBuildGeometryInfoKHR asBuildGeometryInfo{
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = type,
		.flags = buildFlags,
		.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
		.geometryCount = uint32_t(input.asBuildRangeInfo.size()),
		.pGeometries = input.asGeometry.data(),
	};

	VkAccelerationStructureBuildSizesInfoKHR asBuildSizesInfo{
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
	};

	std::vector<uint32_t> numPrimitives;
	numPrimitives.reserve(input.asBuildRangeInfo.size());
	for (int i = 0; i < input.asBuildRangeInfo.size(); ++i)
	{
		numPrimitives.push_back(input.asBuildRangeInfo[i].primitiveCount);
	}

	vkGetAccelerationStructureBuildSizesKHR(
		device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&asBuildGeometryInfo,
		numPrimitives.data(),
		&asBuildSizesInfo);

	// AccelerationStructureを確保する
	m_accelerationStructure.buffer = AccelerationStructureBuffer::Create(asBuildSizesInfo.accelerationStructureSize);

	VkAccelerationStructureCreateInfoKHR asCreateInfo{
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
		.buffer = m_accelerationStructure.buffer->GetVkBuffer(),
		.size = m_accelerationStructure.buffer->GetBufferSize(),
		.type = asBuildGeometryInfo.type,
	};
	auto res = vkCreateAccelerationStructureKHR(
		device, &asCreateInfo, nullptr, &m_accelerationStructure.handle);
	assert(res == VK_SUCCESS);

	// AccelerationStructureのデバイスアドレスを取得
	VkAccelerationStructureDeviceAddressInfoKHR asDeviceAddressInfo{
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
		.accelerationStructure = m_accelerationStructure.handle,
	};
	m_accelerationStructure.deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(device, &asDeviceAddressInfo);

	// スクラッチバッファを準備する
	if (asBuildSizesInfo.buildScratchSize > 0) {
		m_scratchBuffer = StorageBuffer::Create(asBuildSizesInfo.buildScratchSize, StorageBuffer::AccessMode::GPUOnlyAccess);
	}

	// AccelerationStructureを構築する
	asBuildGeometryInfo.dstAccelerationStructure = m_accelerationStructure.handle;
	asBuildGeometryInfo.scratchData.deviceAddress = m_scratchBuffer->GetDeviceAddress();
	Build(asBuildGeometryInfo, input.asBuildRangeInfo);
}

void AccelerationStructure::Build(
	const VkAccelerationStructureBuildGeometryInfoKHR& asBuildGeometryInfo,
	const std::vector<VkAccelerationStructureBuildRangeInfoKHR>& asBuildRangeInfo)
{
	std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> asBuildRangeInfoPtrs;
	for (auto& v : asBuildRangeInfo) {
		asBuildRangeInfoPtrs.push_back(&v);
	}
	auto command = VulkanContext::Get().CreateCommandBuffer();
	command->Begin();

	vkCmdBuildAccelerationStructuresKHR(
		*command, 1, &asBuildGeometryInfo, asBuildRangeInfoPtrs.data());

	// メモリバリアが必要
	VkMemoryBarrier2 memoryBarrier{
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
		.srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
		.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
		.dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
		.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
	};
	VkDependencyInfo dependencyInfo{
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.memoryBarrierCount = 1,
		.pMemoryBarriers = &memoryBarrier,
	};
	vkCmdPipelineBarrier2(*command, &dependencyInfo);

	command->End();

	// AccelerationStructureビルド＆完了するまで待機
	VulkanContext::Get().SubmitAndWait(command);
}
