#pragma once
#include <memory>
#include <glm.hpp>

#include "../../core/vulkan_context.h"
#include "../../core/buffer_resource.h"
#include "../../core/resource_uploader.h"

#include "model_resource_ms.h"
#include <string>

namespace Render::ms
{
    class ModelResource;

    // 描画用インスタンスクラス
    class DrawObject {
    public:
        DrawObject() = default;
        ~DrawObject() = default;

        void Initialize(std::shared_ptr<ModelResource>& modelResource, ResourceUploader& uploader, VkDescriptorSetLayout dsMaterialLayout, glm::mat4 mtxWorld);
        void Cleanup();

        struct Material {
            glm::vec4 baseColor;
            float metallicFactor;
            float roughnessFactor;
            uint32_t  alphaMode;
            uint32_t  _padd0;

            uint32_t  baseTexture;
            uint32_t  metallicRoughnessTexture;
            uint32_t  normalTexture;
            float     alphaCutoff;
        };

        std::shared_ptr<ModelResource> GetModel() const { return m_model; }
        struct Node
        {
            std::string name;
            glm::mat4 mtxLocal;
            glm::mat4 mtxWorld;
            int parent = -1;
            std::vector<int> children;
            std::vector<int> meshes;

            Node(const Render::ms::ModelResource::Node& src)
                : name(src.name), mtxLocal(src.mtxLocal), mtxWorld(1.0f), parent(src.parent), children(src.children), meshes(src.meshes)
            {
            }
        };


        const std::vector<Node>& GetNodes() const { return m_nodes; }
        const std::vector<Material>& GetMaterials() const { return m_materials; }

        void UpdateWorldMatrices(const glm::mat4& mtxTransform);

        VkDescriptorSet GetMaterialDescriptorSet() const { return m_materialDescriptorSet; }
    private:

        std::shared_ptr<ModelResource> m_model;
        std::vector<Material> m_materials;
        VkDescriptorSet m_materialDescriptorSet;
        std::shared_ptr<StorageBuffer> m_materialBuffer;

        glm::mat4 m_baseWorldMatrix = glm::mat4(1.0f);
        std::vector<Node> m_nodes;
    };
}
