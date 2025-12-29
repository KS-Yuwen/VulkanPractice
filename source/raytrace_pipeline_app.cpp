#include "raytrace_pipeline_app.h"
#include "core/shader_loader.h"
#include "Render/model_loader.h"
#include "core/asset_path.h"
#include "core/shader_binding_table_builder.h"
#include "core/graphics_pipeline_builder.h"

#include <thread>
#include <chrono>

constexpr uint32_t MaxRaytraceTextureCount = 4096;

VkTransformMatrixKHR ConvertTransform(const glm::mat4x3& m)
{
	VkTransformMatrixKHR mtx{};
	auto mT = glm::transpose(m);
	memcpy(&mtx.matrix[0], &mT[0], sizeof(float) * 4);
	memcpy(&mtx.matrix[1], &mT[1], sizeof(float) * 4);
	memcpy(&mtx.matrix[2], &mT[2], sizeof(float) * 4);
	return mtx;
}

void ClassicRaytraceApp::OnInitialize()
{
    m_resourceUploader.Initialize();

    m_defaultSampler = Sampler::Create();
    m_defaultSampler->Initialize(VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT);

    CreateSceneUniformBuffer();
    CreateDescriptorSetLayout();

    PrepareModelData();

    CreateAccelerationStructure();
    CreateResultImage();

    CreateRaytracePipeline();
    CreateShaderBindingTable();

    CreateCopyImagePipeline();
    CreateDescriptorSets();

    m_resourceUploader.SubmitAndWait();
}

void ClassicRaytraceApp::OnDrawFrame()
{
    auto& vulkanCtx = VulkanContext::Get();
    auto& swapchain = vulkanCtx.GetSwapchain();
    auto device = vulkanCtx.GetVkDevice();
    auto extent = swapchain->GetExtent();

    if (vulkanCtx.AcquireNextImage() != VK_SUCCESS)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return;
    }

    auto frameIndex = vulkanCtx.GetCurrentFrameIndex();
    auto* frameCtx = vulkanCtx.GetCurrentFrameContext();

    // シーン用ユニフォームバッファの更新
    SceneConstants sceneConstants{};
    glm::vec3 eyePos = glm::vec3(0.0, 3.0f, -7.0f);
    sceneConstants.mtxView = glm::lookAtRH<float>(eyePos, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0, 1, 0));
    sceneConstants.mtxProj = glm::perspectiveFov(glm::radians(45.0f), float(extent.width), float(extent.height), 0.1f, 100.0f);
    sceneConstants.mtxViewInv = glm::inverse(sceneConstants.mtxView);
    sceneConstants.mtxProjInv = glm::inverse(sceneConstants.mtxProj);
    sceneConstants.lightDir = glm::vec4(-4.0, 10.0, -5.0, 0);
    sceneConstants.eyePosition = glm::vec4(eyePos, 0);

    // DynamicUniformBuffer の所定の位置に書き込む
    if (void* p = m_sceneUniform->Map(); p != nullptr)
    {
        memcpy(p, &sceneConstants, sizeof(sceneConstants));
        m_sceneUniform->Unmap();
    }

    // 描画処理
    auto commandBuffer = frameCtx->commandBuffer;
    commandBuffer->Begin();

    vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_rtPipeline);
    uint32_t dsOffsets[] = { m_sceneUniform->GetCurrentOffset() };
    vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pipelineLayout, 0, 1, &m_sceneDescriptorSet, 1, dsOffsets);

    uint32_t width = m_rtResult->GetExtent().width;
    uint32_t height = m_rtResult->GetExtent().height;
    vkCmdTraceRaysKHR(*commandBuffer,
        &m_sbt.region.rgen, &m_sbt.region.miss, &m_sbt.region.rchit, &m_sbt.region.callable,
        width, height, 1);

    // 結果をスワップチェインのイメージへコピーする
    CopyResultToSwapchainImage();

    commandBuffer->End();
    vulkanCtx.SubmitPresent();
}

