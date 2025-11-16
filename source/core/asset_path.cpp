#include "asset_path.h"
#include <array>
#include <string_view>

namespace {
	std::filesystem::path g_assetRoot = "assets/";

	std::string_view ToSubDirectoryName(AssetType type)
	{
		constexpr std::array<std::string_view, int(AssetType::AssetTypeMax)> kAssetDirs = {
			"shaders",
			"textures",
			"models",
		};
		return kAssetDirs[int(type)];
	}
}

void SetAssetRootPath(const std::filesystem::path& path)
{
	auto fullPath = std::filesystem::absolute(path);
	g_assetRoot = std::filesystem::canonical(fullPath);
}

std::filesystem::path GetAssetRootPath()
{
	return g_assetRoot;
}

std::filesystem::path GetAssetPath(AssetType type, const std::filesystem::path& filename)
{
	return GetAssetRootPath() / ToSubDirectoryName(type) / filename;
}
