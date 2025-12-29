
#include "vulkan_context.h"
#include "swapchain.h"
#include <stdexcept>
#include <cassert>
#include <sstream>
#include <iostream>
#include <set>

#define VK_GET_INSTANCE_PROC_ADDR(instance, name, ...) \
    reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(instance, #name))

VulkanContext& VulkanContext::Get()
{
	static VulkanContext instance;
	return instance;
}

void VulkanContext::Initialize(
	const char* appName,
	ISurfaceProvider* surfaceProvider)
{
	m_surfaceProvider = surfaceProvider;

	// 最初にVolkの初期化
	volkInitialize();

	CreateInstance(appName);        // Vulkanインスタンスの作成

	// VkInstance 作成後に Volk 呼び出し
	volkLoadInstance(m_vkInstance);

	PickPhysicalDevice();           // 物理デバイスの選択
	CreateDebugMessenger();         // デバッグ機能についての準備
	CreateLogicalDevice();          // 論理デバイスの作成

	// VkDevice 作成後に Volk 呼び出し
	volkLoadDevice(m_vkDevice);

	CreateCommandPool();            // コマンドプールの作成
	CreateDescriptorPool();         // ディスクリプタプールの作成
}

void VulkanContext::Cleanup()
{
	// デバイスがアイドル状態になってから破棄処理を進める
	vkDeviceWaitIdle(m_vkDevice);

	DestroyFrameContexts();
	vkDestroyCommandPool(
		m_vkDevice,
		m_commandPool,
		nullptr);

	if (m_debugMessenger != VK_NULL_HANDLE)
	{
		auto func = VK_GET_INSTANCE_PROC_ADDR(
			m_vkInstance,
			vkDestroyDebugUtilsMessengerEXT);

		if (func != nullptr)
		{
			func(
				m_vkInstance,
				m_debugMessenger,
				nullptr);
		}
		m_debugMessenger = VK_NULL_HANDLE;
	}

	if (m_swapchain)
	{
		m_swapchain->Cleanup();
		m_swapchain.reset();
	}

	if (m_surface != VK_NULL_HANDLE)
	{
		vkDestroySurfaceKHR(
			m_vkInstance,
			m_surface,
			nullptr);
		m_surface = VK_NULL_HANDLE;
	}

	vkDestroyDevice(m_vkDevice, nullptr);
	vkDestroyInstance(m_vkInstance, nullptr);
	m_vkDevice = VK_NULL_HANDLE;
	m_vkInstance = VK_NULL_HANDLE;

}

void VulkanContext::RecreateSwapchain()
{
	if (m_swapchain == nullptr)
	{
		m_swapchain = std::make_unique<Swapchain>();
	}

	if (m_surface == VK_NULL_HANDLE)
	{
		CreateSurface();
	}

	auto width = m_surfaceProvider->GetFramebufferWidth();
	auto height = m_surfaceProvider->GetFramebufferHeight();
	m_swapchain->Recreate(width, height);

	DestroyFrameContexts();
	CreateFrameContexts();
}

std::shared_ptr<CommandBuffer> VulkanContext::CreateCommandBuffer()
{
	VkCommandBufferAllocateInfo commandAI{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = m_commandPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};

	VkCommandBuffer commandBuffer{};
	vkAllocateCommandBuffers(
		m_vkDevice,
		&commandAI,
		&commandBuffer);

	return std::make_shared<CommandBuffer>(commandBuffer);
}

VkDescriptorSet VulkanContext::AllocateDescriptorSet(VkDescriptorSetLayout layout)
{
	VkDescriptorSetAllocateInfo allocInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = m_descriptorPool,
		.descriptorSetCount = 1,
		.pSetLayouts = &layout,
	};

	VkDescriptorSet descriptorSet;
	if (vkAllocateDescriptorSets(m_vkDevice, &allocInfo, &descriptorSet) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to allocate descriptor set!");
	}

	return descriptorSet;
}

void VulkanContext::FreeDescriptorSet(VkDescriptorSet descriptorSet)
{
	vkFreeDescriptorSets(m_vkDevice, m_descriptorPool, 1, &descriptorSet);
}