void ClassicRaytraceApp::OnCleanup()
{
    auto& vulkanCtx = VulkanContext::Get();
    auto device = vulkanCtx.GetVkDevice();

    // GPU状態がアイドルになるのを待ってから後始末を開始
    vkDeviceWaitIdle(device);

    m_drawObject->Cleanup();
    m_drawObject.reset();

    m_modelResource->Cleanup();
    m_modelResource.reset();

    vkDestroyPipeline(device, m_rtPipeline, nullptr);

    m_sceneBlasList.clear();
    m_sceneTlas->Destroy();
    m_sceneTlas.reset();

    m_materialBuffer->Cleanup();
    m_materialBuffer.reset();

    m_sceneUniform->Cleanup();
    m_sceneUniform.reset();

    m_asInstanceBuffer->Cleanup();
    m_asInstanceBuffer.reset();
    m_sbt.buffer->Cleanup();
    m_sbt.buffer.reset();

    m_rtResult->Cleanup();
    m_rtResult.reset();

    m_defaultSampler->Cleanup();
    m_defaultSampler.reset();

    vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);

    vkDestroyPipeline(device, m_resultCopy.pipeline, nullptr);
    vkDestroyPipelineLayout(device, m_resultCopy.pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, m_resultCopy.descriptorSetLayout, nullptr);
    m_resultCopy.sampler.reset();

    m_resourceUploader.Cleanup();
}

void ClassicRaytraceApp::CreateDescriptorSetLayout()
{
    auto& vulkanCtx = VulkanContext::Get();
    auto device = vulkanCtx.GetVkDevice();

    // ディスクリプタセットレイアウトの作成
    VkDescriptorSetLayoutCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    };

    std::vector<VkDescriptorSetLayoutBinding> dsLayoutBindings = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
            .pImmutableSamplers = nullptr,
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
            .pImmutableSamplers = nullptr,
        },
        {
            .binding = 2,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
            .pImmutableSamplers = nullptr,
        },
        {   // Materials
            .binding = 3,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
            .pImmutableSamplers = nullptr,
        },
        {   // Textures
            .binding = 4,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = MaxRaytraceTextureCount,
            .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
            .pImmutableSamplers = nullptr,
        }
    };
    createInfo.bindingCount = dsLayoutBindings.size();
    createInfo.pBindings = dsLayoutBindings.data();
    auto result = vkCreateDescriptorSetLayout(device, &createInfo, nullptr, &m_descriptorSetLayout);
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create descriptor set layout!");
    }
}

void ClassicRaytraceApp::CreateSceneUniformBuffer()
{
    m_sceneUniform = DynamicUniformBuffer::Create(sizeof(SceneConstants));
}

void ClassicRaytraceApp::CreateResultImage()
{
    auto& vulkanCtx = VulkanContext::Get();
    auto& swapchain = vulkanCtx.GetSwapchain();
    auto extent = swapchain->GetExtent();
    auto format = swapchain->GetFormat().format;
    m_rtResult = StorageImage2D::Create(extent, format, 1);

    // レイアウトをGENERALにしておく
    auto commandBuffer = vulkanCtx.CreateCommandBuffer();
    commandBuffer->Begin();

    ImageLayoutTransition toGeneral{
        .oldLayout = m_rtResult->GetLayout(),
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcAccessMask = VK_ACCESS_NONE,
        .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .srcStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        .dstStage = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
    };
    commandBuffer->TransitionLayout(m_rtResult, toGeneral);
    commandBuffer->End();
    vulkanCtx.SubmitAndWait(commandBuffer);
}

