#pragma once
#include "core/sample_app.h"
#include <string>
#include <vector>
#include <array>
#include <stdexcept>

// GLMで設定する値単位をラジアンに
// #define GLM_FORCE_RADIANS
// Vulkan ではClip空間 Z:[0,1]のため定義が必要
// #define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include <glm.hpp>
#include <ext.hpp>

#include "core/vulkan_context.h"
#include "core/swapchain.h"
#include "core/asset_path.h"
#include "core/shader_loader.h"
#include "core/resource_uploader.h"
#include "core/buffer_resource.h"
#include "core/image_resource.h"

class SimpleCubeApp : public ISampleApp
{
public:
    virtual void OnInitialize() override;
    virtual void OnDrawFrame() override;
    virtual void OnCleanup() override;

    struct Vertex
    {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec3 color;
    };
    struct SceneConstants
    {
        glm::mat4 mtxWorld;
        glm::mat4 mtxView;
        glm::mat4 mtxProj;
        glm::vec4 lightDir;
        glm::vec4 eyePosition;
    };
private:
    void CreateCubeGeometry();
    void CreateSphereGeometry();
    void CreateDescriptorSetLayout();
    void CreateUniformBuffers();
    void CreateDescriptorSets();
    void CreateDepthBuffer();
    void CreateGraphicsPipeline();

    ResourceUploader m_resourceUploader{};

    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

    struct {
        std::shared_ptr<VertexBuffer> vertexBuffer;
        std::shared_ptr<IndexBuffer>  indexBuffer;

        uint32_t indexCount;
    } m_cube{};
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;

    std::array<std::shared_ptr<UniformBuffer>, 2> m_uniformBuffers;
    std::array<VkDescriptorSet, 2> m_descriptorSets;

    std::shared_ptr<DepthBuffer> m_depthBuffer;
};
