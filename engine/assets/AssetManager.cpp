
#include "engine/assets/AssetManager.h"
#include "engine/core/Logger.h"

#include <glm/gtc/type_ptr.hpp>       
#include <glm/gtc/matrix_transform.hpp> 
#include <glm/gtc/quaternion.hpp>     
#define TINYGLTF_NO_EXTERNAL_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <tiny_gltf.h>

#include <stdexcept>

namespace cge {

namespace {

glm::mat4 nodeMatrix(const tinygltf::Node& node)
{
    
    glm::mat4 m(1.0f);
    if (!node.matrix.empty()) {
        m = glm::make_mat4(node.matrix.data());
        return m;
    }
    
    glm::vec3 t(0.0f), s(1.0f);
    glm::quat r(1.0f, 0.0f, 0.0f, 0.0f);
    if (node.translation.size() == 3) t = glm::make_vec3(node.translation.data());
    if (node.scale.size() == 3)       s = glm::make_vec3(node.scale.data());
    if (node.rotation.size() == 4)    r = glm::make_quat(node.rotation.data());

    const glm::mat4 tm = glm::translate(glm::mat4(1.0f), t);
    const glm::mat4 rm = glm::mat4_cast(r);          
    const glm::mat4 sm = glm::scale(glm::mat4(1.0f), s);
    return tm * rm * sm;
}


void walkNodes(const tinygltf::Model& model, const tinygltf::Node& node,
               const glm::mat4& parentMatrix, std::vector<MeshData>& out)
{
    const glm::mat4 world = parentMatrix * nodeMatrix(node);

    if (node.mesh >= 0) {
        const tinygltf::Mesh& mesh = model.meshes[node.mesh];
        for (const tinygltf::Primitive& prim : mesh.primitives) {
            MeshData data;

            
            const auto& posAcc = model.accessors[prim.attributes.at("POSITION")];
            const auto& posView = model.bufferViews[posAcc.bufferView];
            const float* posData = reinterpret_cast<const float*>(
                &model.buffers[posView.buffer]
                 .data[posView.byteOffset + posAcc.byteOffset]);
            data.positions.resize(posAcc.count);
            for (size_t i = 0; i < posAcc.count; ++i) {
                const glm::vec3 local(posData[i * 3], posData[i * 3 + 1], posData[i * 3 + 2]);
                data.positions[i] = glm::vec3(world * glm::vec4(local, 1.0f));  // bake!
            }

            
            if (prim.attributes.count("NORMAL")) {
                const auto& nAcc = model.accessors[prim.attributes.at("NORMAL")];
                const auto& nView = model.bufferViews[nAcc.bufferView];
                const float* nData = reinterpret_cast<const float*>(
                    &model.buffers[nView.buffer]
                     .data[nView.byteOffset + nAcc.byteOffset]);
                const glm::mat3 normalMat(world);   
                data.normals.resize(nAcc.count);
                for (size_t i = 0; i < nAcc.count; ++i) {
                    const glm::vec3 n(nData[i * 3], nData[i * 3 + 1], nData[i * 3 + 2]);
                    data.normals[i] = glm::normalize(normalMat * n);
                }
            } else {
                data.normals.resize(posAcc.count, glm::vec3(0.0f, 1.0f, 0.0f));
            }

            
            if (prim.attributes.count("COLOR_0")) {
                const auto& cAcc = model.accessors[prim.attributes.at("COLOR_0")];
                const auto& cView = model.bufferViews[cAcc.bufferView];
                const float* cData = reinterpret_cast<const float*>(
                    &model.buffers[cView.buffer]
                     .data[cView.byteOffset + cAcc.byteOffset]);
                data.colors.resize(cAcc.count);
                for (size_t i = 0; i < cAcc.count; ++i) {
                    data.colors[i] = glm::vec3(cData[i * 3], cData[i * 3 + 1], cData[i * 3 + 2]);
                }
            } else {
                data.colors.resize(posAcc.count, glm::vec3(0.8f));  
            }

            
            const auto& idxAcc = model.accessors[prim.indices];
            const auto& idxView = model.bufferViews[idxAcc.bufferView];
            const auto& buffer  = model.buffers[idxView.buffer];
            const auto* idxBytes = buffer.data.data() + idxView.byteOffset + idxAcc.byteOffset;
            data.indices.resize(idxAcc.count);
            for (size_t i = 0; i < idxAcc.count; ++i) {
                switch (idxAcc.componentType) {
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                        data.indices[i] = reinterpret_cast<const uint32_t*>(idxBytes)[i]; break;
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                        data.indices[i] = reinterpret_cast<const uint16_t*>(idxBytes)[i]; break;
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                        data.indices[i] = idxBytes[i]; break;
                    default:
                        throw std::runtime_error("Unsupported index component type");
                }
            }

            CGE_LOG_INFO("Loaded mesh primitive: " + std::to_string(data.positions.size()) +
                         " verts, " + std::to_string(data.indices.size()) + " indices");
            out.push_back(std::move(data));
        }
    }

    for (int child : node.children) {
        walkNodes(model, model.nodes[child], world, out);
    }
}

} 

std::vector<MeshData> AssetManager::loadGlb(const std::string& path) const
{
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    
    if (!loader.LoadBinaryFromFile(&model, &err, &warn, path)) {
        throw std::runtime_error("Failed to load GLB '" + path + "': " + err);
    }
    if (!warn.empty()) {
        CGE_LOG_WARN("GLB warning: " + warn);
    }
    CGE_LOG_INFO("GLB parsed: " + std::to_string(model.meshes.size()) + " meshes, " +
                 std::to_string(model.nodes.size()) + " nodes, " +
                 std::to_string(model.animations.size()) + " animations (used in Phase 22)");

    std::vector<MeshData> result;
    const tinygltf::Scene& scene = model.scenes[model.defaultScene];
    for (int nodeIndex : scene.nodes) {
        walkNodes(model, model.nodes[nodeIndex], glm::mat4(1.0f), result);
    }
    return result;
}

} // namespace cge