void ClassicRaytraceApp::CreateDescriptorSets()
{
    auto& vulkanCtx = VulkanContext::Get();
    auto device = vulkanCtx.GetVkDevice();

    m_sceneDescriptorSet = vulkanCtx.AllocateDescriptorSet(m_descriptorSetLayout);

    auto sceneUboInfo = m_sceneUniform->GetDescriptorInfo();
    auto handleTlas = m_sceneTlas->GetHandle();
    VkWriteDescriptorSetAccelerationStructureKHR asDescriptor{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
      .accelerationStructureCount = 1,
      .pAccelerationStructures = &handleTlas
    };
    auto resultImageInfo = m_rtResult->GetStorageReadWriteDescriptorInfo(VK_NULL_HANDLE);
    auto materialSsbo = m_materialBuffer->GetDescriptorInfo();

    // レイトレーシングで使用しているテクスチャのリストを作成する
    std::vector<VkDescriptorImageInfo> textureList;

    const auto& textureImageList = m_modelResource->GetTextureImageList();
    const auto& textureBindings = m_modelResource->GetTextureBindings();

    for (auto& binding : textureBindings)
    {
        const auto& image = textureImageList[binding.textureImageIndex];
        textureList.push_back(std::move(image->GetDescriptorInfo(*binding.sampler)));
    }

    std::vector<VkWriteDescriptorSet> writes = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = m_sceneDescriptorSet,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .pBufferInfo = &sceneUboInfo,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = &asDescriptor,
            .dstSet = m_sceneDescriptorSet,
            .dstBinding = 1,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = m_sceneDescriptorSet,
            .dstBinding = 2,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo = &resultImageInfo,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = m_sceneDescriptorSet,
            .dstBinding = 3,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &materialSsbo,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = m_sceneDescriptorSet,
            .dstBinding = 4,
            .dstArrayElement = 0,
            .descriptorCount = uint32_t(textureList.size()),
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = textureList.data(),
        },
    };

    vkUpdateDescriptorSets(device, uint32_t(writes.size()), writes.data(), 0, nullptr);

    // 結果コピー用のディスクリプタセットを構築
    auto& swapchain = vulkanCtx.GetSwapchain();
    m_resultCopy.descriptorSet = vulkanCtx.AllocateDescriptorSet(m_resultCopy.descriptorSetLayout);
    VkDescriptorImageInfo resultImageAsCombinedImage = {
        .sampler = m_resultCopy.sampler->GetVkSampler(),
        .imageView = m_rtResult->GetVkImageView(),
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };

    writes = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = m_resultCopy.descriptorSet,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &resultImageAsCombinedImage
        },
    };
    vkUpdateDescriptorSets(device, uint32_t(writes.size()), writes.data(), 0, nullptr);
}

void ClassicRaytraceApp::CreateAccelerationStructure()
{
    const auto& meshPrimitives = m_drawObject->GetMeshPrimitiveList();

    // BLASを配置
    std::vector<VkAccelerationStructureInstanceKHR> asInstances;
    asInstances.reserve(meshPrimitives.size());

    for (uint32_t i = 0; i < meshPrimitives.size(); ++i)
    {
        const auto& meshPrimitive = meshPrimitives[i];
        auto mtxWorld = m_drawObject->GetNodes()[meshPrimitive.nodeIndex].mtxWorld;
        VkAccelerationStructureInstanceKHR asInstance{
            .transform = ConvertTransform(mtxWorld),
            .instanceCustomIndex = 0,
            .mask = 0xFF,
            .instanceShaderBindingTableRecordOffset = i,
            .flags = 0,
            .accelerationStructureReference = meshPrimitive.accelerationStructure->GetDeviceAddress(),
        };
        asInstances.push_back(std::move(asInstance));
    }

    auto bufferSize = sizeof(VkAccelerationStructureInstanceKHR) * asInstances.size();
    m_asInstanceBuffer = StorageBuffer::Create(
        bufferSize,
        StorageBuffer::AccessMode::CPUAccessible);
    auto p = m_asInstanceBuffer->MapTyped<VkAccelerationStructureInstanceKHR>();
    memcpy(p, asInstances.data(), bufferSize);
    m_asInstanceBuffer->Unmap();

    {
        // TLAS
        AccelerationStructure::Input inputTlas;
        VkAccelerationStructureGeometryKHR asGeometry{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
            .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
            .flags = VK_GEOMETRY_OPAQUE_BIT_KHR
        };
        asGeometry.geometry.instances = VkAccelerationStructureGeometryInstancesDataKHR{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
            .arrayOfPointers = VK_FALSE,
            .data = {
                .deviceAddress = m_asInstanceBuffer->GetDeviceAddress(),
            }
        };

        auto asBuildRangeInfo = VkAccelerationStructureBuildRangeInfoKHR{
            .primitiveCount = uint32_t(asInstances.size()), // 登録したBLAS数
        };

        inputTlas.asGeometry = { asGeometry };
        inputTlas.asBuildRangeInfo = { asBuildRangeInfo };
        m_sceneTlas = std::make_shared<AccelerationStructure>();
        m_sceneTlas->BuildAS(
            VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
            inputTlas,
            VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
        );
    }
}

