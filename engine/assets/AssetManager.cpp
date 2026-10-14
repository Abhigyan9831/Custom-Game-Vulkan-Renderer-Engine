#include "engine/assets/AssetManager.h"
#include "engine/core/Logger.h"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#define TINYGLTF_NO_EXTERNAL_IMAGE
#include <tiny_gltf.h>

#include <stdexcept>

namespace cge {

namespace {

int imageIndexOf(const tinygltf::Model& model, int textureIndex)
{
    if (textureIndex < 0 || textureIndex >= static_cast<int>(model.textures.size())) {
        return -1;
    }
    const int img = model.textures[textureIndex].source;
    if (img < 0 || img >= static_cast<int>(model.images.size())) {
        return -1;
    }
    return img;
}

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
        const glm::mat4 meshWorld = (node.skin >= 0) ? glm::mat4(1.0f) : world;
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
                data.positions[i] = glm::vec3(meshWorld * glm::vec4(local, 1.0f));
            }

            if (prim.attributes.count("NORMAL")) {
                const auto& nAcc = model.accessors[prim.attributes.at("NORMAL")];
                const auto& nView = model.bufferViews[nAcc.bufferView];
                const float* nData = reinterpret_cast<const float*>(
                    &model.buffers[nView.buffer]
                     .data[nView.byteOffset + nAcc.byteOffset]);
                const glm::mat3 normalMat(meshWorld);
                data.normals.resize(nAcc.count);
                for (size_t i = 0; i < nAcc.count; ++i) {
                    const glm::vec3 n(nData[i * 3], nData[i * 3 + 1], nData[i * 3 + 2]);
                    data.normals[i] = glm::normalize(normalMat * n);
                }
            } else {
                data.normals.resize(posAcc.count, glm::vec3(0.0f, 1.0f, 0.0f));
            }

            glm::vec2 uvScale(1.0f);
            glm::vec2 uvOffset(0.0f);
            if (prim.material >= 0) {
                const auto& mat = model.materials[prim.material];
                const auto& baseTex = mat.pbrMetallicRoughness.baseColorTexture;
                auto it = baseTex.extensions.find("KHR_texture_transform");
                if (it != baseTex.extensions.end() && it->second.IsObject()) {
                    const auto& ext = it->second;
                    if (ext.Has("scale") && ext.Get("scale").ArrayLen() == 2) {
                        uvScale.x = static_cast<float>(ext.Get("scale").Get(0).GetNumberAsDouble());
                        uvScale.y = static_cast<float>(ext.Get("scale").Get(1).GetNumberAsDouble());
                    }
                    if (ext.Has("offset") && ext.Get("offset").ArrayLen() == 2) {
                        uvOffset.x = static_cast<float>(ext.Get("offset").Get(0).GetNumberAsDouble());
                        uvOffset.y = static_cast<float>(ext.Get("offset").Get(1).GetNumberAsDouble());
                    }
                }
            }

            if (prim.attributes.count("TEXCOORD_0")) {
                const auto& uvAcc = model.accessors[prim.attributes.at("TEXCOORD_0")];
                const auto& uvView = model.bufferViews[uvAcc.bufferView];
                const float* uvData = reinterpret_cast<const float*>(
                    &model.buffers[uvView.buffer]
                     .data[uvView.byteOffset + uvAcc.byteOffset]);
                data.uvs.resize(uvAcc.count);
                for (size_t i = 0; i < uvAcc.count; ++i) {
                    data.uvs[i] = glm::vec2(uvData[i * 2] * uvScale.x + uvOffset.x,
                                            uvData[i * 2 + 1] * uvScale.y + uvOffset.y);
                }
            } else {
                data.uvs.resize(posAcc.count, glm::vec2(0.0f));
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
                data.colors.resize(posAcc.count, glm::vec3(1.0f));
            }