VkResult VulkanContext::AcquireNextImage()
{
	auto* frame = GetCurrentFrameContext();
	auto fence = frame->inflightFence;
	vkWaitForFences(
		m_vkDevice,
		1,
		&fence,
		VK_TRUE,
		UINT64_MAX);

	auto result = m_swapchain->AcquireNextImage();
	if (result == VK_SUCCESS)
	{
		vkResetFences(m_vkDevice, 1, &fence);
	}
	else if (result == VK_ERROR_OUT_OF_DATE_KHR)
	{
		// 最小化時の対策
		auto width = m_surfaceProvider->GetFramebufferWidth();
		auto height = m_surfaceProvider->GetFramebufferHeight();
		if (width > 0 && height > 0)
		{
			m_swapchain->Recreate(width, height);
		}
	}
	assert(result != VK_ERROR_DEVICE_LOST);
	return result;
}

void VulkanContext::SubmitPresent()
{
	auto& frame = m_frameContext[GetCurrentFrameIndex()];

	VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo submitInfo{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO
	};
	VkSemaphore renderCompleteSem = m_swapchain->GetRenderCompleteSemaphore();
	VkSemaphore presentCompleteSem = m_swapchain->GetPresentCompleteSemaphore();

	VkCommandBuffer commandBuffer = frame.commandBuffer->Get();
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;
	submitInfo.pWaitDstStageMask = &waitStageMask;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &presentCompleteSem;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &renderCompleteSem;
	auto result = vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, frame.inflightFence);
	assert(result != VK_ERROR_DEVICE_LOST);

	// GraphicsQueue が既にPresentをサポートしていることはチェック済み
	m_swapchain->QueuePresent(m_graphicsQueue);
	AdvanceFrame();
}

void VulkanContext::SubmitAndWait(std::shared_ptr<CommandBuffer> commandBuffer)
{
}

VulkanContext::FrameContext* VulkanContext::GetCurrentFrameContext()
{
	return &m_frameContext[m_currentFrameIndex];
}

uint32_t VulkanContext::FindMemoryType(const VkMemoryRequirements& requirements, VkMemoryPropertyFlags prorerties) const
{
	for (uint32_t i = 0; i < m_memoryProperties.memoryTypeCount; i++)
	{
		const bool isTypeCompatible = (requirements.memoryTypeBits & (1 << i)) != 0;
		const bool hasDesiredProperties = (m_memoryProperties.memoryTypes[i].propertyFlags & prorerties) == prorerties;

		if (isTypeCompatible && hasDesiredProperties)
		{	// メモリプロパティを満たし、memoryTypeBitsに含まれている
			return i;
		}
	}
	
	throw std::runtime_error("Failed to find suitable memory type");
}

uint32_t VulkanContext::MinUniformOffsetAlignment() const
{
	const auto& limits = m_physicalDeviceProperties.limits;
	return limits.minUniformBufferOffsetAlignment;
}

uint32_t VulkanContext::MinStorageBufferOffsetAlignment() const
{
	const auto& limits = m_physicalDeviceProperties.limits;
	return limits.minStorageBufferOffsetAlignment;
}

uint32_t VulkanContext::NonCoherentAtomSize() const
{
	const auto& limits = m_physicalDeviceProperties.limits;
	return limits.nonCoherentAtomSize;
}

void VulkanContext::SetDebugObjectName(void* objectHandle, VkObjectType type, const char* name)
{
#if DEBUG || _DEBUG
	if (m_pfnSetDebugUtilsObjectNameEXT)
	{
		VkDebugUtilsObjectNameInfoEXT nameInfo{
			.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
			.objectType = type,
			.objectHandle = reinterpret_cast<uint64_t>(objectHandle),
			.pObjectName = name,
		};
		m_pfnSetDebugUtilsObjectNameEXT(
			m_vkDevice,
			&nameInfo);
	}
#endif
}

