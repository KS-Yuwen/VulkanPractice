#pragma once
#include "core/sample_app.h"
#include <string>
#include <vector>
#include <array>
#include <stdexcept>

#include "glm.hpp"
#include "ext.hpp"

#include "core/vulkan_context.h"
#include "core/swapchain.h"
#include "core/resource_uploader.h"
#include "core/buffer_resource.h"
#include "core/image_resource.h"
#include "core/sampler.h"
#include "core/acceleration_structure.h"

#include "Render/rt/model_resource_rt.h"
#include "Render/rt/draw_object_rt.h"

class ClassicRaytraceApp : ISampleApp
{
public:
    virtual void OnInitialize() override;
    virtual void OnDrawFrame() override;
    virtual void OnCleanup() override;

    struct SceneConstants
    {
        glm::mat4 mtxView;
        glm::mat4 mtxProj;
        glm::mat4 mtxViewInv;
        glm::mat4 mtxProjInv;
        glm::vec4 lightDir;
        glm::vec4 eyePosition;
    };

    struct Vertex
    {
        glm::vec3 position;
    };

    enum class ShaderGroup : uint32_t {
        RayGen = 0,
        MissHit,
        MissHitShadow,
        ClosestHit,
        GroupMax,
    };

    struct ShaderRecordData
    {
        uint64_t indexBuffer;
        uint64_t vbPosition;
        uint64_t vbNormal;
        uint64_t vbTexcoord;
        uint32_t materialIndex;
    };
private:
    void CreateDescriptorSetLayout();
    void CreateSceneUniformBuffer();
    void CreateResultImage();
    void CreateDescriptorSets();
    void CreateAccelerationStructure();
    void CreateRaytracePipeline();
    void CreateShaderBindingTable();
    void PrepareModelData();

    void CreateCopyImagePipeline();
    void CopyResultToSwapchainImage();

    ResourceUploader m_resourceUploader{};

    VkPipeline m_rtPipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

    std::shared_ptr<DynamicUniformBuffer> m_sceneUniform;
    VkDescriptorSet m_sceneDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;

    std::shared_ptr<Render::rt::ModelResource> m_modelResource;
    std::shared_ptr<Render::rt::DrawObject> m_drawObject;

    using BLAS = std::shared_ptr<AccelerationStructure>;
    std::vector<BLAS> m_sceneBlasList;
    std::shared_ptr<AccelerationStructure> m_sceneTlas;

    struct SBT
    {
        struct {
            VkStridedDeviceAddressRegionKHR rgen = { };
            VkStridedDeviceAddressRegionKHR miss = { };
            VkStridedDeviceAddressRegionKHR rchit = { };
            VkStridedDeviceAddressRegionKHR callable = { };
        } region;
        std::shared_ptr<ShaderBindingTableBuffer> buffer;

    } m_sbt;


    std::shared_ptr<StorageBuffer>  m_asInstanceBuffer;
    std::shared_ptr<StorageImage2D> m_rtResult;

    std::shared_ptr<StorageBuffer>  m_materialBuffer;
    std::shared_ptr<Sampler> m_defaultSampler;

    struct {
        VkPipeline pipeline;
        VkPipelineLayout pipelineLayout;

        VkDescriptorSet   descriptorSet;
        VkDescriptorSetLayout descriptorSetLayout;

        std::shared_ptr<Sampler>  sampler;
    } m_resultCopy;
};
