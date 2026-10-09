
#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace cge {

struct MeshData {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec3> colors;       
    std::vector<uint32_t>  indices;
};

} // namespace cge