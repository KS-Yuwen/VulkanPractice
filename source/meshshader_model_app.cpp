#include "meshshader_model_app.h"
#include "core/shader_loader.h"
#include "Render/model_loader.h"
#include "core/asset_path.h"
#include "core/graphics_pipeline_builder.h"

#include <thread>
#include <chrono>

constexpr uint32_t MaxRaytraceTextureCount = 4096;

#if false
// 他ファイルで存在するのでコメントアウト
uint32_t toAlign(uint32_t size, uint32_t align)
{
    return (size + align - 1) & ~(align - 1);
}
#endif

void MeshShaderModelApp::OnInitialize()
{
    m_resourceUploader.Initialize();

    CreateSceneUniformBuffer();

    m_renderPBR = std::make_shared<Render::ms::BasicPBRRender>();
    m_renderPBR->Initialize();

    PrepareModelData();

    CreateDescriptorSets();

    m_resourceUploader.SubmitAndWait();
}

void MeshShaderModelApp::OnDrawFrame()
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
    glm::vec3 eyePos = glm::vec3(0.0, 2.0f, 5.0f);
    sceneConstants.mtxView = glm::lookAtRH<float>(eyePos, glm::vec3(0.0f, 1.5f, 0.0f), glm::vec3(0, 1, 0));
    sceneConstants.mtxProj = glm::perspectiveFov(glm::radians(45.0f), float(extent.width), float(extent.height), 0.1f, 1000.0f);
    sceneConstants.lightDir = glm::vec4(0.0f, 1.0f, 1.0f, 0.0f);

    sceneConstants.eyePosition = eyePos;
    sceneConstants.exposure = 2.0f;

    // DynamicUniformBuffer の所定の位置に書き込む
    if (void* p = m_sceneUniform->Map(); p != nullptr)
    {
        memcpy(p, &sceneConstants, sizeof(sceneConstants));
        m_sceneUniform->Unmap();
    }

    // モデルのワールド行列を更新
    static int frameCount = 0; frameCount++;
    auto mtxWorld = glm::rotate(glm::mat4(1.0f), frameCount * 0.01f, glm::vec3(0, 1, 0));
    m_drawObject->UpdateWorldMatrices(mtxWorld);

    // 描画処理
    auto commandBuffer = frameCtx->commandBuffer;
    commandBuffer->Begin();

    m_renderPBR->BeginScene(commandBuffer);

    // シーン用ユニフォームバッファ/ディスクリプタセットのセット
    auto uboOffset = m_sceneUniform->GetCurrentOffset();
    m_renderPBR->SetSceneDescriptor(m_sceneDescriptorSet, uboOffset);

    // モデルを描画する
    m_renderPBR->Draw(commandBuffer, m_drawObject);

    m_renderPBR->EndScene(commandBuffer);
    commandBuffer->End();

    vulkanCtx.SubmitPresent();
}

void MeshShaderModelApp::OnCleanup()
{
    auto& vulkanCtx = VulkanContext::Get();
    auto device = vulkanCtx.GetVkDevice();

    // GPU状態がアイドルになるのを待ってから後始末を開始
    vkDeviceWaitIdle(device);

    m_drawObject->Cleanup();
    m_drawObject.reset();

    m_modelResource->Cleanup();
    m_modelResource.reset();

    m_renderPBR->Cleanup();
    m_renderPBR.reset();

    m_sceneUniform->Cleanup();
    m_sceneUniform.reset();

    m_resourceUploader.Cleanup();
}

void MeshShaderModelApp::CreateSceneUniformBuffer()
{
    m_sceneUniform = DynamicUniformBuffer::Create(sizeof(SceneConstants));
}

void MeshShaderModelApp::CreateDescriptorSets()
{
    auto& vulkanCtx = VulkanContext::Get();
    auto device = vulkanCtx.GetVkDevice();

    auto dsLayout = m_renderPBR->GetSceneLayout();

    m_sceneDescriptorSet = vulkanCtx.AllocateDescriptorSet(dsLayout);
    auto sceneUniform = m_sceneUniform->GetDescriptorInfo();

    std::vector<VkWriteDescriptorSet> writes = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = m_sceneDescriptorSet,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .pBufferInfo = &sceneUniform,
        },
    };

    vkUpdateDescriptorSets(device, uint32_t(writes.size()), writes.data(), 0, nullptr);
}

void MeshShaderModelApp::PrepareModelData()
{
    std::filesystem::path modelFilePath;
    //modelFilePath = GetAssetPath(AssetType::Model, L"cube/Cube.gltf");
    //modelFilePath = GetAssetPath(AssetType::Model, L"OrientationTest/OrientationTest.glb");
    //modelFilePath = GetAssetPath(AssetType::Model, L"alpha-test/AlphaBlendModeTest.glb");
    modelFilePath = GetAssetPath(AssetType::Model, L"sponza/Sponza.gltf");
    //modelFilePath = GetAssetPath(AssetType::Model, L"bunny/bunny.glb");

    ModelData modelData{};
    loader::LoadModelDataFromFile(&modelData, modelFilePath);
    m_modelResource = Render::ms::ModelResource::CreateFromModelData(modelData, m_resourceUploader);

    // モデルジオメトリ情報をVRAMへ転送
    m_resourceUploader.SubmitAndWait();

    m_drawObject = std::make_shared<Render::ms::DrawObject>();
    m_drawObject->Initialize(m_modelResource, m_resourceUploader, m_renderPBR->GetMaterialLayout(), glm::mat4(1.0));

    m_resourceUploader.SubmitAndWait();
}
