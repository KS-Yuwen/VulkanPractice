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

class ComputeApp : public ISampleApp
{
public:
	virtual void OnInitialize() override;
	virtual void OnDrawFrame() override;
	virtual void OnCleanup() override;

    struct Vertex
    {
        glm::vec3 position;
        glm::vec2 uv;
    };
    struct SceneConstants
    {
        glm::mat4 mtxView;
        glm::mat4 mtxProj;
        glm::vec4 lightDir;
        glm::vec4 eyePosition;
        glm::vec2 tessParameters;
        uint32_t  frameTime;
        uint32_t  padding;
    };

private:
    void CreatePlaneGeometry();
    void CreateDepthBuffer();

    void CreateDescriptorSetLayout();
    void CreateGraphicsPipeline();
    void CreateSceneUniformBuffer();
    void CreateDescriptorSets();

    void BeginScene(std::shared_ptr<CommandBuffer> commandBuffer);
    void EndScene(std::shared_ptr<CommandBuffer> commandBuffer);

    void PrepareVectorAddData();
    void ComputeVectorAdd(std::shared_ptr<CommandBuffer> commandBuffer);

    void PrepareImageFilterProcessData();
    void ApplyImageFilter(std::shared_ptr<CommandBuffer> commandBuffer);

    ResourceUploader m_resourceUploader{};

    enum
    {
        ADD_ARRAY_LENGTH = 100 * 10000,
    };

    struct {
        std::shared_ptr<VertexBuffer> vertexBuffer;
        std::shared_ptr<IndexBuffer>  indexBuffer;
        uint32_t indexCount;
    } m_plane;

    struct
    {
        std::shared_ptr<StorageBuffer> srcBuffer;
        std::shared_ptr<StorageBuffer> resultBuffer;
        std::shared_ptr<StorageBuffer> cpuAccessBuffer;

        std::vector<uint32_t> sampleData;

        VkPipeline pipeline;
        VkPipelineLayout pipelineLayout;
        VkDescriptorSetLayout descriptorSetLayout;
        VkDescriptorSet  descriptorSet;
    } m_vectorAdd;

    struct
    {
        std::shared_ptr<StorageImage2D> srcImage;
        std::shared_ptr<StorageImage2D> workImage;
        std::shared_ptr<StorageImage2D> filteredImage;

        VkPipeline pipelineFilterH;
        VkPipeline pipelineFilterV;
        VkPipelineLayout pipelineLayout;
        VkDescriptorSetLayout descriptorSetLayout;
        VkDescriptorSet descriptorSet1;
        VkDescriptorSet descriptorSet2;

    } m_imageFilter;

    std::shared_ptr<Sampler> m_sampler;

    std::shared_ptr<DynamicUniformBuffer> m_sceneUniform;

    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_sceneDescriptorSet = VK_NULL_HANDLE;

    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    std::shared_ptr<DepthBuffer> m_depthBuffer;
};
