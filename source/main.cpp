#include <Windows.h>
#define GLFW_INCULDE_VULKAN
#include <GLFW/glfw3.h>
#include "core/vulkan_context.h"
#include "triangle_app.h"
#include "core/glfw_surface_provider.h"

int APIENTRY WinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPSTR lpCmdLine,
	_In_ int nCmdShow)
{
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
		"Triangle",
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
	vulkanCtx.Initialize("Triangle", &surfaceProvider);
	vulkanCtx.RecreateSwapchain();

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
	vulkanCtx.Cleanup();

	glfwDestroyWindow(window);
	glfwTerminate();

	return 0;
}