#include <Windows.h>
#define GLFW_INCULDE_VULKAN
#include <GLFW/glfw3.h>
#include <iostream>
#include <filesystem>
#include <array>

#include "core/vulkan_context.h"
#include "core/glfw_surface_provider.h"
#include "core/asset_path.h"

#include "glm.hpp"

#include "triangle_app.h"
#include "simplecube_app.h"

namespace fs = std::filesystem;

void triangle(GLFWwindow* window)
{
	// アプリケーションの初期化
	TriangleApp theApp{};
	theApp.OnInitialize();

	// メッセージループ処理
	while (glfwWindowShouldClose(window) == GLFW_FALSE)
	{
		glfwPollEvents();

		// 描画処理
		theApp.OnDrawFrame();
	}

	// 終了処理
	theApp.OnCleanup();
}

void simpleCube(GLFWwindow* window)
{
	SimpleCubeApp theApp{};
	theApp.OnInitialize();

	// メッセージループ処理
	while (glfwWindowShouldClose(window) == GLFW_FALSE)
	{
		glfwPollEvents();

		// 描画処理
		theApp.OnDrawFrame();
	}

	// 終了処理
	theApp.OnCleanup();
}

int APIENTRY WinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPSTR lpCmdLine,
	_In_ int nCmdShow)
{
	wchar_t exePath[MAX_PATH];
	GetModuleFileNameW(
		nullptr,
		exePath,
		MAX_PATH);
	fs::path exeDir = fs::path(exePath).parent_path();
	SetCurrentDirectory(exeDir.c_str());

	fs::path assetDir = exeDir / "../../assets";
	SetAssetRootPath(assetDir);

	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	// 初期化処理
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

	// ウィンドウの作成
	auto window = glfwCreateWindow(
		1280,
		720,
		"Vulkan Practice",
		nullptr,
		nullptr);
	GLFWSurfaceProvider surfaceProvider(window);

	// Vulkanの初期化
	auto& vulkanCtx = VulkanContext::Get();
	vulkanCtx.GetWindowSystemExtensions = [=](auto& extensionList) {
		uint32_t extCount = 0;
		const char** extensions = glfwGetRequiredInstanceExtensions(&extCount);
		if (extCount > 0)
		{
			extensionList.insert(extensionList.end(), extensions, extensions + extCount);
		}
	};
	vulkanCtx.Initialize("Vulkan Practice", &surfaceProvider);
	vulkanCtx.RecreateSwapchain();

	//triangle(window);
	simpleCube(window);

	// 終了処理
	vulkanCtx.Cleanup();

	glfwDestroyWindow(window);
	glfwTerminate();

	return 0;
}