void ClassicRaytraceApp::CreateRaytracePipeline()
{
    auto& vulkanCtx = VulkanContext::Get();
    auto device = vulkanCtx.GetVkDevice();

    VkPipelineLayoutCreateInfo layoutCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &m_descriptorSetLayout,
    };
    vkCreatePipelineLayout(device, &layoutCreateInfo, nullptr, &m_pipelineLayout);

    VkShaderModule rgenShaderModule = loader::LoadShaderModule(GetAssetPath(AssetType::Shader, "raytrace-pipeline/shader.rgen.spv"));
    VkShaderModule rmissShaderModule = loader::LoadShaderModule(GetAssetPath(AssetType::Shader, "raytrace-pipeline/shader.rmiss.spv"));
    VkShaderModule rmissShadowShaderModule = loader::LoadShaderModule(GetAssetPath(AssetType::Shader, "raytrace-pipeline/shader.shadow.rmiss.spv"));
    VkShaderModule rchitShaderModule = loader::LoadShaderModule(GetAssetPath(AssetType::Shader, "raytrace-pipeline/shader.rchit.spv"));

    std::vector<VkPipelineShaderStageCreateInfo> stages = {
        // [0] : Ray Generation
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
            .module = rgenShaderModule,
            .pName = "main"
        },
        // [1] : Miss 
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_MISS_BIT_KHR,
            .module = rmissShaderModule,
            .pName = "main"
        },
        // [2] : Miss (Shadow)
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_MISS_BIT_KHR,
            .module = rmissShadowShaderModule,
            .pName = "main"
        },
        // [3] : ClosestHit
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
            .module = rchitShaderModule,
            .pName = "main"
        },
    };

    std::vector<VkRayTracingShaderGroupCreateInfoKHR> groups((uint32_t)ShaderGroup::GroupMax);
    groups[(uint32_t)ShaderGroup::RayGen] = {
        .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
        .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
        .generalShader = 0, // stages[0] が Ray Generation
        .closestHitShader = VK_SHADER_UNUSED_KHR,
        .anyHitShader = VK_SHADER_UNUSED_KHR,
        .intersectionShader = VK_SHADER_UNUSED_KHR,
    };
    groups[(uint32_t)ShaderGroup::MissHit] = {
        .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
        .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
        .generalShader = 1, // stages[1] が Miss Hit
        .closestHitShader = VK_SHADER_UNUSED_KHR,
        .anyHitShader = VK_SHADER_UNUSED_KHR,
        .intersectionShader = VK_SHADER_UNUSED_KHR,
    };
    groups[(uint32_t)ShaderGroup::MissHitShadow] = {
        .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
        .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
        .generalShader = 2, // stages[2] が Miss Hit(Shadow)
        .closestHitShader = VK_SHADER_UNUSED_KHR,
        .anyHitShader = VK_SHADER_UNUSED_KHR,
        .intersectionShader = VK_SHADER_UNUSED_KHR,
    };
    groups[(uint32_t)ShaderGroup::ClosestHit] = {
        .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
        .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
        .generalShader = VK_SHADER_UNUSED_KHR,
        .closestHitShader = 3,  // stages[3] が Closest Hit
        .anyHitShader = VK_SHADER_UNUSED_KHR,
        .intersectionShader = VK_SHADER_UNUSED_KHR,
    };

    VkRayTracingPipelineCreateInfoKHR rtPipelineCI{
      .sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
      .stageCount = uint32_t(stages.size()),
      .pStages = stages.data(),
      .groupCount = uint32_t(groups.size()),
      .pGroups = groups.data(),
      .maxPipelineRayRecursionDepth = 1,
      .layout = m_pipelineLayout,
    };
    auto result = vkCreateRayTracingPipelinesKHR(
        device,
        VK_NULL_HANDLE,
        VK_NULL_HANDLE,
        1, &rtPipelineCI, nullptr, &m_rtPipeline);
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create raytracing pipeline");
    }

    vkDestroyShaderModule(device, rgenShaderModule, nullptr);
    vkDestroyShaderModule(device, rmissShaderModule, nullptr);
    vkDestroyShaderModule(device, rmissShadowShaderModule, nullptr);
    vkDestroyShaderModule(device, rchitShaderModule, nullptr);
}

