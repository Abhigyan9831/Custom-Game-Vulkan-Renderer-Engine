
#pragma once
#include "engine/assets/MeshData.h"
#include <string>
#include <vector>

namespace cge{
class AssetManager {
public:
    
    [[nodiscard]] std::vector<MeshData> loadGlb(const std::string& path) const;
};

}