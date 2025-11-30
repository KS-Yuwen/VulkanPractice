#pragma once
#include "model_data.h"
#include <filesystem>

struct ModelData;
namespace loader
{
	void LoadModelDataFromFile(ModelData* modelData, const std::filesystem::path& filePath);
}