void VulkanContext::CreateInstance(const char* appName)
{
	VkApplicationInfo appInfo{};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = appName;
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "VulkanBookEngine";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_3;

	std::vector<const char*> extensionList;
	std::vector<const char*> layerList;

#if DEBUG || _DEBUG
	// 開発時にはVK_EXT_debug_utilsを有効化
	extensionList.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

	// 開発時には検証レイヤーを有効化
	layerList.push_back("VK_LAYER_KHRONOS_validation");
#endif

	// GLFWから有効化する拡張機能名を回収
	GetWindowSystemExtensions(extensionList);

	VkInstanceCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;
	createInfo.enabledExtensionCount = uint32_t(extensionList.size());
	createInfo.ppEnabledExtensionNames = extensionList.data();
	createInfo.enabledLayerCount = uint32_t(layerList.size());
	createInfo.ppEnabledLayerNames = layerList.data();

	if (vkCreateInstance(&createInfo, nullptr, &m_vkInstance) != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to create instance");
	}
}

void VulkanContext::CreateSurface()
{
	m_surface = m_surfaceProvider->CreateSurface(m_vkInstance);

	// グラフィックスキューはこのサーフェイスへPresentを発行できるか
	VkBool32 present = false;
	vkGetPhysicalDeviceSurfaceSupportKHR(
		m_vkPhysicalDevice,
		m_graphicsQueueFamilyIndex,
		m_surface,
		&present);

	if (present == VK_FALSE)
	{
		throw std::runtime_error("not supported presentation");
	}
}

void VulkanContext::PickPhysicalDevice()
{
	uint32_t count = 0;
	vkEnumeratePhysicalDevices(
		m_vkInstance,
		&count,
		nullptr);
	std::vector<VkPhysicalDevice> devices(count);
	vkEnumeratePhysicalDevices(
		m_vkInstance,
		&count,
		devices.data());
	m_vkPhysicalDevice = devices[0];

	// 情報の取得
	vkGetPhysicalDeviceMemoryProperties(
		m_vkPhysicalDevice,
		&m_memoryProperties);
	vkGetPhysicalDeviceProperties(
		m_vkPhysicalDevice,
		&m_physicalDeviceProperties);

}

void VulkanContext::CreateLogicalDevice()
{
	// グラフィックスのキューインデックスを調査
	uint32_t queueCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(
		m_vkPhysicalDevice,
		&queueCount,
		nullptr);
	std::vector<VkQueueFamilyProperties> queues(queueCount);
	vkGetPhysicalDeviceQueueFamilyProperties(
		m_vkPhysicalDevice,
		&queueCount,
		queues.data());

	m_graphicsQueueFamilyIndex = 0;
	for (uint32_t i = 0; const auto& props : queues)
	{
		if (props.queueFlags & VK_QUEUE_GRAPHICS_BIT)
		{
			m_graphicsQueueFamilyIndex = i;
			break;
		}
		++i;
	}

	// 拡張機能の設定
	BuildVkFeatures();
	std::vector<const char*> deviceExtensions = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
	};
	// 上下方向を合わせるために有効とする
	deviceExtensions.push_back(VK_KHR_MAINTENANCE1_EXTENSION_NAME);

	// レイトレーシングパイプライン用有効化の宣言
	deviceExtensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
	deviceExtensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
	deviceExtensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);


	float priority = 1.0f;
	VkDeviceQueueCreateInfo queueInfo{};
	queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueInfo.queueFamilyIndex = m_graphicsQueueFamilyIndex;
	queueInfo.queueCount = 1;
	queueInfo.pQueuePriorities = &priority;

	VkDeviceCreateInfo deviceInfo{};
	deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceInfo.queueCreateInfoCount = 1;
	deviceInfo.pQueueCreateInfos = &queueInfo;
	deviceInfo.enabledExtensionCount = uint32_t(deviceExtensions.size());
	deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();

	deviceInfo.pNext = &m_physDevFeatures;
	deviceInfo.pEnabledFeatures = nullptr;

	auto result = vkCreateDevice(
		m_vkPhysicalDevice,
		&deviceInfo,
		nullptr,
		&m_vkDevice);

	if (result != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to create logical device");
	}

	vkGetDeviceQueue(
		m_vkDevice,
		m_graphicsQueueFamilyIndex,
		0,
		&m_graphicsQueue);
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT severity,
	VkDebugUtilsMessageTypeFlagsEXT type,
	const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void* pUserData)
{
	std::stringstream ss;
	ss << "[Validation Layer]" << pCallbackData->pMessage << std::endl;

#if defined(WIN32)
	OutputDebugStringA(ss.str().c_str());
#else
	std::cerr << ss.str() << std::endl;
#endif

	return VK_FALSE;
}

