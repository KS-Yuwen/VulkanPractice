#include "draw_object_rt.h"
#include <queue>

void Render::rt::DrawObject::Initialize(std::shared_ptr<ModelResource>& modelResource, glm::mat4 mtxWorld)
{
    auto& vulkanCtx = VulkanContext::Get();
    auto device = vulkanCtx.GetVkDevice();

    // マテリアルのコピー
    m_materials.reserve(modelResource->m_materials.size());
    for (const auto& src : modelResource->m_materials)
    {
        RaytraceMaterial material{};
        material.baseColor = glm::vec4(src.baseColorFactor, src.alpha);
        material.metallicFactor = src.metallicFactor;
        material.roughnessFactor = src.roughnessFactor;

        material.baseTexture = src.baseColorTexture;
        material.metallicRoughnessTexture = src.metallicRoughnessTexture;
        material.normalTexture = src.normalMap;

        material.materialKind = 0;  // Lambert+Phong
        if (src.name.find("mirror") != src.name.npos ||
            src.name.find("Mirror") != src.name.npos ||
            src.metallicFactor > 0.0)
        {
            material.materialKind = 1; // 完全反射のマテリアル
        }

        if (src.name.find("refract") != src.name.npos ||
            src.name.find("Refract") != src.name.npos)
        {
            material.materialKind = 2; // 屈折のマテリアル
        }

        m_materials.push_back(std::move(material));
    }

    // ノードのコピー
    for (const auto& node : modelResource->m_nodes)
    {
        m_nodes.push_back(node);
    }

    // 単位行列でワールド行列を更新
    UpdateWorldMatrices(glm::mat4(1.0f));

    // モデルリソースへの参照を持たせる
    m_model = modelResource;

    // BLAS を作成する
    CreateMeshPrimitiveList();
}

void Render::rt::DrawObject::Cleanup()
{
    auto& vulkanCtx = VulkanContext::Get();

    for (auto& mesh : m_meshPrimitiveList)
    {
        mesh.accelerationStructure->Destroy();
    }
    m_meshPrimitiveList.clear();
}

void Render::rt::DrawObject::UpdateWorldMatrices(const glm::mat4& mtxTransform)
{
    std::queue<int> nodeQueue;

    // 幅優先探索でノードを処理
    nodeQueue.push(0);
    while (!nodeQueue.empty())
    {
        auto index = nodeQueue.front();
        nodeQueue.pop();
        auto& node = m_nodes[index];

        auto mtxParent = mtxTransform;
        if (node.parent >= 0)
        {
            mtxParent = m_nodes[node.parent].mtxWorld;
        }
        node.mtxWorld = mtxParent * node.mtxLocal;


        // 子供ノードを追加
        for (auto& i : node.children)
        {
            nodeQueue.push(i);
        }
    }
}

void Render::rt::DrawObject::CreateMeshPrimitiveList()
{
    m_meshPrimitiveList.clear();
    for (int nodeIndex = 0; nodeIndex < int(m_nodes.size()); ++nodeIndex)
    {
        const auto& node = m_nodes[nodeIndex];
        if (node.meshes.empty())
        {
            continue;
        }
        for (auto mesh : node.meshes)
        {
            auto& m = m_model->GetMeshes()[mesh];
            BuildBlas(nodeIndex, m_model->GetMeshes()[mesh]);
        }
    }
}

void Render::rt::DrawObject::BuildBlas(int nodeIndex, const rt::ModelResource::Mesh& mesh)
{
    const auto& vbPosition = m_model->GetVertexAttribute((uint32_t)rt::ModelResource::VertexAttributeIndex::Position);
    const auto& vbNormal = m_model->GetVertexAttribute((uint32_t)rt::ModelResource::VertexAttributeIndex::Normal);
    const auto& vbTexcoord = m_model->GetVertexAttribute((uint32_t)rt::ModelResource::VertexAttributeIndex::Texcoord0);
    const auto indexBuffer = m_model->GetIndexBuffer();
    for (auto& srcPrimitive : mesh.primitives)
    {
        auto vertexCount = srcPrimitive.vertexCount;
        auto indexCount = srcPrimitive.indexCount;
        auto vertexOffset = srcPrimitive.vertexOffset;
        auto indexOffset = srcPrimitive.firstIndex;

        auto blas = std::make_shared<AccelerationStructure>();
        MeshPrimitive meshPrimitive{
            .accelerationStructure = blas,
            .nodeIndex = nodeIndex,
            .materialIndex = uint32_t(srcPrimitive.material),
            .primitive = {
                .indexCount = indexCount,
                .vertexCount = vertexCount,
                .indexOffset = 0,
                .vertexOffset = 0,
            }
        };


        auto addrPosition = vbPosition.buffer->GetDeviceAddress();
        auto addrNormal = vbNormal.buffer->GetDeviceAddress();
        auto addrTexcoord = vbTexcoord.buffer->GetDeviceAddress();
        auto addrIndexBuffer = indexBuffer->GetDeviceAddress();

        addrIndexBuffer += sizeof(uint32_t) * indexOffset;
        addrPosition += vbPosition.stride * vertexOffset;
        addrNormal += vbNormal.stride * vertexOffset;
        addrTexcoord += vbTexcoord.stride * vertexOffset;

        auto& geometry = meshPrimitive.geometry;
        geometry.vbPosition = addrPosition;
        geometry.vbNormal = addrNormal;
        geometry.vbTexcoord = addrTexcoord;
        geometry.indexBuffer = addrIndexBuffer;

        m_meshPrimitiveList.push_back(std::move(meshPrimitive));

        VkAccelerationStructureGeometryKHR asGeometry{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
            .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
            .flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
        };
        asGeometry.geometry.triangles = VkAccelerationStructureGeometryTrianglesDataKHR{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
            .vertexFormat = vbPosition.format,
            .vertexData = {
                .deviceAddress = addrPosition,
            },
            .vertexStride = vbPosition.stride,
            .maxVertex = vertexCount,
            .indexType = VK_INDEX_TYPE_UINT32,
            .indexData = {
                .deviceAddress = addrIndexBuffer,
            }
        };
        VkAccelerationStructureBuildRangeInfoKHR asBuildRangeInfo{
          .primitiveCount = indexCount / 3,
          .primitiveOffset = 0,
          .firstVertex = 0,
          .transformOffset = 0,
        };
        AccelerationStructure::Input inputBlas;
        inputBlas.asGeometry = { asGeometry };
        inputBlas.asBuildRangeInfo = { asBuildRangeInfo };

        blas->BuildAS(
            VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
            inputBlas,
            VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
    }
}

