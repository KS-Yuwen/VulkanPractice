#pragma once
#include <filesystem>

// アセットのルートパスを設定します
void SetAssetRootPath(const std::filesystem::path& path);

// 現在のアセットルートパスを取得します。
std::filesystem::path GetAssetRootPath();

enum class AssetType
{
	Shader = 0,
	Texture,
	Model,
	AssetTypeMax,
};

// アセット種別指定ありのファイルパスを取得します。
std::filesystem::path GetAssetPath(AssetType type, const std::filesystem::path& filename);