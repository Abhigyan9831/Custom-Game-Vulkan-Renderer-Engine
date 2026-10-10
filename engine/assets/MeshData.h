

#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace cge{

struct TextureData{
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;

};
struct MeshData {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> uvs;
    std::vector<glm::vec3> colors;
    std::vector<uint32_t>  indices;
    int textureIndex = -1;    
};

struct ModelData {
    std::vector<MeshData> meshes;
    std::vector<TextureData> textures;
};


}