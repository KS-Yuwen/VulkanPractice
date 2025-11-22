#pragma once
#include "vulkan_context.h"

struct ImageLayoutTransition
{
	VkImageLayout oldLayout;
	VkImageLayout newLayout;
	VkAccessFlags srcAccessMask;
	VkAccessFlags dstAccessMask;
	VkPipelineStageFlags srcStage;
	VkPipelineStageFlags dstStage;

	// Undefined状態から描画先としてのレイアウトへ
	static ImageLayoutTransition FromUndefinedToColorAttachment();

	// PresentSrc状態から描画先としてのレイアウトへ
	static ImageLayoutTransition FromPresentSrcToColorAttachment();

	// 描画先からPresentSrcの状態レイアウトへ
	static ImageLayoutTransition FromColorToPresent();

	// 初期状態から転送先レイアウトへ
	static ImageLayoutTransition FromUndefToTransferDst();

	// 転送先レイアウトから転送元レイアウトへ
	static ImageLayoutTransition FromTransferDstToTransferSrc();
};