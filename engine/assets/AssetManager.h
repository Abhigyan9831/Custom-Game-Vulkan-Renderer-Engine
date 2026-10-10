#pragma once
#include "engine/assets/MeshData.h"

#include <string>

namespace cge {

class AssetManager {
public:
    [[nodiscard]] ModelData loadGlb(const std::string& path) const;
};

} // namespace cge