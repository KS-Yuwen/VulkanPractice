#pragma once

class IImageResource;

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

	static ImageLayoutTransition ToShaderReadonlyOptimal(const IImageResource* image);

	// ストレージイメージとしてシェーダーから読み書きできるように、
	// 画像のレイアウトとアクセスマスクをVK_IMAGE_LAYOUT_GENERAL に遷移する
	// Compute Shader および Frangment Shader からの読み書きを想定しており、
	// それらのステージへの依存関係を確保する
	static ImageLayoutTransition ToStorageImageGeneralLayout(const IImageResource* image);
};