void ClassicRaytraceApp::CreateShaderBindingTable()
{
    auto& vulkanCtx = VulkanContext::Get();
    auto device = vulkanCtx.GetVkDevice();

    ShaderBindingTableBuilder sbtBuilder(m_rtPipeline, (uint32_t)ShaderGroup::GroupMax);

    // 各シェーダーエントリを登録
    sbtBuilder.AddShaderGroup(RaytraceShaderGroupType::RayGen, (uint32_t)ShaderGroup::RayGen);
    sbtBuilder.AddShaderGroup(RaytraceShaderGroupType::Miss, (uint32_t)ShaderGroup::MissHit);
    sbtBuilder.AddShaderGroup(RaytraceShaderGroupType::Miss, (uint32_t)ShaderGroup::MissHitShadow);

    // ヒットグループにはそれぞれのBLASが使用するShaderRecordDataも記録
    for (auto& mesh : m_drawObject->GetMeshPrimitiveList())
    {
        ShaderRecordData recordData{
            .indexBuffer = mesh.geometry.indexBuffer,
            .vbPosition = mesh.geometry.vbPosition,
            .vbNormal = mesh.geometry.vbNormal,
            .vbTexcoord = mesh.geometry.vbTexcoord,
            .materialIndex = mesh.materialIndex,
        };
        auto recordSize = sizeof(recordData);
        sbtBuilder.AddShaderGroup(RaytraceShaderGroupType::HitGroup, (uint32_t)ShaderGroup::ClosestHit, &recordData, recordSize);
    }

    m_sbt.buffer = sbtBuilder.Build();
    const auto& regions = sbtBuilder.GetShaderBindingRegions();

    m_sbt.region.rgen = regions.raygen;
    m_sbt.region.miss = regions.miss;
    m_sbt.region.rchit = regions.rchit;
}

void ClassicRaytraceApp::PrepareModelData()
{
    std::filesystem::path modelFilePath;
    modelFilePath = GetAssetPath(AssetType::Model, L"cube/Cube.gltf");
    //modelFilePath = GetAssetPath(AssetType::Model, L"OrientationTest/OrientationTest.glb");
    modelFilePath = GetAssetPath(AssetType::Model, L"alpha-test/AlphaBlendModeTest.glb");
    modelFilePath = GetAssetPath(AssetType::Model, L"classic-raytrace/VkRaytraceScene.glb");

    ModelData modelData{};
    loader::LoadModelDataFromFile(&modelData, modelFilePath);
    m_modelResource = Render::rt::ModelResource::CreateFromModelData(modelData, m_resourceUploader);

    // モデルジオメトリ情報をVRAMへ転送
    m_resourceUploader.SubmitAndWait();

    m_drawObject = std::make_shared<Render::rt::DrawObject>();
    m_drawObject->Initialize(m_modelResource, glm::mat4(1.0));

    // 全てのマテリアル情報を記録した配列・バッファを用意
    size_t bufferSize;
    std::vector<Render::rt::DrawObject::RaytraceMaterial> materialList;
    materialList = m_drawObject->GetMaterials();
    bufferSize = sizeof(Render::rt::DrawObject::RaytraceMaterial) * materialList.size();
    m_materialBuffer = StorageBuffer::Create(bufferSize, StorageBuffer::AccessMode::CPUAccessible);
    if (void* p = m_materialBuffer->Map(); p != nullptr)
    {
        memcpy(p, materialList.data(), bufferSize);
        m_materialBuffer->Unmap();
    }
}

