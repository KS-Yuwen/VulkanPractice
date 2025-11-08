#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <functional>
#include <stdint.h>
#include <cstring>

#include "command_buffer.h"

class Swapchain;
class CommandBuffer;
class ISurfaceProvider;

class VulkanContext
{
public:
	static constexpr uint32_t MaxInflightFrames = 2;
	static VulkanContext& Get();

	// 初期化
	void Initialize(const char* appName, ISurfaceProvider* surfaceProvider);

	// 終了処理
	void Cleanup();

	// スワップチェインの作成
	void RecreateSwapchain();

	// 各種Vulkanオブジェクトの取得
	VkInstance GetVkInstance() const { return m_vkInstance; }
	VkDevice GetVkDevice() const { return m_vkDevice; }
	VkPhysicalDevice GetVkPhysicalDevice() const { return m_vkPhysicalDevice; }
	VkDescriptorPool GetVkDescriptorPool() const { return m_descriptorPool; }

	VkQueue GetGraphicsQueue() const { return m_graphicsQueue; }
	uint32_t GetGraphicsFamily() const { return m_graphicsQueueFamilyIndex; }
	uint32_t GetPresentFamily() const { return m_presentQueueFamilyIndex; }

	VkCommandPool GetCommandPool() const { return m_commandPool; }
	VkSurfaceKHR GetSurface() const { return m_surface; }

	// コマンドバッファの作成
	std::shared_ptr<CommandBuffer> CreateCommandBuffer();

	// ディスクリプタセットの確保
	VkDescriptorSet AllocateDescriptorSet(VkDescriptorSetLayout layout);
	// ディスクリプタセットの解放
	void FreeDescriptorSet(VkDescriptorSet descriptorSet);

	// 描画フレーム単位で取り扱うコンテキスト情報
	struct FrameContext
	{
		std::shared_ptr<CommandBuffer> commandBuffer;
		VkFence inflightFence = VK_NULL_HANDLE;
	};

	// 現在のフレームインデックスを取得
	uint32_t GetCurrentFrameIndex() const { return m_currentFrameIndex; }
	// 描画可能なスワップチェインイメージの切り替え
	VkResult AcquireNextImage();

	// 現在のフレームコンテキストのコマンドを実行し、プレゼンテーションを発行
	void SubmitPresent();

	// 指定されたコマンドバッファを実行し、完了を待機
	void SubmitAndWait(std::shared_ptr<CommandBuffer> commandBuffer);

	// 現在のフレームコンテキストの取得
	FrameContext* GetCurrentFrameContext();

	// スワップチェインの取得
	std::unique_ptr<Swapchain>& GetSwapchain() { return m_swapchain; }

	// メモリタイプの取得
	uint32_t FindMemoryType(const VkMemoryRequirements& requirements, VkMemoryPropertyFlags prorerties);

	// Function Callback(s)
	std::function<void(std::vector<const char*>&)> GetWindowSystemExtensions;

	// オブジェクトに名前を設定する
	void SetDebugObjectName(void* objectHandle, VkObjectType type, const char* name);

private:
	VulkanContext() = default;
	~VulkanContext() = default;

private:
	void CreateInstance(const char* appName);
	void CreateSurface();
	void PickPhysicalDevice();
	void CreateLogicalDevice();
	void CreateDebugMessenger();
	void CreateCommandPool();
	void CreateDescriptorPool();
	void CreateFrameContexts();
	void DestroyFrameContexts();

	void AdvanceFrame();
	void BuildVkFeatures();

	ISurfaceProvider* m_surfaceProvider{};
	VkInstance m_vkInstance{};

	VkPhysicalDevice m_vkPhysicalDevice{};
	VkDevice m_vkDevice{};
	VkQueue  m_graphicsQueue{};
	uint32_t m_graphicsQueueFamilyIndex{};
	uint32_t m_presentQueueFamilyIndex{};
	VkPhysicalDeviceMemoryProperties m_memoryProperties{};
	VkPhysicalDeviceProperties m_physicalDeviceProperties{};

	VkSurfaceKHR m_surface{};
	VkCommandPool m_commandPool{};
	VkDescriptorPool m_descriptorPool{};
	std::vector<FrameContext> m_frameContext;
	std::unique_ptr<Swapchain> m_swapchain;

	VkDebugUtilsMessengerEXT m_debugMessenger{};
	PFN_vkSetDebugUtilsObjectNameEXT m_pfnSetDebugUtilsObjectNameEXT{};

	uint32_t m_currentFrameIndex = 0;

	// -------------------------------
	VkPhysicalDeviceFeatures2 m_physDevFeatures{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2
	};
	VkPhysicalDeviceVulkan11Features m_vulkan11Features{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES
	};
	VkPhysicalDeviceVulkan12Features m_vulkan12Features{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES
	};
	VkPhysicalDeviceVulkan13Features m_vulkan13Features{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES
	};
	VkPhysicalDeviceShaderAtomicFloatFeaturesEXT m_atomicFloatFeatures
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT
	};
};
