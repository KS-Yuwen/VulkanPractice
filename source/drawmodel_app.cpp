#include "drawmodel_app.h"
#include "Render/model_loader.h"
#include "core/asset_path.h"

#include <thread>
#include <chrono>

uint32_t toAlign(uint32_t size, uint32_t align)
{
	return (size + align - 1) & ~(align - 1);
}

void DrawModelApp::OnInitialize()
{
    m_resourceUploader.Initialize();

    std::filesystem::path modelFilePath;

    modelFilePath = GetAssetPath(AssetType::Model, L"cube/Cube.gltf");
    modelFilePath = GetAssetPath(AssetType::Model, L"alpha-test/AlphaBlendModeTest.glb");
    //modelFilePath = GetAssetPath(AssetType::Model, L"OrientationTest/OrientationTest.glb");
    modelFilePath = GetAssetPath(AssetType::Model, L"sponza/Sponza.gltf");

    ModelData modelData{};
    loader::LoadModelDataFromFile(&modelData, modelFilePath);

    m_modelResource = render::ModelResource::CreateFromModelData(modelData, m_resourceUploader);

    m_resourceUploader.SubmitAndWait();

    m_renderPBR = std::make_shared<render::BasicPBRRender>();
    m_renderPBR->Initialize();

    CreateSceneUniformBuffer();
    CreateDescriptorSets();

    m_drawObject = std::make_shared<render::DrawObject>();
    m_drawObject->Initialize(m_modelResource, m_renderPBR->GetMaterialLayout());
}

void DrawModelApp::OnDrawFrame()
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

    // モデルのワールド行列を更新
    static int frameCount = 0; frameCount++;
    auto mtxWorld = glm::rotate(glm::mat4(1.0f), frameCount * 0.01f, glm::vec3(0, 1, 0));
    m_drawObject->UpdateWorldMatrices(mtxWorld);

    // シーン用ユニフォームバッファの更新
    SceneConstants sceneConstants{};
    glm::vec3 eyePos = glm::vec3(0.0, 1.0f, 5.0f);
    sceneConstants.mtxView = glm::lookAtRH<float>(eyePos, glm::vec3(0.0f, 0.5f, 0.0f), glm::vec3(0, 1, 0));
    sceneConstants.mtxProj = glm::perspectiveFov(glm::radians(45.0f), float(extent.width), float(extent.height), 0.1f, 100.0f);
    sceneConstants.lightDir = glm::vec4(0.0f, 1.0f, 1.0f, 0.0f);
    sceneConstants.eyePosition = eyePos;
    sceneConstants.exposure = 2.0f;

    // DynamicUniformBuffer の所定の位置に書き込む
    if (void* p = m_sceneUniform->Map(); p != nullptr)
    {
        memcpy(p, &sceneConstants, sizeof(sceneConstants));
        m_sceneUniform->Unmap();
    }

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

void DrawModelApp::OnCleanup()
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

void DrawModelApp::CreateSceneUniformBuffer()
{
    m_sceneUniform = DynamicUniformBuffer::Create(sizeof(SceneConstants));
}

void DrawModelApp::CreateDescriptorSets()
{
    auto& vulkanCtx = VulkanContext::Get();
    auto device = vulkanCtx.GetVkDevice();

    auto sceneDsLayout = m_renderPBR->GetSceneLayout();
    m_sceneDescriptorSet = vulkanCtx.AllocateDescriptorSet(sceneDsLayout);

    auto sceneUniformInfo = m_sceneUniform->GetDescriptorInfo();
    VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = m_sceneDescriptorSet,
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
        .pBufferInfo = &sceneUniformInfo,
    };
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}