void ClassicRaytraceApp::CreateCopyImagePipeline()
{
    auto& vulkanCtx = VulkanContext::Get();
    auto& swapchain = vulkanCtx.GetSwapchain();
    auto device = vulkanCtx.GetVkDevice();

    std::vector<VkDescriptorSetLayoutBinding> dsLayoutBindings = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
    };
    VkDescriptorSetLayoutCreateInfo dsLayoutCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = uint32_t(dsLayoutBindings.size()),
        .pBindings = dsLayoutBindings.data(),
    };
    auto result = vkCreateDescriptorSetLayout(device, &dsLayoutCreateInfo, nullptr, &m_resultCopy.descriptorSetLayout);
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create descriptor set layout!");
    }

    // パイプラインレイアウトの作成
    VkPipelineLayoutCreateInfo layoutCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &(m_resultCopy.descriptorSetLayout),
    };
    result = vkCreatePipelineLayout(device, &layoutCreateInfo, nullptr, &m_resultCopy.pipelineLayout);
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create pipeline layout !");
    }

    VkShaderModule vsCopy = loader::LoadShaderModule(GetAssetPath(AssetType::Shader, "raytrace-pipeline/copy_image.vert.spv"));
    VkShaderModule fsCopy = loader::LoadShaderModule(GetAssetPath(AssetType::Shader, "raytrace-pipeline/copy_image.frag.spv"));

    GraphicsPipelineBuilder builder{};
    builder.AddShaderStage(VK_SHADER_STAGE_VERTEX_BIT, vsCopy);
    builder.AddShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, fsCopy);
    auto swapchainExtent = swapchain->GetExtent();
    builder.SetViewport(swapchainExtent);
    builder.SetPipelineLayout(m_resultCopy.pipelineLayout);
    builder.UseDynamicRendering(swapchain->GetFormat().format);
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
    };
    builder.SetInputAssembly(inputAssemblyInfo);

    m_resultCopy.pipeline = builder.Build();
    if (m_resultCopy.pipeline == VK_NULL_HANDLE)
    {
        throw std::runtime_error("failed to create compute pipeline !");
    }
    vkDestroyShaderModule(device, vsCopy, nullptr);
    vkDestroyShaderModule(device, fsCopy, nullptr);

    m_resultCopy.sampler = Sampler::Create();
    m_resultCopy.sampler->Initialize(
        VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST,
        VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT
    );
}

void ClassicRaytraceApp::CopyResultToSwapchainImage()
{
    // レイトレーシング結果の画像をスワップチェインイメージへコピー
    auto& vulkanCtx = VulkanContext::Get();
    auto& swapchain = vulkanCtx.GetSwapchain();
    auto extent = swapchain->GetExtent();
    auto* frameCtx = vulkanCtx.GetCurrentFrameContext();
    auto commandBuffer = frameCtx->commandBuffer;

    VkImageSubresourceRange subresourceRange{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0, .levelCount = 1,
        .baseArrayLayer = 0, .layerCount = 1,
    };

    ImageLayoutTransition toReadBarrier{
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .srcStage = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
        .dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
    };
    commandBuffer->TransitionLayout(m_rtResult, toReadBarrier);
    commandBuffer->TransitionLayout(
        swapchain->GetCurrentImage(), subresourceRange,
        ImageLayoutTransition::FromUndefinedToColorAttachment());

    // グラフィックスパイプラインを用いてコピー処理をおこなう
    VkRenderingAttachmentInfo colorAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = swapchain->GetCurrentView(),
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = VkClearValue{.color = {{0.0f, 0.0f, 0.0f, 0.0f}} }
    };
    VkRenderingInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { {0, 0}, extent },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment
    };
    vkCmdBeginRendering(*commandBuffer, &renderingInfo);
    vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_resultCopy.pipeline);
    vkCmdBindDescriptorSets(*commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_resultCopy.pipelineLayout, 0,
        1, &m_resultCopy.descriptorSet,
        0, nullptr);
    vkCmdDraw(*commandBuffer, 4, 1, 0, 0);
    vkCmdEndRendering(*commandBuffer);

    // 表示用バリアを設定
    commandBuffer->TransitionLayout(
        swapchain->GetCurrentImage(), subresourceRange,
        ImageLayoutTransition::FromColorToPresent()
    );
    // レイトレーシング結果書込みバッファを状態遷移
    ImageLayoutTransition toWrite{
        .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        .dstStage = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
    };
    commandBuffer->TransitionLayout(
        m_rtResult->GetVkImage(), subresourceRange, toWrite
    );
}