void VulkanContext::CreateDebugMessenger()
{
	VkDebugUtilsMessengerCreateInfoEXT createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	createInfo.messageType =
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	createInfo.messageType =
		VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	createInfo.pfnUserCallback = VulkanDebugCallback;

	auto vkCreateDebugUtilMessenger = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
		m_vkInstance,
		"vkCreateDebugUtilsMessengerEXT");

	if (vkCreateDebugUtilMessenger &&
		vkCreateDebugUtilMessenger(m_vkInstance, &createInfo, nullptr, &m_debugMessenger) != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to set up debug Messenger!");
	}

	m_pfnSetDebugUtilsObjectNameEXT = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetInstanceProcAddr(
		m_vkInstance,
		"vkSetDebugUtilsObjectNameEXT");
}

void VulkanContext::CreateCommandPool()
{
	VkCommandPoolCreateInfo commandPoolCI{
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
	};
	commandPoolCI.queueFamilyIndex = m_graphicsQueueFamilyIndex;
	commandPoolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	vkCreateCommandPool(
		m_vkDevice,
		&commandPoolCI,
		nullptr,
		&m_commandPool);
}

void VulkanContext::CreateDescriptorPool()
{
	std::vector<VkDescriptorPoolSize> poolSizes = {
	{
		.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 4096
	},
	};

	VkDescriptorPoolCreateInfo poolInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
		.maxSets = 4096,
		.poolSizeCount = uint32_t(poolSizes.size()),
		.pPoolSizes = poolSizes.data(),
	};

	if (vkCreateDescriptorPool(m_vkDevice, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create descriptor pool!");
	}
}

void VulkanContext::CreateFrameContexts()
{
	m_frameContext.resize(MaxInflightFrames);
	for (auto& frame : m_frameContext)
	{
		frame.commandBuffer = CreateCommandBuffer();
		VkFenceCreateInfo fenceCI{
			.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
			.flags = VK_FENCE_CREATE_SIGNALED_BIT,
		};

		vkCreateFence(
			m_vkDevice,
			&fenceCI,
			nullptr,
			&frame.inflightFence);
	}
}

void VulkanContext::DestroyFrameContexts()
{
	for (auto& frame : m_frameContext)
	{
		vkDestroyFence(
			m_vkDevice,
			frame.inflightFence,
			nullptr);
	}
	m_frameContext.clear();
}

void VulkanContext::AdvanceFrame()
{
	m_currentFrameIndex = (m_currentFrameIndex + 1) % MaxInflightFrames;
}


// Vulkanの構造体pNextをつなぐ処理簡略化のためのテンプレート
template<typename T>
void BuildVkExtensionChain(T& last)
{
	last.pNext = nullptr;
}

template<typename T, typename U, typename... Rest>
void BuildVkExtensionChain(T& current, U& next, Rest&... rest)
{
	current.pNext = &next;
	BuildVkExtensionChain(next, rest...);
}

void VulkanContext::BuildVkFeatures()
{
	// デバイスからサポート範囲の情報を取得した後で、使いたいものを有効化する
	// ここでサポートされない機能を有効にすると、デバイス作成時にエラーとなる
	BuildVkExtensionChain(
		m_physDevFeatures,
		m_vulkan11Features,
		m_vulkan12Features,
		m_vulkan13Features);

	// サポート情報を取得
	vkGetPhysicalDeviceFeatures2(
		m_vkPhysicalDevice,
		&m_physDevFeatures);

	// 機能を有効化
	m_vulkan13Features.dynamicRendering = VK_TRUE;
	m_vulkan13Features.synchronization2 = VK_TRUE;

	// レイトレーシングパイプライン有効
	m_raytracingPipelineFeatures.rayTracingPipeline = VK_TRUE;
	m_accelerationStructureFeatures.accelerationStructure = VK_TRUE;

	m_vulkan12Features.bufferDeviceAddress = VK_TRUE;
	m_vulkan12Features.descriptorIndexing = VK_TRUE;
	m_vulkan12Features.descriptorBindingVariableDescriptorCount = VK_TRUE;

	// レイクエリー有効
	m_rayqueryFeatures.rayQuery = VK_TRUE;
	m_accelerationStructureFeatures.accelerationStructure = VK_TRUE;
}