            // Apply glTF material baseColorFactor (spec-correct tint, replaces hardcoded guesses)
            if (prim.material >= 0) {
                const auto& mat = model.materials[prim.material];
                const auto& bcf = mat.pbrMetallicRoughness.baseColorFactor; // [R,G,B,A]
                if (bcf.size() == 4) {
                    const glm::vec3 factor(
                        static_cast<float>(bcf[0]),
                        static_cast<float>(bcf[1]),
                        static_cast<float>(bcf[2]));
                    // Only apply if not pure white (optimization: skip no-op)
                    if (glm::length(factor - glm::vec3(1.0f)) > 0.001f) {
                        for (auto& col : data.colors) {
                            col *= factor;
                        }
                    }
                }
            }

            data.baseColorIndex = -1;
            data.metallicRoughnessIndex = -1;
            data.normalIndex = -1;
            if (prim.material >= 0) {
                const auto& mat = model.materials[prim.material];
                const auto& pbr = mat.pbrMetallicRoughness;
                data.baseColorIndex = imageIndexOf(model, pbr.baseColorTexture.index);
                data.metallicRoughnessIndex = imageIndexOf(model, pbr.metallicRoughnessTexture.index);
                if (mat.normalTexture.index >= 0) {
                    data.normalIndex = imageIndexOf(model, mat.normalTexture.index);
                }
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
                         " verts, " + std::to_string(data.indices.size()) + " indices" +
                         (data.baseColorIndex >= 0 ? " (textured)" : " (no texture)"));
            out.push_back(std::move(data));
        }
    }

    for (int child : node.children) {
        walkNodes(model, model.nodes[child], world, out);
    }
}

} // anonymous namespace

ModelData AssetManager::loadGlb(const std::string& path) const
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
                 std::to_string(model.images.size()) + " images, " +
                 std::to_string(model.animations.size()) + " animations (used in Phase 22)");

    ModelData result;

    for (const auto& image : model.images) {
        TextureData tex;
        tex.width  = image.width;
        tex.height = image.height;
        tex.pixels = image.image;
        result.textures.push_back(std::move(tex));
    }

    int barkIdx = -1, seamIdx = -1;
    for (size_t i = 0; i < model.images.size(); ++i) {
        if (model.images[i].name == "Bark_01_4K_Base_Color") barkIdx = static_cast<int>(i);
        if (model.images[i].name == "sakuratree_trunk_seamremove") seamIdx = static_cast<int>(i);
    }
    if (barkIdx >= 0 && seamIdx >= 0 && barkIdx < static_cast<int>(result.textures.size()) &&
        seamIdx < static_cast<int>(result.textures.size())) {
        auto& seam = result.textures[seamIdx];
        const auto& bark = result.textures[barkIdx];
        if (seam.width == bark.width && seam.height == bark.height &&
            seam.pixels.size() == bark.pixels.size() &&
            seam.pixels.size() >= static_cast<size_t>(seam.width * seam.height * 4)) {
            for (size_t p = 0; p < seam.pixels.size(); p += 4) {
                const float a = static_cast<float>(seam.pixels[p + 3]) / 255.0f;
                seam.pixels[p + 0] = static_cast<unsigned char>(seam.pixels[p + 0] * a + bark.pixels[p + 0] * (1.0f - a));
                seam.pixels[p + 1] = static_cast<unsigned char>(seam.pixels[p + 1] * a + bark.pixels[p + 1] * (1.0f - a));
                seam.pixels[p + 2] = static_cast<unsigned char>(seam.pixels[p + 2] * a + bark.pixels[p + 2] * (1.0f - a));
                seam.pixels[p + 3] = 255;
            }
            CGE_LOG_INFO("Composited trunk seam overlay over base bark texture");
        }
    }

    if (!result.textures.empty()) {
        CGE_LOG_INFO("Loaded " + std::to_string(result.textures.size()) + " textures");
    } else {
        CGE_LOG_WARN("No textures in GLB — white fallback will be used");
    }

    const tinygltf::Scene& scene = model.scenes[model.defaultScene];
    for (int nodeIndex : scene.nodes) {
        walkNodes(model, model.nodes[nodeIndex], glm::mat4(1.0f), result.meshes);
    }
    return result;
}

} // namespace cge